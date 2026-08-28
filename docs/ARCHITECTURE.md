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
| `imapp`   | App interface (`IApp`/`IWindow`/`IGui`) + default `CanvasWindow` implementation             | INTERFACE    | `imcore`, `imevent`, `imui`, `iminput` |
| `apps/<story>_app` | Demo app implementing `make_app()` (the only place app code lives)                 | STATIC       | `imapp`                             |
| `imshell` | Platform shells (win / linux-x11 / linux-fb / nds); each owns the main loop                | executable   | all modules + `<story>_app`         |
| `binding` | `zbapi` C-ABI shared library + C smoke test                                                | SHARED       | `imapp`, `<story>_app`              |

Dependency rule (enforced by review): framework libraries never depend on
apps; apps never become framework code; only the shell and the binding
instantiate an app. `imcore` is a CMake `SHARED` library so the Linux host
`zbapi` can load it as a sibling `.so`; module boundaries are logical.

## 3. Targets

What each target is; how to configure and build it is operational
knowledge and lives with the quick-start material.

| Target | Implementation | Fixed characteristics |
|---|---|---|
| Windows | `imshell/win` (Win32) | 32bpp BGRA presentation; a `COLOR_DEPTH=16` build compiles and passes tests but has no presenting shell |
| Linux | `imshell/fb`, `imshell/x11` | X11 is the input-capable backend; the framebuffer backend presents only (no input source) |
| Nintendo DS | `imshell/nds` + `cmake/nds.toolchain.cmake` | ARM9, 4 MB RAM, no FPU/RTTI/libatomic; 16bpp abgr1555; ROM packaged by ndstool (POST_BUILD) |
| WebAssembly | `demo/wasm` (Emscripten) | JS host wraps the pixel buffer as canvas; node smoke test in-tree |
| Host languages | `binding` (`zbapi` shared library) | Python/ctypes demo and a C smoke test drive the C-ABI |

Tests (`test/test_imui`) run on hosts only (embedded builds skip them)
and cover 30+ suites with plain asserts.

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
  (win32, x11) call `paint()` after input only when `is_dirty()` became true
  (the shared `send_input` helper) and present on the `painted` event.
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
- **Key mapping lives in the shells.** Physical keys → `key_code`
  (navigation/editing) and printable characters → `ch` (a Unicode code
  point; ASCII today) are mapped per platform (Win32 `WM_KEYDOWN`/`WM_CHAR`,
  X11 `XLookupString`, NDS D-pad/A/B, JS `e.key`, pygame unicode). The
  framework never derives `ch` from `key`. Space is a key (activation), not
  `ch`.
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
  16bpp = 2 bytes abgr1555); `zb_buffer_bpp()` reports the byte width.
  The 4th byte of a 32bpp pixel is **not** part of the contract — hosts must
  present pixels as opaque.
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

## 5. Known limitations (public)

- No Mac shell (CMake fails with a clear `FATAL_ERROR`).
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

Items from the 2026-08-23 read-only architecture review. Status: **open** —
recorded risks and proposed directions, not scheduled work.

### A-1. Pixel-format / presentation seam (highest priority for expansion)

- **Problem.** The pixel memory model is a global compile-time macro matrix
  (`COLOR_DEPTH` × `RGB_MODEL` × `ENDIAN`) that the rasterizer, the shells,
  and the C-ABI buffer contract all share. Every new embedded display
  format (RGB565, BGR565, RGB888, grayscale e-ink, 12-bit, different
  endianness) currently means new `#elif` branches in `color*.hpp`, new
  CMake matrix entries, new blend behavior (16bpp is *one* abgr1555 model),
  and updates to every shell + the C-ABI format documentation.
- **Impact.** Each new target multiplies build/test matrix cells and forks
  logic across headers and shells; the byte layout also occupies stable C-ABI
  real estate. The framework has runtime seams for almost everything else
  (`GlyphProvider`, `Event`, `IApp`) but the pixel boundary is still
  "everything in the kernel".
