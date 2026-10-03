// node smoke test for the wasm story builds (run in the emscripten image)
// usage: node demo/wasm/smoke.js [path to <story>_mod.js]
const path = require("path");
const OUT = process.argv[2] || path.join(__dirname, "tictactoe_mod.js");
const DIR = path.dirname(OUT);
const NAME = path.basename(OUT).replace(/_mod\.js$/, "");
const IS_SHOWCASE = NAME.indexOf("showcase") === 0;

// screen sizes: the SIGNAL-ONE / showcase_html / life / seedmap canvases
// design at 320x240; tictactoe and the portal games stay 256x192
const SIZES = {
    showcase: [320, 240],
    showcase_html: [320, 240],
    life: [320, 240],
    seedmap: [320, 240]
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
        // SIGNAL-ONE: the ABOUT overlay round trip. Click ABOUT (bottom
        // bar) -- the declarative overlay dims the console; click CLOSE
        // -- the frame returns. Pixel (10,120) sits in the left panel
        // margin: static across frames (the desktop battery probes the
        // same pixel), so the live-feed telemetry cannot confound the
        // comparison, and it dims with the overlay.
        const pxAt = function (buf, x, y) {
          const o = (y * W + x) * 4;
          return buf[o] + buf[o + 1] + buf[o + 2];
        };
        const before = snapOf(app);
        click(app, 128, 226);  // ABOUT
        const dimmed = snapOf(app);
        if (pxAt(before, 10, 120) === pxAt(dimmed, 10, 120)) {
          throw new Error("ABOUT overlay did not dim the console");
        }
        click(app, 103, 156);  // CLOSE
        const restored = snapOf(app);
        if (pxAt(restored, 10, 120) !== pxAt(before, 10, 120)) {
          throw new Error("CLOSE did not restore the console");
        }
        Module._zb_app_destroy(app);
        Module._free(w); Module._free(h);
        console.log("showcase about overlay: ok");
        console.log("SMOKE TEST OK");
        process.exit(0);
      }

      if (NAME === "g2048") {
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
        if (same(before, snapOf(app))) throw new Error("first reveal did not change the field");
        // FLAG toggles the mode button caption
        click(app, 148, 16);   // FLAG button
        const flagged = snapOf(app);
        click(app, 148, 16);
        if (same(flagged, snapOf(app))) throw new Error("FLAG toggle did not repaint");
        console.log("mines: reveal + flag toggle ok");
        Module._zb_app_destroy(app);
        Module._free(w); Module._free(h);
        console.log("SMOKE TEST OK");
        process.exit(0);
      }

      if (NAME === "life") {
        // the animation advances off the paint loop: the colony (gun) is
        // live, so two snapshots a few generations apart must differ
        const a = snapOf(app);
        for (let i = 0; i < 16; i++) paint(app);  // two colony steps
        const b = snapOf(app);
        if (same(a, b)) throw new Error("life colony did not evolve across paints");
        // plasma mode paints continuously as a pure function of the frame
        click(app, 246, 19);   // MODE -> plasma
        const p1 = snapOf(app);
        const p2 = snapOf(app);
        if (same(p1, p2)) throw new Error("plasma does not advance per paint");
        console.log("life: colony evolves, plasma advances");
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
        click(app, 164, 19);   // DRAW (same seed)
        if (!sameMap(before, snapOf(app))) {
          throw new Error("same seed produced a different map");
        }
        // a different seed produces a different map
        click(app, 68, 19);    // focus the seed box
        keyDown(app, 122, 122);  // 'z' lands at the caret
        click(app, 164, 19);   // DRAW
        if (sameMap(before, snapOf(app))) throw new Error("new seed did not change the map");
        console.log("seedmap: same seed byte-identical, new seed differs");
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
