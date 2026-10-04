/*
 * JS shell for the Imprint WASM demos (gh-pages).
 *
 * The host owns the frame loop (requestAnimationFrame), samples input
 * and presents the framebuffer. The C-ABI (zbapi.h) is exposed on the
 * Module via EXPORTED_FUNCTIONS / EXPORTED_RUNTIME_METHODS.
 *
 * The page declares the app's MINIMUM design size on the canvas element
 * (width/height attributes); the framebuffer is sized from the canvas's
 * laid-out CSS box (times devicePixelRatio, capped at 2) so the software
 * render is presented without a nearest-neighbor upscale, and the app is
 * re-created when the viewport resizes.
 */
(function () {
  "use strict";

  var canvas = document.getElementById("screen");
  var MIN_W = parseInt(canvas.getAttribute("width"), 10) || 320;
  var MIN_H = parseInt(canvas.getAttribute("height"), 10) || 240;
  var W = MIN_W, H = MIN_H; // framebuffer size, decided by decideSize()
  var PIXELS = W * H * 4;

  // software-rasterizer frame budget (see demo_common.js for the twin)
  var MAX_BUFFER_PIXELS = 1440 * 900;

  function decideSize() {
    var dpr = Math.max(1, Math.min(window.devicePixelRatio || 1, 2));
    var rect = canvas.getBoundingClientRect();
    var w = Math.round(Math.max(1, rect.width) * dpr);
    var h = Math.round(Math.max(1, rect.height) * dpr);
    if (w * h > MAX_BUFFER_PIXELS) {
      var s = Math.sqrt(MAX_BUFFER_PIXELS / (w * h));
      w = Math.round(w * s);
      h = Math.round(h * s);
    }
    return [Math.max(MIN_W, w), Math.max(MIN_H, h)];
  }

  function applySize(w, h) {
    W = w;
    H = h;
    PIXELS = W * H * 4;
    canvas.width = W;
    canvas.height = H;
    ctx = canvas.getContext("2d");
    imageData = ctx.createImageData(W, H);
    rgba = imageData.data;
  }

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

  /* translate a client coordinate into framebuffer space (W x H) */
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

  /* ---- app (re)creation: a resized buffer means a new app ---- */
  function createApp() {
    app = Module.ccall("zb_app_create", "number", ["number", "number"], [W, H]);
    if (!app) {
      status.textContent = "failed to create app";
      return false;
    }
    var closedCb = Module.addFunction(function () {
      // app-requested shutdown: only stop the frame loop -- this fires
      // from inside zb_paint, destroying the app here would be
      // use-after-free; wasm memory is reclaimed with the page
      closed = true;
      status.textContent = "app closed";
      status.style.color = "#aaa";
    }, "vi");
    Module.ccall("zb_set_closed_callback", null, ["number", "number", "number"], [app, closedCb, 0]);
    return true;
  }

  var resizeTimer = 0;
  window.addEventListener("resize", function () {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(function () {
      if (!app || closed) return;
      var s = decideSize();
      if (s[0] === W && s[1] === H) return;
      applySize(s[0], s[1]);
      // the old app keeps its closed-callback table slot; the wasm
      // memory is reclaimed with the page, so leaking one entry per
      // resize is bounded and harmless in a demo shell
      Module._zb_app_destroy(app);
      closed = false;
      if (!createApp()) return;
      status.textContent = "ready — " + W + "×" + H + " @ " + PIXELS + " B/frame";
    }, 300);
  });

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
      applySize.apply(null, decideSize());
      if (!createApp()) return;
      zbInput = Module.cwrap("zb_input", null, ["number", "number", "number", "number", "number", "number", "number"]);
      zbPaint = Module.cwrap("zb_paint", null, ["number"]);
      zbBuffer = Module.cwrap("zb_buffer", "number", ["number", "number", "number"]);
      wPtr = Module._malloc(8); // two uint32 out params
      hPtr = Module._malloc(8);
      Module.HEAPU32[wPtr >> 2] = 0;
      Module.HEAPU32[hPtr >> 2] = 0;

      // the app is authoritative on its own size (an app may clamp);
      // resync the present path if the buffer differs from the request
      var ptr = zbBuffer(app, wPtr, hPtr);
      if (ptr) {
        var aw = Module.HEAPU32[wPtr >> 2], ah = Module.HEAPU32[hPtr >> 2];
        if (aw && ah && (aw !== W || ah !== H)) applySize(aw, ah);
      }

      status.textContent = "ready — " + W + "×" + H + " @ " + PIXELS + " B/frame";
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
