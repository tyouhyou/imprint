# Imprint UI

[![English](https://img.shields.io/badge/English-blue)](README.md) [![中文](https://img.shields.io/badge/%E4%B8%AD%E6%96%87-lightgrey)](README.zh-CN.md) [![日本語](https://img.shields.io/badge/%E6%97%A5%E6%9C%AC%E8%AA%9E-lightgrey)](README.ja.md)

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS%20%7C%20NDS%20%7C%20WASM%20%7C%20Python-lightgrey.svg)]()

**Same input, same pixels — assertable in CI, with no display attached.**

Imprint is a deterministic, embeddable UI runtime for C++17. One pixel
buffer, software-rasterized: no GPU, no OS GUI toolkit, no timers, no
threads. The host drives every frame, so a given input sequence always
yields the same frame bytes — asserting UI logic pixel-by-pixel in
headless CI is a property of the contract, not a test-harness trick.
One UI source tree — widgets in code, or a screen described in a design
file — compiles unchanged for Windows, Linux, macOS, SIXEL terminals,
WebAssembly, the Nintendo DS, and any C host via `zbapi`.

**Design-first.** A screen *is designed in HTML* — ids, tags, styles;
zero widget code — and materialized into a widget tree at build time.
The hero below is one HTML file rendered by Imprint's own rasterizer
into the pixel buffer:

<p>
  <img src="assets/designs/imprint_console.png" width="860" alt="Imprint Console: the vacuum-tube dashboard (tubes, VU bank, power meter, diagnostics paragraph), designed in HTML and rendered by Imprint">
</p>

Not a mockup — a live widget tree. Designers hand over HTML or the compact
`.ui` format; both materialize into the exact tree your C++ builds, so the
design is what ships on every target. The HTML path is spelled out after
the `.ui` example below; either way, one tree, one buffer, many targets.

**One UI source tree. One pixel buffer. Many targets.**

The same design-file showcase — a warm terminal punched into the pixel
buffer — on desktop, on the Nintendo DS, and in the browser:

![showcase_html on linux, nds, wasm](assets/showcase/montage.png)

```
                same UI source
                      │
      ┌───────────┬───┴───────┬───────────┐
      ↓           ↓           ↓           ↓
   Windows       Linux       macOS     terminal
      │        (X11/FB)                (SIXEL)
      └───────────┼───────────┘
                  ↓
           WebAssembly  ←  try it in your browser
                  ↓
            Nintendo DS
                  ↓
       your embedded board (C-ABI)
```

The terminal is a first-class target: on any sixel-capable terminal
(WezTerm, foot, iTerm2) the same widget tree renders as SIXEL graphics
with SGR mouse and keyboard input — no windowing system at all.

Measured footprints (Release builds of the showcase demo):

| Target | Shipped footprint |
|---|---|
| WebAssembly | 583 KB single `.js` file — wasm and the Inter TTF embedded, runs from `file://` |
| Nintendo DS | 256×192 16-bpp framebuffer (96 KB VRAM); integer-only geometry and non-atomic refcounting options for libatomic-less toolchains |

No GPU required. No OS GUI toolkit required. No platform-specific UI code.

## The showcase, live

**SIGNAL-ONE** is a working task console — the demo behind the
footprints above — and everything on its screen is drawn by Imprint's
own rasterizer. Below, it is recorded end to end: the recorder drives
the app through its public API with a fixed input script, so the GIF
is byte-identical on every platform.

<p>
  <img src="assets/showcase/showcase.gif" width="480" alt="SIGNAL-ONE recorded end to end: boots into live telemetry with the trend line advancing, a drag across the GAIN knob pulls the dB readout, the load gauge and the temp meter, MODE cycles three accent themes (cyan, amber, green), the module toggles flip, ABOUT opens the modal overlay and CLOSE dismisses it, RESET restores the boot state">
</p>

