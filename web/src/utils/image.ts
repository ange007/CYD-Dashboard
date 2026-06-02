// Client-side image conversion for SD-card uploads.
//
// Backgrounds → JPEG (≤320×240, ≤96 KB). The ESP32 uses TJPGD which streams
// MCU-blocks (~4 KB working buffer), so JPEG sidesteps the heap fragmentation
// that killed the old PNG/lodepng path.
//
// Icons → LVGL v9 binary image (12-byte header + raw ARGB8888 pixels). The
// built-in .bin decoder maps the file straight into the framebuffer with zero
// heap allocation, and alpha is preserved.

function stripExt(name: string): string {
  const i = name.lastIndexOf('.');
  return i > 0 ? name.slice(0, i) : name;
}

function loadImage(file: File): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(file);
    const img = new Image();
    img.onload = () => { URL.revokeObjectURL(url); resolve(img); };
    img.onerror = () => { URL.revokeObjectURL(url); reject(new Error('Failed to load image')); };
    img.src = url;
  });
}

type AnyCanvas = HTMLCanvasElement | OffscreenCanvas;

function makeCanvas(w: number, h: number): AnyCanvas {
  if (typeof OffscreenCanvas !== 'undefined') return new OffscreenCanvas(w, h);
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  return c;
}

function canvasToBlob(canvas: AnyCanvas, type: string, quality?: number): Promise<Blob> {
  if ('convertToBlob' in canvas) {
    return (canvas as OffscreenCanvas).convertToBlob({ type, quality });
  }
  return new Promise((resolve, reject) => {
    (canvas as HTMLCanvasElement).toBlob(
      (b) => b ? resolve(b) : reject(new Error('toBlob failed')),
      type, quality,
    );
  });
}

export type BgFitMode = 'cover' | 'contain' | 'stretch';

export interface ResizeBackgroundOpts {
  /** Exact target width — should match ESP32 display width. */
  targetW: number;
  /** Exact target height — should match ESP32 display height. */
  targetH: number;
  /** How to fit source into target when aspect ratios differ. Default: 'cover'. */
  fit?: BgFitMode;
  /** Hard cap on encoded JPEG size. Default 96 KB. */
  maxBytes?: number;
  /** Initial JPEG quality (will step down to 0.5 if maxBytes exceeded). Default 0.85. */
  quality?: number;
}

/**
 * Resize / re-encode a background image as JPEG of EXACTLY targetW×targetH.
 *
 * The output dimensions match the device screen 1:1 so LVGL can blit decoded
 * pixels straight into the framebuffer without scaling — significantly faster
 * during scroll and lower CPU load overall.
 *
 * Returns a new File with `.jpg` extension (renamed from original basename).
 */
export async function resizeBackgroundJpeg(
  file: File,
  opts: ResizeBackgroundOpts,
): Promise<File> {
  const { targetW, targetH } = opts;
  const fit: BgFitMode = opts.fit ?? 'cover';
  const maxBytes = opts.maxBytes ?? 96 * 1024;
  const quality = opts.quality ?? 0.85;

  const img = await loadImage(file);

  // Fast-path: already JPEG with exact dimensions and within size budget.
  if (img.width === targetW && img.height === targetH
      && file.type === 'image/jpeg' && file.size <= maxBytes) {
    return file;
  }

  const canvas = makeCanvas(targetW, targetH);
  const ctx = canvas.getContext('2d') as CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;
  if (!ctx) throw new Error('2D context unavailable');
  // JPEG has no alpha; fill black so transparent PNGs / letterbox bars are solid.
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, targetW, targetH);

  if (fit === 'stretch') {
    ctx.drawImage(img, 0, 0, targetW, targetH);
  } else if (fit === 'cover') {
    // Crop source to target aspect, then draw filling the canvas.
    const srcAspect = img.width / img.height;
    const tgtAspect = targetW / targetH;
    let sx = 0, sy = 0, sw = img.width, sh = img.height;
    if (srcAspect > tgtAspect) {
      // Source wider than target → crop horizontal sides.
      sw = img.height * tgtAspect;
      sx = (img.width - sw) / 2;
    } else {
      // Source taller → crop top/bottom.
      sh = img.width / tgtAspect;
      sy = (img.height - sh) / 2;
    }
    ctx.drawImage(img, sx, sy, sw, sh, 0, 0, targetW, targetH);
  } else {
    // 'contain' — fit whole image inside target, letterbox/pillarbox black.
    const scale = Math.min(targetW / img.width, targetH / img.height);
    const dw = Math.max(1, Math.round(img.width * scale));
    const dh = Math.max(1, Math.round(img.height * scale));
    const dx = Math.round((targetW - dw) / 2);
    const dy = Math.round((targetH - dh) / 2);
    ctx.drawImage(img, 0, 0, img.width, img.height, dx, dy, dw, dh);
  }

  let blob = await canvasToBlob(canvas, 'image/jpeg', quality);
  let q = quality;
  while (blob.size > maxBytes && q > 0.5) {
    q = Math.max(0.5, q - 0.05);
    blob = await canvasToBlob(canvas, 'image/jpeg', q);
  }

  const newName = stripExt(file.name) + '.jpg';
  return new File([blob], newName, { type: 'image/jpeg' });
}

