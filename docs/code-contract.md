# Code Contract

This file is the framework's public API contract. When implementation and
documentation conflict, this file wins; an API shape must not change before
this file is amended first.

Division of labor (one-way dependency): architecture-level facts — module
boundaries, render loop, input pipeline, pixel model, event system, C-ABI
host rules, build matrix — are **not** restated here; `docs/ARCHITECTURE.md`
is their single home. The `.ui` design-file grammar and materialization
semantics live in `docs/design-file.md`. This file only describes the
**API-level contracts** on top of them: error handling, text/Unicode, glyph
providers, tree mutation, layout invalidation, allocation budget.

---

## 1. Error handling

### 1.1 Two paths, one principle

Framework APIs are classified by call frequency and failure semantics into
two paths. When designing a new API, **classify the path first, then write
the implementation**:

| Path | Criteria | Failure signal | Forbidden |
|---|---|---|---|
| Hot path | reachable every frame / every input dispatch (rendering, hit testing, dispatch, property setters) | return values: `nullptr` / `bool` / explicit out parameters | throwing, abort, integer error codes |
| Init path | one-shot: construction, resource/file/font loading | throws `zb::ui::error` (a `std::exception` subclass carrying a msg) | swallowing errors silently, bare integer error codes |

Rationale: exceptions are expensive on embedded targets such as the NDS and
some toolchains disable them by default; init-path failures (missing font
file, corrupt resource) must carry context, and the `error` msg is the
cheapest carrier.

### 1.2 Naming convention

- The `_safe` suffix is a binding "never throws" promise: when both
  `foo()` (init/internal-assert semantics) and `foo_safe()` (hot-path
  semantics) are offered, the `_safe` variant must be literally
  never-throws.
- When only one form exists, hot-path APIs take **no** suffix (not
  throwing is the default), e.g. `Widget::set_text`.

### 1.3 Current compliance inventory

| Location | Status | Verdict |
|---|---|---|
| `Graphics::clip_safe` | RAII `ClipGuard` (stack value, zero allocation): saves/restores draw state, never throws; used by the widget draw path | ✓ compliant |
| `Graphics::clip` | removed (no call sites; throwing semantics superseded by ClipGuard) | ✓ |
| `Graphics::clone` | kept for one-shot deep copies; no hot-path call sites | ✓ compliant |
| `Font` constructor | throws `Font::error` (RAII leak guard in place) | ✓ compliant (init path) |
| `Graphics` constructor / `clone` | throws `zb::ui::error`: the constructor rejects zero sizes and pixel counts overflowing `int` (64-bit multiply prevents wraparound — 65536×65536 used to wrap to 0, allocate an empty buffer, and still report a full-size draw_area → first fill/draw wrote out of bounds); clone's bounds guard uses 64-bit sums (`x+width` used to wrap negative and pass the check → out-of-bounds read) | ✓ compliant (init path) |
| `codec/` (png/jpeg, vendored stb) | `int` error codes, 0 = OK; read path: 1=open failed 2=not this format 3=decode failed 4=zero size 5=info callback rejected 6=row callback rejected; write path: 1=zero size 2=row callback rejected 3=open failed 4=write failed. Renumbered once when switching to stb (2026-08; previously libpng/libjpeg numbering 1~5/-1); semantics unchanged, numbers are not a long-term commitment | in-boundary contract: file I/O may run in exception-disabled environments, error codes are **retained** at this boundary; a future external wrapper throws `zb::ui::error` per the init path |
| `fb.cpp` TODO("throw error") | unimplemented | keep the TODO; do not turn it into a hot-path throw |

### 1.4 Shape of `zb::ui::error`

```cpp
class error : public std::exception
{
public:
    explicit error(const std::string &msg) noexcept;
    const char *what() const noexcept override;
private:
    std::string msg;
};
```

`Font::error` is a same-named type that exists today; it will converge to
an alias of — or be replaced by — the common `zb::ui::error`. The
`zb::ui::error` type itself already exists in
`imcore/include/core/error.hpp` (`what() const noexcept`, message taken by
`const&`); until then `Font::error` satisfies the "exception carrying a
msg" requirement.

