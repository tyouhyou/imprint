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