**[Try it live in your browser](https://tyouhyou.github.io/imprint/)** —
the same console compiled to WebAssembly, presented on a `<canvas>`
through the same C-ABI a desktop shell uses. Drag the gain knob, cycle
the MODE themes, open the ABOUT overlay. No server, no install: the
wasm is embedded in the page.

## Highlights

- **Deterministic, host-driven runtime** — the shell owns the loop; same input sequence → same pixels; repaint-on-demand with dirty tracking, no hidden redraws
- **Automation by contract** — a script can replace the user: feed input, pump frames, assert on pixels; single-threaded and timer-free, so drivers never sleep — the test battery includes an end-to-end `automation` suite driven through the public API
- **Design files** — describe a screen in `.ui` or external HTML, validate and pack at build time, load from a C array on any target; `ui_preview` renders files directly
- **Retained-mode widget tree** — `Button`, `Label`, `Dialog`, `FlexPanel`, `ListBox` and more
- **Software rendering into a raw pixel buffer** — no GPU, no external rendering library; the buffer format is fixed at build time (`COLOR_DEPTH`)
- **C-ABI as a first-class citizen** — stable `zbapi` C interface with Python (ctypes), WebAssembly and C smoke-test hosts
- **Embedded-grade** — no RTTI, 16-bit color (abgr1555), integer-only geometry option, non-atomic refcounting option (NDS has no libatomic)
- **Zero-allocation hot paths** — RAII `ClipGuard`, event tombstoning, `Subscription`
- **UTF-8 text throughout** — built-in 5x7 bitmap glyph fallback (auto-subsetted from source strings); optional runtime TTF text via vendored stb_truetype, vendored stb codecs (PNG/JPEG) and a hand-written GIF writer
- **C++17, CMake, static libraries** — everything is composable, nothing is forced

## Non-goals

GPU-accelerated drawing (the render kernel stays CPU software rasterization) ·
animation/transition system · runtime backend switching · multithreaded
rendering · IME composition · RTL layout. Imprint deliberately stays small:
one widget tree, one pixel buffer, one input stream — everything else is the
host's job.

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

### HTML as an external designer

Prefer a real markup toolchain? The same materialization path accepts an
external **HTML** design file. The hero at the top of this README is
[`assets/designs/imprint_console.html`](assets/designs/imprint_console.html)
— open it in a browser to edit, hand it to Imprint's designer, and it
becomes the identical widget tree the C++ example builds above (an
`html` / `vectordial` subset: layout, labels, controls, vector dials —
not a web engine). Preview it the same way:

```
UI_PREVIEW_FILES="assets/designs/imprint_console.html" cmake -B build/build_html -DSTORY=ui_preview -DIM_SHELL_BACKEND=FB && cmake --build build/build_html
```

Both formats — `.ui` and HTML — feed one tree, one pixel buffer, every
target. SIGNAL-ONE, the showcase at the top, is exactly this: an
85-line HTML design file plus 266 lines of C++ behavior.

## Your project, your main — use as a library

Imprint consumes three ways: an in-repo demo story (above), a **library
subproject** driven by your own `main` (this section), or a pure **C-ABI
host** in any language (further below). Nothing forces your application
to live in this tree: add Imprint as a subproject and drive it through
`zb::shell::run` — the same host loop the platform shells run:

```cpp
// your main.cpp — MyWindow : zb::app::CanvasWindow, or any zb::app::IApp
#include "shell/run.hpp"

#include "my_window.hpp"

int main()
{
    return zb::shell::run(std::make_shared<MyWindow>());
}
```

```cmake
# your CMakeLists.txt
add_subdirectory(imprint)          # or FetchContent
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE imprint::imapp_canvas imprint::shell_backend)
```

As a subproject Imprint configures **libraries only** — no demo apps, no
binding, no host tools; the `IMPRINT_WITH_TOOLS` / `IMPRINT_WITH_TESTS` /
`IMPRINT_WITH_DEMOS` switches re-enable each piece (the default in-tree
build keeps them all). The headless path — the one CI asserts pixels
with — needs no shell at all: `CanvasWindow::create()` + `paint()` (see
`test/external_smoke/`, wired into the test battery). The shell-loop
contract is `docs/code-contract.md` §11.

## Deterministic testing in CI

One input sequence always yields the same frame bytes — so UI logic is
assertable pixel-by-pixel with no display attached. The `zb::snap`
helper (link `imprint::snapshot`) is the whole workflow:

```cpp
#include "snapshot.hpp"

// drive your surface headlessly (input/paint), then:
auto r = zb::snap::check(*app.window(), "main_view", "tests/baselines");
if (r.status == zb::snap::check_result::status::missing)
{
    zb::snap::record(*app.window(), "main_view", "tests/baselines");  // first run
}
// status::mismatch also ships tests/baselines/main_view.actual.gif for diffing
```

Commit the `.zbsnap` baselines; CI fails on any pixel drift and the
mismatch artifact shows what changed. Baselines are valid per build
configuration (code-contract §12.2); within one pixel class the hash is
byte-identical across Windows / macOS / Linux. The in-tree references:
`test/test_snapshot.cpp` (workflow) and the showcase recorder, which
rides the same determinism.

No app code at all? Render a design file straight to pixels — the hero
at the top of this README comes out of this one line (it prints the
frame hash too):

```
imprint-render assets/designs/imprint_console.html --out hero.png
```

The CI recipe — no browser, no display (our Tier-1 job runs exactly
this):

```yaml
- run: |
    cmake --build build_ci --target imprint-render
    ./build_ci/bin/imprint-render tools/examples/menu.ui --out menu1.png
    ./build_ci/bin/imprint-render tools/examples/menu.ui --out menu2.png
    cmp menu1.png menu2.png   # two runs, byte-identical frames
```

## Script-language GUIs — a design file + callbacks

The declarative path needs no C++ on the user side at all:
`zb_app_create_from_ui` builds the widget tree from a design file
(`.ui` grammar, or the HTML subset with `is_html=1` — the same two
front-ends the designer uses), and actions come back by id. The Python
demo is the whole story (`demo/python/ui_app.py`):

```python
UI = """
column id="root" spacing=8 padding=12
  label id="count" text="Clicks: 0"
  button id="inc" text="Count up"
"""

def on_action(widget_id, userdata):
    if widget_id == b"inc":
        clicks[0] += 1
        lib.zb_widget_set_text(app, b"count", ("Clicks: %d" % clicks[0]).encode())

app = lib.zb_app_create_from_ui(UI.encode(), 0, 320, 240)
lib.zb_set_event_callback(app, b"inc", action_cb, None)
# drive zb_input / zb_paint in your own loop -- the host is the shell
```

Any language that can call a C ABI gets the same protocol. Static
structure lives in the file; behavior lives in the host — the
declarative boundary is unchanged.

## Build

| Target | Command | Notes |
|---|---|---|
| Windows (MSVC) | `cmake -S . -B build/build_win && cmake --build build/build_win` | zero-dependency default (32bpp) |
| Runtime TTF text | `cmake -S . -B build/build_rt_ttf -DUSE_TTF_RUNTIME=ON && cmake --build build/build_rt_ttf` | runtime glyph rasterization (batch L-5): apps load a font via `TtfFamily`, no external dependency |
| macOS (AppKit) | `cmake -S . -B build/build_mac && cmake --build build/build_mac` | no deployment-target pin (toolchain default), no extra options |
| Linux (X11) | `cmake -S . -B build/build_linux -DIM_SHELL_BACKEND=X11 && cmake --build build/build_linux` | input-capable backend |
| Linux (framebuffer) | `cmake -S . -B build/build_linux -DIM_SHELL_BACKEND=FB && cmake --build build/build_linux` | presents only; use X11 for interaction |
| Terminal (SIXEL) | `cmake -S . -B build/build_term -DIM_SHELL_BACKEND=SIXEL && cmake --build build/build_term` | demo target: the same UI in a sixel terminal (WezTerm/foot/iTerm2), input via SGR mouse + keys; `IM_TERM_SIZE=WxH` resizes |
| Nintendo DS | `docker run --rm -v $PWD:/src -w /src devkitpro/devkitarm:20260610 sh -c 'cmake -S . -B build/build_nds -DCMAKE_TOOLCHAIN_FILE=cmake/nds.toolchain.cmake && cmake --build build/build_nds'` | produces `build/build_nds/bin/tictactoe.nds`; add `-DSTORY=showcase` for the SIGNAL-ONE ROM (it additionally needs the host-built `html_embed` and `bytes_embed`, passed as `-DHTML_EMBED_EXECUTABLE=` / `-DBYTES_EMBED_EXECUTABLE=`), or `-DSTORY=showcase_html` for the HTML showcase (same, plus the host-built `html_embed` as `-DHTML_EMBED_EXECUTABLE=`) |
| WebAssembly | `demo/wasm/build.sh` (docker emscripten) | `build.sh showcase` produces the SIGNAL-ONE page as one self-contained `.js` (wasm embedded); includes a node smoke test |
| Python | build the `binding` shared lib, then `SDL_VIDEODRIVER=dummy python3 demo/python/myapp.py --lib <libzbapi>` | ctypes + pygame host |

Tests: `test/test_imui` — plain asserts, no test framework; run via `ctest -R test_imui` (or the binary) on desktop; skipped on NDS.

## Window & presentation

The app owns a **fixed-size pixel buffer** (`create_window(w, h)`) and
never re-lays out for a window resize. Desktop shells (win32 / X11 /
macOS) open the window at buffer size and let you resize it freely: the
buffer is presented scaled to fit, aspect preserved, centered on a black
letterbox, with nearest-neighbor resampling — the same buffer at the
same window size renders identically on every desktop platform. Pointer
input maps back through the same integer formula the stretch uses
(`buf = (win - dest) * buf / dest`), so hit-testing stays exact at any
scale; clicks on the letterbox are ignored. The NDS and framebuffer
shells present 1:1; WASM/Python hosts scale host-side.

## Documentation

**Suggested reading order** (first pass for a new maintainer):
1. [`docs/getting-started.md`](docs/getting-started.md) — run your first app and make it your own (~5 minutes)
2. this README → **Build** (get a binary running on every target)
3. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) §1–§2 — what the system is, module map & dependency rules
4. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) §3–§5 — the normative contracts, targets, limitations
5. [`docs/code-contract.md`](docs/code-contract.md) — the API-level contracts
6. [`docs/design-file.md`](docs/design-file.md) — when working with `.ui` files

