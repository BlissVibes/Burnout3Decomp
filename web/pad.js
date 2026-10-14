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

  // Keyboard: the pad bit each key sets, and the four keys of the left stick.
  const KEY_BITS = { ArrowUp: 4, ArrowRight: 5, ArrowDown: 6, ArrowLeft: 7, KeyZ: 14, KeyX: 13, KeyA: 15, KeyS: 12,
                     KeyQ: 10, KeyW: 11, Digit1: 8, Digit2: 9, Enter: 3, Backspace: 0 };
  const STICK_KEYS = ['KeyI', 'KeyJ', 'KeyK', 'KeyL'];
  // Gamepad: [button index in the standard layout, pad bit].
  const PAD_BUTTONS = [[0, 14], [1, 13], [2, 15], [3, 12], [4, 10], [5, 11], [6, 8], [7, 9], [8, 0], [9, 3], [10, 1], [11, 2],
                       [12, 4], [13, 6], [14, 7], [15, 5]];
  const NO_TOUCH = { buttons: 0, x: 0, y: 0, stick: false };

  // The pad as the game sees it: the keys held, joined with the touch controls and the first gamepad.
  // send(buttons, lx, ly, rx, ry) is called from poll() whenever that changes.
  function createInput(send) {
    const keys = new Set();
    let last = -1;
    return {
      // True when the key is one of the pad's (the page then keeps the browser from acting on it).
      keyDown(code) {
        if (!(code in KEY_BITS) && !STICK_KEYS.includes(code)) return false;
        keys.add(code);
        return true;
      },
      keyUp(code) { keys.delete(code); },
      // A key held while the window loses focus never reports its release, so the page lets go of them all.
      releaseKeys() { keys.clear(); },
      poll(touchPad = NO_TOUCH, gamepads = []) {
        let buttons = touchPad.buttons, lx = 128, ly = 128, rx = 128, ry = 128;
        for (const k of keys) if (k in KEY_BITS) buttons |= 1 << KEY_BITS[k];
        if (keys.has('KeyJ')) lx = 0;
        if (keys.has('KeyL')) lx = 255;
        if (keys.has('KeyI')) ly = 0;
        if (keys.has('KeyK')) ly = 255;
        if (touchPad.stick) {
          lx = toPs2Axis(touchPad.x);
          ly = toPs2Axis(touchPad.y);
        }
        for (const pad of gamepads) {
          if (!pad) continue;
          for (const [index, bit] of PAD_BUTTONS) if (pad.buttons[index] && pad.buttons[index].pressed) buttons |= 1 << bit;
          if (pad.axes.length >= 4) {
            const left = applyDeadZone(pad.axes[0], pad.axes[1]);
            const right = applyDeadZone(pad.axes[2], pad.axes[3]);
            if (lx === 128) lx = toPs2Axis(left.x);
            if (ly === 128) ly = toPs2Axis(left.y);
            rx = toPs2Axis(right.x);
            ry = toPs2Axis(right.y);
          }
          break;
        }
        const state = buttons + lx * 65536 + ly * 16777216 + rx * 4294967296 + ry * 1099511627776;
        if (state !== last) {
          last = state;
          send(buttons, lx, ly, rx, ry);
        }
      },
    };
  }
  const api = { applyDeadZone, toPs2Axis, createInput };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.B3Pad = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
