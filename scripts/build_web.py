"""
PlatformIO extra script — automatically builds the Vue web app and
gzip-compresses assets before the LittleFS filesystem image is packed.

Triggered only on:  pio run -t buildfs
                    pio run -t uploadfs

Does NOT run during normal firmware build (pio run / pio run -t upload).

ESPAsyncWebServer automatically serves *.gz files with Content-Encoding: gzip
when the original is absent, so we delete originals to save LittleFS space.
"""

import subprocess
import shutil
import gzip
import os
import struct
import zlib
from pathlib import Path

Import("env")  # noqa: F821  (SCons built-in)

COMPRESS_EXTENSIONS = {".html", ".js", ".css", ".json", ".svg", ".map", ".webmanifest"}

# app.js is delivered raw over WebSocket on no-PSRAM boards — the browser imports
# it via import(blobUrl). Gzip-compressed bytes cannot be imported directly.
# CSS is injected by the bundle itself (inlineCssPlugin in vite.config.ts), so no
# separate app.css is produced. HTTP path on PSRAM boards serves app.js as-is
# (slightly larger transfer than gzipped, negligible on local WiFi).
SKIP_GZIP = {"app.js"}


def _png_chunk(tag: bytes, data: bytes) -> bytes:
    body = tag + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def _blend(bg, fg, alpha):
    return tuple(int(bg[i] * (1 - alpha) + fg[i] * alpha) for i in range(3))


def _in_rounded_rect(x, y, rx, ry, rw, rh, r):
    """True if pixel (x,y) is inside a rounded rectangle."""
    if x < rx or x >= rx + rw or y < ry or y >= ry + rh:
        return False
    cx = max(rx + r, min(x, rx + rw - 1 - r))
    cy = max(ry + r, min(y, ry + rh - 1 - r))
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def generate_pwa_icon(path: Path, size: int):
    """Generate a PNG PWA icon without external dependencies."""
    bg   = (17, 17, 17)
    blue = (74, 158, 255)
    hi   = _blend(bg, blue, 0.90)
    lo   = _blend(bg, blue, 0.55)

    pad  = max(2, size // 16)
    mid  = size // 2
    gap  = max(1, size // 32)
    r    = max(1, size // 16)   # corner radius for cards
    br   = max(2, size //  6)   # corner radius for background square

    # pre-build quadrant card bounds
    cards = [
        (pad,       pad,       mid - pad - gap,   mid - pad - gap,   hi),  # top-left
        (mid + gap, pad,       size - pad - gap,  mid - pad - gap,   lo),  # top-right
        (pad,       mid + gap, mid - pad - gap,   size - pad - gap,  lo),  # bottom-left
        (mid + gap, mid + gap, size - pad - gap,  size - pad - gap,  hi),  # bottom-right
    ]

    raw = bytearray()
    for y in range(size):
        raw.append(0)  # PNG filter byte: None
        for x in range(size):
            # Outer background with rounded corners
            if not _in_rounded_rect(x, y, 0, 0, size, size, br):
                raw += bytes([0, 0, 0])
                continue
            color = bg
            for (x0, y0, x1, y1, c) in cards:
                if _in_rounded_rect(x, y, x0, y0, x1 - x0 + 1, y1 - y0 + 1, r):
                    color = c
                    break
            raw += bytes(color)

    ihdr = _png_chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
    idat = _png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    iend = _png_chunk(b"IEND", b"")

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + ihdr + idat + iend)

    print(f"   icon {size}×{size} → {path.name} ({path.stat().st_size} bytes)")


def build_web_app(source, target, env):
    root   = Path(env["PROJECT_DIR"])
    webDir = root / "web"
    outDir = root / "data" / "www"

    print("\n" + "="*60)
    print(">>> [build_web] Pre-action fired — building web UI")
    print(f">>> [build_web] webDir = {webDir}")
    print(f">>> [build_web] outDir = {outDir}")
    print("="*60)
    print("\n>>> [build_web] Running: npm run build")
    result = subprocess.run(
        ["npm", "run", "build"],
        cwd=str(webDir),
        shell=(os.name == "nt"),   # Windows needs shell=True for npm.cmd
    )
    if result.returncode != 0:
        raise SystemExit("[build_web] npm run build failed — aborting uploadfs")

    distDir = webDir / "dist"
    if not distDir.exists():
        raise SystemExit(f"[build_web] Expected dist/ at {distDir} but it does not exist")

    print(f">>> [build_web] Copying {distDir} → {outDir}")
    if outDir.exists():
        shutil.rmtree(str(outDir))
    shutil.copytree(str(distDir), str(outDir))

    print(">>> [build_web] Generating PWA icons...")
    icons_dir = outDir / "icons"
    icons_dir.mkdir(exist_ok=True)
    generate_pwa_icon(icons_dir / "icon-192.png", 192)
    generate_pwa_icon(icons_dir / "icon-512.png", 512)

    print(">>> [build_web] Compressing assets...")
    total_saved = 0
    for f in outDir.rglob("*"):
        if f.is_file() and f.suffix in COMPRESS_EXTENSIONS:
            if f.name in SKIP_GZIP:
                print(f"   {f.name}: kept raw (WS bootstrap path requires uncompressed)")
                continue
            original_size = f.stat().st_size
            gz_path = f.with_suffix(f.suffix + ".gz")
            with open(f, "rb") as src, gzip.open(gz_path, "wb", compresslevel=9) as dst:
                dst.write(src.read())
            compressed_size = gz_path.stat().st_size
            saved = original_size - compressed_size
            total_saved += saved
            f.unlink()  # remove original; server falls back to .gz automatically
            print(f"   {f.name}: {original_size//1024}KB → {compressed_size//1024}KB")

    print(f">>> [build_web] Total saved: {total_saved//1024} KB")
    print(">>> [build_web] Web build done\n")


# Register the pre-action unconditionally on $BUILD_DIR/littlefs.bin.
# SCons only evaluates this target when the user explicitly runs buildfs / uploadfs,
# so the npm build will NOT fire during normal `pio run` firmware builds.
env.AddPreAction("$BUILD_DIR/littlefs.bin", build_web_app)  # noqa: F821