### 1.5 Event callback contract

- `Event::invoke` / `operator()` is a hot path (input callbacks, state
  notifications). **Handlers must not throw**: an exception escaping a
  handler is a contract violation. Rationale in §1.1 — on the C-ABI host
  path the exception is swallowed by the boundary `catch(...)` (on wasm it
  is a trap); if event internal state was mid-update, the program keeps
  running in a "looks alive" state with a permanently corrupted handler
  table.
- Defensive guarantee: invoke depth is maintained by an RAII guard
  (`InvokeGuard` in `imevent/event.hpp`); even if a handler throws in
  violation, the depth unwinds correctly and the outermost invoke still
  performs tombstone compaction — the `unsub` semantics of "erase
  immediately outside invoke, tombstone inside invoke" hold on every path,
  and one exception cannot leave permanent tombstones in the handler table.
- Callback failures are reported via return values / out parameters /
  logging (`LW`/`LE`); exceptions must not be used as control flow
  (same rule as the hot path in §1.1).
- `CanvasWindow::input` is `noexcept`: an app/widget callback throwing out
  of it = `std::terminate` — when frame state is unpredictable, limping on
  is more dangerous than dying fast; hosts must not rely on catching.

---

## 2. Text / Unicode interface decisions

### 2.1 Internal representation: `std::u16string` (current, keep)

- `Widget` stores text as `std::u16string` (`widget.hpp`).
- `Font::measure` / `Font::write` accept `const char16_t*` (FreeType's
  UTF-16 semantics); widget drawing passes the internal buffer directly,
  no copy/convert.

### 2.2 Input form: framework APIs are UTF-8, always

- `const char*` parameters **always mean UTF-8**, no exceptions
  (`set_text`, `make_text_image`, and every future text entry point).
- Rationale: platform-neutral, no wchar width/ABI disputes, passes
  straight through the C-ABI (zbapi), native on Linux/NDS; the Win32 shell
  is responsible for wchar↔UTF-8 conversion and `wchar_t` never enters
  the framework API (root cure for Win32 `TEXT()`/MBCS width problems).
- **Landed**: `set_text(const char*)` decodes via `utf8_to_utf16`
  (`imcore/text/utf8.hpp`); signature unchanged.

### 2.3 Conversion layer location

- The UTF-8 ↔ UTF-16 converter lives in `imcore/text/utf8.hpp`
  (`utf8_to_utf16` / `utf16_to_utf8`, landed), same layer as `Font`.
- `imcore` does not depend on `imui`; conversion at the widget layer is
  just "decode → append to the u16 buffer".

### 2.4 Dual glyph providers (landed)

The `GlyphProvider` abstraction, `BitmapProvider` (built-in 5x7, never
depends on FreeType), and the FreeType wrapper provider live in
`imcore/include/text/`. `Widget::set_glyph_provider()` installs the main
provider; `set_font` remains a convenience alias. Standing contract
obligations:

- Fallback chain: main provider reports "glyph not covered" → fallback
  provider → still missing → skip.
- `#if defined(USE_FONT)` conditional compilation is only allowed at the
  provider-selection point; the widget draw path is unconditional.
- The shape of `set_text` / the internal u16 buffer does not change;
  `make_text_image` stays an independent API (tictactoe depends on it:
  direct bitmap-font blitting, not the widget text path).

**Font subset (batch E final, 2026-08-15; A-7 added 2026-08-28)**: the
BitmapProvider 5x7 table has two segments: built-in ASCII 32..95
(`kGlyphs`) plus a build-time generated subset table
(`subset_glyphs.hpp`, compiled only when `IMCORE_HAS_SUBSET` is defined,
generated at configure time by `imcore/CMakeLists.txt` invoking
`tools/font_subset.py` to scan string literals in `apps/` and `test/`
sources and intersect them with the hand-drawn table in
`tools/extra_glyphs.py`; sorted by code unit for binary search). Contract:

