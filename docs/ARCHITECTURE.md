# Imprint — Architecture

> Living document. Describes the system **as built** and tracks the
> architecture backlog. Public: intended for anyone reading, porting, or
> extending the framework. This is the foundation of the documentation:
> it is self-contained and references no other document.

## 1. What Imprint is

A cross-platform, cross-language **retained-mode GUI framework** written in
C++17, rendered entirely in software (CPU rasterization). One code base
targets:

- desktop: Windows (Win32), Linux (X11 and Linux framebuffer)
- embedded: Nintendo DS (ARM9, 16bpp, no FPU/RTTI/libatomic)
- web: WASM (Emscripten, canvas)
- host languages: Python and any C host through the C-ABI `zbapi`

Because the host drives everything (§4.1), a script can take the host's
place unchanged: every app is drivable headlessly, which makes automation
and end-to-end testing a property of the contract, not an add-on (§4.11).

The default build is zero-dependency: no font, no image codec. Optional
features (FreeType fonts, PNG/JPEG via vendored stb) are compile-time
switches. The NDS toolchain forces a set of embedded-friendly options; every
other target uses the desktop defaults.

## 2. Module map

| Module    | Role                                                                                       | CMake target | Depends on                          |
|-----------|--------------------------------------------------------------------------------------------|--------------|-------------------------------------|
| `imutil`  | Logging macros `LD`/`LI`/`LW`/`LE`/`LF` (level-gated, zero-cost when suppressed)            | INTERFACE    | —                                   |
| `imevent` | Zero-allocation pub/sub `Event<T...>` + RAII `Subscription`; `PAINT_EVENT`/`CLOSE_EVENT`   | INTERFACE    | —                                   |
| `iminput` | `input_event` POD + `key_code` enum                                                         | INTERFACE    | —                                   |
| `imcore`  | Drawing kernel: `Graphics` (rasterizer, clipping, damage culling), compile-time `Color`, text (`GlyphProvider`, 5x7 bitmap, optional FreeType, UTF-8), codecs (`png`/`jpeg`, vendored stb) | SHARED | `imutil`                            |
| `imui`    | Widget tree: `Panel`, `FlexPanel`, `Button`, `Checkbox`, `RadioButton`, `Label`, `Slider`, `ListBox`, `TextInput`, `Dialog`, `InputDispatcher`; design-file layer (`ui_builder`, `ui_file`) | STATIC | `imcore`, `imevent`, `iminput`      |
| `imapp`   | App interface (`IApp`/`IWindow`/`IGui`) + `make_app()` entry; no widget dependency — a graphics-only app links only this | INTERFACE | `imcore`, `imevent`, `iminput` |
| `imapp_canvas` | Optional default `CanvasWindow` (an `IWindow` over the widget tree); the `imapp.hpp` umbrella lives here | INTERFACE | `imapp`, `imui` |
| `apps/<story>_app` | Demo app implementing `make_app()` (the only place app code lives)                 | STATIC       | `imapp_canvas`                      |
| `imshell` | Platform shells (win / linux-x11 / linux-fb / mac / nds), each owning its main loop. Shared A-2 seams in `shell/`: `region_to_present` + `dirty_coalescer` (what to present), `feed_input` (input tail), per-platform translators (`win_input`, `x11_input`). Pure provider: no story code, no executable | `shell_common` (STATIC) + `shell_backend` (INTERFACE usage requirements; backend main sources exported to the top level) | `imapp`, `iminput`, `<platform libs>` |
| `binding` | `zbapi` C-ABI shared library + C smoke test                                                | SHARED       | `imapp`, `<story>_app`              |

Dependency rule (enforced by review): framework libraries never depend on
apps; apps never become framework code; only the shell and the binding
instantiate an app. The executable is composed in exactly one place — the
top level `CMakeLists.txt` links `${STORY}` from `shell_backend` (usage
requirements + backend main sources) and `<story>_app` — so no framework
module names story code. Library-type policy: `INTERFACE` for pure-header
modules, `STATIC` for everything else, `SHARED` only at foreign-host
boundaries (`imcore`, so the Linux host `zbapi` can load it as a sibling
`.so`, and `zbapi` itself); module boundaries are logical.

