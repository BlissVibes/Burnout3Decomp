// Parts of the Burnout 3 page that need no browser. Classic script for shell.html (window.B3Page),
// CommonJS for the tests.
(function (root) {
  // The memory card goes to browser storage a moment after the game writes it, so a burst of writes
  // costs one store. store(done) does the storing and calls done() when it has ended; save(then) asks for
  // one and calls then() after it. flush() stores at once if a save is still waiting: the page calls it
  // when it is hidden or closed, because the timer may never fire after that.
  function cardSaver(store, delay = 300, timers = root) {
    let timer = 0, waiting = false, after = [];
    function run() {
      timers.clearTimeout(timer);
      waiting = false;
      const heard = after;
      after = [];
      store(() => { for (const then of heard) then(); });
    }
    return {
      save(then) {
        if (then) after.push(then);
        timers.clearTimeout(timer);
        waiting = true;
        timer = timers.setTimeout(run, delay);
      },
      flush() {
        if (!waiting) return false;
        run();
        return true;
      },
    };
  }

  // The page's stats line. The measurement scripts read "frame", "fps", "GPU", "idle emu .. vu .. gs .."
  // and "audio" out of it, so new figures go after those, never between or in place of them.
  // Shares are percentages of the time since the last line. emuIdle is the emulation thread asleep with
  // time to spare; emuHeld is the thread stopped until frames it has posted are drawn, which is not spare.
  function statsLine(s) {
    const pct = (v) => v.toFixed(0) + '%';
    return 'frame ' + s.frame + '   ' + s.fps.toFixed(1) + ' fps (' + s.shown.toFixed(0) + ' on screen)   GPU ' + pct(s.gpu) +
           '   idle emu ' + pct(s.emuIdle) + ' vu ' + pct(s.vuIdle) + ' gs ' + pct(s.gsIdle) +
           (s.emuHeld === undefined ? '' : '   emu held ' + pct(s.emuHeld)) +
           '   heap ' + s.heapMb + ' MB   audio ' + s.audioMs.toFixed(0) + ' ms' +
           (s.interpreted > 0 ? '   vu1 interpreted ' + Math.round(s.interpreted) + '/s' : '');
  }

  const api = { cardSaver, statsLine };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.B3Page = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