- Code units used by sources must have a hand-drawn 5x7 glyph in
  `extra_glyphs.py`; a missing glyph only produces a build warning (does
  not fail): that character is skipped with zero width at runtime — same
  semantics as "dynamically generated runtime text is not in the subset";
  characters absent from static sources never enter the table.
- **Design-file sources (A-7)**: `text="…"` / `items="…"` in `.ui`
  documents are source input too and must be scanned
  (`imcore/CMakeLists.txt` gains a `*.ui` glob; `font_subset.py` grows a
  `.ui` parsing branch). Until then, non-ASCII text in `.ui` being
  zero-width-skipped at runtime is a known gap.
- Builds without Python 3 or without `FONT_SUBSET=ON` fall back to
  ASCII-only: `IMCORE_HAS_SUBSET` undefined, coverage is exactly 32..95,
  behavior identical to pre-batch-E (tests assert both branches under
  `#if defined(IMCORE_HAS_SUBSET)`).
- 5x7 cannot render readable CJK (at least 12x12 needed): CJK goes to
  plan 2 (stb_truetype build-time TTF→bitmap layer converter) and is not
  covered by batch E plan 1.

---

## 3. Other API-shape rules

Architecture-level background (POD input events, no-RTTI traversal, C-ABI
host rules, render loop) is in `docs/ARCHITECTURE.md` §4; this section
keeps only API-level supplements.

- Parameter passing: scalar parameters (integers, `bool`, floating point,
  enums) are passed **by value**; `const T&` is reserved for class types.
  (2026-08-28 sweep removed all scalar pass-by-const-ref signatures across
  the public API.)
- Close-notification contract: host responsibility and reentry ban are in
  ARCHITECTURE.md §4.8; API-level supplement — wasm hosts register the
  callback via `addFunction` (build needs `ALLOW_TABLE_GROWTH=1`).

### 3.1 Character event contract

- `input_event.ch` (int, 0 = no character) is the **printable character**
  channel carried by key_down/key_up. Current semantics = ASCII code
  points (0x20~0x7e); the contract is already written for Unicode code
  points — when extended to UTF-32 values, `key` and the in-widget u16
  conversion layer follow in step, API signatures unchanged.
- Semantic split: `key` carries navigation/editing keys (`key_code`:
  tab/enter/space/arrows/backspace/del/escape), `ch` carries characters.
  The two fields are independent: navigation keys set only `key`
  (ch=0); text-producing keys may set both (space is the exception — the
  shell convention routes space through `key` to preserve activation
  semantics, TextInput inserts on key==space; other printable characters
  go through `ch`).
- Routing priority: the dispatcher **never** feeds ch!=0 keys into focus
  navigation; they go to the focused widget's `on_input` first and are
  dropped if unconsumed.
- Host responsibility: the key→ch character mapping happens in each shell
  (x11 XLookupString / win WM_CHAR / js `e.key` / pygame unicode); the
  framework never derives ch from key.
- C-ABI shape: `zb_input(app, type, x, y, key, ch, touch_id)`;
  non-keyboard events always pass ch = 0.
- key_up status: the dispatcher only dispatches key_down (`handle_key`
  returns false for key_up) and desktop shells do not forward release
  events either; before any widget needs release semantics, this contract
  and the shells must be extended first.
- Public boundary headers must be self-contained and includable by C
  hosts; they must not leak C++ types.
- Widget sizing: `Widget::measure()` returns the natural size (default =
  current size; Label/Checkbox/RadioButton/Slider/ListBox override with
  content-derived sizes). `set_size` sets the explicit flag; the layout
  layer (FlexPanel) applies measure() only to non-explicit children;
  `set_size_auto` is for layouts to write sizes back and clear explicit —
  app code does not call it directly. Explicit sizes always win over
  measure().
- **Explicitness is tracked per axis**: `size_explicit_w_/h_` are
  independent; `is_size_explicit()` = either axis (compatibility
  semantics), `is_width_explicit()/is_height_explicit()` query per axis.
  FlexPanel share assignment and size write-back use
  `set_width_auto()/set_height_auto()` to clear **only that axis's flag** —
  an explicit cross-axis size keeps its value and flag when the main axis
  grows.
