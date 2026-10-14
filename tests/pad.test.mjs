import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const { applyDeadZone, toPs2Axis, createInput } = createRequire(import.meta.url)('../web/pad.js');

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

// What the game was last told, as createInput's send() got it.
function listener() {
  const sent = [];
  const input = createInput((buttons, lx, ly, rx, ry) => sent.push({ buttons, lx, ly, rx, ry }));
  return { input, sent, last: () => sent[sent.length - 1] };
}
const REST = { buttons: 0, lx: 128, ly: 128, rx: 128, ry: 128 };

test('a held key reaches the game as its button', () => {
  const { input, last } = listener();
  assert.equal(input.keyDown('KeyX'), true);
  input.poll();
  assert.deepEqual(last(), { ...REST, buttons: 1 << 13 });
  input.keyUp('KeyX');
  input.poll();
  assert.deepEqual(last(), REST);
});

test('a key that is not the pad\'s is left to the browser', () => {
  const { input, sent } = listener();
  assert.equal(input.keyDown('KeyF'), false);
  input.poll();
  assert.deepEqual(sent, [REST]);
});

test('keys held when the window loses focus are let go', () => {
  const { input, last } = listener();
  input.keyDown('KeyX');
  input.keyDown('ArrowLeft');
  input.keyDown('KeyJ');
  input.poll();
  assert.deepEqual(last(), { ...REST, buttons: (1 << 13) | (1 << 7), lx: 0 });
  input.releaseKeys();          // the keyup never arrives: the window was not there to hear it
  input.poll();
  assert.deepEqual(last(), REST);
});

test('the game hears of the pad only when it changes', () => {
  const { input, sent } = listener();
  input.keyDown('Enter');
  for (let i = 0; i < 5; ++i) input.poll();
  assert.equal(sent.length, 1);
  input.keyUp('Enter');
  input.poll();
  input.poll();
  assert.equal(sent.length, 2);
});

test('touch controls and a gamepad join the keys', () => {
  const { input, last } = listener();
  const button = (pressed) => ({ pressed });
  const pad = { buttons: Array.from({ length: 16 }, (_, i) => button(i === 0 || i === 9)), axes: [1, 0, 0, -1] };
  input.keyDown('KeyA');
  input.poll({ buttons: 0x10, x: 0, y: 0, stick: false }, [null, pad]);
  assert.deepEqual(last(), { buttons: (1 << 15) | 0x10 | (1 << 14) | (1 << 3), lx: 255, ly: 128, rx: 128, ry: 0 });
  // The touch stick and the stick keys come before the gamepad's left stick.
  input.poll({ buttons: 0, x: -1, y: 1, stick: true }, [pad]);
  assert.deepEqual(last(), { buttons: (1 << 15) | (1 << 14) | (1 << 3), lx: 0, ly: 255, rx: 128, ry: 0 });
});