## 3. Targets

What each target is; how to configure and build it is operational
knowledge and lives with the quick-start material.

| Target | Implementation | Fixed characteristics |
|---|---|---|
| Windows | `imshell/win` (Win32) | 32bpp BGRA presentation; a `COLOR_DEPTH=16` build compiles and passes tests but has no presenting shell |
| macOS | `imshell/mac` (AppKit) | 32bpp ARGB presentation through a zero-copy CGImage; behavior verified on the maintainer's macOS 13 machine, CI compiles the shell and runs the host battery |
| Linux | `imshell/fb`, `imshell/x11` | X11 is the input-capable backend; the framebuffer backend presents only (no input source) |
| Nintendo DS | `imshell/nds` + `cmake/nds.toolchain.cmake` | ARM9, 4 MB RAM, no FPU/RTTI/libatomic; 16bpp abgr1555; ROM packaged by ndstool (POST_BUILD) |
| WebAssembly | `demo/wasm` (Emscripten) | JS host wraps the pixel buffer as canvas; node smoke test in-tree |
| Host languages | `binding` (`zbapi` shared library) | Python/ctypes demo and a C smoke test drive the C-ABI |

Tests (`test/test_imui`) run on hosts only (embedded builds skip them)
and cover 30+ suites with plain asserts; the platform input-mapping
suites follow their platform (win32 suite on Windows, x11 suite where
the X11 shell builds).

### 3.1 Per-target policy (what belongs where)

Adding or adapting a target is glue, not framework surgery. Each layer
owns exactly what is listed; anything not listed is framework code and
stays target-agnostic.

| Layer | Owns |
|---|---|
| Toolchain file (`cmake/*.toolchain.cmake`) | Cross-compilation (compiler, sysroot, ABI/CPU) and FORCE of the §4.9 options an embedded target needs (NDS: `COLOR_DEPTH=16`, `USE_INTEGER_GEOMETRY`, `USE_NON_ATOMIC_PTR`) |
| Shell (`imshell/src/<platform>`) | The event loop, window/surface creation including screen geometry (`create_window(256, 192)` on NDS, `(320, 240)` on the FB shell), the platform blit, input translation, platform link libraries and backend main sources (exported via `IMPRINT_SHELL_*`). Shared present/input decisions come from the `shell/` seams (§4.1, §4.2), so a new target lands as ~100–200 lines of glue, not a new event loop |
| Composition & packaging (top-level `CMakeLists.txt`) | The `${STORY}` executable — the only composition point (§2) — and target packaging: the NDS `ndstool` POST_BUILD step, ELF rpath, framework linking |
| Binding (`zbapi`) | The foreign-host boundary only; hosts introspect the linked build at runtime (`zb_buffer_format()`, `zb_buffer_bpp()`, `zb_version()`, §4.8) instead of pinning build configurations |

Consequences:

- Target attributes — screen geometry, VRAM/DMA, ROM packaging, panel
  format — never enter a framework module or a story app; one story
  builds into every shell unchanged.
- A new panel format is a presentation-edge row converter (§4.4), never
  a kernel matrix entry; a new embedded constraint is a §4.9-style
  compile-time option (default OFF on desktop, FORCE ON in the
  toolchain), never runtime backend switching.
- Shared shell code never grows per-target branches; a target's facts
  live in that target's own shell main and toolchain file.

## 4. Architecture contract (as implemented, 2026-08-23)

This section states the contracts the current implementation actually
satisfies. Changing any of these is an architecture change.

### 4.1 Rendering loop and frame lifecycle

- **The shell owns the main loop.** The framework never calls the shell back
  at fixed points except through the event protocol below.
- App protocol (`IApp`): `input(ev)` feeds an event; `paint()` requests a
  frame; the app renders into its buffer and emits `painted(data)` (the
  framebuffer pointer) = "frame submitted, present it"; `is_dirty()` reports
  whether a frame is owed; `dirty_region()` reports what the last frame
  actually drew.
- **Always-on displays** (linux-fb, NDS) poll `is_dirty()` in their own
  loop and skip `paint()` while no frame is owed. **Event-driven shells**
  (win32, x11, mac) feed translated events through the shared `feed_input`
  seam (A-2), which calls `paint()` only when `is_dirty()` became true,
  and present on the `painted` event.