- **Proposed direction.** Introduce a presentation seam: keep one internal
  buffer format and add a small conversion stage at the presentation edge
  (shell or binding) that writes the panel's format (row-conversion,
  optional dithering). New panel formats then implement the converter once
  instead of forking the kernel. The C-ABI could later report the format via
  a new export instead of build-time-only documentation.
- **First step for a good test case.** Linux FB panels frequently expose
  RGB565 — coverting `fb.cpp`'s wrap-around blit to go through one
  `convert_row(format, ...)` would validate the seam against a real target.

### A-2. Shared shell presenter / input-mapper abstraction

- **Problem.** Every shell re-implements the same jobs differently: present
  (Win32 `SetDIBitsToDevice` hard-coded 32bpp, X11 `XCreateImage`+`XPutImage`,
  FB row-memcpy+`msync`, NDS `dmaCopy` to VRAM), region tracking, and input
  mapping (wheel delta: Win32 `GET_WHEEL_DELTA` vs X11 button4/5 ±1; the
  `send_input` "paint after input when dirty" helper was added twice across
  reviews). NDS additionally hard-codes `256×192`, one VRAM bank, and DMA.
- **Impact.** Each new target is a whole new shell (event loop + blit +
  input mapping + packaging) with duplicated, subtly divergent logic;
  embedded panels (SPI, e-paper, tiled, rotated) would each be hand-rolled.
- **Proposed direction.** Extract two small interfaces used by all shells:
  a `Presenter` (buffer format + region blit + optional dirty-region
  protocol) and an `InputSource` (physical pointer/key/touch →
  `input_event`, centralizing `key_code` mapping). A new embedded target
  then implements ~100–200 lines instead of a shell.
- **Related.** A-1 (the presenter is where the pixel conversion lives) and
  A-3 (target attributes must not be baked into a single shell).

### A-3. Target-attribute policy (de-harden the NDS path; runtime format query)

- **Problem.** The "embedded target" story is the NDS story: toolchain
  `FORCE`s, ROM packaging (`ndstool` + ARM7 binary), screen geometry,
  `MODE_FB0`/VRAM/DMA, and the C-ABI "format fixed at build time" wording
  are all NDS-specific. A second handheld/MCU target would copy the NDS
  shell and tweak magic numbers, or fork a new platform scheme.
- **Impact.** Every new ARM/embedded target pays for NDS assumptions and
  re-implements packaging; hosts cannot introspect the buffer format at
  runtime.
- **Proposed direction.** Document a per-target policy (what belongs in the
  toolchain file vs the shell vs the binding), add a generic Linux-FB+
  "any panel format" first-class target (beyond the current warning-only
  mismatch check in `fb.cpp`), and consider a `zb_buffer_format()` export so
  hosts can adapt without recompiling their expectations.

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
- **A-4.3 Present logic duplication.** The region-present logic exists in
  every shell with different edge cases; A-2's `Presenter` should absorb it.

### A-5..A-11. Backlog from 2026-08-28 Muse review (prioritized)

Source: `reports/codereview_muse_20260828.md`. Items are **open, not
scheduled**; priority is P0 (ship-blocker) → P3 (hygiene). Facts that
require an API shape change have a companion entry in
`docs/code-contract.md`.

- **A-5. Shell repaint divergence — Win `WM_CHAR` bypass (P0).**
  **Problem.** `imshell/src/win/main.cpp:281` (`WM_CHAR`) calls
  `g_app->input(ev)` directly, bypassing the shared `send_input`
  helper (`input → is_dirty → paint → InvalidateRect`). `TextInput`
  edits via `ch` (`docs/code-contract.md` §3.1) therefore mark damage
  but the Win shell never drives the next frame until the next `WM_KEYDOWN`/
  mouse event. X11 (`main_x.cpp:201`), NDS, WASM and Python hosts go
  through `send_input`/`zb_input`'s dirty check and are unaffected.
  **Proposed direction.** Make `WM_CHAR` reuse `send_input`; add a
  `ShellTest` harness or `test_dispatch` extension that asserts
  "a `ch` edit is dirty after dispatch" on every shell path.
  **First step.** One-line fix in `win/main.cpp` + manual Win smoke
  (`TextInput` doc in `ui_preview`).

