// Gamepad helpers for the Burnout 3 page. Loaded as a classic script by shell.html
// and importable as a module by the tests.
(function (root) {
  function applyDeadZone(x, y, radius = 0.08) {
    const len = Math.hypot(x, y);
    if (len <= radius) return { x: 0, y: 0 };
    const scaled = Math.min(1, (len - radius) / (1 - radius));
    return { x: (x / len) * scaled, y: (y / len) * scaled };
  }
  function toPs2Axis(v) {
    return Math.max(0, Math.min(255, Math.round((v + 1) * 127.5)));
  }
  const api = { applyDeadZone, toPs2Axis };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.B3Pad = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
