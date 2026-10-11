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