**Where to look by task:** touching public API → `code-contract.md` first (the contract changes before the API) · new target / pixel format / build option → `docs/backlog.md` & ARCHITECTURE §4 · `.ui` grammar or packaging → `design-file.md` · C-ABI host → `zbapi.h` + ARCHITECTURE §4.8 · build & run commands → **Build** below.

- [`docs/getting-started.md`](docs/getting-started.md) — from a fresh clone to your own app: run the `hello` story, understand the `IApp`/`CanvasWindow` seam, register your own story
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — the as-built architecture: module map & dependency rules, contracts (frame lifecycle, input, pixel model, text, events, errors, C-ABI hosts, build options), and known limitations
- [`docs/backlog.md`](docs/backlog.md) — the living backlog: architecture items, product feature batches (L/I/F), and condition-triggered items
- [`docs/code-contract.md`](docs/code-contract.md) — the API-level interface contract: error paths, UTF-8/text, glyph provider, tree mutation, layout invalidation, alloc budget, the presentation-seam converter
- [`docs/design-file.md`](docs/design-file.md) — the `.ui` design-file format: grammar, packaging pipeline, materialization semantics
- [`docs/html-path.md`](docs/html-path.md) — the HTML design-file path: element/attribute/CSS whitelist
- [`binding/include/zbapi.h`](binding/include/zbapi.h) — the C-ABI surface for hosts (Python, WASM, C); host rules in ARCHITECTURE §4.8