- **FlexPanel wrapping and cross-axis semantics (batch K/N2, N3)**: the
  wrap test accumulates only non-flex children's main-axis demand; a flex
  child contributes 0 to wrapping (it absorbs in-row leftover space, its
  share clamps to ≥ 0, so it never overflows). Children with a
  non-explicit cross axis (including flex children) always take their
  `measure()` content size — framework text does not wrap, so content
  height is unaffected by main-axis assignment.
- Keyboard constraints: with a modal open, keyboard focus and
  Tab/arrow navigation are confined to the modal subtree (`focus_next`
  scopes to the modal); if the focused widget lies outside the modal
  (focused before it opened) or has become invisible
  (`is_effectively_visible()` = self and all ancestors visible), focus is
  released before the next key. When the pressed target is hidden
  mid-press, pointer events deliver `on_cancel` first, then clear the
  press; later move/release events are not delivered.
- Wheel channel: `ev.delta` is measured in **signed notches** (one wheel
  step = ±1); shells normalize before dispatch (win =
  `GET_WHEEL_DELTA_WPARAM / WHEEL_DELTA`, free-spinning sub-notch
  increments are dropped; x11 = button4/5 → ±1) — widgets may rely on the
  magnitude, not just the sign. The C-ABI `zb_input` `key` parameter
  carries this delta for mouse_wheel (existing zbapi.h contract) and
  `zbapi.cpp` maps it into `ev.delta`; `ev.x/y` must be **pointer
  coordinates in client space** (the dispatcher picks targets by
  coordinates).
- **Presentation-seam row converter (A-1)**: `core/pixel_convert` appears
  only at the shell presentation edge, never in the rasterizer hot path.
  `convert_row(format, src, count, dst, cap)` writes one row of internal
  `Color` as panel bytes and returns the byte count written; unknown
  format or insufficient `cap` → returns 0 and writes nothing (silent
  rejection; the caller owns alarming). `panel_pixel_bytes(format)`
  reports the format's bytes per pixel. New panel formats add a converter,
  not kernel macro-matrix combinations (`docs/ARCHITECTURE.md` §4.4).
  Green pack semantics: red/blue are 5-bit on both sides of the seam and
  recover exactly at every depth; green is 6-bit in the bgr565 word —
  truncated from the 8-bit channel at 32bpp, replicated from the 5-bit
  internal channel at 16bpp (full internal green packs to full panel
  green).
- **Shell presentation/input seams (A-2)**: shells keep only their
  platform blit and their non-input event cases; the shared decisions
  live in `imshell/include/shell/`. `region_to_present(...)` decides what
  a shell blits: the app's dirty region when the frame drew something,
  the whole buffer when dirty tracking is absent, nothing when the frame
  drew nothing. `dirty_coalescer` unions painted callbacks until the
  present and must never lose pending regions (an empty frame adds
  nothing; the win `WM_PAINT` presenter clears after presenting).
  `feed_input(app, ev)` feeds one event and repaints exactly when
  `is_dirty()` — shells never bypass it to present. Platform input
  translators (`win_input::translate`, `x11_input::translate`, the mac
  NSEvent mapping) centralize the `key_code` table per platform and
  return **handled** (feed the event), **swallowed** (an input-shaped
  event the framework deliberately drops — unmapped keydown, sub-notch
  wheel delta, middle button) or **not-handled** (the shell's own cases /
  `DefWindowProc`). They are pure — no window, server or display — and
  their behavior is locked by the dummy-driven suites
  (`test_shell_presenter`, `test_win_input`, `test_x11_input`).
- **Module consumption paths (A-22)**: `imapp` (`IApp`/`IWindow`/`IGui` +
  `make_app`) has no widget dependency — a graphics-only app links
  `imapp` plus a shell backend and implements `IApp` directly on
  `Graphics`. `CanvasWindow`, the default `IWindow` over the widget
  tree, is an optional add-on: link `imapp_canvas` (it pulls imui); the
  `imapp.hpp` umbrella includes `canvas_window.hpp` and therefore
  requires it. The shell executable is composed in exactly one place,
  the top-level `CMakeLists.txt`: `${STORY}` = `shell_backend` (imshell's
  per-platform usage requirements) + the backend main sources +
  `<story>_app`. Framework modules never name story code; only the
  shell and the binding instantiate an app, and only the top level
  links the executable.

