import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const { applyDeadZone, toPs2Axis } = createRequire(import.meta.url)('../web/pad.js');

test('rest position inside the dead zone is exactly centred', () => {
  assert.deepEqual(applyDeadZone(0.03, -0.05), { x: 0, y: 0 });
});

test('full deflection still reaches the rim', () => {
  const { x, y } = applyDeadZone(1, 0);
  assert.ok(Math.abs(x - 1) < 1e-9 && y === 0);
});

test('just outside the dead zone starts near zero, not at the zone edge', () => {
  const { x } = applyDeadZone(0.09, 0);
  assert.ok(x > 0 && x < 0.02, `got ${x}`);
});

test('diagonal is not clipped per axis', () => {
  const { x, y } = applyDeadZone(0.7071, 0.7071);
  assert.ok(Math.abs(x - y) < 1e-6 && Math.hypot(x, y) <= 1 + 1e-9);
});

test('ps2 axis mapping', () => {
  assert.equal(toPs2Axis(0), 128);
  assert.equal(toPs2Axis(-1), 0);
  assert.equal(toPs2Axis(1), 255);
  assert.equal(toPs2Axis(2), 255);
});
