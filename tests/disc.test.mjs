import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const { bootProgram, identify, EXPECTED_BOOT } = createRequire(import.meta.url)('../web/disc.js');

const SECTOR = 2048;
function u32le(v) { return [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255]; }
function u32be(v) { return u32le(v).reverse(); }
function dirRecord(name, lba, size, flags = 0) {
  const nameBytes = [...name].map((c) => c.charCodeAt(0));
  const body = [0, ...u32le(lba), ...u32be(lba), ...u32le(size), ...u32be(size), 0, 0, 0, 0, 0, 0, 0, flags, 0, 0, 1, 0, 0, 1, nameBytes.length, ...nameBytes];
  if (nameBytes.length % 2 === 0) body.push(0);
  return new Uint8Array([body.length + 1, ...body]);
}
export function makeIso(systemCnf) {
  const image = new Uint8Array(20 * SECTOR);
  const pvd = image.subarray(16 * SECTOR, 17 * SECTOR);
  pvd[0] = 1; pvd.set([67, 68, 48, 48, 49], 1); pvd[6] = 1;
  pvd.set(u32le(20), 80); pvd.set(u32be(20), 84);
  pvd.set(dirRecord('\0', 18, SECTOR, 2), 156);
  const root = image.subarray(18 * SECTOR, 19 * SECTOR);
  let off = 0;
  for (const rec of [dirRecord('\0', 18, SECTOR, 2), dirRecord('\u0001', 18, SECTOR, 2)]) { root.set(rec, off); off += rec.length; }
  if (systemCnf !== null) {
    const text = new TextEncoder().encode(systemCnf);
    root.set(dirRecord('SYSTEM.CNF;1', 19, text.length), off);
    image.set(text, 19 * SECTOR);
  }
  return new Blob([image]);
}

test('reads the boot program from SYSTEM.CNF', async () => {
  assert.equal(await bootProgram(makeIso('BOOT2 = cdrom0:\\SLUS_210.50;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n')), 'SLUS_210.50');
});

test('identify accepts only the NTSC-U disc', async () => {
  assert.deepEqual(await identify(makeIso('BOOT2 = cdrom0:\\SLUS_210.50;1\r\n')), { boot: 'SLUS_210.50', ok: true });
  assert.deepEqual(await identify(makeIso('BOOT2 = cdrom0:\\SLES_525.84;1\r\n')), { boot: 'SLES_525.84', ok: false });
  assert.equal(EXPECTED_BOOT, 'SLUS_210.50');
});

test('missing SYSTEM.CNF gives null', async () => {
  assert.equal(await bootProgram(makeIso(null)), null);
});

test('not an ISO throws a readable error', async () => {
  await assert.rejects(bootProgram(new Blob([new Uint8Array(40000)])), /not an ISO 9660 image/);
});

test('a file shorter than 17 sectors throws, does not hang', async () => {
  await assert.rejects(bootProgram(new Blob([new Uint8Array(100)])), /not an ISO 9660 image/);
});

// A fake of the slice of the File System Access API that disc.js uses.
function fakeStorage({ quota = 1e12, usage = 0, persist = true } = {}) {
  const files = new Map();
  const handle = (name) => ({
    name,
    async getFile() { const parts = files.get(name) || []; return new File(parts, name); },
    async createWritable() {
      const parts = [];
      return {
        async write(chunk) { parts.push(chunk); },
        async close() { files.set(name, parts); },
        async abort() {},
        // pipeTo support: WritableStream-like adapter
        getWriter() { return { write: async (c) => parts.push(c), close: async () => files.set(name, parts), releaseLock() {} }; },
      };
    },
  });
  const dir = {
    async getFileHandle(name, opts = {}) {
      if (!files.has(name) && !opts.create) { const e = new Error('nf'); e.name = 'NotFoundError'; throw e; }
      if (!files.has(name) && opts.create) files.set(name, []);
      return handle(name);
    },
    async removeEntry(name) { if (!files.delete(name)) { const e = new Error('nf'); e.name = 'NotFoundError'; throw e; } },
  };
  return { files, async getDirectory() { return dir; }, async estimate() { return { quota, usage }; }, async persist() { return persist; } };
}

const { cachedDisc, cacheDisc, ejectDisc, CACHE_NAME, MARKER_NAME } = createRequire(import.meta.url)('../web/disc.js');

test('nothing cached yet', async () => {
  assert.equal(await cachedDisc(fakeStorage()), null);
});

test('cache then load round-trips the bytes and reports progress', async () => {
  const storage = fakeStorage();
  const data = new Uint8Array(3 * 1024 * 1024).map((_, i) => i & 255);
  const seen = [];
  await cacheDisc(new File([data], 'x.iso'), (done, total) => seen.push([done, total]), storage);
  const back = await cachedDisc(storage);
  assert.equal(back.size, data.length);
  assert.deepEqual(new Uint8Array(await back.slice(1000, 1010).arrayBuffer()), data.subarray(1000, 1010));
  assert.equal(seen.at(-1)[0], data.length);
  assert.equal(seen.at(-1)[1], data.length);
});

test('an interrupted copy (no marker, or wrong size) is treated as absent', async () => {
  const storage = fakeStorage();
  storage.files.set(CACHE_NAME, [new Uint8Array(5000)]);          // no marker
  assert.equal(await cachedDisc(storage), null);
  storage.files.set(MARKER_NAME, [new TextEncoder().encode('9999')]);  // marker disagrees with size
  assert.equal(await cachedDisc(storage), null);
  storage.files.set(MARKER_NAME, [new TextEncoder().encode('5000')]);
  assert.equal((await cachedDisc(storage)).size, 5000);
});

test('refuses to start when the quota is too small, naming the sizes', async () => {
  const storage = fakeStorage({ quota: 1000, usage: 500 });
  await assert.rejects(cacheDisc(new File([new Uint8Array(4000)], 'x.iso'), null, storage), (e) => e.name === 'QuotaError' && /need 0\.00 GB/.test(e.message) && /0\.00 GB free/.test(e.message));
  assert.equal(storage.files.size, 0);
});

test('eject removes both files and tolerates absence', async () => {
  const storage = fakeStorage();
  await cacheDisc(new File([new Uint8Array(10)], 'x.iso'), null, storage);
  await ejectDisc(storage);
  assert.equal(storage.files.size, 0);
  await ejectDisc(storage);
});

test('re-caching frees the old copy before checking quota', async () => {
  // fake whose usage counts the bytes it holds
  const storage = fakeStorage({ quota: 12000 });
  const base = storage.estimate;
  storage.estimate = async () => ({ quota: 12000, usage: [...storage.files.values()].flat().reduce((n, c) => n + (c.byteLength ?? c.size ?? 0), 0) });
  await cacheDisc(new File([new Uint8Array(8000)], 'a.iso'), null, storage);
  await cacheDisc(new File([new Uint8Array(9000)], 'b.iso'), null, storage);   // would fail if the 8000 still counted
  assert.equal((await cachedDisc(storage)).size, 9000);
});

test('an absurd root directory size is rejected, not read', async () => {
  const image = new Uint8Array(20 * SECTOR);
  const pvd = image.subarray(16 * SECTOR, 17 * SECTOR);
  pvd[0] = 1; pvd.set([67, 68, 48, 48, 49], 1); pvd[6] = 1;
  pvd.set(u32le(18), 158); pvd.set(u32le(0x7fffffff), 166);
  await assert.rejects(bootProgram(new Blob([image])), /root directory too large/);
});
