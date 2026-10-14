import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
const { cardSaver, statsLine } = createRequire(import.meta.url)('../web/page.js');

// Timers the test turns by hand.
function fakeTimers() {
  let next = 1;
  const waiting = new Map();
  return {
    setTimeout: (fn, ms) => { waiting.set(next, { fn, ms }); return next++; },
    clearTimeout: (id) => { waiting.delete(id); },
    waiting,
    run() {
      const all = [...waiting.values()];
      waiting.clear();
      for (const t of all) t.fn();
    },
  };
}
// A store that finishes when the test says so.
function slowStore() {
  const started = [];
  return { store: (done) => started.push(done), started };
}

test('a burst of card writes is stored once, 300 ms after the last', () => {
  const timers = fakeTimers(), { store, started } = slowStore();
  const saver = cardSaver(store, 300, timers);
  saver.save();
  saver.save();
  saver.save();
  assert.equal(started.length, 0);
  assert.deepEqual([...timers.waiting.values()].map((t) => t.ms), [300]);
  timers.run();
  assert.equal(started.length, 1);
});

test('a save that is still waiting is stored at once when the page is hidden or closed', () => {
  const timers = fakeTimers(), { store, started } = slowStore();
  const saver = cardSaver(store, 300, timers);
  saver.save();
  assert.equal(saver.flush(), true);
  assert.equal(started.length, 1);
  assert.equal(timers.waiting.size, 0);      // and not a second time when the 300 ms are up
  assert.equal(saver.flush(), false);        // nothing waits any more
  assert.equal(started.length, 1);
});

test('with nothing saved, hiding the page stores nothing', () => {
  const timers = fakeTimers(), { store, started } = slowStore();
  assert.equal(cardSaver(store, 300, timers).flush(), false);
  assert.equal(started.length, 0);
});

test('everyone who asked hears when the card is stored', () => {
  const timers = fakeTimers(), { store, started } = slowStore();
  const saver = cardSaver(store, 300, timers);
  const heard = [];
  saver.save(() => heard.push('import'));
  saver.save();                              // the game saves right after: the first caller must not be forgotten
  saver.save(() => heard.push('second'));
  timers.run();
  assert.deepEqual(heard, []);
  started[0]();
  assert.deepEqual(heard, ['import', 'second']);
});

const SAMPLE = { frame: 13262, fps: 57.94, shown: 58.2, gpu: 71.4, emuIdle: 0.4, vuIdle: 0, gsIdle: 46.6, emuHeld: 22.6, heapMb: 307, audioMs: 26.3 };

test('the stats line keeps the fields the measurement scripts read', () => {
  const line = statsLine(SAMPLE);
  // The pattern of the planner's racestats.py, unchanged.
  const m = line.match(/frame (\d+)\s+([\d.]+) fps.*GPU (\d+)%\s+idle emu (\d+)% vu (\d+)% gs (\d+)%.*audio (\d+) ms/);
  assert.ok(m, line);
  assert.deepEqual(m.slice(1), ['13262', '57.9', '71', '0', '0', '47', '26']);
  assert.match(line, /\(58 on screen\)/);
  assert.match(line, /heap 307 MB/);
});

test('time the emulation thread is held is shown beside its idle time, not inside it', () => {
  const line = statsLine(SAMPLE);
  assert.match(line, /idle emu 0% vu 0% gs 47% {3}emu held 23%/);
  assert.doesNotMatch(statsLine({ ...SAMPLE, emuHeld: undefined }), /held/);   // a runtime without the figure
});

test('interpreted VU1 calls are shown only when there are any', () => {
  assert.doesNotMatch(statsLine(SAMPLE), /interpreted/);
  assert.match(statsLine({ ...SAMPLE, interpreted: 119.6 }), /audio 26 ms {3}vu1 interpreted 120\/s$/);
});

// The page cannot be run here, so at least: its script is valid JavaScript and uses what the modules give.
test('the shell loads the page modules and its own script parses', () => {
  const html = readFileSync(new URL('../web/shell.html', import.meta.url), 'utf8');
  for (const name of ['pad.js', 'disc.js', 'page.js']) assert.ok(html.includes('<script src="' + name + '"></script>'), name);
  const inline = html.match(/<script>\n([\s\S]*?)<\/script>/);
  assert.ok(inline, 'inline script');
  new vm.Script(inline[1], { filename: 'shell.html' });   // throws on a syntax error
  const pad = createRequire(import.meta.url)('../web/pad.js'), page = createRequire(import.meta.url)('../web/page.js');
  for (const [, object, member] of inline[1].matchAll(/\b(B3Pad|B3Page)\.(\w+)/g)) assert.ok(member in (object === 'B3Pad' ? pad : page), object + '.' + member);
});
