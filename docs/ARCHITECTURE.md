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

- **A-5. Shell repaint divergence — Win `WM_CHAR` bypass (P0) — DONE 2026-08-28.**
  **Fixed.** `imshell/src/win/main.cpp:283` now routes `WM_CHAR` through
  `send_input()` so the dirty→paint→present chain runs. Verified by
  desktop build.

- **A-6. Header-guard typo — `iminput` (P1, hygiene but blocks
  future includes) — DONE 2026-08-28.**
  **Fixed.** `iminput/include/input.hpp` guard renamed from
  `IMEVENT_INPUT_HPP` to `IMINPUT_INPUT_HPP`. No ABI impact.

- **A-7. `FONT_SUBSET` source scan omits `.ui` documents (P2) — DONE 2026-08-28.**
  **Fixed.** `imcore/CMakeLists.txt:40` glob extended to `*.ui` (CMake done
  in 6c76e66). `tools/font_subset.py` now has `scan_ui_file()` that
  extracts `text="…"` and `items="…"` attributes, handling line
  continuations and escapes. Non-ASCII `.ui` strings now reach the subset
  generator; undrawn glyphs still warn (fallback remains ASCII-only).

- **A-8. `USE_FONT` text path hidden allocation (P2) — DONE 2026-08-28.**
  **Fixed.** `imcore/src/text/font.cpp:190` `draw_alphamap` now reserves
  capacity before resize; steady-state repaints allocate zero. Added
  `USE_FONT` variant to `test_alloc_guard` (scenario 5).

- **A-9. `ListBox` row-cache key omits content (P2) — CLOSED
  2026-08-28 (contract-documented).**
  **Resolution.** The invalidation duty is the API contract, not a bug:
  `docs/code-contract.md` §8 ("ListBox 动态模型失效") binds callers to
  call a setter (e.g. `set_item_text` with the same args) after dynamic
  `ItemText` content changes, and the `ListBox` class doc restates it.
  A fine-grained `touch_row(row)` stays deferred until a consumer needs
  it; the contract clause names the convergence point.

- **A-10. Damage culling over-prunes overflow children (P2) — CLOSED
  2026-08-28 (not reachable in the current render model).**
  **Resolution.** The prune is provably safe: `Widget::draw` clips
  drawing to the widget's own rect (`clip_safe`) *before* descending,
  so a widget's visible contribution is always a subset of its own
  bounds (and of every ancestor's). `parent ∩ damage = ∅` therefore
  implies no descendant pixel could show inside the damage, and the
  subtree prune hides nothing a full frame would draw. The "overflow
  children" named by the review (negative positions, Dialog mask,
  explicit overflow) are clipped to invisibility on full frames too —
  the Dialog mask fills its own size only. `test_dirty` pins the
  invariant in both directions: an overflow child repaints its
  in-bounds sliver on a partial repaint (the prune does not over-cull
  it), and a fully-outside child stays invisible even when damaged.
  The proposed `damage_bounds()` machinery was rejected as fixing an
  unobservable problem. If a future feature renders children outside
  their parent's clip (e.g. an `overflow: visible` mode), the prune
  must be revisited in the same change — see `docs/code-contract.md`
  §9.

- **A-11. Small correctness/hygiene items (P3, batchable) — DONE
  2026-08-28.**
  - `ptr.hpp:140` non-atomic `SharedPtr::reset(nullptr)` no longer
    allocates — added `if (p==nullptr) return nullptr` in `make_count`. **DONE**
  - `imevent/event.hpp:97` `next_id` wraparound now scans live ids
    (O(n) per subscribe on wraparound edge). Comment updated. **DONE**
  - `Checkbox`/`RadioButton` `draw_at` no longer mutates state: the
    label offset is a layout-time value, set by the constructor and the
    `box_size`/`text_gap` (`circle_size`/`text_gap`) setters via
    `sync_text_offset()`; `text_offset_` lost its `mutable` and
    `set_text_offset` its `const`. **DONE**
  - `demo/python/myapp.py` maps `K_BACKSPACE`/`K_DELETE` now;
    `demo/wasm/main.js` keys off `e.key` (not the deprecated
    `keyCode`) with the full navigation/editing key set. **DONE**
  - `imcore/include/core/color32.hpp:9` / `CMakeLists.txt:204`
    32bpp `ENDIAN`/`RGB_MODEL` are mirror names for the same BGRA
    bytes — added equivalence comment. **DONE**

