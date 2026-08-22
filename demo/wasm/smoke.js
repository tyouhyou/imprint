// node smoke test for the wasm tictactoe build (run in the emscripten image)
// usage: node demo/wasm/smoke.js [path to tictactoe_mod.js]
const path = require("path");
const OUT = process.argv[2] || path.join(__dirname, "tictactoe_mod.js");
const DIR = path.dirname(OUT);

const createModule = require(path.resolve(OUT));

createModule({
  // the modularized node build loads the .wasm from disk next to the .js
  locateFile: function (file) {
    return path.join(DIR, path.basename(file));
  },
  onRuntimeInitialized: function () {
    const Module = this;
    try {
      const app = Module._zb_app_create(256, 192);
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

      Module._zb_app_destroy(app);
      Module._free(w); Module._free(h);

      if (ww !== 256 || hh !== 192 || nonzero === 0) {
        console.error("SMOKE TEST FAILED");
        process.exit(1);
      }

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

      const click = function (x, y) {
        Module.ccall("zb_input", null, ["number", "number", "number", "number", "number", "number", "number"],
          [app2, 9, x, y, 0, 0, 0]);
        Module.ccall("zb_input", null, ["number", "number", "number", "number", "number", "number", "number"],
          [app2, 10, x, y, 0, 0, 0]);
      };

      // menu: normal -> first (player starts); then a deterministic DRAW,
      // same script as test_quit.cpp: X(0,0) O(2,0) X(0,2) O(2,1) X(1,2)
      click(158, 130);           // Normal button
      click(113, 130);           // First button
      const cells = [[0, 0], [2, 0], [0, 2], [2, 1], [1, 2]];
      for (const [r, c] of cells) {
        // board at (52,12), 72px cells -> cell centers
        click(52 + c * 72 + 36, 12 + r * 72 + 36);
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

      click(206, 142);           // QUIT
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