- **A-6. Header-guard typo — `iminput` (P1, hygiene but blocks
  future includes).**
  **Problem.** `iminput/include/input.hpp:1` guards with
  `IMEVENT_INPUT_HPP`, a copy-paste from `imevent`. The name collides
  conceptually with the `imevent` module and would silently drop the
  second header if a future file ever uses the same macro.
  **Proposed direction.** Rename to `IMINPUT_INPUT_HPP`; add a
  `grep` CI check for "guard matches path" (`tools/lint_guards.py`).
  Fix is a one-line `edit` with no ABI impact.

- **A-7. `FONT_SUBSET` source scan omits `.ui` documents (P2).**
  **Problem.** `imcore/CMakeLists.txt:40` `GLOB_RECURSE` scans only
  `apps/*/src/*.cpp`, `apps/*/include/*.hpp`, `test/*.cpp`. Strings in
  `.ui` files (`text="…"`) — now a selling point (`docs/design-file.md`,
  backlog I) — never reach `tools/font_subset.py`, so a non-ASCII
  `.ui` falls back to zero-width at runtime with no warning.
  **Proposed direction.** Extend the glob to `*.ui`; teach
  `font_subset.py` to parse `text="…"` / `items="…"` either by reusing
  `parse_ui_text` or by a small `*.ui` scan mode. Fallback remains
  ASCII-only.

- **A-8. `USE_FONT` text path hidden allocation (P2).**
  **Problem.** `imcore/src/text/font.cpp:190` `draw_alphamap` `resize`s
  the per-`Font` color map on every glyph. `USE_FONT` text therefore
  allocates even when the hot-path gate (`test_alloc_guard`,
  `docs/code-contract.md` §8) reports 0 for the bitmap-only path.
  **Proposed direction.** `reserve`/`resize` once or hoist the buffer
  to the `Graphics`/`write` batch; add a `USE_FONT` variant to the
  allocation gate.

- **A-9. `ListBox` row-cache key omits content (P2).**
  **Problem.** `imui/include/list_box.hpp:142` / `list_box.cpp:325`
  key is `(row,sel,w,h,fg,bg)`. A dynamic `ItemText` that changes
  without a setter call (allowed by the current static-model doc) serves
  stale bitmaps. The invalidation duty ("call any setter") lives only in
  a comment (`code-contract.md` §8) and is easy to miss.
  **Proposed direction.** Document the duty as API contract and/or
  provide `touch_row(row)` / content-hash check (`code-contract.md`
  §8 ListBox exemption) so the framework can auto-invalidate.

- **A-10. Damage culling over-prunes overflow children (P2).**
  **Problem.** `imui/src/widget.cpp:38` prunes a whole subtree when
  the widget's own bounds miss the damage region. Children positioned
  outside the parent (negative `pos`, `Dialog` mask, explicit
  overflow) are never drawn on a partial repaint; the rasterizer's
  per-pixel `damage` clip would have been sufficient.
  **Proposed direction.** Replace the subtree prune with a per-widget
  test only, or add a virtual `damage_bounds()` that containers
  override with the union of children; validate with a `test_dirty`
  overflow probe.

