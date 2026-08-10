# Imprint

[![English](https://img.shields.io/badge/English-blue)](README.md) [![中文](https://img.shields.io/badge/%E4%B8%AD%E6%96%87-lightgrey)](README.zh-CN.md) [![日本語](https://img.shields.io/badge/%E6%97%A5%E6%9C%AC%E8%AA%9E-lightgrey)](README.ja.md)

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20NDS%20%7C%20WASM%20%7C%20Python-lightgrey.svg)]()


**One pixel buffer, every target.** Imprint is a dependency-free, software-rendered C++17 UI framework. The same source tree runs on a Nintendo DS, Linux (framebuffer or X11), Windows, in the browser via WebAssembly, and behind a C-ABI driven from Python.

| Windows | Linux (X11) | WebAssembly |
|:---:|:---:|:---:|
| <img src="assets/win.png" width="240"> | <img src="assets/linux_x11.png" width="240"> | <img src="assets/wasm.png" width="160"> |

| Nintendo DS | Python host |
|:---:|:---:|
| <img src="assets/nds.png" width="240"> | <img src="assets/py256.png" width="240"> |

## Highlights

- **Retained-mode widget tree** — `Button`, `Label`, `Dialog`, `FlexPanel`, `GraphicsView` and more
- **Software rendering into a raw pixel buffer** — no GPU, no external rendering library; the buffer format is fixed at build time (`COLOR_DEPTH`)
- **Deterministic repaint-on-demand** — dirty tracking, shell owns the loop, no hidden redraws
- **C-ABI as a first-class citizen** — stable `zbapi` C interface with Python (ctypes), WebAssembly and C smoke-test hosts
- **Embedded-grade** — no RTTI, 16-bit color (abgr1555), integer-only geometry option, non-atomic refcounting option (NDS has no libatomic)
- **Zero-allocation hot paths** — RAII `ClipGuard`, event tombstoning, `Subscription`
- **UTF-8 text throughout** — built-in 5x7 bitmap glyph fallback; optional FreeType / libpng / libjpeg
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

## Platforms

| Platform | Shell | Notes |
|---|---|---|
| Nintendo DS | `imshell/nds` | ARM9, 4 MB RAM, no FPU; ROM packaged with ndstool |
| Linux | `imshell/fb`, `imshell/x11` | framebuffer or X11 window |
| Windows | `imshell/win` | Win32, 32-bit BGRA or 16-bit |
| WebAssembly | `demo/wasm` | emscripten + canvas |
| Python | `demo/python` | ctypes + pygame over the C-ABI |

## Build

| Target | Command |
|---|---|
| Windows (MSVC) | `cmake -S . -B build && cmake --build build` |
| Linux (framebuffer) | `cmake -S . -B build_linux -DIM_SHELL_BACKEND=FB` |
| Linux (X11) | `cmake -S . -B build_linux -DIM_SHELL_BACKEND=X11` |
| NDS (docker) | `docker run --rm -v $PWD:/src -w /src devkitpro/devkitarm:20260610 sh -c 'cmake -S . -B build_nds -DCMAKE_TOOLCHAIN_FILE=cmake/nds.toolchain.cmake && cmake --build build_nds'` |
| WebAssembly | `demo/wasm/build.sh` (docker emscripten) |
| Python | build `binding` shared lib, then `python3 demo/python/myapp.py --lib <libzbapi>` |

Tests: `test/test_imui.exe` (17 suites, plain asserts, no framework). Automatic on desktop builds; skipped on NDS.

## Demo

The demo app is a TicTacToe game (human vs computer), exercising dialogs, buttons, layout and repaint-on-demand. The NDS build produces `build_nds/bin/tictactoe.nds`.

## Repository layout

| Path | Contents |
|---|---|
| `imcore/` | drawing kernel: Graphics / Color / Font / Image |
| `imui/` | widget library (retained-mode) |
| `imapp/` | app interface: `IApp` / `IWindow` + `CanvasWindow` default |
| `imevent/` | events & input |
| `imutil/` | logging |
| `binding/` | C-ABI `zbapi` shared library + C smoke test |
| `imshell/` | platform shells (NDS / FB / X11 / Win) |
| `apps/` | demo applications |
| `test/` | unit tests |

## License

[MIT](LICENSE) © 2026 tyou hyou