## 4. Declarative UI builder (batch G contract)

- `ui_node` is the sole entry for static descriptions:
  type/id/ordered props/children/items/flex_grow. The fluent builder
  (column/row/panel/label/button/checkbox/radio/slider/list_box/text_input
  + .size/.pos/.text/.named/.checked/.group/.step/.rows/.spacing/.padding/
  .wrap/.flex/.visible) and the future design-file deserializer (G6) share
  one intermediate representation — the props produced by either must be
  consumable by the same property-resolution table.
- `build(host, root)`: the root node itself is the document (the host is
  the real container; the root tag does not instantiate a widget); root's
  spacing/padding/wrap apply when the host is a FlexPanel, spacing/padding
  when it is a Panel; root's children are materialized into the host one
  by one.
- Materialize semantics: unknown tags are logged and skipped (LW); a
  non-container tag with children → children silently dropped (LW); when
  the host is a FlexPanel, children with flex_grow go through flex
  layout, otherwise Panel linear layout.
- Property-resolution tolerance: missing or mistyped props → default
  values (silently, no throw; the init path no-throw principle applies);
  the `text` prop is accepted as a UTF-8 string (converted to u16
  internally).
- No-RTTI constraint: materialize's `static_cast` is legal because the
  property-resolution table only runs on widgets created by the factory
  table — the tag table and the apply branches must stay in sync; any
  change to one side must update the other (both live in ui_builder.cpp).
- Geometry props apply by **presence** (batch K/N8): `width=0`/`pos_x=0`
  are explicit values, not omissions, and are applied at materialize
  (setting explicit); an absent axis defaults to 0. Corresponding parse
  side: `id=` accepts an unquoted integer literal and stores its decimal
  string (batch K/N9; grammar defined in `docs/design-file.md`).
- Contract boundary (must not enter the description layer): dynamic models
  (ListBox's ItemText function pointer), event subscriptions (Event<>
  wiring), font/glyph content, runtime-generated text. The description
  layer carries static structure + props + id only.
- id references: `Widget::find_by_id` searches the subtree depth-first and
  returns the first match (linear search; for wiring/debugging only, not
  hot path); returns nullptr on miss. Event binding = materialize, fetch
  the Widget* by id, then subscribe via Event<> (the framework introduces
  no callback registry).

## 5. Design files (batch G6 final)

### 5.1 Form decisions (user-ratified; do not flip again)

- **Self-built minimal text format (not XML)**: a single C++ parser in
  `imui` (`parse_ui_text`) consumes an in-memory byte string; the source
  is decoupled (desktop = file, NDS = embedded C array, future hot reload
  only swaps the source).
- **No uic-style translation**: no converting text into C++ builder code
  (hosts would no longer share a code path, WASM/Python could not use it,
  and two tables must be kept in sync).
- **Uniform build-time embedding**: all platforms convert `.ui` text into
  C arrays at build time (`tools/ui_embed`); UI definitions do not ship
  with the binary and cannot be modified by users; the previewer is the
  only exception that reads external files.

### 5.2 Text format

The grammar (nodes/key=value, indentation nesting, line continuation,
comments, tolerance semantics), root-node handling, and materialization
semantics have their **single authoritative definition** in
`docs/design-file.md`. The parser implementation (`imui/src/ui_file.cpp`)
must stay consistent with it; change the document before changing the
grammar.

### 5.3 Packaging contract

- `parse_ui_text(text, ok)` is the only parsing entry: returns `ui_node`
  (same intermediate representation as the fluent builder, see §4);
  `ok=false` when the document has no nodes (parsing is a tolerant path,
  no throw).