- **A-11. Small correctness/hygiene items (P3, batchable).**
  - `ptr.hpp:32,118` non-atomic `SharedPtr::reset(nullptr)` still
    `new int(1)` — add `if (p==nullptr) return nullptr` in `make_count`.
  - `imevent/event.hpp:97` `next_id` wraparound only skips `0` — scan
    for live collisions on wraparound (4 B subs edge, industrial
    displays).
  - `imuI` `Checkbox`/`RadioButton` `draw_at` mutates `mutable
    text_offset_` from a `const` method (`widget.hpp:264`,
    `checkbox.cpp:104`) — make the offset a layout-time value.
  - `demo/python/myapp.py:66` and `demo/wasm/main.js:30` map only a
    subset of keys — add `backspace/del` there; prefer `e.key` over
    deprecated `keyCode`.
  - `imcore/include/core/color32.hpp:9` / `CMakeLists.txt:204`
    32bpp `ENDIAN`/`RGB_MODEL` are mirror names for the same BGRA
    bytes — add a one-line equivalence comment or a `static_assert` on
    `offsetof(rgb,b)==0`.

### A-12..A-18. Backlog from 2026-08-28 hy3 review (prioritized)

Source: `reports/codereview_hy3_20260828.md`. Items are **open, not
scheduled**; priority is P0 (ship-blocker) → P3 (hygiene). These are
**not** reachable on the normal `CanvasWindow::paint → clip_safe →
widget::paint` path today — `clip_safe` (`graphics.cpp:104`) intersects
the draw area to the damage rect and refuses a degenerate clip, which
masks A-12/A-13. They become real for any direct `Graphics` draw under
`damage_on_` (host-side draw, future code path) or for a missed
eviction handshake. None duplicates A-1..A-11.