/**
 * Convert any raster image into LVGL v9 binary (ARGB8888) with `.bin` extension.
 * Layout: 12-byte lv_image_header_t + w*h*4 bytes of BGRA pixels (little-endian).
 */
export async function iconToLvglBin(file: File, maxDim = 96): Promise<File> {
  const img = await loadImage(file);

  const scale = Math.min(1, maxDim / Math.max(img.width, img.height));
  const w = Math.max(1, Math.round(img.width * scale));
  const h = Math.max(1, Math.round(img.height * scale));

  const canvas = makeCanvas(w, h);
  const ctx = canvas.getContext('2d') as CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;
  if (!ctx) throw new Error('2D context unavailable');
  ctx.clearRect(0, 0, w, h); // transparent background
  ctx.drawImage(img, 0, 0, w, h);
  const px = ctx.getImageData(0, 0, w, h).data; // RGBA

  // lv_image_header_t (12 bytes, packed little-endian):
  //   [0]    magic   = 0x19
  //   [1]    cf      = LV_COLOR_FORMAT_ARGB8888 (0x10)
  //   [2..3] flags
  //   [4..5] w
  //   [6..7] h
  //   [8..9] stride  = w * 4
  //   [10..11] reserved
  const header = new Uint8Array(12);
  const dv = new DataView(header.buffer);
  header[0] = 0x19;
  header[1] = 0x10;
  dv.setUint16(2, 0, true);
  dv.setUint16(4, w, true);
  dv.setUint16(6, h, true);
  dv.setUint16(8, w * 4, true);
  dv.setUint16(10, 0, true);

  // Canvas ImageData is RGBA; LVGL ARGB8888 stores BGRA in little-endian memory.
  const pixels = new Uint8Array(w * h * 4);
  for (let i = 0, j = 0; i < px.length; i += 4, j += 4) {
    pixels[j]     = px[i + 2]; // B
    pixels[j + 1] = px[i + 1]; // G
    pixels[j + 2] = px[i];     // R
    pixels[j + 3] = px[i + 3]; // A
  }

  const blob = new Blob([header, pixels], { type: 'application/octet-stream' });
  const newName = stripExt(file.name) + '.bin';
  return new File([blob], newName, { type: 'application/octet-stream' });
}

/**
 * Resize an existing LVGL .bin (source) fetched from `url` to the given
 * `targetDim` (max width or height), producing a new display .bin File.
 * The output filename matches the source. Returns null on error.
 */