- **Present region (A-2):** what a shell blits is decided by the shared
  `region_to_present` rule (the app's dirty region when it drew something,
  the whole buffer otherwise, nothing when the frame drew nothing).
  Presenters whose platform batches invalidations (win `WM_PAINT`) keep
  the union of painted callbacks in `dirty_coalescer` until the present;
  presenters whose platform unions for them (mac `setNeedsDisplayInRect`,
  x11 immediate) present straight away.
- `CanvasWindow` (the default window): owns a `Graphics` framebuffer, a root
  `Panel`, and an `InputDispatcher`. Paint order is
  **auto-layout → damage walk → clear damaged region → draw tree → `painted`**.
- **Damage tracking**: every widget reports a dirty rect; the flags bubble to
  the root (`subtree_dirty_`), so `is_dirty()` is O(1). `walk_damage`
  **consumes rects as it reads them**; damage reported during a draw
  survives to the next frame. Presenting shells may blit only the last
  painted region.
- The rasterizer hard-clips all writes to the damaged region while damage
  mode is active, so partial redraws cannot smear over neighbors.

### 4.2 Input pipeline

- `input_event` is a **POD**: `{type, x, y, button, delta, key, touch_id,
  ch}`. This is the C-ABI pass-through shape and must never grow STL
  containers, virtuals, or `std::any`.
- The data model is multi-touch (`touch_id` identifies a pointer, mouse = 0),
  but the dispatcher tracks **one active press**; move/up from a different
  `touch_id` never interferes.
- The dispatcher: press picks the deepest hittable widget and claims it;
  release always delivers to the claimed target; a move cancels the press
  only if the pointer left the target by more than the **slop** (8 px),
  unless the widget `captures_pointer()` (slider/ListBox thumb drag).
- **Key mapping lives in the shell InputSource (A-2).** Each platform
  translator centralizes its `key_code` table in one testable place
  (`win_input::key_from_virtual_key`, `x11_input::key_from_keysym`, the
  mac NSEvent mapping) and maps printable characters into `ch` (a Unicode
  code point; ASCII today) per platform (Win32 `WM_KEYDOWN`/`WM_CHAR`,
  X11 `XLookupString`, NSEvent characters, NDS D-pad/A/B, JS `e.key`,
  pygame unicode). The framework never derives `ch` from `key`. Space is
  a key (activation), not `ch`. The translators are pure (no window,
  no server, no display) and dummy-driven unit-tested with synthetic
  messages; each returns handled / swallowed / not-handled so the shell
  keeps only its blit and its non-input event cases.
- Focus is keyboard-only and modal-scoped; `Tab`/arrows cycle focusable
  widgets; `Enter`/`Space` activate. Focus does not travel through hidden
  widgets or closed dialogs.

### 4.3 Widget tree

- Containers own children (`unique_ptr`); every widget keeps a raw `parent`
  pointer and renders children inside its own `draw_at`.
- **No RTTI** (NDS builds with `-fno-rtti`): traversal and hit-testing use
  the virtuals `pick`, `child_count`, `child_at`, `hit`. The declarative
  builder `static_cast`s only on widgets it created itself, so the tag table
  and the property table must stay in sync.
- The base widget draws background (color, then image) and then text by
  default; `draw_at()` overrides the foreground. `measure()` returns the
  natural size; `FlexPanel` sizes non-explicit axes from it and writes back
  per-axis (`set_width_auto`/`set_height_auto`) so an explicit cross-axis
  size survives a main-axis grow.
- Every state setter reports damage (`mark_dirty`) and layout change
  (`mark_layout_dirty`); this is a per-widget authoring obligation.
- **Tree mutation protocol**: removing a subtree that ever reached a
  dispatcher must go through `InputDispatcher::evict` /
  `CanvasWindow::remove_from` so pressed/focus/modal references are cleared
  before the nodes die.

### 4.4 Graphics and pixel model