## Demo

**Hello** (`-DSTORY=hello`) — the getting-started app: a label and a click-counting button; copy it to start your own app (see [`docs/getting-started.md`](docs/getting-started.md)).

**Showcase** (`-DSTORY=showcase`) — SIGNAL-ONE, the console demoed live above: an 85-line HTML design file (`apps/showcase/signal.html`) materialized into a widget tree at build time, behavior in 266 lines of C++. The live telemetry feed advances one deterministic step per frame (the trend line is pure in the frame counter); the GAIN knob drives the dB readout, the load gauge and the temp meter; MODE cycles three accent themes (cyan / amber / green); ABOUT opens a declarative modal overlay; RESET restores the boot state. Text renders through the runtime-TTF path (Inter, packed by `bytes_embed`) — configure `-DUSE_TTF_RUNTIME=ON` for the intended proportional look; the 5x7 bitmap fallback keeps non-TTF builds green. The GIF in *The showcase, live* comes out of `showcase_gif`, the deterministic recorder; the browser demo and the DS cross-build (see Build) run the same sources.

**TicTacToe** (default story) — a human-vs-computer game exercising dialogs, buttons, layout and repaint-on-demand; the NDS build produces `build/build_nds/bin/tictactoe.nds`. A third app, `ui_preview` (`-DSTORY=ui_preview`), renders design files from `UI_PREVIEW_FILES` (space-separated paths; left/right keys switch documents) — pass `.ui` or HTML paths.

| Windows | macOS | Linux (X11) | WebAssembly | Nintendo DS | Python host |
|:---:|:---:|:---:|:---:|:---:|:---:|
| <img src="assets/tictactoe/win.png" width="240"> | <img src="assets/tictactoe/mac.png" width="240"> | <img src="assets/tictactoe/linux_x11.png" width="240"> | <img src="assets/tictactoe/wasm.png" width="160"> | <img src="assets/tictactoe/nds.png" width="200"> | <img src="assets/tictactoe/py256.png" width="240"> |

## License

[MIT](LICENSE) © 2026 tyou hyou