### A-12..A-18. Backlog from 2026-08-28 hy3 review (prioritized)

Source: `reports/codereview_hy3_20260828.md`. Items are **open, not
scheduled**; priority is P0 (ship-blocker) → P3 (hygiene). These are
**not** reachable on the normal `CanvasWindow::paint → clip_safe →
widget::paint` path today — `clip_safe` (`graphics.cpp:104`) intersects
the draw area to the damage rect and refuses a degenerate clip, which
masks A-12/A-13. They become real for any direct `Graphics` draw under
`damage_on_` (host-side draw, future code path) or for a missed
eviction handshake. None duplicates A-1..A-11.

- **A-12. Damage hard-clip off-by-one + `fill()` underflow (P2) — DONE
  2026-08-28.**
  **Fixed.** `draw_pixel` clips through the `damage_contains` predicate
  (A-13): the half-open rect means `sx == damage_r_` / `sy == damage_b_`
  are outside, so the boundary column/row is no longer written. `fill`
  intersects the inclusive draw area with `[l, r-1] x [t, b-1]` and
  returns early on a degenerate result, so a draw area that does not
  intersect the damage rect no longer produces `start > end` and a
  negative width fed to `fill_n`. Covered by the new `test_raster_damage`
  suite (direct `Graphics` probes under `damage_on`, bypassing
  `clip_safe`): exclusive-edge rejection, partial-overlap clipping,
  non-intersecting no-op, degenerate rect, `draw_image` delegation.

- **A-13. Split damage/draw-area clip conventions (P2, root cause of
  A-12) — DONE 2026-08-28.**
  **Fixed.** One canonical point where the half-open damage rect and the
  inclusive draw area meet: `Graphics::damage_contains(x, y)`
  (`graphics.hpp`). `draw_pixel` uses it directly; `fill` derives its
  clamp from the same bounds; `draw_image` delegates per pixel to
  `draw_pixel` and inherits the clip (it never had its own clamp site —
  the review listed three, the code had two). The half-open convention
  is documented on `set_damage` and in `docs/code-contract.md` §9.

- **A-14. Dispatcher cached-pointer liveness (P3, defensive) — DONE
  2026-08-28.**
  **Fixed.** `InputDispatcher::dispatch` probes the three cached
  pointers (`pressed_target`, `focus_target`, `modal`) with
  `is_descendant_of(&root)` before any other handling and drops the
  ones that left the tree, so a `remove_child` without the evict
  handshake no longer feeds moves/releases/keys to a detached widget.
  A **destroyed** widget is still the caller's contract breach
  (removal goes through `CanvasWindow::remove_from`, which evicts
  first); the guard covers the removed-but-alive case. Covered by a
  `test_dispatch` "removed without evict" block.

- **A-15. Cross-shell wheel delta magnitude (P3, fragile) — DONE
  2026-08-28.**
  **Fixed.** The unit is one signed notch (±1) on every shell: the
  Win32 shell divides `GET_WHEEL_DELTA_WPARAM` by `WHEEL_DELTA`
  (sub-notch deltas from free-spinning wheels are dropped), the X11
  shell already sent ±1. Consumers may now rely on magnitude, not just
  sign; the unit is documented in `docs/code-contract.md` §3.1. The
  mapping will move into the A-2 `InputSource` extraction unchanged.

- **A-16. FB source-size memcpy bound (P3, latent) — DONE 2026-08-28.**
  **Fixed.** `FB::draw` clamps the region against the *source*
  dimensions as well (`rw <= w - rx`, `rh <= h - ry`), so an app
  surface smaller than the panel can no longer read past the source
  buffer when the region is pinned to a panel edge. Not unit-tested:
  `FB::draw` needs a live `/dev/fb0`; the change is a three-line clamp
  mirrored from the existing screen-side clamp.

- **A-17. `font_subset` dedicated `.ui` extractor (P3, follow-up to
  A-7) — DONE 2026-08-28.**
  **Done.** `tools/font_subset.py` has `scan_ui_file()` that parses
  `text="…"` / `items="…"` explicitly (no generic regex), and
  `.ui`-sourced code units no longer trigger the "not drawn" warning
  (a design file may carry strings the app never draws at runtime);
  they still participate in the generated subset. Source literals keep
  warning as before.