- `ui_embed <out_header> <file.ui>...`: build-time packager, zero runtime
  dependencies; pass1 validates each document with the library parser
  (failure exits 1 = build error), pass2 generates
  `embedded_ui_file{name,bytes,len}` + `kUiFiles[]` + `find_ui_file(name)`.
  The generated byte array carries a `0x00` sentinel after the document
  bytes (`parse_ui_text` scans NUL-terminated input, matching the shape
  pass1 validated); `size` records the document byte count excluding the
  sentinel — consumers may rely on `data[size] == 0`.
- Consumption: `find_ui_file` fetches bytes → `parse_ui_text` →
  `build(host, doc)`; all platforms take the same code path.

---

## 6. Tree-mutation protocol (batch J3 final)

Safe contract for dynamically adding/removing widgets, guarding the
dispatcher's raw pointers against dangling/UAF:

- **Removal must go through the coordinating entry**: for a tree that has
  seen input, removing a child must go through
  `CanvasWindow::remove_from(panel, widget)` (or
  `clear_root_children()`); for a tree-level root use
  `root_->remove_child(w)`.
- **Calling `Panel::remove_child` / `FlexPanel::remove_child` directly is
  the coordination-free path**: allowed only when the caller guarantees
  that subtree never participated in input dispatch (no pressed/focus/
  modal pointer points into it); otherwise call
  `InputDispatcher::evict(widget)` first.
- `evict` cleans the three pointers: pressed_target (delivers on_cancel
  for an active press before clearing), focus_target (releases focus),
  modal (including the case where the modal is inside the subtree).
- `remove_child` returns `std::unique_ptr<Widget>` (ownership transferred
  to the caller; nullptr if not found) **and resets `w->parent` to
  nullptr**; `clear_children()` detaches and destroys all.
- After removal, `find_by_id` no longer matches; removing the same pointer
  twice returns nullptr.
- Event subscriptions held by removed nodes unsubscribe safely during
  destruction.

## 7. Layout protocol (batch J5 final)

- `layout_dirty_` is the layout invalidation flag: geometry/content
  setters (set_size, set_size_auto, set_text, set_font,
  set_glyph_provider; container add_child/remove_child,
  set_orientation/set_spacing/set_padding) call `mark_layout_dirty()`
  which bubbles to the root along the parent chain (zero allocation). A
  fresh tree starts dirty (`layout_dirty_ = true`). Layout clears the
  flag when done, so a given state triggers at most one layout.
- **Auto-layout is a gated, host-opt-in behavior**
  (`CanvasWindow::set_auto_layout(true)`, default off): when on,
  `paint()` runs `if (root_->is_layout_dirty()) root_->layout();` before
  the damage walk. When off, current behavior (explicit
  set_position/set_size preserved verbatim, manual layout idempotent) is
  unchanged. UI-description hosts (ui_preview) must opt in explicitly.
- **The in-paint order (layout → damage → draw) is defined
  architecturally in ARCHITECTURE.md §4.1**; API obligation: mark_dirty
  calls triggered inside layout must be picked up by the subsequent
  damage walk, so geometry and drawing agree within the frame.
- Invalidation propagation only sets the layout flag and does not
  additionally mark render dirty (geometry changes produce damage
  naturally when layout writes sizes back through its setters).
- Text advance cache (batch J4) invalidation duty: any setter that
  changes glyph content (set_text/set_font/set_glyph_provider) must reset
  the cache; when adding such a setter, invalidate in the same change +
  update this section + rely on the gate test as backstop.
- Intrinsic-setter audit duty: for widgets with a measure() override, the
  setters that feed parameters into measure() (checkbox box_size/text_gap,
  radio circle_size/text_gap, list_box row_height — the latter recomputes
  the derived height via set_size, which invalidates as a side effect)
  must call `mark_layout_dirty()`; new setters of this kind follow the
  same §7 invalidation duty.

## 8. Allocation budget (batch J7 final)

- **Hot-path definition**: the per-frame render path
  (paint/draw/walk_damage) and every input dispatch path (dispatch/pick)
  — hit testing, property setters, drawing. New hot-path code **must not
  allocate** (vector growth, make_shared, temporary
  string/stringstream).
