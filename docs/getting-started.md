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