export async function resizeLvglBin(url: string, targetDim: number, fileName: string): Promise<File | null> {
  try {
    const resp = await fetch(url);
    if (!resp.ok) return null;
    const buf = await resp.arrayBuffer();
    if (buf.byteLength < 2) return null;
    const dv = new DataView(buf);

    // Dispatch by format: LVGL .bin (magic 0x19 0x10) → direct resize,
    // otherwise treat the response as a browser-decodable image (PNG/JPG/
    // SVG/WEBP) and run it through the same pipeline as iconToLvglBin().
    let dstCanvas: HTMLCanvasElement | OffscreenCanvas;
    let w: number, h: number;

    const isLvglBin = dv.getUint8(0) === 0x19 && dv.getUint8(1) === 0x10;
    if (isLvglBin) {
      if (buf.byteLength < 12) return null;
      const srcW = dv.getUint16(4, true);
      const srcH = dv.getUint16(6, true);
      if (!srcW || !srcH || buf.byteLength < 12 + srcW * srcH * 4) return null;

      const srcPx = new Uint8Array(buf, 12, srcW * srcH * 4);
      const srcCanvas = makeCanvas(srcW, srcH);
      const srcCtx = srcCanvas.getContext('2d') as CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;
      if (!srcCtx) return null;
      const imgData = srcCtx.createImageData(srcW, srcH);
      for (let i = 0; i < srcPx.length; i += 4) {
        imgData.data[i]     = srcPx[i + 2];
        imgData.data[i + 1] = srcPx[i + 1];
        imgData.data[i + 2] = srcPx[i];
        imgData.data[i + 3] = srcPx[i + 3];
      }
      srcCtx.putImageData(imgData, 0, 0);

      const scale = Math.min(1, targetDim / Math.max(srcW, srcH));
      w = Math.max(1, Math.round(srcW * scale));
      h = Math.max(1, Math.round(srcH * scale));
      dstCanvas = makeCanvas(w, h);
      const dstCtx = dstCanvas.getContext('2d') as CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;
      if (!dstCtx) return null;
      dstCtx.clearRect(0, 0, w, h);
      dstCtx.drawImage(srcCanvas as any, 0, 0, w, h);
    } else {
      // Compressed source (PNG / JPG / SVG / WEBP / GIF) — delegate decoding
      // to the browser via loadImage. Works for everything <img> accepts.
      const blob = new Blob([buf]);
      const img = await loadImage(new File([blob], 'src', { type: resp.headers.get('content-type') || 'image/png' }));
      const scale = Math.min(1, targetDim / Math.max(img.width, img.height));
      w = Math.max(1, Math.round(img.width * scale));
      h = Math.max(1, Math.round(img.height * scale));
      dstCanvas = makeCanvas(w, h);
      const dstCtx = dstCanvas.getContext('2d') as CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;
      if (!dstCtx) return null;
      dstCtx.clearRect(0, 0, w, h);
      dstCtx.drawImage(img, 0, 0, w, h);
    }

    const dstCtx2 = dstCanvas.getContext('2d') as CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;
    if (!dstCtx2) return null;
    const dstPx = dstCtx2.getImageData(0, 0, w, h).data;

    // Build LVGL .bin header + BGRA pixels
    const header = new Uint8Array(12);
    const hdv = new DataView(header.buffer);
    header[0] = 0x19;
    header[1] = 0x10;
    hdv.setUint16(2, 0, true);
    hdv.setUint16(4, w, true);
    hdv.setUint16(6, h, true);
    hdv.setUint16(8, w * 4, true);
    hdv.setUint16(10, 0, true);

    const pixels = new Uint8Array(w * h * 4);
    for (let i = 0, j = 0; i < dstPx.length; i += 4, j += 4) {
      pixels[j]     = dstPx[i + 2]; // B
      pixels[j + 1] = dstPx[i + 1]; // G
      pixels[j + 2] = dstPx[i];     // R
      pixels[j + 3] = dstPx[i + 3]; // A
    }

    const blob = new Blob([header, pixels], { type: 'application/octet-stream' });
    return new File([blob], fileName, { type: 'application/octet-stream' });
  } catch {
    return null;
  }
}

/**
 * Decode an LVGL v9 .bin (ARGB8888 only) at `url` into a PNG data URL for
 * preview in <img> tags. Returns empty string on any error/unsupported format.
 */
export async function lvglBinToDataUrl(url: string): Promise<string> {
  try {
    const buf = await (await fetch(url)).arrayBuffer();
    if (buf.byteLength < 12) return '';
    const dv = new DataView(buf);
    if (dv.getUint8(0) !== 0x19) return '';
    const cf = dv.getUint8(1);
    if (cf !== 0x10) return ''; // only ARGB8888 supported here
    const w = dv.getUint16(4, true);
    const h = dv.getUint16(6, true);
    if (!w || !h || buf.byteLength < 12 + w * h * 4) return '';

    const src = new Uint8Array(buf, 12, w * h * 4);
    const canvas = document.createElement('canvas');
    canvas.width = w; canvas.height = h;
    const ctx = canvas.getContext('2d');
    if (!ctx) return '';
    const out = ctx.createImageData(w, h);
    // .bin stores BGRA → ImageData expects RGBA.
    for (let i = 0; i < src.length; i += 4) {
      out.data[i]     = src[i + 2]; // R
      out.data[i + 1] = src[i + 1]; // G
      out.data[i + 2] = src[i];     // B
      out.data[i + 3] = src[i + 3]; // A
    }
    ctx.putImageData(out, 0, 0);
    return canvas.toDataURL('image/png');
  } catch {
    return '';
  }
}