- **The pixel format is fixed at build time** by a macro matrix in
  `imcore/include/core/color.hpp` + `imcore/CMakeLists.txt`:
  `COLOR_DEPTH` (32 or 16) selects `color32_t`/`color16_t`;
  `RGB_MODEL` + `ENDIAN` select the field layout via token-pasting
  (`MAKERGBNAME`, `MAKECOLORTYPENAME`).
- NDS forces `COLOR_DEPTH=16`, `RGB_MODEL=abgr`, `ENDIAN=1555` (memory layout
  `XBBBBBGGGGGRRRRR`). Desktop uses 32bpp; the actual byte order on x86
  little-endian is BGRA. **The typedef names are endian-relative and
  confusing** (`argb32_be_t` holds bytes b,g,r,a); porters should reason
  about byte order, not names.
- 32bpp has an 8-bit alpha channel; alpha blending is opt-in
  (`Graphics::enable_alpha`). 16bpp has a single alpha bit, so the blend
  path is replaced by binary opacity.
- `Graphics` can wrap an external writable buffer (wrapper mode) — the
  shape used by hosts that supply the framebuffer (WASM, Python,
  `CanvasWindow::create(w, h, buffer)`).
- `clip_safe()` returns a stack RAII `ClipGuard` (zero allocation per widget
  per frame); off-screen widgets get an invalid guard and draw nothing.