- Gate: `test/test_alloc_guard.cpp` (the `test_alloc_guard` suite) locks
  parked-tree repaint, in-slop move dispatch, text drawing, and ListBox
  warm repaint after scrolling at zero allocations; every change to
  hot-path code must pass it.
- Exemptions and boundaries:
  - Log macros construct a stringstream at the default level (debug)
    (timestamp + message body): hot-path LD allocations are a **known,
    exempted item** — after `Logging::set_min_level` is raised to info or
    above, suppressed levels construct and allocate nothing; the gate
    test pins log-free paths at the default level.
  - `focus_next`'s per-keypress temporary `std::vector`: measured ≈41µs
    at 1011 nodes, far below the 5%-of-frame-budget evidence threshold —
    handled under the "conditional task" threshold, not pre-optimized.
  - The ListBox row-image cache (batch J2) — rebuild itself is
    <b>bounded</b> (budget `row_cache_budget`, window = visible rows);
    the exact rebuild count is locked by `test_list_box`, steady-state
    drawing allocates 0; invalidation duty for dynamic ItemText content
    changes lies with the caller (call any setter).
  - Init paths (construction, resource loading) are outside this budget
    (§1).
- Shared obligation: the process-wide shared BitmapProvider (batch J6)
  rests on it having no per-instance state; adding state to it requires
  removing the sharing first.
- **USE_FONT text path (A-8, 2026-08-28)**: `Font::draw_alphamap`'s
  per-glyph `resize` is a hot-path allocation; `test_alloc_guard`
  covers both the bitmap path and a `USE_FONT` variant (scenario 5:
  label+button repaint through FreeType allocates nothing after the
  first frame). New code on the `USE_FONT=ON` text draw path must meet
  the same zero-allocation requirement (reuse a buffer or batch
  `reserve`).
- **ListBox dynamic-model invalidation (A-9, 2026-08-28)**: the row-cache
  key is `(row,sel,w,h,fg,bg)` and does not include string content. If
  `ItemText` content changes without any setter call, hitting a stale
  bitmap is a caller violation. Callers must call `set_item_text` (same
  arguments suffice) or any setter with `invalidate_row_cache` semantics
  after changing content; if a fine-grained `touch_row` interface is ever
  provided, this clause converges into it.
  The key also contains the **absolute row number** (batch K/N4):
  inserting/deleting rows mid-model invalidates the keys of all following
  rows; callers likewise trigger a full rebuild through any setter (e.g.
  re-`set_item_count`).
- **Shell input→repaint chain (A-5)**: shell character events (the `ch`
  channel) must go through the `is_dirty → paint → present` chain
  (including Win `WM_CHAR` reusing `send_input`), otherwise `TextInput`'s
  `mark_dirty` never reaches the screen.

## 9. Frame-debt / repaint protocol

Frame lifecycle, damage propagation (`walk_damage` consumes as it reads,
damage reported during drawing survives to the next frame), and the
consistency between event-driven and polling shells — the architecture
definition is in ARCHITECTURE.md §4.1. This section keeps only API-level
obligations:

- Apps changing the UI outside the input path (timers/protocol callbacks)
  do not need — and should not — invalidate manually; damage propagates
  through the tree automatically. `invalidate()` is reserved for
  out-of-tree changes (glass/off-tree drawing where no widget can report).
- `walk_clear_damage` remains a whole-tree reset primitive; paint does not
  call it.
- **Rasterizer damage hard-clipping convention (A-12/A-13)**:
  `Graphics::set_damage(l,t,r,b)` is half-open (`r`/`b` exclusive),
  `draw_area` is closed; their single point of contact is the private
  predicate `damage_contains(x, y)` (`draw_pixel` uses it directly;
  `fill` clamps by the same boundaries then intersects, degenerating to
  an early exit). `draw_image` delegates per-pixel to `draw_pixel` and
  inherits the clipping. Direct drawing without `clip_safe` is therefore
  also safe in damage mode (`test_raster_damage` pins this).
- Invariant: a node with `subtree_dirty_` true implies all its ancestors
  are true (maintained jointly by bubble-set on the way up and
  post-order recomputation); bubbling may terminate early on that
  invariant (repeated setters are O(1)).
