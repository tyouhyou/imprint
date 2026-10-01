# Getting started

From a fresh clone to your own app running on your machine — about five
minutes. All you need is CMake 3.16+ and a C++17 compiler. No GPU, no OS
GUI toolkit, no package manager, no other dependencies.

## Run something right away

The repo ships demo apps called *stories*, selected with the `STORY` CMake
variable. `hello` is the smallest one — a label and a button that counts
its own clicks.

Windows (MSVC):

```
cmake -S . -B build/build_hello -DSTORY=hello
cmake --build build/build_hello --config Release
build\build_hello\bin\Release\hello.exe
```

Linux (X11 backend; interactive):

```
cmake -S . -B build/build_hello -DSTORY=hello -DIM_SHELL_BACKEND=X11 -DCMAKE_BUILD_TYPE=Release
cmake --build build/build_hello
./build/build_hello/bin/hello
```

Want the full widget gallery instead? Use `-DSTORY=showcase`. Every other
target — macOS, WebAssembly, Nintendo DS, the Python host — is one line in
the README's Build table too.

## How `hello` works

The whole app lives in three small files under `apps/hello/`:

```
apps/hello/
  CMakeLists.txt          declares hello_app (a static library)
  include/hello.hpp       the app itself
  src/app_maker.cpp       the make_app() factory
```

The pieces that matter:

1. **An app implements `zb::app::IApp`.** The easiest way is to wrap the
   provided `CanvasWindow` (a framebuffer + widget tree + input dispatcher),
   which is what `Hello` does: `create_window()` makes the window and builds
   the widget tree; `input()`, `paint()`, `is_dirty()` forward to it.
2. **`zb::app::make_app()` is the one entry point the shell looks for.**
   `app_maker.cpp` returns your app class; the platform shell (win32, X11,
   framebuffer, AppKit, NDS) supplies `main` and drives the loop.
3. **Widgets are ordinary objects in a tree.** Create them, configure them,
   hang them on `window_->root()`:

   ```cpp
   auto button = std::make_unique<zb::ui::Button>();
   button->set_text("Click me");
   button->set_size(120, 40);
   button->set_position(20, 100);
   button->clicked += [counter] { counter->set_text("Clicks: 1"); };
   root.add_child(std::move(button));
   ```

   Available widgets: `Button`, `Label`, `Checkbox`, `RadioButton`,
   `Slider`, `ProgressBar`, `ListBox`, `TextInput`, `Dialog`, and the
   `FlexPanel` row/column container.
4. **Placement is explicit unless you ask for layout.** `set_position` /
   `set_size` are respected as-is; `FlexPanel` stacks its children for you.
5. **Repaint is on demand.** Widget setters report damage automatically;
   the shell presents a frame only when `is_dirty()` is true. You never
   call a redraw function from a timer.

Nothing under `apps/` contains platform-specific code — the same sources
compile for every target.

## Make it your own app

1. Copy `apps/hello/` to `apps/myapp/` and rename the pieces:
   `hello.hpp` → `myapp.hpp` (class `MyApp`, namespace `zb::app::myapp`),
   and in `CMakeLists.txt` rename the library `hello_app` → `myapp_app`.
2. Make `app_maker.cpp` return your class:

   ```cpp
   zb::SharedPtr<zb::app::IApp> zb::app::make_app()
   {
       return zb::make_shared<zb::app::myapp::MyApp>();
   }
   ```

3. Register the story in `apps/CMakeLists.txt`:

   ```cmake
   add_subdirectory(myapp)
   ```

   and add `myapp` to the `STORY` whitelist check at the bottom of that
   file (and optionally to the `STRINGS` property, for cmake-gui).
4. Build and run:

   ```
   cmake -S . -B build/build_myapp -DSTORY=myapp
   cmake --build build/build_myapp --config Release
   ```

That's the whole loop. From here it is a matter of adding widgets, wiring
signals, and — when you want an embedded target — configuring the same
source tree for it.

