// node smoke test for the wasm story builds (run in the emscripten image)
// usage: node demo/wasm/smoke.js [path to <story>_mod.js]
const path = require("path");
const OUT = process.argv[2] || path.join(__dirname, "tictactoe_mod.js");
const DIR = path.dirname(OUT);
const NAME = path.basename(OUT).replace(/_mod\.js$/, "");
const IS_SHOWCASE = NAME.indexOf("showcase") === 0;

// screen sizes: the ORION / showcase_html / life / seedmap canvases
// design at 320x240; tictactoe and the portal games stay 256x192
const SIZES = {
    showcase: [320, 240],
    showcase_html: [320, 240],
    life: [320, 240],
    seedmap: [320, 240],
    seq303: [800, 520],
    drift: [800, 520],
    sandbox: [800, 520],
    firefly: [800, 520],
    windloom: [800, 520],
    rotafarm: [800, 520],
    comet: [800, 520],
    crystal: [800, 520],
    moonpull: [800, 520],
    fogkeeper: [800, 520],
    hoursmith: [800, 520]
};
const SZ = SIZES[NAME] || [256, 192];
const W = SZ[0], H = SZ[1];

const createModule = require(path.resolve(OUT));

createModule({
  // the modularized node build loads the .wasm from disk next to the .js
  locateFile: function (file) {
    return path.join(DIR, path.basename(file));
  },
  onRuntimeInitialized: function () {
    const Module = this;
    try {
      const app = Module._zb_app_create(W, H);
      if (!app) throw new Error("zb_app_create failed");

      Module.ccall("zb_input", null, ["number", "number", "number", "number", "number", "number", "number"],
        [app, 9, 128, 96, 0, 0, 0]); // touch down (finger 0)
      Module.ccall("zb_input", null, ["number", "number", "number", "number", "number", "number", "number"],
        [app, 10, 128, 96, 0, 0, 0]); // touch up (finger 0)
      Module.ccall("zb_input", null, ["number", "number", "number", "number", "number", "number", "number"],
        [app, 12, 0, 0, 13, 0, 0]); // enter
      Module.ccall("zb_input", null, ["number", "number", "number", "number", "number", "number", "number"],
        [app, 12, 0, 0, 0, 65, 0]); // printable character via the ch field

      Module.ccall("zb_paint", null, ["number"], [app]);

      const w = Module._malloc(4), h = Module._malloc(4);
      const ptr = Module.ccall("zb_buffer", "number", ["number", "number", "number"], [app, w, h]);
      const ww = Module.HEAPU32[w >> 2], hh = Module.HEAPU32[h >> 2];
      console.log("buffer size:", ww + "x" + hh, "ptr:", ptr);

      let nonzero = 0;
      for (let i = 0; i < ww * hh * 4; i += 4) {
        const px = Module.HEAPU8[ptr + i] | Module.HEAPU8[ptr + i + 1] |
                   Module.HEAPU8[ptr + i + 2] | Module.HEAPU8[ptr + i + 3];
        if (px !== 0) nonzero++;
      }
      const first = [Module.HEAPU8[ptr], Module.HEAPU8[ptr + 1], Module.HEAPU8[ptr + 2], Module.HEAPU8[ptr + 3]];
      console.log("non-zero pixels:", nonzero, "first px (bgra):", first.join(","));

      if (ww !== W || hh !== H || nonzero === 0) {
        console.error("SMOKE TEST FAILED");
        process.exit(1);
      }

      // ---- shared helpers (app-parameterized) ----
      const input = function (a, type, x, y, key, ch, tid) {
        Module.ccall("zb_input", null, ["number", "number", "number", "number", "number", "number", "number"],
          [a, type, x, y, key, ch, tid]);
      };
      const paint = function (a) { Module.ccall("zb_paint", null, ["number"], [a]); };
      const keyDown = function (a, code, ch) { input(a, 12, 0, 0, code, ch || 0, 0); };
      const click = function (a, x, y) {
        input(a, 9, x, y, 0, 0, 0);
        input(a, 10, x, y, 0, 0, 0);
      };
      const snapOf = function (a) {
        paint(a);
        const buf = Module.ccall("zb_buffer", "number", ["number", "number", "number"], [a, w, h]);
        return Module.HEAPU8.slice(buf, buf + W * H * 4);
      };
      const same = function (a, b) {
        if (a.length !== b.length) return false;
        for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
        return true;
      };
      const ZK_LEFT = 258, ZK_UP = 256, ZK_RIGHT = 259, ZK_DOWN = 257;

      if (IS_SHOWCASE) {
        // ORION NX-07: the STAR MAP modal round trip, keyboard-driven
        // ('m' toggles per the design's KEYS hint) so the smoke needs no
        // pixel coordinates -- picking inside a clipped flex container
        // would depend on layout details the smoke should not encode.
        // Pixel (10,120) sits in the left panel margin: static across
        // frames (the desktop battery probes the same pixel), so the
        // live telemetry cannot confound the comparison, and the modal's
        // full-screen rgba(1,3,8,0.72) veil dims it while open.
        const pxAt = function (buf, x, y) {
          const o = (y * W + x) * 4;
          return buf[o] + buf[o + 1] + buf[o + 2];
        };
        const before = snapOf(app);
        keyDown(app, 0, 109);  // 'm' -> STAR MAP modal opens
        const dimmed = snapOf(app);
        if (pxAt(before, 10, 120) === pxAt(dimmed, 10, 120)) {
          throw new Error("STAR MAP modal did not dim the console");
        }
        keyDown(app, 0, 109);  // 'm' again -> modal closes
        const restored = snapOf(app);
        if (pxAt(restored, 10, 120) !== pxAt(before, 10, 120)) {
          throw new Error("modal close did not restore the console");
        }
        Module._zb_app_destroy(app);
        Module._free(w); Module._free(h);
        console.log("showcase star-map modal: ok");
        console.log("SMOKE TEST OK");
        process.exit(0);
      }

      if (NAME === "g2048") {
        // tiles are child Labels of the board Panel -- they render only
        // if the draw_at override chains the base (a silent bug found in
        // the browser). Count tile-palette pixels on a fresh board: both
        // start tiles are 2 (238/228/218) or 4 (237/224/200), channel
        // sums are unique against the board bg (520) and empty cell (578).
        const tilePix = function (buf) {
          let n = 0;
          for (let i = 0; i < buf.length; i += 4) {
            const s = buf[i] + buf[i + 1] + buf[i + 2];
            if (s === 238 + 228 + 218 || s === 237 + 224 + 200) n++;
          }
          return n;
        };
        const fresh = tilePix(snapOf(app));
        if (fresh < 600) {
          throw new Error("fresh board has no visible tiles (" + fresh + " tile px)");
        }

        // arrows move the board (pixels change after some direction)
        let moved = false;
        for (const k of [ZK_LEFT, ZK_UP, ZK_RIGHT, ZK_DOWN]) {
          const before = snapOf(app);
          keyDown(app, k, 0);
          if (!same(before, snapOf(app))) { moved = true; break; }
        }
        if (!moved) throw new Error("no arrow key moved the 2048 board");

        // deterministic replay: same fresh app + same input stream =
        // byte-identical frames (the time-travel demo's foundation)
        const replay = function () {
          const a = Module._zb_app_create(W, H);
          const script = [ZK_LEFT, ZK_UP, ZK_RIGHT, ZK_LEFT, ZK_DOWN, ZK_RIGHT];
          const frames = [];
          for (const k of script) {
            keyDown(a, k, 0);
            frames.push(snapOf(a));
          }
          Module._zb_app_destroy(a);
          return frames;
        };
        const r1 = replay(), r2 = replay();
        for (let i = 0; i < r1.length; i++) {
          if (!same(r1[i], r2[i])) {
            throw new Error("replay frame " + i + " diverged (determinism broken)");
          }
        }
        console.log("g2048: arrows move, replay byte-identical over " + r1.length + " frames");
        Module._zb_app_destroy(app);
        Module._free(w); Module._free(h);
        console.log("SMOKE TEST OK");
        process.exit(0);
      }

      if (NAME === "mines") {
        // the field sits at (60,30): tap the first cell, it must reveal
        const before = snapOf(app);
        click(app, 68, 38);
        const after = snapOf(app);
        if (same(before, after)) throw new Error("first reveal did not change the field");
        // revealed number cells carry digit ink -- pixels inside the
        // field box that are none of the flat slot colors (covered
        // AE/C0/CF, revealed D6/E2/EB, border 6E/7F/8D, board bg
        // 7B/8A/97). The runtime-TTF digits antialias, so the ink count
        // is checked against the slot palette, not exact glyph colors
        // (proves the child Labels render through the draw_at override)
        const inkAt = function (buf) {
          let n = 0;
          for (let y = 30; y < 166; ++y) {
            for (let x = 60; x < 196; ++x) {
              const o = (y * W + x) * 4;
              const s = buf[o] + buf[o + 1] + buf[o + 2];
              if (s !== 413 && s !== 753 && s !== 394 && s !== 412) n++;
            }
          }
          return n;
        };
        if (inkAt(after) < 8) {
          throw new Error("no digit ink visible after reveal");
        }
        // right-click (ZB_INPUT_MOUSE_RIGHT_DOWN=4) flags the first cell
        // in place; a second right-click unflags it (the browser maps
        // mouse button 2 onto this family)
        const fresh = Module._zb_app_create(W, H);
        const pristine = snapOf(fresh);
        input(fresh, 4, 68, 38, 0, 0, 0);
        const flagged = snapOf(fresh);
        if (same(pristine, flagged)) throw new Error("right-click did not flag");
        input(fresh, 4, 68, 38, 0, 0, 0);
        if (!same(pristine, snapOf(fresh))) throw new Error("second right-click did not unflag");
        Module._zb_app_destroy(fresh);
        console.log("mines: reveal + right-click flag ok");
        Module._zb_app_destroy(app);
        Module._free(w); Module._free(h);
        console.log("SMOKE TEST OK");
        process.exit(0);
      }

      if (NAME === "life") {
        // plasma is the default scene and paints continuously as a pure
        // function of the frame counter
        const p1 = snapOf(app);
        const p2 = snapOf(app);
        if (same(p1, p2)) throw new Error("plasma does not advance per paint");
        // the LIFE button (third in the row) switches to the colony and
        // seeds the glider gun; the colony cells render in the exact
        // green 0x7FD47F (channel sum 466), which neither plasma nor the
        // starfield produces in quantity
        const greenAt = function (buf) {
          let n = 0;
          for (let i = 0; i < buf.length; i += 4) {
            if (buf[i] + buf[i + 1] + buf[i + 2] === 466) n++;
          }
          return n;
        };
        click(app, 162, 19);   // LIFE button (128..180 @ u=1)
        const a = snapOf(app);
        for (let i = 0; i < 16; i++) paint(app);  // two colony steps
        const b = snapOf(app);
        if (same(a, b)) throw new Error("life colony did not evolve across paints");
        if (greenAt(b) < 40) throw new Error("no green colony pixels (" + greenAt(b) + ")");
        // back to PLASMA: the green colony disappears
        click(app, 42, 19);
        if (greenAt(snapOf(app)) > 10) throw new Error("PLASMA did not replace the colony");
        console.log("life: plasma advances, colony evolves");
        Module._zb_app_destroy(app);
        Module._free(w); Module._free(h);
        console.log("SMOKE TEST OK");
        process.exit(0);
      }

      if (NAME === "seedmap") {
        // same seed + DRAW = the same map, byte for byte (the demo's
        // determinism selling point). Only the map region below the
        // header (y >= 40) compares: clicking a button leaves a focus
        // outline in the header, which is presentational state, not map.
        const MAP_Y = 40 * W * 4;
        const sameMap = function (a, b) { return same(a.subarray(MAP_Y), b.subarray(MAP_Y)); };
        const before = snapOf(app);
        click(app, 130, 19);   // DRAW (same seed)
        if (!sameMap(before, snapOf(app))) {
          throw new Error("same seed produced a different map");
        }
        // a different seed produces a different map
        click(app, 53, 19);    // focus the seed box
        keyDown(app, 122, 122);  // 'z' lands at the caret
        click(app, 130, 19);   // DRAW
        if (sameMap(before, snapOf(app))) throw new Error("new seed did not change the map");
        console.log("seedmap: same seed byte-identical, new seed differs");
        Module._zb_app_destroy(app);
        Module._free(w); Module._free(h);
        console.log("SMOKE TEST OK");
        process.exit(0);
      }

      if (NAME === "playground") {
        // the declarative C-ABI path the playground page drives: build a
        // design file, register a callback, click, read the text back
        const strPtr = function (s) {
          const p = Module._malloc(s.length + 1);
          for (let i = 0; i < s.length; i++) Module.HEAPU8[p + i] = s.charCodeAt(i);
          Module.HEAPU8[p + s.length] = 0;
          return p;
        };
        const ui = [
          'panel id="root" width=256 height=192',
          '  label id="out" text="0"',
          '  button id="add" text="Add" width=200 height=100'
        ].join("\n");
        const uiBuf = strPtr(ui);

        const declApp = Module.ccall("zb_app_create_from_ui", "number",
          ["number", "number", "number", "number"], [uiBuf, 0, 256, 192]);
        if (!declApp) throw new Error("zb_app_create_from_ui failed");

        let clicks = 0;
        const cb = Module.addFunction(function () {
          clicks++;
          Module.ccall("zb_widget_set_text", null, ["number", "number", "number"],
            [declApp, strPtr("out"), strPtr(String(clicks))]);
        }, "vii");
        Module.ccall("zb_set_event_callback", null, ["number", "number", "number", "number"],
          [declApp, strPtr("add"), cb, 0]);

        // the declarative root is a FlexPanel: layout moves children from
        // their node positions on the first paint, so click the big
        // button only after the layout has settled (its flex slot keeps
        // (50,50) covered either way)
        paint(declApp);
        for (let i = 0; i < 3; i++) {
          input(declApp, 9, 50, 50, 0, 0, 0);
          input(declApp, 10, 50, 50, 0, 0, 0);
        }
        paint(declApp);
        const out = Module._malloc(32), cap = 32;
        Module.ccall("zb_widget_text", "number", ["number", "number", "number", "number"],
          [declApp, strPtr("out"), out, cap]);
        let len = 0;
        while (Module.HEAPU8[out + len]) len++;
        const text = String.fromCharCode.apply(null, Module.HEAPU8.subarray(out, out + len));
        if (clicks !== 3 || text !== "3") {
          throw new Error("declarative roundtrip broken (clicks=" + clicks + ", text='" + text + "')");
        }
        console.log("playground: declarative C-ABI roundtrip ok (clicks=" + clicks + ", text=" + text + ")");
        Module._zb_app_destroy(declApp);
        Module._free(uiBuf); Module._free(out);
        Module._free(w); Module._free(h);
        console.log("SMOKE TEST OK");
        process.exit(0);
      }

      if (NAME === "seq303") {
        // transport: PLAY at buffer (124,16)..(212,68), the pattern
        // strip (LEAD/BASS, A..D, CHAIN, COPY) at y 76..124 (u=2 at the
        // 800x520 web tier). After 30 paints the lookahead queue must
        // carry both voices' events with advancing global-step tags
        const drain = function (a) {
          const out = [];
          for (;;) {
            const ev = Module.ccall("seq303_note_pop", "number");
            if (ev < 0) break;
            out.push(ev);
          }
          return out;
        };
        click(app, 168, 42);  // PLAY
        let notes = [];
        for (let i = 0; i < 30; i++) {
          paint(app);
          notes = notes.concat(drain(app));
        }
        if (Module.ccall("seq303_is_playing", "number") !== 1) {
          throw new Error("PLAY did not start the sequencer");
        }
        if (notes.length < 4) throw new Error("no note events after PLAY (" + notes.length + ")");
        let maxG = 0, lead = 0, bass = 0;
        for (const ev of notes) {
          const row = ev & 15, layer = (ev >> 4) & 1;
          if (ev >> 7 > maxG) maxG = ev >> 7;
          if (row < 8 && layer === 0) lead++;
          if (row < 8 && layer === 1) bass++;
        }
        if (maxG < 3) throw new Error("step counter did not advance (maxG=" + maxG + ")");
        if (lead === 0) throw new Error("no LEAD voice events");
        if (bass === 0) throw new Error("no BASS voice events (preloaded dice fills both)");
        // the BASS tab switches the editing layer (param 15 mirrors it)
        click(app, 268, 100);
        if (Module.ccall("seq303_param", "number", ["number"], [15]) !== 1) {
          throw new Error("BASS tab did not switch the layer");
        }
        click(app, 168, 100);  // back to LEAD
        // pattern B: queued while playing, takes over at the bar wrap;
        // param 22 mirrors the playing slot, so the quantized switch is
        // observable without pixels. Then COPY queues C the same way.
        click(app, 444, 100);  // B
        for (let i = 0; i < 130; i++) { paint(app); drain(app); }
        if (Module.ccall("seq303_param", "number", ["number"], [22]) !== 1) {
          throw new Error("pattern B did not take over at the bar wrap");
        }
        click(app, 746, 100);  // COPY (B -> C queued)
        for (let i = 0; i < 130; i++) { paint(app); drain(app); }
        if (Module.ccall("seq303_param", "number", ["number"], [22]) !== 2) {
          throw new Error("COPY did not queue the next slot");
        }
        // determinism: the same taps + paints + drains on two fresh
        // apps render byte-identical frames (the queue is a
        // process-global mirror, so both runs drain it after every
        // paint)
        const replay303 = function () {
          const a = Module._zb_app_create(W, H);
          input(a, 9, W >> 1, H >> 1, 0, 0, 0);  // mirror the preamble tap
          input(a, 10, W >> 1, H >> 1, 0, 0, 0);
          click(a, 168, 42);   // PLAY
          click(a, 268, 100);  // BASS tab
          click(a, 168, 100);  // LEAD tab
          click(a, 444, 100);  // pattern B (queued)
          const frames = [];
          for (let i = 0; i < 90; i++) {
            paint(a);
            drain(a);
            if (i % 15 === 14) frames.push(snapOf(a));
          }
          Module._zb_app_destroy(a);
          return frames;
        };
        const r1 = replay303(), r2 = replay303();
        for (let i = 0; i < r1.length; i++) {
          if (!same(r1[i], r2[i])) throw new Error("seq303 replay frame " + i + " diverged");
        }
        console.log("seq303: both voices flow, layers/patterns switch, replay byte-identical");
        Module._zb_app_destroy(app);
        Module._free(w); Module._free(h);
        console.log("SMOKE TEST OK");
        process.exit(0);
      }

      if (NAME === "drift") {
        // the feed is a pure function of the frame counter: two apps
        // painted the same number of times render byte-identical frames,
        // and consecutive sample frames differ (the feed animates)
        const runDrift = function () {
          const a = Module._zb_app_create(W, H);
          const frames = [];
          for (let i = 0; i < 90; i++) {
            paint(a);
            if (i === 10 || i === 80) frames.push(snapOf(a));
          }
          Module._zb_app_destroy(a);
          return frames;
        };
        const r1 = runDrift(), r2 = runDrift();
        if (same(r1[0], r1[1])) throw new Error("the feed does not animate between frames");
        for (let i = 0; i < r1.length; i++) {
          if (!same(r1[i], r2[i])) throw new Error("drift replay frame " + i + " diverged");
        }
        console.log("drift: feed animates, replay byte-identical");
        Module._zb_app_destroy(app);
        Module._free(w); Module._free(h);
        console.log("SMOKE TEST OK");
        process.exit(0);
      }

      if (NAME === "sandbox") {
        // the preloaded world settles under gravity within 60 frames,
        // and a stroked input stream replays byte-identically (the
        // page's "same input, same world" promise)
        const first = snapOf(app);
        for (let i = 0; i < 60; i++) paint(app);
        if (same(first, snapOf(app))) throw new Error("the preloaded world does not evolve");
        const replaySand = function () {
          const a = Module._zb_app_create(W, H);
          input(a, 9, 300, 200, 0, 0, 0);
          input(a, 11, 340, 210, 0, 0, 0);
          input(a, 11, 380, 220, 0, 0, 0);
          input(a, 10, 380, 220, 0, 0, 0);
          const frames = [];
          for (let i = 0; i < 40; i++) {
            paint(a);
            if (i % 10 === 9) frames.push(snapOf(a));
          }
          Module._zb_app_destroy(a);
          return frames;
        };
        const r1 = replaySand(), r2 = replaySand();
        for (let i = 0; i < r1.length; i++) {
          if (!same(r1[i], r2[i])) throw new Error("sandbox replay frame " + i + " diverged");
        }
        console.log("sandbox: world evolves, stroked replay byte-identical");
        Module._zb_app_destroy(app);
        Module._free(w); Module._free(h);
        console.log("SMOKE TEST OK");
        process.exit(0);
      }

      if (NAME === "firefly") {
        // intro card -> START (card centered under the 32u HUD strip at
        // the 800x520 web tier); the night then runs as a pure function
        // of the frame counter, and a lantern stroke replays
        // byte-identically (the determinism contract)
        const intro = snapOf(app);
        click(app, 400, 332);  // START (card bottom, u=2 tier)
        let a = snapOf(app), b = null;
        for (let i = 0; i < 90; i++) {
          paint(app);
          if (i === 45) a = snapOf(app);
        }
        b = snapOf(app);
        if (same(a, b)) throw new Error("the night does not advance between frames");
        const replayFly = function () {
          const f = Module._zb_app_create(W, H);
          const frames = [];
          click(f, 400, 332);  // START
          for (let i = 0; i < 90; i++) {
            if (i === 10) { input(f, 9, 300, 200, 0, 0, 0); }
            if (i === 40) { input(f, 11, 340, 230, 0, 0, 0); }
            if (i === 60) { input(f, 10, 340, 230, 0, 0, 0); }
            paint(f);
            if (i % 20 === 19) frames.push(snapOf(f));
          }
          Module._zb_app_destroy(f);
          return frames;
        };
        const r1 = replayFly(), r2 = replayFly();
        for (let i = 0; i < r1.length; i++) {
          if (!same(r1[i], r2[i])) throw new Error("firefly replay frame " + i + " diverged");
        }
        console.log("firefly: night runs, lantern stroke replay byte-identical");
        Module._zb_app_destroy(app);
        Module._free(w); Module._free(h);
        console.log("SMOKE TEST OK");
        process.exit(0);
      }

      // ---- the 3D stories (moonpull/fogkeeper/hoursmith, 2026-10-26/27):
      // the rgba32/le pixel model renders byte 0 = red, so the pixel
      // probes read r,g,b in natural order. Same contract as the rest:
      // world advances on its own, and a stroke replay is byte-identical
      if (NAME === "moonpull" || NAME === "fogkeeper" || NAME === "hoursmith") {
        const livePx = function (a, x, y) {
          paint(a);
          const b = Module.ccall("zb_buffer", "number", ["number", "number", "number"], [a, w, h]);
          const o = (y * W + x) * 4;
          return [Module.HEAPU8[b + o], Module.HEAPU8[b + o + 1], Module.HEAPU8[b + o + 2]];
        };
        // the frame is water at the lower left (moonpull/fogkeeper) or
        // warm stone plaza (hoursmith): r,b split tells them apart
        const sea = livePx(app, 100, H - 60);
        if (NAME === "hoursmith") {
          // the plaza's lower center: warm stone, r >= g >= b
          const stone = livePx(app, 400, H - 102);
          if (!(stone[0] > 110 && stone[0] >= stone[1] && stone[1] >= stone[2])) {
            throw new Error(NAME + ": no stone plaza at the lower center (r,g,b " + stone.join(",") + ")");
          }
          // above the plaza's far rim the dawn sky shows: dark and cool
          const sky = livePx(app, 30, 100);
          if (sky[0] + sky[1] + sky[2] > 220 || sky[2] < sky[0]) {
            throw new Error(NAME + ": top-left corner is not dawn sky (r,g,b " + sky.join(",") + ")");
          }
          // PICK/DRAG LOCK (2026-10-27 review): the canvas widget sits
          // below the HUD strip and the dispatcher delivers events in
          // WINDOW space, so on_input must translate before hit-testing.
          // The probe drags the level-1 stone from its projected window
          // position and requires the stone to land under the cursor --
          // with the translation missing, the pick misses by the HUD
          // height and the stone never moves. The camera mirror below
          // must track DialCanvas::build_camera.
          {
            const HUD = 68;  // 34 * u, u = h/240 at 520
            const vh = H - HUD;
            const FY = -185 / 315, FZ = 255 / 315, VS = W / (2 * 0.68),
                  HZ = 0.36 * vh;
            const proj = function (x, y, z) {
              const ry = y - 185, rz = z + 255;
              const v = ry * FY + rz * FZ;
              const up = ry * FZ + rz * 0.58729;
              return [W / 2 + x * VS / v, HZ - up * VS / v + HUD];
            };
            const crimsonNear = function (px, py, rad) {
              const b = Module.ccall("zb_buffer", "number", ["number", "number", "number"], [app, w, h]);
              for (let y = py - rad; y <= py + rad; y++) {
                for (let x = px - rad; x <= px + rad; x++) {
                  if (x < 0 || y < 0 || x >= W || y >= H) continue;
                  const o = (y * W + x) * 4;
                  const r = Module.HEAPU8[b + o], g = Module.HEAPU8[b + o + 1],
                        bl = Module.HEAPU8[b + o + 2];
                  if (r > 120 && r - g > 50 && g - bl > 8 && bl < 100) return true;
                }
              }
              return false;
            };
            const s0 = proj(36, 0, 84);   // the level-1 stone's base
            const t0 = proj(-34, 0, -40); // a plain patch of plaza
            input(app, 9, Math.round(s0[0]), Math.round(s0[1]) - 6, 0, 0, 0);
            for (let s = 1; s <= 8; s++) {
              input(app, 11,
                Math.round(s0[0] + (t0[0] - s0[0]) * s / 8),
                Math.round(s0[1] - 6 + (t0[1] - (s0[1] - 6)) * s / 8), 0, 0, 0);
            }
            input(app, 10, Math.round(t0[0]), Math.round(t0[1]), 0, 0, 0);
            paint(app);
            if (!crimsonNear(Math.round(t0[0]), Math.round(t0[1]), 10)) {
              throw new Error(NAME + ": dragged stone did not land under the cursor");
            }
            if (crimsonNear(Math.round(s0[0]), Math.round(s0[1]), 14)) {
              throw new Error(NAME + ": dragged stone stayed at its start");
            }
          }
          // SHADOW-AREA LOCK (2026-10-27 review): the dial clip is a
          // Sutherland-Hodgman circle clip, and taking the wrong quadratic
          // root for an ENTRY crossing silently drops a wedge of every
          // long shadow. It only bites when the shadow actually leaves the
          // plaza -- dawn and dusk, where the stone's tip runs past the
          // rim (measured: 508 px with the bad root, 1113 with the fix at
          // frame 30) -- so the census runs at dawn, not at a noon-ish
          // hour where nothing is clipped.
          {
            const f = Module._zb_app_create(W, H);
            for (let i = 0; i < 30; i++) paint(f);
            const b = Module.ccall("zb_buffer", "number", ["number", "number", "number"], [f, w, h]);
            let ink = 0;
            for (let y = 100; y < 340; y++) {
              for (let x = 180; x < 700; x++) {
                const o = (y * W + x) * 4;
                const r = Module.HEAPU8[b + o], g = Module.HEAPU8[b + o + 1],
                      bl = Module.HEAPU8[b + o + 2];
                if (r < 45 && g < 50 && bl > r + 10 && bl < 80) ink++;
              }
            }
            Module._zb_app_destroy(f);
            if (ink < 800) {
              throw new Error(NAME + ": dawn shadow is only " + ink + " px (clip wedge lost?)");
            }
          }
          // the shell's level key ('3' via ch): three colored wells
          // appear -- scan for the amber one right of the tower
          input(app, 12, 0, 0, 0, 51, 0);
          let heroFound = false;
          {
            paint(app);
            const b = Module.ccall("zb_buffer", "number", ["number", "number", "number"], [app, w, h]);
            for (let y = 220; y < 340 && !heroFound; y += 2) {
              for (let x = 430; x < 720; x += 2) {
                const o = (y * W + x) * 4;
                const r = Module.HEAPU8[b + o], g2 = Module.HEAPU8[b + o + 1],
                      b2 = Module.HEAPU8[b + o + 2];
                if (r > 140 && r - b2 > 60 && r - g2 > 20) {
                  heroFound = true;
                  break;
                }
              }
            }
          }
          if (!heroFound) {
            throw new Error(NAME + ": amber ring missing after level key");
          }
        } else {
          if (!(sea[2] > sea[0] && sea[2] > 25)) {
            throw new Error(NAME + ": no sea at the lower left (r,g,b " + sea.join(",") + ")");
          }
          // the top-left corner is sky past the scene geometry: dark
          const sky = livePx(app, 30, 100);
          if (sky[0] + sky[1] + sky[2] > 90) {
            throw new Error(NAME + ": top-left corner is not night sky (r,g,b " + sky.join(",") + ")");
          }
          // the hero light: moonpull's moon (a mesh sphere, bone-white on
          // the night sky) or the lamp glow (fogkeeper). The moon's spot
          // moves with its default azimuth, so scan the sky band for the
          // disc instead of pinning a pixel
          let heroFound = false;
          {
            paint(app);
            const b = Module.ccall("zb_buffer", "number", ["number", "number", "number"], [app, w, h]);
            if (NAME === "moonpull") {
              for (let y = 55; y < 140 && !heroFound; y += 2) {
                for (let x = 100; x < 700; x += 2) {
                  const o = (y * W + x) * 4;
                  if (Module.HEAPU8[b + o] > 230 && Module.HEAPU8[b + o + 1] > 210) {
                    heroFound = true;
                    break;
                  }
                }
              }
            } else {
              const o = (273 * W + (W >> 1)) * 4;
              heroFound = Module.HEAPU8[b + o] > 100;
            }
          }
          if (!heroFound) {
            throw new Error(NAME + ": hero light missing");
          }
        }
        // drag stroke: sideways drag (moon orbit / beam aim / stone move)
        input(app, 9, 300, 200, 0, 0, 0);
        input(app, 11, 360, 210, 0, 0, 0);
        input(app, 11, 420, 220, 0, 0, 0);
        input(app, 10, 420, 220, 0, 0, 0);
        for (let i = 0; i < 30; i++) paint(app);
        const a1 = snapOf(app);
        for (let i = 30; i < 60; i++) paint(app);
        const a2 = snapOf(app);
        if (same(a1, a2)) throw new Error(NAME + " does not advance between frames");
        const runOnce = function () {
          const f = Module._zb_app_create(W, H);
          const frames = [];
          if (NAME === "hoursmith") input(f, 12, 0, 0, 0, 51, 0);
          input(f, 9, 300, 200, 0, 0, 0);
          input(f, 11, 360, 210, 0, 0, 0);
          input(f, 11, 420, 220, 0, 0, 0);
          input(f, 10, 420, 220, 0, 0, 0);
          for (let i = 0; i < 60; i++) {
            paint(f);
            if (i % 15 === 14) frames.push(snapOf(f));
          }
          Module._zb_app_destroy(f);
          return frames;
        };
        const r1 = runOnce(), r2 = runOnce();
        for (let i = 0; i < r1.length; i++) {
          if (!same(r1[i], r2[i])) throw new Error(NAME + " replay frame " + i + " diverged");
        }
        console.log(NAME + ": world pixels ok, stroke replay byte-identical");
        Module._zb_app_destroy(app);
        Module._free(w); Module._free(h);
        console.log("SMOKE TEST OK");
        process.exit(0);
      }

      // ---- shared determinism template for the 2026-10-26 concept
      // batch (windloom/rotafarm/comet/crystal): every world is a pure
      // function of the input stream -- same stroke script, same bytes
      if (NAME === "windloom" || NAME === "rotafarm" || NAME === "comet" ||
          NAME === "crystal") {
        // the worlds have quiet openings (a parked comet, first seed at
        // frame 30, slow sun) -- check for motion at per-story
        // checkpoints (comet flies then exits and re-parks quickly)
        input(app, 9, 300, 200, 0, 0, 0);
        input(app, 11, 340, 210, 0, 0, 0);
        input(app, 11, 380, 220, 0, 0, 0);
        input(app, 10, 380, 220, 0, 0, 0);
        const cp = NAME === "comet" ? [5, 15] : [30, 60];
        for (let i = 0; i < cp[0]; i++) paint(app);
        const a1 = snapOf(app);
        for (let i = cp[0]; i < cp[1]; i++) paint(app);
        const a2 = snapOf(app);
        if (same(a1, a2)) throw new Error(NAME + " does not advance between frames");
        const runOnce = function () {
          const f = Module._zb_app_create(W, H);
          const frames = [];
          input(f, 9, 300, 200, 0, 0, 0);
          input(f, 11, 340, 210, 0, 0, 0);
          input(f, 11, 380, 220, 0, 0, 0);
          input(f, 10, 380, 220, 0, 0, 0);
          for (let i = 0; i < 60; i++) {
            paint(f);
            if (i % 15 === 14) frames.push(snapOf(f));
          }
          Module._zb_app_destroy(f);
          return frames;
        };
        const r1 = runOnce(), r2 = runOnce();
        for (let i = 0; i < r1.length; i++) {
          if (!same(r1[i], r2[i])) throw new Error(NAME + " replay frame " + i + " diverged");
        }
        console.log(NAME + ": world advances, stroke replay byte-identical");
        Module._zb_app_destroy(app);
        Module._free(w); Module._free(h);
        console.log("SMOKE TEST OK");
        process.exit(0);
      }

      // ---- tictactoe (default story) ----
      Module._zb_app_destroy(app);
      Module._free(w); Module._free(h);

      // close-flow E2E: play a draw, click QUIT, the closed callback must
      // fire and AGAIN's pixels must survive the partial repaint (the
      // region-exact damage fix in Graphics). Geometry mirrors
      // apps/tictactoe/include/tictactoe_layout.hpp (320x240 window).
      const app2 = Module._zb_app_create(320, 240);
      if (!app2) throw new Error("zb_app_create (2nd) failed");

      let closedFired = 0;
      const closedCb = Module.addFunction(function () { closedFired = 1; }, "vi");
      Module.ccall("zb_set_closed_callback", null, ["number", "number", "number"],
        [app2, closedCb, 0]);

      const click2 = function (x, y) {
        input(app2, 9, x, y, 0, 0, 0);
        input(app2, 10, x, y, 0, 0, 0);
      };

      // menu: normal -> first (player starts); then a deterministic DRAW,
      // same script as test_quit.cpp: X(0,0) O(2,0) X(0,2) O(2,1) X(1,2)
      click2(158, 130);           // Normal button
      click2(113, 130);           // First button
      const cells = [[0, 0], [2, 0], [0, 2], [2, 1], [1, 2]];
      for (const [r, c] of cells) {
        // board at (52,12), 72px cells -> cell centers
        click2(52 + c * 72 + 36, 12 + r * 72 + 36);
      }

      // result dialog buttons: AGAIN center (110,142), QUIT center (206,142)
      const w2 = Module._malloc(4), h2 = Module._malloc(4);
      const paintAndBuffer = function () {
        Module.ccall("zb_paint", null, ["number"], [app2]);
        return Module.ccall("zb_buffer", "number", ["number", "number", "number"], [app2, w2, h2]);
      };
      const px = function (buf, x, y) {
        return [
          Module.HEAPU8[buf + (y * 320 + x) * 4],
          Module.HEAPU8[buf + (y * 320 + x) * 4 + 1],
          Module.HEAPU8[buf + (y * 320 + x) * 4 + 2]
        ];
      };
      const isWhite = function (p) { return p[0] > 240 && p[1] > 240 && p[2] > 240; };
      let buf2 = paintAndBuffer();
      if (!isWhite(px(buf2, 110, 142))) throw new Error("AGAIN interior not white before QUIT");

      click2(206, 142);           // QUIT
      buf2 = paintAndBuffer();

      if (!closedFired) throw new Error("closed callback did not fire on QUIT");
      if (!isWhite(px(buf2, 110, 142))) throw new Error("AGAIN erased after QUIT repaint");

      Module._zb_app_destroy(app2);
      Module._free(w2); Module._free(h2);

      console.log("close flow: callback fired=" + closedFired + ", AGAIN intact=true");
      console.log("SMOKE TEST OK");
      process.exit(0);
    } catch (e) {
      console.error("SMOKE TEST FAILED:", e);
      process.exit(1);
    }
  }
}).catch((e) => {
  console.error("MODULE LOAD FAILED:", e);
  process.exit(1);
});
