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
    if (rootSize > 262144) throw new Error('not an ISO 9660 image (root directory too large)');
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

  async function readMarker(dir) {
    try {
      const file = await (await dir.getFileHandle(MARKER_NAME)).getFile();
      return Number(await file.text());
    } catch (e) {
      if (e.name === 'NotFoundError') return null;
      throw e;
    }
  }

  async function cachedDisc(storage = root.navigator && root.navigator.storage) {
    const dir = await storage.getDirectory();
    let file;
    try {
      file = await (await dir.getFileHandle(CACHE_NAME)).getFile();
    } catch (e) {
      if (e.name === 'NotFoundError') return null;
      throw e;
    }
    const expected = await readMarker(dir);
    if (expected === null || expected !== file.size) return null;
    return file;
  }

  async function writeAll(handle, source, onProgress, total) {
    const writable = await handle.createWritable();
    const writer = writable.getWriter ? writable.getWriter() : writable;
    const reader = source.stream().getReader();
    let done = 0;
    try {
      for (;;) {
        const { value, done: finished } = await reader.read();
        if (finished) break;
        await writer.write(value);
        done += value.byteLength;
        if (onProgress) onProgress(done, total);
      }
      await writer.close();
    } catch (e) {
      if (writer.abort) await writer.abort().catch(() => {});
      throw e;
    }
  }

  async function cacheDisc(file, onProgress, storage = root.navigator && root.navigator.storage) {
    if (storage.persist) await storage.persist().catch(() => {});
    // The old copy goes first, so the space it holds counts as free.
    const dir = await storage.getDirectory();
    for (const name of [MARKER_NAME, CACHE_NAME]) {
      try { await dir.removeEntry(name); } catch (e) { if (e.name !== 'NotFoundError') throw e; }
    }
    const { quota, usage } = await storage.estimate();
    const free = Math.max(0, quota - usage);
    if (free < file.size) {
      const gb = (n) => (n / 1073741824).toFixed(2);
      const e = new Error('Not enough browser storage: need ' + gb(file.size) + ' GB, ' + gb(free) + ' GB free');
      e.name = 'QuotaError';
      throw e;
    }
    await writeAll(await dir.getFileHandle(CACHE_NAME, { create: true }), file, onProgress, file.size);
    await writeAll(await dir.getFileHandle(MARKER_NAME, { create: true }), new Blob([String(file.size)]), null, 0);
    return (await dir.getFileHandle(CACHE_NAME)).getFile();
  }

  async function ejectDisc(storage = root.navigator && root.navigator.storage) {
    const dir = await storage.getDirectory();
    for (const name of [MARKER_NAME, CACHE_NAME]) {
      try { await dir.removeEntry(name); } catch (e) { if (e.name !== 'NotFoundError') throw e; }
    }
  }

  const api = { SECTOR, EXPECTED_BOOT, CACHE_NAME, MARKER_NAME, bootProgram, identify, cachedDisc, cacheDisc, ejectDisc };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.B3Disc = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