The steps above keep your app *inside this tree* (framework mode). To
keep your app in **your own project** and link Imprint as a library
instead — your own `main`, `zb::shell::run`, the `imprint::` CMake
targets — see "Use as a library" in `README.md`.

One constraint to plan around: **the buffer resolution is fixed for
the lifetime of the window.** `create_window(w, h)` sizes it once;
there is no runtime resize. If your host window can resize, present
the fixed buffer scaled (that is what the platform shells do), and
treat a true resolution change as a recreate-the-app operation —
there is no API for resizing a live buffer (code-contract §11.1).

## Text at scale — bitmap default vs runtime TTF

The built-in glyph provider is a fixed **5x7 bitmap font**: zero
dependencies, byte-deterministic, ideal for small buffers (the NDS's
256x192, a terminal panel). It does **not** scale — glyphs render at
their native cell, so on a large buffer (1080p canvas, 4K panel) text
stays small and becomes hard to read. This is the single most common
surprise for library-mode consumers targeting high-resolution buffers.

If your buffer is large, switch text to the **runtime TTF provider**:

1. Build with `-DUSE_TTF_RUNTIME=ON` (adds the vendored stb_truetype
   rasterizer — no external dependency, no FreeType).
2. Load a font and make it the global family:

   ```cpp
   #include "runtime_ttf_provider.hpp"   // zb::ui::TtfFamily

   static const zb::ui::TtfFamily family =
       zb::ui::TtfFamily::from_file("assets/fonts/Inter-Regular.ttf");
   zb::ui::set_font_family(family);
   // per widget: label->set_font_size(28, family);
   ```

3. On targets without a filesystem (embedded ROM, one-file WASM
   pages), pack the TTF into a C array at build time with
   `tools/bytes_embed` and load it with `TtfFamily::from_memory(bytes,
   n)` — it borrows the buffer, so keep it alive for the app's
   lifetime.

In-tree references: `apps/showcase/src/showcase.cpp` (`from_file` +
`USE_TTF_RUNTIME`) and `demo/wasm/build.sh` (`bytes_embed` + a packed
font). Without the switch the 5x7 bitmap fallback keeps every build
green. To ship only the glyphs you need, subset the font first with
`tools/font_subset.py`.

## Dialog assembly — two things the defaults won't do for you

`Dialog` lays its frame out manually, which has two consequences that
fail silently if you miss them (both bite at `auto_layout`-off, the
default when you build the tree by hand):

1. **Call `layout()` once after configuring the dialog.** Without it
   the frame is never centered in the dialog area — no error, just a
   misplaced frame. (With `CanvasWindow::set_auto_layout(true)` the
   paint loop performs pending layouts for you.)
2. **Title height follows the title's font size only if the title
   declares one.** The default title box is 16px; if you set a larger
   font on the title (`dialog.get_title().set_font_size(px)`), the box
   auto-grows to `px + 4` at layout time — unless you gave the title an
   explicit `set_size`, which always wins.

## Describe UIs as text (optional)

Layouts can also be written as `.ui` design files — a tiny text format
packed and validated at build time, then loaded from a C array on any
target (no filesystem needed, NDS included). The showcase's two pages are
built this way; `tools/examples/menu.ui` is a minimal example. Preview any
`.ui` file interactively with the `ui_preview` story (command in the
README's Build table), and read `docs/design-file.md` for the grammar.

## Where to go next

- `docs/ARCHITECTURE.md` §1–§2 — the big picture, module map and
  dependency rules; §3–§5 — the normative contracts (frame lifecycle,
  input, pixel model, C-ABI hosts, build options)
- `docs/code-contract.md` — the API-level contracts you code against
- `binding/include/zbapi.h` — the stable C-ABI, if you want to host the
  UI from C, Python (ctypes, see `demo/python/`) or WebAssembly
  (see `demo/wasm/`)
