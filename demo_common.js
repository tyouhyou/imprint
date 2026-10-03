/*
 * Shared JS shell for the Imprint UI gh-pages demo pages.
 *
 * The page configures it before the (SINGLE_FILE) wasm script loads:
 *
 *   <script>window.DEMO_CONFIG = { width: 256, height: 192,
 *                                  timeTravel: true };</script>
 *   <script src="demo_common.js"></script>
 *   <script src="g2048.js"></script>
 *
 * The host owns the frame loop (requestAnimationFrame), samples input and
 * presents the framebuffer. Two demo-only tools ride on the framework's
 * contracts, neither needs framework changes:
 *
 *  - repaint overlay: a per-frame JS diff of the framebuffer draws the
 *    bounding box of what actually changed -- retained-mode damage
 *    tracking made visible.
 *  - time travel: every input event is recorded; scrubbing the timeline
 *    re-creates the app and replays the truncated stream. This is only
 *    possible because a frame is a pure function of the input stream.
 */
(function () {
  "use strict";

  var CFG = window.DEMO_CONFIG || {};
  var W = CFG.width;
  var H = CFG.height;
  var PIXELS = W * H * 4;

  // input types, must match ZB_INPUT_* in zbapi.h
  var ZB_INPUT_TOUCH_DOWN = 9;
  var ZB_INPUT_TOUCH_UP = 10;
  var ZB_INPUT_TOUCH_MOVE = 11;
  var ZB_INPUT_KEY_DOWN = 12;
  var ZB_INPUT_KEY_UP = 13;
  var ZB_INPUT_MOUSE_WHEEL = 7;
  // ScrollPanel::wheel_step -- one notch = 32 px; delta > 0 is "up"
  var WHEEL_STEP = 32;

  // key codes, must match ZB_KEY_* in zbapi.h (ASCII used verbatim)
  var ZB_KEY_BACKSPACE = 8, ZB_KEY_TAB = 9, ZB_KEY_ENTER = 13, ZB_KEY_ESCAPE = 27, ZB_KEY_SPACE = 32;
  var ZB_KEY_DEL = 127;
  var ZB_KEY_UP = 256, ZB_KEY_DOWN = 257, ZB_KEY_LEFT = 258, ZB_KEY_RIGHT = 259;

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
  var rgba = imageData.data;

  // damage-overlay canvas sits exactly over #screen
  var overlay = document.getElementById("overlay");
  var octx = overlay ? overlay.getContext("2d") : null;
  if (overlay) {
    overlay.width = W;
    overlay.height = H;
  }
  var dmgToggle = document.getElementById("dmg");
  var prevFrame = null; // previous frame copy for the repaint diff

  var status = document.getElementById("status");
  var timeline = document.getElementById("timeline");
  var tlLabel = document.getElementById("tl-label");

  var app = null;
  var closed = false;
  var zbInput = null, zbPaint = null, zbBuffer = null;
  var wPtr = 0, hPtr = 0;

  /* ---- time travel: the recorded input stream ---- */
  var recording = [];
  var scrubbing = false;

  function scale(e) {
    var rect = canvas.getBoundingClientRect();
    var x = Math.floor((e.clientX - rect.left) * W / rect.width);
    var y = Math.floor((e.clientY - rect.top) * H / rect.height);
    return [Math.max(0, Math.min(W - 1, x)), Math.max(0, Math.min(H - 1, y))];
  }

  function rawSend(type, x, y, key, ch, touchId) {
    if (app && !closed && zbInput) {
      zbInput(app, type, x | 0, y | 0, key | 0, ch | 0, touchId | 0);
    }
  }

  // send + record (recording only while the app is live; replays go
  // through rawSend so history is not duplicated)
  function send(type, x, y, key, ch, touchId) {
    rawSend(type, x, y, key, ch, touchId);
    if (!scrubbing && CFG.timeTravel) {
      recording.push([type, x | 0, y | 0, key | 0, ch | 0, touchId | 0]);
      updateTimeline(true);
    }
  }

  function replay(n) {
    for (var i = 0; i < n && i < recording.length; i++) {
      var e = recording[i];
      rawSend(e[0], e[1], e[2], e[3], e[4], e[5]);
    }
  }

  function recreateApp() {
    if (app) Module._zb_app_destroy(app);
    closed = false;
    app = Module.ccall("zb_app_create", "number", ["number", "number"], [W, H]);
    rawSend(0, 0, 0, 0, 0, 0); // no-op: keeps the ABI warm, no recording
    paintNow();
  }

  function scrubTo(n) {
    scrubbing = true;
    recreateApp();
    replay(n);
    scrubbing = false;
  }

  // toEnd: the slider follows the live edge (a new event arrived / fresh
  // app); without it a scrub position must stay where the user put it
  function updateTimeline(toEnd) {
    if (!timeline) return;
    timeline.max = recording.length;
    if (toEnd) {
      timeline.value = recording.length;
    }
    if (tlLabel) {
      tlLabel.textContent = "move " + timeline.value + " / " + recording.length;
    }
  }

  function paintNow() {
    zbPaint(app);
    present();
  }

  /* ---- present + damage diff ---- */
  function present() {
    var ptr = zbBuffer(app, wPtr, hPtr);
    if (!ptr) return;
    var src = Module.HEAPU8.subarray(ptr, ptr + PIXELS);
    var i, j;
    for (i = 0, j = 0; i < PIXELS; i += 4, j += 4) {
      rgba[j]     = src[i + 2]; // r <- b
      rgba[j + 1] = src[i + 1];
      rgba[j + 2] = src[i];     // b <- r
      rgba[j + 3] = 0xff;
    }
    ctx.putImageData(imageData, 0, 0);

    // repaint overlay: bounding box of what changed since last frame
    if (octx) {
      octx.clearRect(0, 0, W, H);
      var showDmg = dmgToggle && dmgToggle.checked;
      if (showDmg) {
        var minX = W, minY = H, maxX = -1, maxY = -1;
        if (prevFrame) {
          for (i = 0; i < PIXELS; i += 4) {
            if (src[i] !== prevFrame[i] || src[i + 1] !== prevFrame[i + 1] ||
                src[i + 2] !== prevFrame[i + 2]) {
              var px = (i >> 2);
              var x = px % W, y = (px / W) | 0;
              if (x < minX) minX = x;
              if (x > maxX) maxX = x;
              if (y < minY) minY = y;
              if (y > maxY) maxY = y;
            }
          }
        }
        if (maxX >= 0) {
          octx.strokeStyle = "rgba(255,64,64,0.9)";
          octx.lineWidth = 1;
          octx.strokeRect(minX - 0.5, minY - 0.5, maxX - minX + 2, maxY - minY + 2);
        }
      }
      prevFrame = new Uint8Array(src); // copy: src is a live view
    }
  }

  function frame() {
    if (app && !closed) {
      zbPaint(app);
      present();
    }
    requestAnimationFrame(frame);
  }

  /* ---- input wiring ---- */
  function resolveKey(e) {
    if (KEY_MAP[e.key] !== undefined) return [KEY_MAP[e.key], 0];
    if (e.key === " ") return [ZB_KEY_SPACE, 0];
    if (e.key.length === 1) {
      var c = e.key.charCodeAt(0);
      if ((c >= 97 && c <= 122) || (c >= 48 && c <= 57)) return [c, c];
      if (c >= 65 && c <= 90) return [c + 32, c];
      return [0, c];
    }
    return null;
  }
  function onKeyDown(e) {
    var r = resolveKey(e);
    if (!r) return;
    if (r[0] === ZB_KEY_SPACE || (r[0] >= ZB_KEY_UP && r[0] <= ZB_KEY_RIGHT)) {
      e.preventDefault();
    }
    send(ZB_INPUT_KEY_DOWN, 0, 0, r[0], r[1], 0);
  }
  function onKeyUp(e) {
    var r = resolveKey(e);
    if (!r) return;
    send(ZB_INPUT_KEY_UP, 0, 0, r[0], 0, 0);
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
  function onWheel(e) {
    var notches = Math.round(e.deltaY / 100);
    if (notches === 0 && e.deltaY !== 0) notches = e.deltaY > 0 ? 1 : -1;
    e.preventDefault();
    var p = scale(e);
    send(ZB_INPUT_MOUSE_WHEEL, p[0], p[1], -notches * WHEEL_STEP, 0, 0);
  }

  canvas.addEventListener("mousedown", onDown);
  canvas.addEventListener("mousemove", onMove);
  window.addEventListener("mouseup", onUp);
  canvas.addEventListener("wheel", onWheel, { passive: false });
  canvas.addEventListener("touchstart", onTouch, { passive: false });
  canvas.addEventListener("touchmove", onTouch, { passive: false });
  canvas.addEventListener("touchend", onTouch, { passive: false });
  window.addEventListener("keydown", onKeyDown);
  window.addEventListener("keyup", onKeyUp);

  if (timeline) {
    timeline.addEventListener("input", function () {
      if (tlLabel) tlLabel.textContent = "move " + timeline.value + " / " + recording.length;
      scrubTo(parseInt(timeline.value, 10));
    });
  }

  function fail(msg) {
    status.textContent = msg;
    status.style.color = "#f66";
  }

  window.Module = {
    onAbort: function (what) { fail("aborted: " + what); },
    onRuntimeInitialized: function () {
      app = Module.ccall("zb_app_create", "number", ["number", "number"], [W, H]);
      if (!app) { status.textContent = "failed to create app"; return; }
      zbInput = Module.cwrap("zb_input", null, ["number", "number", "number", "number", "number", "number", "number"]);
      zbPaint = Module.cwrap("zb_paint", null, ["number"]);
      zbBuffer = Module.cwrap("zb_buffer", "number", ["number", "number", "number"]);
      wPtr = Module._malloc(8);
      hPtr = Module._malloc(8);
      Module.HEAPU32[wPtr >> 2] = 0;
      Module.HEAPU32[hPtr >> 2] = 0;

      var closedCb = Module.addFunction(function () {
        closed = true;
        status.textContent = "app closed";
        status.style.color = "#aaa";
      }, "vi");
      Module.ccall("zb_set_closed_callback", null, ["number", "number", "number"], [app, closedCb, 0]);

      status.textContent = "ready — " + W + "x" + H;
      updateTimeline(true);
      requestAnimationFrame(frame);
    }
  };

  window.addEventListener("unhandledrejection", function (e) {
    var why = e && e.reason && e.reason.message ? e.reason.message : String(e && e.reason);
    fail("failed to load wasm: " + why +
      (location.protocol === "file:" ? " (file:// blocked? use a local HTTP server)" : ""));
  });
})();
