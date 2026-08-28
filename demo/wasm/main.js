/*
 * JS shell for the WASM TicTacToe build.
 *
 * The host owns the frame loop (requestAnimationFrame), samples input
 * and presents the framebuffer. The C-ABI (zbapi.h) is exposed on the
 * Module via EXPORTED_FUNCTIONS / EXPORTED_RUNTIME_METHODS.
 */
(function () {
  "use strict";

  var W = 256; // framebuffer width  (COLOR_DEPTH=32, bgra32, 4 B/px)
  var H = 192; // framebuffer height
  var PIXELS = W * H * 4;

  // input types, must match ZB_INPUT_* in zbapi.h
  var ZB_INPUT_TOUCH_DOWN = 9;
  var ZB_INPUT_TOUCH_UP = 10;
  var ZB_INPUT_TOUCH_MOVE = 11;
  var ZB_INPUT_KEY_DOWN = 12;
  var ZB_INPUT_KEY_UP = 13;

  // key codes, must match ZB_KEY_* in zbapi.h (ASCII used verbatim)
  var ZB_KEY_BACKSPACE = 8, ZB_KEY_TAB = 9, ZB_KEY_ENTER = 13, ZB_KEY_ESCAPE = 27, ZB_KEY_SPACE = 32;
  var ZB_KEY_DEL = 127;
  var ZB_KEY_UP = 256, ZB_KEY_DOWN = 257, ZB_KEY_LEFT = 258, ZB_KEY_RIGHT = 259;

  // e.key values of the navigation/editing keys; printable characters
  // travel through the ch field instead
  var KEY_MAP = {
    ArrowLeft: ZB_KEY_LEFT,
    ArrowUp: ZB_KEY_UP,
    ArrowRight: ZB_KEY_RIGHT,
    ArrowDown: ZB_KEY_DOWN,
    Backspace: ZB_KEY_BACKSPACE,
    Delete: ZB_KEY_DEL,
    Tab: ZB_KEY_TAB,
    Enter: ZB_KEY_ENTER,
    Escape: ZB_KEY_ESCAPE
  };

  var canvas = document.getElementById("screen");
  canvas.width = W;
  canvas.height = H;
  var ctx = canvas.getContext("2d");
  var imageData = ctx.createImageData(W, H);
  var rgba = imageData.data; // bgra -> rgba scratch buffer

  var status = document.getElementById("status");

  var app = null;
  var closed = false; // app requested shutdown: stop driving input/paint
  var zbInput = null, zbPaint = null, zbBuffer = null;
  var wPtr = 0, hPtr = 0; // out params for zb_buffer

  /* translate a client coordinate into framebuffer space (256x192) */
  function scale(e) {
    var rect = canvas.getBoundingClientRect();
    var x = Math.floor((e.clientX - rect.left) * W / rect.width);
    var y = Math.floor((e.clientY - rect.top) * H / rect.height);
    return [Math.max(0, Math.min(W - 1, x)), Math.max(0, Math.min(H - 1, y))];
  }

  // the browser touch identifier becomes the framework's touch_id
  function send(type, x, y, key, ch, touchId) {
    if (app && !closed && zbInput) zbInput(app, type, x | 0, y | 0, key | 0, ch | 0, touchId | 0);
  }

  function onDown(e) {
    var p = scale(e);
    send(ZB_INPUT_TOUCH_DOWN, p[0], p[1], 0, 0, 0);
  }
  function onMove(e) {
    if (e.buttons === 0) return;
    var p = scale(e);
    send(ZB_INPUT_TOUCH_MOVE, p[0], p[1], 0, 0, 0);
  }
  function onUp(e) {
    var p = scale(e);
    send(ZB_INPUT_TOUCH_UP, p[0], p[1], 0, 0, 0);
  }
  function onTouch(e) {
    e.preventDefault();
    var t = e.changedTouches[0];
    var rect = canvas.getBoundingClientRect();
    var x = Math.max(0, Math.min(W - 1, Math.floor((t.clientX - rect.left) * W / rect.width)));
    var y = Math.max(0, Math.min(H - 1, Math.floor((t.clientY - rect.top) * H / rect.height)));
    var type = e.type === "touchstart" ? ZB_INPUT_TOUCH_DOWN
             : e.type === "touchmove" ? ZB_INPUT_TOUCH_MOVE
             : ZB_INPUT_TOUCH_UP;
    send(type, x, y, 0, 0, t.identifier);
  }

  /* navigation/editing keys fill key (ch 0); other single-char keys fill
   * ch (printable text), matching the shell convention (B2/B5) */
  function resolveKey(e) {
    if (KEY_MAP[e.key] !== undefined) {
      return [KEY_MAP[e.key], 0];
    }
    if (e.key === " ") {
      // space is a key (activation), never ch
      return [ZB_KEY_SPACE, 0];
    }
    if (e.key.length === 1) {
      return [0, e.key.charCodeAt(0)];
    }
    return null; // unknown non-printable key: send nothing
  }
  function onKeyDown(e) {
    var resolved = resolveKey(e);
    if (!resolved) {
      return;
    }
    var code = resolved[0], ch = resolved[1];
    if (code === ZB_KEY_SPACE || code === ZB_KEY_UP || code === ZB_KEY_DOWN ||
        code === ZB_KEY_LEFT || code === ZB_KEY_RIGHT) {
      e.preventDefault(); // don't scroll the page
    }
    send(ZB_INPUT_KEY_DOWN, 0, 0, code, ch, 0);
  }
  function onKeyUp(e) {
    var resolved = resolveKey(e);
    if (!resolved) {
      return;
    }
    send(ZB_INPUT_KEY_UP, 0, 0, resolved[0], 0, 0);
  }

  canvas.addEventListener("mousedown", onDown);
  canvas.addEventListener("mousemove", onMove);
  window.addEventListener("mouseup", onUp);
  canvas.addEventListener("touchstart", onTouch, { passive: false });
  canvas.addEventListener("touchmove", onTouch, { passive: false });
  canvas.addEventListener("touchend", onTouch, { passive: false });
  window.addEventListener("keydown", onKeyDown);
  window.addEventListener("keyup", onKeyUp);

  /* ---- frame loop: paint -> read framebuffer -> present ---- */
  function frame() {
    if (app && !closed) {
      zbPaint(app);

      var ptr = zbBuffer(app, wPtr, hPtr);
      if (ptr) {
        // wasm memory is a growable ArrayBuffer; subarray must be taken
        // from the live HEAPU8 view on every frame
        var src = Module.HEAPU8.subarray(ptr, ptr + PIXELS);
        var i, j;
        for (i = 0, j = 0; i < PIXELS; i += 4, j += 4) {
          rgba[j]     = src[i + 2]; // r <- b
          rgba[j + 1] = src[i + 1]; // g <- g
          rgba[j + 2] = src[i];     // b <- r
          rgba[j + 3] = 0xff;       // a
        }
        ctx.putImageData(imageData, 0, 0);
      }
    }
    requestAnimationFrame(frame);
  }

  /* ---- bind the C-ABI after the wasm runtime is ready ---- */
  function fail(msg) {
    status.textContent = msg;
    status.style.color = "#f66";
  }

  window.Module = {
    // runtime aborts (compile/OOM/...) otherwise die silently in the console
    onAbort: function (what) {
      fail("aborted: " + what);
    },
    onRuntimeInitialized: function () {
      app = Module.ccall("zb_app_create", "number", ["number", "number"], [W, H]);
      if (!app) {
        status.textContent = "failed to create app";
        return;
      }
      zbInput = Module.cwrap("zb_input", null, ["number", "number", "number", "number", "number", "number", "number"]);
      zbPaint = Module.cwrap("zb_paint", null, ["number"]);
      zbBuffer = Module.cwrap("zb_buffer", "number", ["number", "number", "number"]);
      wPtr = Module._malloc(8); // two uint32 out params
      hPtr = Module._malloc(8);
      Module.HEAPU32[wPtr >> 2] = 0;
      Module.HEAPU32[hPtr >> 2] = 0;

      // app-requested shutdown (e.g. the tictactoe QUIT button): stop the
      // frame loop. The app is NOT destroyed here -- the callback fires from
      // inside zb_paint, destroying it reentrantly would be use-after-free;
      // wasm memory is reclaimed with the page.
      var closedCb = Module.addFunction(function () {
        closed = true;
        status.textContent = "app closed";
        status.style.color = "#aaa";
      }, "vi");
      Module.ccall("zb_set_closed_callback", null, ["number", "number", "number"], [app, closedCb, 0]);

      status.textContent = "ready — 256x192 @ " + PIXELS + " B/frame";
      requestAnimationFrame(frame);
    }
  };

  // wasm fetch/instantiate failures reject without ever reaching onRuntimeInitialized
  window.addEventListener("unhandledrejection", function (e) {
    var why = e && e.reason && e.reason.message ? e.reason.message : String(e && e.reason);
    fail("failed to load wasm: " + why +
      (location.protocol === "file:" ? " (file:// blocked? rebuild with SINGLE_FILE or use a local HTTP server)" : ""));
  });
})();