- **A-18. `event.hpp` misleading comment (P3, hygiene) — DONE
  2026-08-28.**
  **Fixed.** The comment now states the truth: the live-id scan runs
  on every subscribe (one O(n) pass over the handlers in the common
  collision-free case); the loop only iterates more than once on the
  2^32 wraparound edge, and the theoretical all-ids-live
  non-termination is documented as unreachable.

### Batch K from the 2026-08-28 nemotron3 review — triaged 2026-08-28

Source: `reports/codereview_nemotron3_2026-08-28.md` (N1..N10
implementation items, D1..D10 architecture debt). Implementation items
are disposed below; the debt items are recorded, not scheduled.

- **K-N1. `draw_image(image_t)` row_stride lower bound — DONE
  2026-08-28.**
  **Fixed.** `Graphics::draw_image(const image_t &)` rejects a
  malformed view (`pixels == nullptr`, or an explicit `row_stride`
  below `width` — rows would overlap) instead of reading past each row;
  nothing is drawn, matching the silent-rejection convention of the
  other `Graphics` bounds checks. The contract is documented on
  `image_t` (`imcore/include/core/image_view.hpp`) and covered by a
  `test_raster_damage` block.
- **K-N2. Flex wrap ignores flex children's main demand — CLOSED
  (by design, documented).** Flex children contribute 0 to the wrap
  decision and absorb the leftover line space; shares are clamped at
  0, so no overflow is possible. Semantics documented in
  `docs/code-contract.md` §3.
- **K-N3. Cross-axis uses `measure()` for flex children — CLOSED
  (by design, documented).** Text never wraps in this framework, so a
  content-derived cross size cannot overflow; this matches the CSS
  default (no stretch). Same contract bullet as K-N2.
- **K-N4. Row-cache key carries the absolute row index — CLOSED
  (contract-documented).** Inserting/removing rows invalidates every
  later row's cache entry; the caller re-triggers a full rebuild with
  any setter. Added to the A-9 clause in `docs/code-contract.md` §8.
- **K-N5. `TextInput` drops non-ASCII `ch` — CLOSED (contracted).**
  The `ch` channel is ASCII by contract with UTF-32 as the stated
  future extension (`docs/code-contract.md` §3.1); IME composition is
  out of scope for this framework.
- **K-N6. `input()` is `noexcept` across user handlers — CLOSED
  (contracted).** Contract §1.5 already forbids throwing handlers;
  termination on violation is the intended consequence, same as a
  destroyed-widget breach.
- **K-N7. `Dialog::layout` leaves the frame's layout flag set — DONE
  2026-08-28.** **Fixed.** The dialog bypasses the frame's
  `Panel::layout` by design (its linear placement would overwrite the
  dialog geometry), so `Dialog::layout` now clears the frame's flag
  itself. `Dialog::get_frame()` exposes the panel; `test_dialog`
  asserts the flag clears under auto layout.
- **K-N8. Explicit zero geometry dropped by the builder — DONE
  2026-08-28.** **Fixed.** `apply_common` gates `width`/`height`/
  `pos_x`/`pos_y` on property presence, not value; a declared 0 is an
  explicit value. Covered by a `test_builder` block; contract bullet
  in §4.
- **K-N9. Integer `id=` silently dropped — DONE 2026-08-28.**
  **Fixed.** The `.ui` parser accepts an unquoted integer id and
  stores its decimal string; `docs/design-file.md` carries the grammar
  note, `test_ui_file` the regression.
- **K-N10. `zb_buffer` pointer lifetime — DONE 2026-08-28.**
  **Documented.** `zbapi.h` now states the pointer is valid only until
  the next `zb_paint` and must be copied out synchronously (caching it
  across frames is a use-after-free).

Debt items (D1..D10), triaged: D2 merges into A-9's deferred
`touch_row`; D5 into the A-2 Presenter dirty-region protocol; D8 is
A-3; D10 is superseded by the CI matrix (`.github/workflows/ci.yml`);
D6 is closed by the §5.1 minimalism decision (design-file.md). D1
(min/max sizes), D3 (focus history), D4 (Event once/priority) and D9
(resource management) stay open without consumers and are not
scheduled.