- **Safety precondition of subtree pruning (A-10)**: damage pruning cuts
  whole subtrees by the widget's own bounds; correctness rests on the
  clipping-chain invariant — every widget first `clip_safe`s to its own
  rect before descending (visible contribution ⊆ own bounds ⊆ ancestor
  bounds chain), so pruning cannot hide pixels a full frame would draw
  (`test_dirty`'s overflow probes pin this in both directions). Any new
  feature letting children draw outside the parent's clip region (e.g.
  `overflow: visible`) must re-audit `Widget::draw`'s pruning in the same
  change.

## 10. Theme (batch S1 contract)

### 10.1 Form

`zb::ui::Theme` is a flat token struct — a fixed set of named
`core::Color` values. No stylesheet engine, no nesting, no per-state
tables:

```cpp
struct Theme
{
    core::Color background;    // window base fill (CanvasWindow)
    core::Color text;          // default text
    core::Color text_inverted; // text drawn over `selection`
    core::Color border;        // outlines: button frame, checkbox box,
                               // radio ring, text-input frame, slider track
    core::Color accent;        // interactive fill: pressed button, check
                               // mark, radio dot, slider thumb, caret,
                               // focused border
    core::Color selection;     // selected list-row background
    core::Color field_bg;      // list / text-field background
    core::Color scroll_track;
    core::Color scroll_thumb;
    core::Color focus_mark;    // keyboard-focus indicator rect
    core::Color mask;          // modal overlay (alpha meaningful at 32bpp)
};
```

The token inventory is closed: adding a token is a contract change
(this section first). Framework widgets draw no raw color literals —
every color a widget draws is either a per-widget override or a theme
token.

### 10.2 Scope and lifetime

- One **process-wide active theme**; widgets read it at draw time.
  Storage follows the shared BitmapProvider rationale (ARCHITECTURE
  §6 A-4.1): the active theme is an intentionally leaked singleton,
  created on first use, so a static-lifetime widget can never touch a
  dead theme.
- Built-in presets: `light_theme()` (the default active theme) and
  `dark_theme()`. The light preset reproduces the pre-theme look
  pixel-exactly — the existing pixel-asserting suites lock it.
- `theme()` returns the active theme; `set_theme(const Theme&)`
  replaces it value-wise and bumps the theme generation counter.

### 10.3 Per-widget overrides

- The existing color setters (`set_text_color`,
  `Button::set_pressed_color`, `Button::set_border_color`, ...) keep
  their signatures and now set **overrides**: a set value wins over its
  token; an unset widget follows the active theme. Overrides are stored
  unset-by-default (empty `std::optional`), so a freshly constructed
  tree tracks theme switches with no re-apply step.
- Draw-time lookup is the contract: tokens are read inside the draw
  path, never snapshotted at construction — a `set_theme` after tree
  construction recolors every non-overridden widget on the next frame.

### 10.4 Invalidation

- `set_theme` bumps the generation; `CanvasWindow::paint` compares its
  last-seen generation and, on mismatch, forces a whole-frame repaint
  (damage pruning is bypassed for that frame). Apps do not invalidate
  manually after switching themes.
- The ListBox row-image cache needs no extra invalidation: the cache
  key already contains the fg/bg `Color` values (§8, A-9), so
  recolored rows miss the old entries automatically.

### 10.5 Path classification

- `theme()` reads and the draw-time token lookups are hot paths: zero
  allocation (plain object access). `set_theme` copies a fixed-size
  value type and never throws; it is callable from input handlers.
- The `mask` token's alpha channel is meaningful only at 32bpp; 16bpp
  builds bake an opaque approximation — the same policy the modal
  overlay uses today. Theme colors intended for 16bpp targets must
  stay legible after bgr565 quantization (presentation seam, §3).

### 10.6 Not contracted

Per-window themes, `.ui` design-file theme attributes, hover/disabled
state tokens (widgets have no such states today), and spacing/radius
tokens are explicitly out of scope; introducing any of them amends
this section first.