- **Presentation seam (A-1).** The kernel renders exactly one internal
  format per build (the `COLOR_DEPTH` matrix above); conversion to a panel
  format happens only at the presentation edge (a shell's blit), as a row
  conversion (`core/pixel_convert`: one row of internal `Color` → panel
  bytes). A new panel format (RGB565, grayscale e-ink, ...) adds a
  converter — it never adds a new `COLOR_DEPTH`/`RGB_MODEL`/`ENDIAN`
  combination to the kernel or forks the rasterizer. The C-ABI buffer
  keeps exposing the internal format (§4.8); conversion is a shell/host
  duty. Dithering is a future converter option, not part of the seam.

### 4.5 Text

- Framework API text input is **UTF-8**; widget text is stored as
  `std::u16string`; conversion lives in `imcore/text/utf8`.
- Rendering goes through the `GlyphProvider` seam: a primary provider
  (custom or FreeType via `USE_FONT`) with the built-in 5x7 `BitmapProvider`
  as fallback for uncovered code units; uncovered-by-both units are skipped.
- The 5x7 table covers ASCII (plus lowercase → uppercase) and, when Python
  is available at configure time, a build-generated subset of code units
  actually used in app/test sources (`FONT_SUBSET`).
- `USE_FONT` (FreeType) is optional and currently links through hardcoded
  per-platform paths — a known limitation, not a porting example.

### 4.6 Event system

- `Event<T...>` keeps a flat vector of `(id, handler)` pairs; `invoke()`
  iterates by index over the entry count, so subscribing during invocation
  defers the new handler to the next invoke, and unsubscribing during
  invocation tombstones the entry (compacted when the outermost invoke
  finishes). **No allocation on the invoke path.**
- `Subscription` is RAII and detaches safely in either destruction order
  (event first or subscriber first).
- Events are single-threaded; a handler must not throw (the C-ABI boundary
  catches — a swallowed exception plus partial state is worse than failing).

### 4.7 Error handling

- **Hot path (per-frame, per-input, setter) never throws**: failure is a
  return value (`nullptr`/`bool`/out parameter) or a log line.
- **Initialization path (construction, resource load) throws**
  `zb::ui::error` with a message.
- Codecs (png/jpeg) return integer codes; `0` = OK, documented per-call;
  these are the contract inside "exceptions-disabled" environments.
- The C-ABI boundary wraps every export in `catch (...)`: no exception may
  cross into a C host (UB, and a trap in WASM); failures are logged and the
  call returns a safe value.

### 4.8 Host-facing C-ABI (`zbapi`)

- **The host is the shell**: it drives `zb_input` / `zb_paint` /
  `zb_buffer` and nothing else.
- Buffer format is fixed at build time by `COLOR_DEPTH` (32bpp = 4 bytes,
  16bpp = 2 bytes abgr1555); `zb_buffer_bpp()` reports the byte width and
  `zb_buffer_format()` the format enum (`ZB_FORMAT_BGRA8` /
  `ZB_FORMAT_ABGR1555`), so a host can adapt at runtime instead of pinning
  a build configuration. The 4th byte of a 32bpp pixel is **not** part of
  the contract — hosts must present pixels as opaque.
- `zb_version()` reports the ABI version of the linked library
  (`ZB_API_VERSION` in `zbapi.h`); adding exports bumps it only when an
  existing export changes shape.
- All entry points must be called from **one thread**.
- Callbacks (`zb_set_painted_callback`, `zb_set_closed_callback`,
  `zb_set_log_callback`) fire inside the triggering `zb_*` call; a host must
  not destroy the app from inside a callback (use-after-free).
- The app never exits a process itself; on close it fires the closed
  callback and the host stops driving it.

### 4.9 Build-time configuration

| Option                    | Default | NDS toolchain | Meaning                                   |
|---------------------------|---------|---------------|-------------------------------------------|
| `COLOR_DEPTH`             | 32      | 16 (FORCE)    | bits per pixel; 16 = embedded only        |
| `USE_INTEGER_GEOMETRY`    | OFF     | ON (FORCE)    | integer circle/ellipse bounds (no FPU)    |
| `USE_NON_ATOMIC_PTR`      | OFF     | ON (FORCE)    | non-atomic `SharedPtr` refcount (no libatomic) |
| `USE_FONT` / `USE_PNG` / `USE_JPEG` | OFF | OFF    | optional features; codecs are vendored stb |
| `FONT_SUBSET`             | ON      | ON            | build-time 5x7 glyph subset (needs Python) |
| `STORY`                   | tictactoe | —          | selects which demo app the shell links    |

`USE_INTEGER_GEOMETRY` and `USE_NON_ATOMIC_PTR` are examples of the
framework's philosophy: **compile-time configuration per target list is a
positive asset for embedded**, not a wart — each target pays only for what
it needs, and there is no runtime backend switching.

### 4.10 Design-file layer

- `ui_node` is the single intermediate representation (fluent builders and
  the text parser both produce it); `build(host, root)` materializes it into
  a live widget tree; `parse_ui_text` parses the minimal text format;
  `tools/ui_embed` packs documents as C byte arrays at build time;
  `apps/ui_preview` is the desktop-only previewer.
- The layer is deliberately **static**: no dynamic models (ListBox `ItemText`
  callbacks), no event wiring, no font/glyph content in the description.

### 4.11 Script-driven hosts and test automation

The host-drives-everything protocol (§4.1) and the C-ABI host surface
(§4.8) mean a script can replace the human host unchanged: feed input,
pump frames, observe the result. Automation is a property of the
contract, not an add-on:

- **Determinism.** One thread drives everything; there are no timers, no
  animation, no background work. A frame exists only because the driver
  pumped it, so scripts never race the UI and never need sleeps.
- **Frame-ready signal.** The `painted` event (`zb_set_painted_callback`
  at the C-ABI) is the synchronization point: input that changed
  something owes exactly one frame, idle input owes none (§4.1).
- **In-process queries.** A C++ driver locates widgets by id
  (`Widget::find_by_id`), computes their rectangle in the input
  coordinate system (`get_absolute_position` + `get_size`), and reads
  focus (`InputDispatcher::get_focus_target`, `Widget::is_focused`) and
  text (`Widget::get_text`) directly. The `automation` suite in the test
  battery drives a real app this way through the public API only.
- **At the C-ABI surface** a foreign host has the same driving power
  (`zb_input` / `zb_paint`) and asserts on pixels (`zb_buffer`); widget
  geometry comes from the design file or the app's own layout constants.
- **Constraint on future changes.** A future cross-thread posting
  contract (a message queue such as `zb::ui::post(closure)`) must
  preserve all of the above: single-threaded driving semantics,
  deterministic frame order, and the observable painted signal.
  Automation suites are consumers of this contract.

## 5. Known limitations (public)

- The macOS AppKit shell (A-20) is verified behaviorally only on the
  maintainer's macOS 13 machine so far; CI compiles it and runs the host
  test battery, and the shell presents at 1x scale (no Retina mapping yet).
- `USE_FONT` needs hardcoded per-platform FreeType paths before it can be
  enabled elsewhere (`find_package`/CACHE is the intended fix).
- 16bpp builds are embedded-only: the desktop shells and DIB/XImage present
  assume 32bpp; a desktop `COLOR_DEPTH=16` build is compile/test-only.
- The Linux framebuffer shell has **no input source** (no keyboard/pointer);
  use the X11 backend for interactive desktop use.
- Input tracks a single active press even though the data model is
  multi-touch; key-up is not dispatched yet.
- By design: no GPU acceleration (rendering stays CPU; future "GPU support"
  would be presentation-only), no animation/transition system, no runtime
  backend switching, no multi-threaded rendering, no IME, no RTL/bidi.

## 6. Architecture backlog

Open items from the architecture reviews of 2026-08-23 .. 2026-08-29.
Completed items are removed from this list once done — the A-numbering
is stable, so gaps are finished work; the record lives in `git log`.

### A-4. Smaller items

- **A-4.1 Singleton lifetime.** `Widget` shares one process-wide
  `BitmapProvider`, intentionally leaked so widgets outliving `main` never
  touch a dead provider. Fine today; if a target ever needs a teardown/
  shutdown phase, this must be revisited (and it must stay stateless while
  shared — see the note in `widget.hpp`).
- **A-4.2 Shared/static duality of `imcore`.** The kernel is a CMake SHARED
  library (needed for host loading on Linux) yet is used as an embedded
  component in ROMs. This is intentional but is another axis new targets
  must handle; a packaging abstraction would help.

### A-19. Kernel pixel model as compile-time pixel traits (condition-triggered)

Proposal: re-express the §4.4 macro matrix
(`COLOR_DEPTH` × `RGB_MODEL` × `ENDIAN`) as `constexpr` pixel traits
(bit width, channel shifts, blend policy) selected at compile time.
**Runtime-neutral.** This is compile-time dispatch: exactly one
instantiation per build, the same machine code the macro switch
produces — the embedded rationale (no runtime format conversion) is
unaffected, and the NDS build already leans on the same technique
(`Event<T...>`, `zb::SharedPtr<T>`). The real costs are compile time
and a one-time refactor of `color*.hpp` plus the CMake option
plumbing. **Trigger.** Do not start on spec. Act only when the macro
matrix is about to grow for a need the presentation seam (§4.4) cannot
absorb — a genuinely new *kernel-internal* format, not a new panel
format (those are converters). Until then the macro matrix is the
cheaper representation.

### A-21. Retire the non-atomic `SharedPtr` branch (condition-triggered)

Today every non-embedded build uses `std::shared_ptr` (`zb::SharedPtr`
is an alias); the ~150-line non-atomic implementation exists only for
targets without atomics (NDS ARM9: devkitARM ships no libatomic). Its
semantics are locked by `test_ptr.cpp` (compiled against the custom
branch on the host) and the CI non-atomic matrix job runs the whole
battery against it. **What is deferred:** collapsing the duality —
either `std::shared_ptr` on the NDS too (needs a toolchain decision:
`__atomic` support on arm926ej-s / shipping a libatomic) or an
intrusive refcount owned by the objects themselves. Both are
ABI-adjacent changes with no current payoff. **Trigger.** Act when the
custom branch needs a real fix again, or when a second non-atomic
target appears; until then the tests keep it cheap to carry.

### A-23. Selective build/package switches (condition-triggered)

The whole tree always configures and builds; there is no
`IMPRINT_WITH_*` switch to trim the configure. Binary granularity is
already right — static linking drops unreferenced objects, the Linux
host ships only `libimcore.so` + `zbapi.so`, the NDS ROM is fully
static — and `zbapi.so` statically embeds imui + the story app, which
is inherent to the current C-ABI contract (a foreign host drives a
whole app). **Trigger.** Add configure-time module switches only when
a real distribution case appears that must ship or withhold specific
modules at configure time; until then the whole-tree build is the
cheaper representation.

### Unscheduled design debt (batch K triage)

D1 (FlexPanel min/max sizes), D3 (focus history), D4 (`Event`
once/priority) and D9 (resource management) stay open without
consumers and are not scheduled.