- **A-12. Damage hard-clip off-by-one + `fill()` underflow (P2).**
  **Problem.** Damage bounds are *exclusive* (`set_damage(l,t,r=x+w,b=y+h)`,
  see `damage_intersects` `graphics.hpp:111`), but the rasterizer mixes
  conventions: `draw_pixel` (`graphics.cpp:260`) rejects `sx > damage_r_`
  and so *permits* `sx == damage_r_` — one column/row written outside the
  region. `fill` (`graphics.cpp:236`) `draw_width = end_x - start_x + 1`
  with a one-sided damage clamp (lines 219‑234) can yield `start_x > end_x`
  → negative width → giant `std::fill_n` count (OOB write) when the draw
  area does not intersect the damage rect. `a7abe30` ("clip rasterizer
  writes to the damage region") only partially clips; the real protection
  today comes from the outer `clip_safe`, not these tests.
  **Proposed direction.** In `draw_pixel` use `sx >= damage_r_` /
  `sy >= damage_b_`; in `fill` clamp `end_x = damage_r_ - 1` (draw area is
  inclusive) and early-out when `end_x < start_x`. Or fold both into the
  A-13 unified helper.
  **First step.** Add a `test_raster_damage` probe that calls
  `Graphics::draw_pixel` / `fill` directly with `damage_on_` and an
  abutting/non-intersecting draw area, asserting no write past the region
  and no underflow.

- **A-13. Split damage/draw-area clip conventions (P2, root cause of
  A-12).**
  **Problem.** Damage rect is exclusive; `draw_area` is inclusive; the
  two are reconciled in three duplicated, slightly-wrong clamp spots in
  `graphics.cpp` (`draw_pixel`, `fill`, `draw_image`) with no single
  canonical "clip to damage" helper. A-12 is a direct symptom.
  **Proposed direction.** Add one `intersect_damage(area)` (returns an
  inclusive draw area) or one `in_damage(x,y)` predicate reused by every
  raster entry point, and delete the per-call clamp logic. Validates
  alongside the A-12 probe.
  **First step.** Refactor `draw_pixel` to call the single predicate;
  keep `fill`/`draw_image` on the same helper in the same commit.

- **A-14. Dispatcher cached-pointer liveness (P3, defensive).**
  **Problem.** `InputDispatcher` holds raw `Widget*` caches
  (`pressed_target`, `focus_target`, `modal`) and dereferences
  `pressed_target` at `dispatcher.cpp:255`
  (`!pressed_target->is_effectively_visible()`) *before* the null guard.
  If a widget is removed via `Panel::remove_child` /
  `FlexPanel::remove_child` without the documented `evict()` /
  `CanvasWindow::remove_from` handshake and then destroyed, this is a
  use-after-free. The contract (panel.hpp:65‑73, flex_panel.hpp:79‑87)
  requires eviction first and `evict()` (`dispatcher.cpp:75`) does clear
  it, so it is contract-gated — but note `handle_key` *does* re-validate
  `focus_target` liveness (line 151) while the press path does not, an
  inconsistency.
  **Proposed direction.** Validate liveness on every deref (e.g.
  `is_descendant_of(root)`), or make `Panel::remove_child` itself call
  `evict` so the safe path is the default; align the press path with the
  focus-path check.
  **First step.** Add a liveness guard in `dispatch` before the
  `pressed_target` deref; cover with a `test_dispatch` "remove while
  pressed" case.

- **A-15. Cross-shell wheel delta magnitude (P3, fragile).**
  **Problem.** `imshell/src/win/main.cpp:348` sends
  `GET_WHEEL_DELTA_WPARAM` (±120); `imshell/src/linux/main_x.cpp:260`
  (and other shells) send `±1`. Safe only because current consumers
  (`list_box.cpp:265`, `slider.cpp:115`) use the *sign*; a future
  magnitude-based wheel handler breaks on one shell.
  **Proposed direction.** Normalize wheel delta to a shell-independent
  unit in the `InputSource` layer (part of the A-2 Presenter/InputSource
  extraction); document the unit in `docs/code-contract.md`.
  **First step.** Pick a canonical unit (e.g. signed "notches"), convert
  at each shell's `send_input`, assert sign-only consumers still work.

- **A-16. FB source-size memcpy bound (P3, latent).**
  **Problem.** `imshell/src/linux/fb.cpp:46‑49` clamps `rw/rh` against
  `screen_width - rx` / `screen_height - ry`, but the source is buffer
  `b` of width `w`; if app surface width `w` differs from panel width,
  `rw` can exceed `w - rx` and the per-row `memcpy` reads past the
  source. Latent (current `main_fb` matches sizes).
  **Proposed direction.** Bound `rw/rh` against the *source* `w/h`
  (and the destination separately); add a `test_fb` with mismatched
  surface/panel widths.
  **First step.** Clamp copy width to `min(screen_width - rx, w - rx)`.

- **A-17. `font_subset` dedicated `.ui` extractor (P3, follow-up to
  A-7).**
  **Problem.** A-7's glob now feeds `*.ui` to `tools/font_subset.py`, but
  the script has no `.ui` branch — it scans `.ui` via its generic
  string-literal regex (works by accident) and emits noisy "code unit
  not drawn" warnings for any `.ui` string whose glyphs aren't in
  `extra_glyphs.py`. If `.ui` quoting ever changes, the scan silently
  misses glyphs.
  **Proposed direction.** Add a dedicated `.ui` text extractor (reuse
  `parse_ui_text` or a small scan mode for `text="…"` / `items="…"`),
  supressing the false warnings; keep the ASCII-only fallback.
  **First step.** Parse `.ui` `text`/`items` attributes explicitly in
  `font_subset.py`; verify a non-ASCII `.ui` string lands in
  `subset_glyphs.hpp` only when drawn.

- **A-18. `event.hpp` misleading comment (P3, hygiene).**
  **Problem.** `imevent/include/event.hpp:100` says the live-id scan
  "only runs on the 2^32 wraparound edge", but `while
  (id_in_use(next_id))` is evaluated on **every** `sub()` (one O(n) scan
  per subscription). Also theoretical: if all `2^32−1` ids were
  simultaneously subscribed the loop never terminates (not reachable).
  **Proposed direction.** Correct the comment to "scans live ids on
  every subscribe; cost is O(n) over handlers"; optionally note the
  all-subscribed non-termination as accepted.
  **First step.** One-line comment fix; no ABI impact.
