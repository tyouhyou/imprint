# Imprint

[![English](https://img.shields.io/badge/English-blue)](README.md) [![中文](https://img.shields.io/badge/%E4%B8%AD%E6%96%87-lightgrey)](README.zh-CN.md) [![日本語](https://img.shields.io/badge/%E6%97%A5%E6%9C%AC%E8%AA%9E-lightgrey)](README.ja.md)

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20NDS%20%7C%20WASM%20%7C%20Python-lightgrey.svg)]()


**One pixel buffer, every target.** Imprint is a dependency-free, software-rendered C++17 UI framework. The same source tree runs on a Nintendo DS, Linux (framebuffer or X11), Windows, macOS (AppKit), in the browser via WebAssembly, and behind a C-ABI driven from Python.

| Windows | Linux (X11) | WebAssembly |
|:---:|:---:|:---:|
| <img src="assets/win.png" width="240"> | <img src="assets/linux_x11.png" width="240"> | <img src="assets/wasm.png" width="160"> |

| Nintendo DS | Python host |
|:---:|:---:|
| <img src="assets/nds.png" width="240"> | <img src="assets/py256.png" width="240"> |

## Highlights

- **Retained-mode widget tree** — `Button`, `Label`, `Dialog`, `FlexPanel`, `GraphicsView` and more
- **Design files** — describe a UI in a small text format (`.ui`), validate and pack it at build time, load it from a C array on any target; a preview app renders files directly
- **Software rendering into a raw pixel buffer** — no GPU, no external rendering library; the buffer format is fixed at build time (`COLOR_DEPTH`)
- **Deterministic repaint-on-demand** — dirty tracking, shell owns the loop, no hidden redraws
- **C-ABI as a first-class citizen** — stable `zbapi` C interface with Python (ctypes), WebAssembly and C smoke-test hosts
- **Embedded-grade** — no RTTI, 16-bit color (abgr1555), integer-only geometry option, non-atomic refcounting option (NDS has no libatomic)
- **Zero-allocation hot paths** — RAII `ClipGuard`, event tombstoning, `Subscription`
- **UTF-8 text throughout** — built-in 5x7 bitmap glyph fallback (auto-subsetted from source strings); optional FreeType (fonts) and vendored stb codecs (PNG/JPEG)
- **C++17, CMake, static libraries** — everything is composable, nothing is forced

## Quick Example

```cpp
#include "imapp.hpp"
#include "imui.hpp"

int main()
{
    auto app = zb::app::make_app();
    app->create_window(320, 240);
    auto* win = static_cast<zb::app::CanvasWindow*>(app->window().get());

    auto btn = std::make_unique<zb::ui::Button>();
    btn->set_size(100, 40);
    btn->set_text("Click Me");
    btn->clicked += [] { printf("Hello!\n"); };
    win->root().add_child(std::move(btn));

    app->paint();
}
```

The same screen described as a design file (`tools/examples/menu.ui`):

```
column id="root" spacing=6 padding=10
  label id="title" text="Settings"
  checkbox id="sound" text="Sound"
  slider min=0 max=100 step=10
  list_box rows=3 items="Easy" "Normal" "Hard"
  row spacing=4
    button id="ok" text="OK"
    button id="cancel" text="Cancel"
```

Pack it at build time with `ui_embed` (fails the build on invalid files), then
`parse_ui_text` + `build()` materialize it — the same code path on every
platform. Preview interactively with the `ui_preview` app:

```
UI_PREVIEW_FILES="tools/examples/menu.ui" cmake -B build/build_linux -DSTORY=ui_preview -DIM_SHELL_BACKEND=FB && cmake --build build/build_linux
```

## Build

| Target | Command | Notes |
|---|---|---|
| Windows (MSVC) | `cmake -S . -B build/build_win && cmake --build build/build_win` | zero-dependency default (32bpp) |
| Windows + fonts | `cmake -S . -B build/build_font -DUSE_FONT=ON && cmake --build build/build_font` | run needs `freetype.dll` on PATH |
| macOS (AppKit) | `cmake -S . -B build/build_mac && cmake --build build/build_mac` | deployment target 11.0, no extra options |
| Linux (X11) | `cmake -S . -B build/build_linux -DIM_SHELL_BACKEND=X11 && cmake --build build/build_linux` | input-capable backend |
| Linux (framebuffer) | `cmake -S . -B build/build_linux -DIM_SHELL_BACKEND=FB && cmake --build build/build_linux` | presents only; use X11 for interaction |
| Nintendo DS | `docker run --rm -v $PWD:/src -w /src devkitpro/devkitarm:20260610 sh -c 'cmake -S . -B build/build_nds -DCMAKE_TOOLCHAIN_FILE=cmake/nds.toolchain.cmake && cmake --build build/build_nds'` | produces `build/build_nds/bin/tictactoe.nds` |
| WebAssembly | `demo/wasm/build.sh` (docker emscripten) | includes a node smoke test |
| Python | build the `binding` shared lib, then `SDL_VIDEODRIVER=dummy python3 demo/python/myapp.py --lib <libzbapi>` | ctypes + pygame host |

Tests: `test/test_imui` — plain asserts, no framework; automatic on desktop builds, skipped on NDS.

## Documentation

**Suggested reading order** (first pass for a new maintainer):
1. this README → **Build** (get a binary running)
2. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) §1–§2 — what the system is, module map & dependency rules
3. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) §3–§5 — the normative contracts, targets, limitations
4. [`docs/code-contract.md`](docs/code-contract.md) — the API-level contracts
5. [`docs/design-file.md`](docs/design-file.md) — when working with `.ui` files

**Where to look by task:** touching public API → `code-contract.md` first (the contract changes before the API) · new target / pixel format / build option → ARCHITECTURE §6 · `.ui` grammar or packaging → `design-file.md` · C-ABI host → `zbapi.h` + ARCHITECTURE §4.8 · build & run commands → **Build** below.

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — the as-built architecture: module map & dependency rules, contracts (frame lifecycle, input, pixel model, text, events, errors, C-ABI hosts, build options), known limitations, and the architecture backlog
- [`docs/code-contract.md`](docs/code-contract.md) — the API-level interface contract: error paths, UTF-8/text, glyph provider, tree mutation, layout invalidation, alloc budget, the presentation-seam converter
- [`docs/design-file.md`](docs/design-file.md) — the `.ui` design-file format: grammar, packaging pipeline, materialization semantics
- [`binding/include/zbapi.h`](binding/include/zbapi.h) — the C-ABI surface for hosts (Python, WASM, C); host rules in ARCHITECTURE §4.8

## Demo

The demo app is a TicTacToe game (human vs computer), exercising dialogs, buttons, layout and repaint-on-demand. The NDS build produces `build/build_nds/bin/tictactoe.nds`. A second app, `ui_preview` (`-DSTORY=ui_preview`), renders design files from `UI_PREVIEW_FILES` (space-separated paths; left/right keys switch documents).

## License

[MIT](LICENSE) © 2026 tyou hyou
