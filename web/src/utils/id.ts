/**
 * UUID v4 generator that works in any context (HTTP, HTTPS, localhost).
 * crypto.randomUUID() requires a secure context (HTTPS) so we can't use it
 * when the UI is served from the ESP32 over plain HTTP.
 */
export function generateId(): string {
  return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => {
    const r = (Math.random() * 16) | 0;
    const v = c === 'x' ? r : (r & 0x3) | 0x8;
    return v.toString(16);
  });
}
