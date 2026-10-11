// Disc handling for the Burnout 3 page: identify an ISO and cache it in the browser.
// Classic script for shell.html (window.B3Disc), CommonJS for the tests.
(function (root) {
  const SECTOR = 2048;
  const EXPECTED_BOOT = 'SLUS_210.50';
  const CACHE_NAME = 'burnout3.iso';
  const MARKER_NAME = 'burnout3.ok';

  async function bytes(blob, offset, length) {
    return new Uint8Array(await blob.slice(offset, offset + length).arrayBuffer());
  }
  const le32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16)) + b[o + 3] * 16777216;
  const ascii = (b) => String.fromCharCode(...b);

  async function bootProgram(blob) {
    const pvd = await bytes(blob, 16 * SECTOR, SECTOR);
    if (pvd.length < 190 || pvd[0] !== 1 || ascii(pvd.subarray(1, 6)) !== 'CD001') throw new Error('not an ISO 9660 image (no volume descriptor at sector 16)');
    const rootLba = le32(pvd, 158), rootSize = le32(pvd, 166);
    const dir = await bytes(blob, rootLba * SECTOR, Math.ceil(rootSize / SECTOR) * SECTOR);
    for (let off = 0; off < rootSize && off < dir.length;) {
      const len = dir[off];
      if (!len) { off = (Math.floor(off / SECTOR) + 1) * SECTOR; continue; }
      const name = ascii(dir.subarray(off + 33, off + 33 + dir[off + 32])).split(';')[0].toUpperCase();
      if (name === 'SYSTEM.CNF') {
        const text = new TextDecoder().decode(await bytes(blob, le32(dir, off + 2) * SECTOR, le32(dir, off + 10)));
        const m = text.match(/^BOOT2\s*=\s*(.+)$/m);
        return m ? m[1].trim().split('\\').pop().split(';')[0] : null;
      }
      off += len;
    }
    return null;
  }

  async function identify(blob) {
    const boot = await bootProgram(blob);
    return { boot, ok: boot === EXPECTED_BOOT };
  }

  const api = { SECTOR, EXPECTED_BOOT, CACHE_NAME, MARKER_NAME, bootProgram, identify };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.B3Disc = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
