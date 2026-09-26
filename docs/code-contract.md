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
| `Font` constructor | removed with the FreeType path (2026-09-06, L-5); init-path font failures now throw `zb::ui::error` from `TtfFamily` (§2.4) | ✓ |
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

`zb::ui::error` is defined in `imcore/include/core/error.hpp`
(`what() const noexcept`, message taken by `const&`); the legacy
same-shaped `Font::error` type left the codebase with the FreeType path
(2026-09-06, L-5).

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
- Provider `measure` / `write` accept `const char16_t*` (UTF-16
  semantics); widget drawing passes the internal buffer directly, no
  copy/convert.

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
  (`utf8_to_utf16` / `utf16_to_utf8`, landed), the same layer as the
  text providers.
- `imcore` does not depend on `imui`; conversion at the widget layer is
  just "decode → append to the u16 buffer".

### 2.4 Dual glyph providers (landed)

The `GlyphProvider` abstraction and the built-in `BitmapProvider` (5x7,
zero dependencies) live in `imcore/include/text/`, next to the
build-time subset provider (§2.4 plan 2) and the runtime TTF provider
(§2.4 L-5). `Widget::set_glyph_provider()` installs the main provider.
Standing contract obligations:

- Fallback chain: main provider reports "glyph not covered" → fallback
  provider → still missing → skip.
- Provider conditional compilation (`IMCORE_HAS_TTF_RUNTIME`; formerly
  the FreeType `USE_FONT`, removed 2026-09-06) is only allowed at the
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
- **Design-file sources (A-7, closed)**: `text="…"` / `items="…"` in
  `.ui` documents are scanned too — `imcore/CMakeLists.txt` globs
  `tools/examples/*.ui` and `apps/*/*.ui`, and `font_subset.py` parses
  the `.ui` `text=`/`items=` attributes — so non-ASCII text in `.ui`
  enters the subset instead of being zero-width-skipped at runtime.
- Builds without Python 3 or without `FONT_SUBSET=ON` fall back to
  ASCII-only: `IMCORE_HAS_SUBSET` undefined, coverage is exactly 32..95,
  behavior identical to pre-batch-E (tests assert both branches under
  `#if defined(IMCORE_HAS_SUBSET)`).
- 5x7 cannot render readable CJK (at least 12x12 needed): CJK goes to
  plan 2 (stb_truetype build-time TTF→bitmap layer converter) and is not
  covered by batch E plan 1.
- Demo-driven coverage (htmldemo dials): `°` `■` `◆` join `é`/`·` in the
  hand-drawn table. `.html` documents are not scan inputs — their
  non-ASCII units ride the runtime table only when a scanned source
  (in practice the tests) uses them too.

**Font subset plan 2 (batch S2)**: a build-time TTF→bitmap
converter rasterizes a TrueType font into a glyph subset table
(`ttf_glyphs.hpp`), compiled only when `IMCORE_HAS_TTF_SUBSET` is
defined. Contract:

- Pipeline: `tools/font_subset.py --codepoints-out` emits the sorted
  list of code units the sources (`apps/`, `test/`, `.ui`
  `text=`/`items=`) actually use; `tools/ttf_subset` (a build-time
  tool over the vendored stb_truetype) rasterizes each listed unit at
  `TTF_PIXEL_SIZE` from the font given in `TTF_FONT` and writes the
  table — sorted by code unit for binary search, per-glyph metrics
  plus 8-bit coverage bytes, line metrics as constants.
- Runtime: compiling the table in provides `TtfSubsetProvider` (a
  stateless `GlyphProvider`) plus `ttf_subset_provider()`, which
  returns the shared instance; widgets opt in through the existing
  `set_glyph_provider` seam — the same selection-point-only
  conditional rule as the runtime provider (§2.4 L-5). Default
  rendering stays 5x7 unless
  a widget installs the provider, and the fallback chain is unchanged
  (units absent from the table fall back to the 5x7 bitmap provider,
  then skip). `make_text_image` stays bitmap-backed.
- Tolerance: a code unit the font does not contain produces a build
  warning and is omitted from the table — it falls back through the
  bitmap chain at runtime, never an error. `TTF_FONT` empty (the
  default) leaves the build identical to pre-S2.
- Rendering: coverage bytes are drawn as the foreground color's alpha
  (per-pixel blend; `write` allocates nothing). At 16bpp the single
  alpha bit degrades anti-aliasing to binary opacity (the §10.5 mask
  policy). Kerning is not applied; advances are the font's own.
- Platform default font (`USE_FONT_SIZE`, default OFF): when ON and
  `TTF_FONT` is empty, CMake resolves a per-platform system font
  (Windows: Segoe UI, Linux: DejaVu Sans, macOS: Arial), rasterizes it
  through the same pipeline at `TTF_PIXEL_SIZE` (default 16), and
  defines `IMCORE_USE_FONT_SIZE`; a missing font file fails the
  configure step. With the macro defined, each desktop shell calls
  `zb::shell::install_platform_font()` before `make_app()`, which runs
  `set_default_glyph_provider(ttf_subset_provider())`; a widget with no
  explicit `set_glyph_provider` uses the process default before the 5x7
  fallback — the chain becomes TTF → bitmap → skip. The battery never
  starts a shell, so it stays on the deterministic 5x7 even in a
  `USE_FONT_SIZE=ON` build; the `[font_size]` suite exercises the seam
  by installing and removing the default provider explicitly. Embedded
  targets never opt in (the TTF pipeline is host-only; 5x7 remains the
  universal fallback).

**Runtime TTF provider (batch L-5)**: with `USE_TTF_RUNTIME` (default
OFF; an optional feature like `USE_PNG`, never forced by any
toolchain), imcore compiles the vendored stb_truetype behind one wrapper
TU and defines `IMCORE_HAS_TTF_RUNTIME` PUBLIC. Contract:

- **The seam does not grow.** `TtfFamily` (init path) loads one TTF and
  hands out one `GlyphProvider` per pixel size through
  `provider_for(px)` (1..128; outside the range throws — the init-path
  rule §1.1). Equal `px` returns the same instance; the widget never
  knows a family exists — per-size providers ride `set_glyph_provider`
  unchanged. The `set_font_size(px)` convenience (below) resolves through
  this seam and adds no new provider interface.
- **Sources**: `TtfFamily::from_file(path)` owns a copy of the font
  bytes; `from_memory(bytes, n)` **borrows** — the caller's buffer must
  outlive the family (the ROM-blob case; build-time packing follows the
  `ui_embed`/`asset_gen` precedent and is condition-triggered like V-4
  until a real embedded use case appears). A font stb_truetype cannot
  parse throws `zb::ui::error` at construction.
- **Ownership (the family is the anchor)**: a provider is *not
  self-sustaining* — it borrows the shared family state (a refcounted
  provider would close a reference cycle with the family's per-size
  memo, leaking both). A provider handed out from a family must not
  outlive that family; every real use keeps the family app-scoped (a
  static font / a value held for the app's lifetime) while widgets hold
  providers for their subtree.
- **Bounded glyph cache (§8)**: entries keyed `(pixel size, code unit)`
  rasterize lazily on first draw; the cache counts bytes and drops
  everything when over budget (the ListBox row-cache rule) — rendered
  pixels never depend on cache state. First-frame rasterization may
  allocate; a warm draw allocates nothing. Observability seams on the
  family: `rasterization_count()` (monotonic cache misses) and
  `cache_bytes()` — the counter is the portable proof across the imcore
  DLL boundary (§8, the ListBox precedent).
- **Fallback chain unchanged (standing rules above)**: code units the
  font lacks (glyph index 0) are "not covered" and fall through to the
  5x7 bitmap provider, then skip. `covers` never allocates; `measure`
  never rasterizes (advances only); `line_metrics` is computed once per
  size (ascent/descent/line height, no linegap — the same formula as
  the build-time subset).

**Per-widget font size (`set_font_size`)**: the runtime family's
multi-size capability (`provider_for`) exposed per widget, so HTML
`font-size` and `.ui` `font_size` have a widget seam to land on:

- `Widget::set_font_size(px)` (init path, 1..128) resolves
  `provider_for(px)` against the widget's family (explicit argument or
  the process font family, see below) and installs it through the
  existing `set_glyph_provider` seam — eager resolve, so the draw path
  is untouched and warm draws stay allocation-free (§8). `px <= 0`
  clears the declaration (the axis/percent convention: 0 = unset) and
  resets the primary provider to the process default; out-of-range
  `px` and a missing family throw `zb::ui::error` on the C++ call
  (§1.1), while the declarative builders (`ui_builder`, `html`) apply
  tolerance instead (warn once and keep the current provider).
- State lives in the heap `ext_` sidecar (`font_px`, 0 = unset; bare
  widgets stay allocation-free and read 0), so the inline Widget size
  gates hold. Like every content setter it clears the `advance_cache_`
  and reports damage + layout invalidation (§7); `measure()` follows
  automatically through the resolved provider.
- Family source, in order: the explicit `TtfFamily` argument when the
  caller has one, else the process font family installed by
  `set_font_family()` (parallel to `set_default_glyph_provider`, empty
  by default). The widget holds a family copy for its declaration, so
  a provider never outlives its anchor (the L-5 ownership rule above).
  Overwriting semantics: `set_font_size` replaces the primary provider
  (a prior explicit `set_glyph_provider` is not restored on clear —
  clear resets to the process default); an explicit
  `set_glyph_provider` after `set_font_size` wins until the next
  `set_font_size` call.
- Text-axis inheritance is a PARSE-TIME pass of the html path, not a
  draw-time behavior: `parse_html` propagates `color`, `font-size`,
  `letter-spacing` and `font-weight` (the CSS-inherited text axis)
  from the declaring element down to the text labels the converter
  synthesizes — a styled container's text renders as a CHILD label, so
  without the pass every container declaration stayed behind it (the
  model500 knob labels rendered at the bitmap fallback). Own
  declarations win; the inherited value stamps text-bearing nodes only
  (containers stay unstamped, so no phantom per-size providers are
  created). `.ui` documents keep the explicit per-widget rule: a `.ui`
  child never inherits its parent's size (each sized widget carries
  its own declaration).
- Degradation (documented, not a bug): builds without
  `IMCORE_HAS_TTF_RUNTIME` ignore the declaration (5x7 has no sizes;
  the single-size build-time subset has exactly one) — same standing
  as the 16bpp AA-to-binary rule. `make_text_image` stays
  bitmap-backed. Per-`svg_text` sizes resolve at draw time: an `svg`
  element's own `font_size` sizes the whole canvas through this seam,
  and an item-level `font-size` on `svg <text>` (viewBox units,
  fractional) scales with the viewBox stretch and resolves against the
  process family per draw (the provider handle is call-scoped; absent
  size or family keeps the seam default / bitmap fallback — the
  bitmap's normalized glyphs render `dB` as `DB`).

**Widget text dressing (P-2a)**: three additive, default-off properties
  on the widget text seam (`draw_text` / `draw_text_at` / `advance_of` /
  `text_advance`):
- `letter-spacing Npx` (`set_letter_spacing`, px; negative clamps to
  0): added to the advance of every covered code unit — trailing unit
  included, per CSS — in both measure and draw. Spacing 0 keeps the
  provider-run path bit-identical (no per-glyph split; splitting is
  safe only because kerning is never applied, §2.4). Center/right
  alignment follows automatically through `text_advance`.
- `font-weight` 600+ / `bold` (`set_bold`): double-strike — the string
  draws twice, the second pass shifted +1px in the same color. No bold
  glyph variant is synthesized or required (5x7 stays single-weight).
- `text-shadow: dx dy [blur] color` (`set_text_shadow`): one solid
  offset copy of the whole string (spacing and double-strike included)
  drawn first in the shadow color; the blur radius is
  parsed-and-ignored (no blur primitive); a comma list keeps the first
  shadow only. SVG `text` rides the same seam.
- **Selection-point-only conditional compilation**: stb_truetype is
  never a hard imcore/imui dependency — with the option off no new code
  compiles and the 5x7/build-time subset paths are untouched. Default
  rendering stays 5x7 unless a provider is installed. `make_text_image`
  stays bitmap-backed.
- **The build-time path stays first-class by construction**: fonts are
  reached only through `GlyphProvider` and widgets never know which
  provider is behind the seam — `BitmapProvider` (5x7, zero dependency)
  and `TtfSubsetProvider` (build-time table, zero runtime cost) remain
  the defaults for constrained targets that cannot ship a TTF at all.

**Wrapped text (H-1)**: multi-line text on the widget text seam, the
paragraph capability behind the html `p` element (html-path.md §Text
wrapping):

- API: `Widget::set_text_wrap(bool)` (default off) and
  `Widget::set_line_height(int px)` (0 = unset → the provider's
  line metrics). Both live in the `ext_` sidecar (`wrap_enabled`,
  `line_h`), so bare widgets stay allocation-free and the inline
  size gates hold. `set_line_height` clamps negatives to 0 and
  reports damage + layout invalidation like every text setter.
- The engine is greedy word wrapping over the stored u16 text: break
  candidates at spaces (U+0020), `\n` is a hard break (the html `br`
  inside a wrapping element), no hyphenation; a word wider than the
  whole width sits alone on its line and clips. Pitch =
  `line_height` when declared, else the provider `line_metrics`
  height; block height = `(lines-1) × pitch + line height`.
- Wrap width = the widget's current `size.width` (the assigned box:
  an explicit size, a resolved percent, or the flex-assigned
  extent). Zero width keeps the single-line demand — a wrap-enabled
  label measures its max-content advance until a layout assigns a
  box, then the convergent layout (§7, H-9) re-reads the now
  multi-line height one round later. `Label::measure()` is
  wrap-aware; other widgets draw wrapped (the seam is shared) but
  keep their own measure — the declarative path only puts wrap on
  `p`→`label`.
- Line spans cache in the sidecar as `(offset, length)` pairs into
  `text_` (no string copies), keyed by the wrap width and the text
  state: every setter that clears `advance_cache_` also marks the
  span cache stale (a shared `reset_wrap_cache()` beside those
  sites), and a changed assigned width re-keys it lazily at use
  time. A settled tree draws and measures allocation-free; the
  recompute allocates only when the width actually changed (the
  settling layout rounds), never per warm frame.
- `draw_text` renders the span block: per-line `h_align` within the
  box, whole-block `v_align` (first baseline at ascent, block height
  as above), dressing (letter-spacing, double-strike bold,
  text-shadow) applied per line through `draw_text_at`. SVG `text`
  stays single-line by design.

---

## 3. Other API-shape rules

Architecture-level background (POD input events, no-RTTI traversal, C-ABI
host rules, render loop) is in `docs/ARCHITECTURE.md` §4; this section
keeps only API-level supplements.

- Parameter passing: scalar parameters (integers, `bool`, floating point,
  enums) are passed **by value**; `const T&` is reserved for class types.
  (2026-08-28 sweep removed all scalar pass-by-const-ref signatures across
  the public API.)
- **Pixel model (A-19)**: `core::Color` is `basic_color<Traits>` — a
  fixed-size pixel word (`pixel`) plus channel accessors
  (`r()/g()/b()/a()`, `set_r()/set_g()/set_b()/set_a()`). Accessors are
  8-bit normalized: 16bpp 5-bit channels read expanded (`bits << 3`,
  0..248) and write truncated (`v >> 3`); the single alpha bit reads
  0/1 and writes `v > 0`; 32bpp channels are plain bytes. `from(r,g,b,a)`
  stays the only construction entry. The traits (bit depth, channel
  placement, `per_channel_blend`) are selected at compile time from the
  build options (ARCHITECTURE §4.4); one instantiation per build, no
  runtime dispatch. Code never reaches into the pixel word for channel
  work — the accessors are the API.
- Close-notification contract: host responsibility and reentry ban are in
  ARCHITECTURE.md §4.8; API-level supplement — wasm hosts register the
  callback via `addFunction` (build needs `ALLOW_TABLE_GROWTH=1`).

### 3.1 V-5 composition widgets (ToggleSwitch / GaugeDial / Knob / TrendLine)

All four are ordinary `Widget` subclasses built from the rasterizer
primitives (V-1 + `draw_arc_aa`), theme-token driven, with no animation
system (standing non-goals):

- **Color source (contract 10.3)**: every color a widget draws is the
  active `theme()` token read at draw time (accent / border / field_bg /
  text), each overridable per-widget through a setter that marks dirty
  only when the override actually changes.
- **Display-only (GaugeDial, TrendLine)**: not focusable, consumes no
  input, fires no events, `measure()` returns a fixed natural size
  (the ProgressBar rule). `set_value()` clamps to the range and marks
  dirty only on an actual change — a steadily fed gauge never repaints.
- **Interactive (ToggleSwitch, Knob)**: focusable; the `changed` event
  fires on user interaction only, programmatic setters are silent (the
  Slider/Checkbox rule). Pointer press/release follows the Checkbox state
  machine (ToggleSwitch: toggle on in-area release, cancel on drag-away;
  activation toggles immediately). Knob takes the Slider keyboard/wheel
  path (up/down step, wheel step) plus a vertical drag (drag up = value
  up), capturing the pointer while held.
- **Circular `hit()` (GaugeDial, Knob)**, the first in-tree A-24
  overriding consumers: `hit(x, y)` is the widget-local disc of radius
  `min(w, h)/2 - 1` (matches the drawn face), reproducing the base
  `is_visible()` gate. The override is allocation-free and symmetric with
  `draw_at()`'s geometry (§4.3); damage recovery still uses the
  rectangular bounds.
- **Angle math**: these widgets position geometry (ticks, needles,
  pointers) through the public `core::point_on_circle` helper — the same
  integer-degree + two-trig-path convention as `draw_arc_aa`
  (0° = +x, positive CCW, visually CW on y-down screens); GaugeDial's
  default arc runs −225° → +45° (270° sweep, gap in the lower half).
- Integer math only; both color depths degrade through the existing
  quantization rules.

### 3.2 SVG path strokes (H-6 first cut)

`SvgCanvas` gains a stroke-only path item beside its lines/texts
(html-path.md §SVG subset is the dialect contract; the whitelist is the
boundary):

- C++ shape: `SvgCanvas::Path` — a pre-flattened polyline in viewBox
  units (`std::vector<std::pair<double, double>>` points, `closed`,
  stroke `color`/`width`/`round_caps`). The canvas strokes polylines;
  it never parses `d` (the html converter owns command parsing,
  curve flattening and subpath splitting; `d` parsing is not a
  widget concern and stays out of the canvas).
- Rendering: the same device-space stroke geometry as `line` —
  per-pixel 2x2 supersampled capsule coverage over the
  integer-Q10 polyline (per-point doubles map once, per-pixel math
  stays fixed point), stroke width scaled by the viewBox geometric
  mean, per-sample distance measured against the whole polyline so
  joints never double-blend, `round_caps` adds end discs per subpath,
  `Z` appends the closing segment. Coverage quantizes through the
  standing color-depth rules (per-channel alpha scaling at 32bpp,
  plot/skip at half at 16bpp). Allocation-free per draw (no
  coverage buffers).
- Draw order in `draw_at`: lines, then paths, then texts — document
  order across kinds is not kept (documented deviation; the
  declarative subset never interleaves them).
- Determinism: parse-time flattening is plain IEEE double with a
  fixed chord tolerance and depth cap, so desktop/WASM/NDS flatten
  identically; the draw keeps the no-FPU-per-pixel rule.

### 3.3 Character event contract

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
  content-derived sizes; ProgressBar overrides with a fixed intrinsic
  100×12). `set_size` sets the explicit flag; the layout
  layer (FlexPanel) applies measure() only to non-explicit children;
  `set_size_auto` is for layouts to write sizes back and clear explicit —
  app code does not call it directly. Explicit sizes always win over
  measure().
- **FlexPanel intrinsic measure (S3)**: `FlexPanel::measure()` overrides
  with a content-derived size so auto-sized (nested) containers are
  hittable and drawable: main axis = sum of the items' main-axis demands
  plus spacing between them, cross axis = the largest cross-axis demand,
  both plus `2*padding`. The demands follow the layout rules exactly —
  an explicit axis uses the current size, otherwise the child's
  `measure()`, and flex items contribute 0 on the main axis — so a
  measured container lays out identically to the size its parent assigns
  from that measure. The computation is allocation-free (the layout path
  calls it on every demand pass).
- **Explicitness is tracked per axis**: `size_explicit_w_/h_` are
  independent; `is_size_explicit()` = either axis (compatibility
  semantics), `is_width_explicit()/is_height_explicit()` query per axis.
  FlexPanel share assignment and size write-back use
  `set_width_auto()/set_height_auto()` to clear **only that axis's flag** —
  an explicit cross-axis size keeps its value and flag when the main axis
  grows.
- **Percentage sizes (batch L-4)**: a widget axis may declare a
  percentage of its **FlexPanel parent's content box** —
  `set_width_percent(pct)` / `set_height_percent(pct)` (1..100; values
  clamp into that range, 0 clears the declaration), queried by
  `is_width_percent()/width_percent()` and the height pair. The
  declaration replaces explicitness on that axis (the axis stops being
  explicit), and `set_size` clears both declarations — the last geometry
  setter on an axis wins. The FlexPanel parent resolves the declaration
  during its `layout()` and writes the result through the per-axis auto
  setters, so the axis never becomes explicit and every re-layout
  re-resolves it (resizing the parent resizes the child; resolution is
  top-down — a nested FlexPanel's own percent size is written before its
  `layout()` runs, so inner percentages resolve against the fresh size).
  Main axis: fixed siblings (explicit axes and measured natural sizes)
  claim their demand first; each percent sibling desires
  `pct * content_main / 100` (integer floor), and when the desires
  overflow the line's remaining space they are scaled down
  proportionally to their percentages — the last percent sibling takes
  the leftover pixel so a scaled line sums exactly to the remainder (the
  same exact-sum rule as the flex-grow distribution). `flex_grow` and
  percentage are per-child mutually exclusive: the percentage wins and
  the grow weight is ignored. Cross axis: resolves against the
  content-box cross size, no sibling interaction. `measure()`: a percent
  child contributes 0 on its percent axis (main like a flex item, cross
  as 0), so an auto-sized container ignores percent children until a
  real parent size resolves them. Outside a FlexPanel (Panel children,
  the tree root) a percent declaration stays unresolved: the axis keeps
  whatever size it has (a fresh widget's 0). With wrap, percent children
  participate in line breaking with their desired size and the
  scale-down applies per line. Integer math only.
- **FlexPanel wrapping and cross-axis semantics (batch K/N2, N3)**: the
  wrap test accumulates only non-flex children's main-axis demand; a flex
  child contributes 0 to wrapping (it absorbs in-row leftover space, its
  share clamps to ≥ 0, so it never overflows). Children with a
  non-explicit cross axis (including flex children) always take their
  `measure()` content size — framework text does not wrap, so content
  height is unaffected by main-axis assignment.
- **FlexPanel main-axis justification (H-7a)**: `FlexPanel::justify`
  (`start` default, `center`, `end`, `space_between`, `space_around`)
  distributes each line's free space — `F = content_main − (Σ final main
  sizes + spacing·(n−1))`, `spacing` acting as the minimum gap (CSS `gap`
  analogy) — as positions only; sizes, `measure()`, and line breaking
  are untouched. `start` keeps the historical padding origin; `end`
  leads by `F`, `center` by `F/2` (floor); `space_between` puts item k at
  `padding + sizes_before + k·F/(n−1)` (`n == 1` behaves as `start`);
  `space_around` puts item k at `padding + sizes_before + (2k+1)·F/(2n)`
  (half gap at both ends). `F ≤ 0` falls back to `start` placement
  (overflow clips at the far edge, never overlaps backwards). Each
  wrapped line justifies independently; abs children resolve in
  `resolve_abs`, never here. The forms are closed over settled sizes, so
  the H-9 convergence loop sees no drift. HTML `justify-content` maps
  `flex-start/start/left → start`, `flex-end/end/right → end`,
  `center → center`, `space-between → space_between`,
  `space-around → space_around`; unknown values warn once and keep the
  current value. `.ui`/builder flows set the same parameter through the
  `justify` node prop (integer code).
- **FlexPanel cross-axis alignment (H-7b)**: `align_items` (`start`
  default preserving the historical line-top packing, `center`, `end`,
  `stretch`) plus a per-item `align_self` (`auto` = inherit the
  container, else one of the four). The effective alignment resolves per
  line; offsets sit inside the line's cross extent (`line_cross`, the
  max child cross size): `start → 0`, `center → (line_cross−c)/2`
  (floor), `end → line_cross−c`,   `stretch → 0` after growing. `stretch`
  writes the child's cross size up to `line_cross` only when that axis
  is auto (measured) — an explicit size or a percent declaration keeps
  its value and the item behaves as `start` (CSS non-auto rule). A
  single line fills the content cross box (`line_cross` takes
  `max(line_cross, avail_cross)`): center/end then place within the
  real extent (viewport centering works) and stretch fills the
  container, not just the sibling max — multi-line stacks keep packing
  from the padding origin. The fill is stable under H-9 (stretch never
  feeds a demand, so an auto-cross pass settles at its demand size). The
  stretch write is additionally **capped at the container content box**
  (H-9e): a child whose natural demand overflows the container — a
  wrapping paragraph's single-line demand is the recorded case — must
  not inflate the line past the container, and a wrapping
  (`text_wrap`) stretch child resolves its stretch width to the
  container content box directly (html-path §Text wrapping). Sizes
  on the main axis, `measure()` (demands, not positions), and line
  breaking are untouched; the stretch write is closed over settled
  demands so the H-9 loop sees no drift. HTML `align-items` maps
  `flex-start/start → start`, `center → center`, `flex-end/end → end`,
  `stretch → stretch` onto the container prop; `align-self` maps the
  same plus `auto` onto the per-item hint (honored only under a flex
  parent, ignored elsewhere like `flex_grow`); `baseline` and anything
  else warn once and keep the current value. An HTML container with no
  `align-items` emits `stretch` (the CSS default, html-path.md) so
  sections fill the content width; `.ui`/builder flows use the `align`
  node prop (container, integer code) and the `align_self` node hint
  (item), keeping the `start` default there.
- **FlexPanel basis & shrink (H-7c)**: a per-item `flex-basis` (auto =
  the historical demand, explicit px, or % of the content-box main
  size) replaces the item's demand as its line claim — for packing,
  wrap breaking, and the grow base — while `flex-shrink` (default 0,
  preserving the historical overflow-clips behavior) weights deficit
  sharing. Final main sizes resolve per line from
  `free = content_main − Σclaims − gaps`: `free ≥ 0` distributes the
  surplus to grow items over their claims (the historical exact-sum
  rule, last grower takes the remainder); `free < 0` distributes the
  deficit by the scaled factors `shrink × claim` (last participant
  takes the remainder, every final floors at 0), and with no shrink
  weight anywhere the line keeps its claims and overflows as before. An
  explicit basis beats an explicit size, a percent declaration, and the
  grow-zero claim alike; `measure()` counts a px basis as demand and a
  % basis as 0 (relative, like a percent child). HTML `flex:` takes one
  (`grow`), two (`grow shrink`), or three (`grow shrink basis`) tokens
  plus `none` (= `0 0 auto`); the historical single-number form keeps
  basis-auto (not CSS's `0%`) and shrink-0 (not CSS's `1`) — additive,
  documented deviations. `flex-basis: auto/px/%` and `flex-shrink: N`
  set the same channels alone; negative or malformed values warn once
  and keep the current values. Min/max constraints stay unscheduled
  (H-7d): nothing on the gating page uses them. Viewport units
  (  `min-height:100vh`) stay out too — but the viewport-centering
  wrapper now grounds: a flex `body` is kept as the document root
  (html-path.md), so its container properties and box dress land on
  the build host while the content width constrains the child.
- **Widget margins (H-3)**: per-side non-negative margins (top/right/
  bottom/left) live in the heap `ext_` sidecar (`has_margin`; bare
  widgets stay allocation-free and read 0). In-flow FlexPanel/Panel
  geometry honors them: a main-axis pitch is margin-before + size +
  margin-after (claims, wrap breaks, surplus/deficit sums and
  justification all count them; they add to `spacing`, never collapse);
  the cross line extent takes the max margin box and stretch fills it
  minus the item's own cross margins; `measure()` includes them so
  shrink-fit parents fit margined children. Margins never shrink (the
  deficit shares only content claims) and absolutely positioned
  children ignore them. Root-node margins are dropped (the host is
  viewport-sized). HTML `margin` takes 1–4 `Npx`/bare values with the
  CSS side mapping (1 = all, 2 = vertical/horizontal, 3 = top/
  horizontal/bottom, 4 = top/right/bottom/left) plus `margin-top` (and
  -right/-bottom/-left) longhands that win over it; negative, `auto`
  and malformed values warn once and keep 0. `.ui`/builder flows use
  the `margin_t`/`margin_r`/`margin_b`/`margin_l` node props.
- Keyboard constraints: with a modal open, keyboard focus and
  Tab/arrow navigation are confined to the modal subtree (`focus_next`
  scopes to the modal); if the focused widget lies outside the modal
  (focused before it opened) or has become invisible
  (`is_effectively_visible()` = self and all ancestors visible), focus is
  released before the next key. When the pressed target is hidden
  mid-press, pointer events deliver `on_cancel` first, then clear the
  press; later move/release events are not delivered.
- **Pointer-supplied lifecycle (A-24 contract, no interface change)**:
  `Widget::on_input()` is the **sole** event entry; `hit(x, y)` is the
  point test. The dispatcher's pointer rules an override author must know:
  - `hit()` is **widget-local** (parent `pick()` already translated) and
    the virtual's default is `visible &&` point-in-rect; an override
    replaces the point test and must check `visible` itself. It is on the
    hot path — allocation-free and cheap.
  - `on_input()` receives **absolute/client-space** coordinates (`ev.x/y`;
    subtract `get_absolute_position()` for local geometry, as Slider
    does). Return **true** = consumed/state changed (frame repaints, a
    press forms the pressed-target lock and may grab focus);
    **false** = unconsumed (a press that returns false is never claimed).
  - **Drag semantics need `captures_pointer()` = true**: while a capture
    press is held, every move is delivered regardless of where the pointer
    is, with no drift cancel. A **non-capturing** pressed widget is never
    fed moves; a move that is still on the widget **or inside its subtree**
    (the deepest pick returns the child, but a container that consumed the
    press owns its children's area) — or off both but within the slop —
    resets the drift counter. A move off the widget's whole area and
    beyond the slop cancels the press (mouse 8 px immediate; touch two
    **consecutive** off-target strikes — any intervening on-target or
    within-slop sample resets the count, so one glitch sample must not eat
    a click).
  - Release always reaches the held `pressed_target` even after the
    pointer leaves the widget (commit-on-release widgets rely on this);
    a new press while one is held cancels the old first (`on_cancel`).
  - Overridden geometry (`hit`) and `draw_at` must be symmetric; damage
    stays rectangle-based (bounding box is the conservative choice).
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
- **Presentation scaling (I-2a, `shell/presentation.hpp`)**: the app
  buffer stays fixed-size forever — desktop shells are what resize. The
  window opens at buffer size (1:1, so captures are unchanged) and the
  user may resize it; the shell presents the buffer fitted into the
  client area, aspect preserved and centered (black letterbox), through
  the shared integer seam. `presentation_fit(win_w, win_h, buf_w,
  buf_h)` computes the presented rect (cross-multiplied orientation
  test, floor division, centered; degenerate sizes give a zero rect).
  `presentation::to_buffer(wx, wy, ...)` is its exact inverse — the same
  integer floor formula as the forward stretch
  (`buf = (win - dest) * buf / dest`, no floats) — and returns false
  for a point on the letterbox, which every desktop shell swallows
  (a pointer event outside the presented rect is not app input).
  `presentation_region(...)` maps a buffer-space dirty region to the
  conservative dest-space rect (ceiled edges), so the
  `region_to_present` protocol composes with the stretch. Resampling is
  nearest-neighbor on every platform (win `StretchDIBits` +
  `COLORONCOLOR`, mac `kCGInterpolationNone`, x11 the shared manual
  resample loop over a dest-sized scratch): at integer scale factors
  the platforms are pixel-identical, which is what keeps native
  captures reproducible — the GIF md5 capture path is shell-less and
  unaffected. NDS/FB shells stay 1:1; wasm/python hosts scale
  host-side. The scaling algorithm is user-facing documented (README
  "Window & presentation"); the seam is locked by `test_shell_presenter`.
- **Device overlay (I-2b, `shell/device_overlay.hpp`)**: bezel/chrome
  around the presented buffer from target screen constraints, as pure
  integer layout math beside the I-2a seam — `chrome_around(p, win)`
  bars (a degenerate present yields one full-window bar), `hinge_bar`
  reusing the `presentation_region` ceil mapping for buffer rows shown
  as chrome (NDS `nds_screen_w/h` constants), `contains_rect`
  half-open like `to_buffer`. Shells paint the bars natively and
  swallow pointer events on chrome/hinge (`to_buffer` success AND NOT
  contained); no shell is rewired here — per-shell adoption needs
  maintainer eyes on the verified present paths. Single-buffer rule: a
  hinge covers rows of the presented buffer itself. Locked by the I-2b
  section of `test_shell_presenter`.
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
- `IWindow::title()` is the single source of the window title: every
  windowing shell (win/x11/mac) reads it once at window creation, and
  non-windowing targets (NDS, linux-fb, wasm) never read it. The
  `CanvasWindow` default is "myapp"; apps override it with
  `set_title()` (the app code sets it before the shell reads the
  title, i.e. inside `create_window`/`make_window`).

## 4. Declarative UI builder (batch G contract)

- `ui_node` is the sole entry for static descriptions:
  type/id/ordered props/children/items/flex_grow. The fluent builder
  (column/row/panel/label/button/checkbox/radio/slider/progress_bar/
  list_box/text_input
  + .size/.pos/.text/.named/.checked/.group/.step/.value/.rows/.spacing/
  .padding/.wrap/.flex/.visible/.width_pct/.height_pct/.background/.color)
  and the design-file deserializers
  (G6) share one intermediate representation — the props produced by
  either must be consumable by the same property-resolution table.
- Two parsing front-ends feed that representation: `parse_ui_text`
  (the `.ui` text format, batch G) and `parse_html` (the HTML/CSS
  subset, batch H; grammar and whitelist in `docs/html-path.md`). Both
  are init-path only, never the frame path. The tag/property table in
  ui_builder.cpp is the single mapping for both — an HTML-what-in-css
  property set lands on the same table rows.
- Shared color resolution: `parse_color` (declared in `html.hpp` so
  `ui_builder.hpp` stays light for the ui_embed host tool) resolves
  `#rgb` / `#rrggbb` / the named subset / `rgb()` / `rgba()` /
  `transparent` (names case-insensitive; comma form only, alpha 0..1;
  false = malformed, transparent, or absent) for the background/color
  props and the HTML page box alike.
- Widget paint dressing (P-1): `set_background_linear(from, to,
  horizontal)`, `set_background_radial(cx_pct, cy_pct, from, from_pos,
  to, to_pos)` (circle, farthest-corner radius, stop offsets 0..100),
  `set_border(width_px, color)`, `set_corner_radius(px)` /
  `set_corner_radius_half()`. Draw order in `draw_background`: solid,
  then gradient, then image, border on top; a gradient never clears the
  solid (builder sets exactly one form). Paint precedence when several
  are set: radial > linear > solid. Radius clamps to half the smaller
  side; `50%` resolves at draw time. A background whose color carries
  alpha < 255 paints with alpha enabled around the fill (opaque path
  unchanged). An opaque border on a rounded box paints as a filled
  frame with the face inside it — box inset by the border on every
  side, radius minus the border, the border-box shape; translucent
  borders keep the face out to the outer arc with the band stroked
  over it, square corners stay stroked. Opacity is the depth's own
  semantics (an 8-bit weight at 32bpp, the single alpha bit at 16bpp):
  16bpp AA coverage quantizes to plot/skip at half (the `plot_aa`
  rule), so blended fringes resolve to their solid core.
  Wireframe degrades every form to an outline (S-1).
- Extended gradients (P-2b/c): one heap sidecar (`Widget::ext_`,
  flagged sections for positioning, extended gradients, and text
  dressing — a bare widget keeps it null, so the zero-alloc ctor and
  the 64-bit/32-bit size gates hold) carries forms the 24B
  `paint_dress` cannot: `set_background_conic(from_deg, stops)` — up
  to 4 `{deg, color}` stops, center fixed at 50%/50%, segment colors
  lerped straight in 8-bit RGB. An extended form overrides every
  `paint_dress` gradient (the builder still sets exactly one form
  overall). Angle math is dual-path like the V-5 arcs: integer
  octant-fold + 1°-LUT binary search under `USE_INTEGER_GEOMETRY` (no
  libm), `atan2` otherwise; the two agree within 1° (tests stay off
  exact stop boundaries).   `has_background()` covers the sidecar.
- Three-stop linear + repeating overlay (P-2c): exactly 3 linear stops
  with a `%`/bare middle emit the ends (compat) plus a mid section in
  the sidecar (`set_background_linear3`, full from/mid/to + mid `%`;
  bare mid = 50; a px-positioned mid falls back to ends-only).
  N-stop linear (4–8 stops, series7 case metal): the full stop list
  rides the sidecar (`set_background_linearN`, kind 6, positions in
  `%`, non-decreasing after clamping; absent positions distribute
  evenly between their specified neighbors, ends defaulting 0/100)
  and paints through `fill_linear_stops` (segment lerp in 8-bit RGB,
  the `fill_gradient` corner-AA row layout generalized); 2 stops stay
  the dress base, >8 stops stay ends-only (P-1 rule). `set_background_repeating` (up to 6
  px-positioned stops incl. double-position `C A B` pairs, period =
  last stop, horizontal/vertical by the P-1 angle rule) paints a
  translucent texture OVER any base (solid/dress/extended) — the topmost
  repeating layer wins; one extended-replace form (conic/lin3) per
  widget, the overlay section is independent. On binary-alpha depths
  every stop follows the standard binary rule (any nonzero authored
  alpha sets the bit and plots — a 4% brushed sheen reads as pinstripes
  there, same as any other translucent paint); desktop blends truly.
- Element opacity + top border (P-2d): `opacity: N (0..1)` / `N%`
  parses to fixed-point 0..1000 (`elem_opacity`, integer-only parse,
  malformed drops) and the builder folds it into the widget's OWN paint
  alphas at build time — solid background, every gradient stop of every
  form, border (+ top), text and text-shadow (`(a * op + 999) / 1000`,
  rounded up so any nonzero stays nonzero — the `set_a(v > 0)` binary
  rule, which also keeps a dimmed LED painted (solid) on 16bpp). There
  is no subtree compositing (the architecture has no offscreen buffer):
  opacity on a container affects only its own box/text paint, never its
  descendants. `border-top: Npx solid <color>` rides its own sidecar
  section and paints a full-width top band over the background
  (  radius corners are not cut); other sides stay off-whitelist.
- Box shadows (P-2e): `box-shadow: [inset] ox oy [blur [spread]]
  color` comma lists (paren-aware split; up to 2 outer + 2 inset per
  widget, extras warn-and-drop at parse; a malformed entry drops
  alone). Inset shadows composite the blurred exterior of a shifted,
  spread-contracted rounded hole, clipped to the rounded padding box,
  after the border. Both offset magnitude and direction apply; a centered
  shadow uses the same algorithm as a directional one. The hole and clip
  use the fill raster's inclusive bounds, radius clamp and continuous
  quarter-pixel row chords (the same arc-center geometry as the fills —
  the inset reaches the tangent rows it used to lose); positive spread
  contracts the hole, including its corner radius.
  Blur is the CSS box-shadow Gaussian: sigma = blur/2, per-axis weights
  exp(-d^2/(2 sigma^2)) sampled at integer offsets, support
  ceil(3 sigma) = ceil(1.5 blur), renormalized over the truncated window
  (mass-conserving). Weights come from integer exp-by-squaring in fixed
  point — no float, no runtime table — so every target computes
  identical values. The mask is the hole box's exact per-pixel area
  coverage (`Graphics::rounded_overlap255`); the filter runs as two
  separable prefix-sum passes (horizontal into an int64 line field,
  vertical through a per-column prefix) with a single final
  normalization. Zero blur degenerates to the unfiltered mask. Each
  pixel composites once per shadow;
  32bpp multiplies shadow alpha by coverage, and binary depths use the
  standard half-coverage rule. Scratch storage is bounded independently of
  widget dimensions; no heap allocation or offscreen color buffer is needed.
  Outer shadows paint their silhouette (spread-expanded, offset) UNDER
  the background, blurred with the SAME Gaussian as the inset (the
  silhouette mask is `Graphics::rounded_overlap255` area coverage, so
  the spill keeps the real corner arcs): a blurred edge reads half
  strength at the silhouette contour and decays smoothly, reaching ~0
  at 1.5 blur (the triangular kernel this replaced died at 1 blur with
  a linear slope — the shadow read harder than the browser's). Scratch
  is a uint8 mask plus an int64 line field over mask ± 3 sigma,
  retained and grown to the high-water mark (no per-frame allocation).
  blur==0 keeps the hard
  silhouette; binary depths threshold the coverage at half (the standard
  `plot_aa` rule, like the inset). Outer shadows paint in a dedicated
  first pass under a clip expanded by offset+spread+ceil(1.5 blur) in
  every direction — the filter's true support, not blur, so the tail
  between blur and 1.5 blur survives — bounded by the SURFACE, not by
  the parent's clip: a browser box-shadow overdraws everything already
  painted and is covered only by later paint, so bounding the pass by
  the parent clip amputates the spill for shrink-wrapped parents (a
  column exactly as wide as its knob) — the model500 knob lost its side
  spill and its corners filled as hard-cut rectangle gradients. The
  silhouette field composites EXACTLY ONCE per pixel: the dress phase
  (face, border, insets, under the widget's box clip) does NOT repaint
  it — the first pass already laid the field under the face, so a
  translucent face shows the shadow through at single coverage.
  Repainting the silhouette in the dress phase double-composited the
  box interior the rounded face leaves uncovered (the corner windows
  between a 50% face and its box): the model500 knob's bottom corners
  read ~2x too dark — dark wedges flush with the bottom tangent while
  the spill below the box stayed correct. Paint order still matches the
  browser: the shadow pass runs per widget in tree order, so later
  siblings' backgrounds cover earlier siblings' spill, and outer
  glows/drop shadows on opaque boxes reach past their box anywhere
  within the surface (the model500 knob drop). Damage reports the
  expanded bounds (the same offset+spread+1.5-blur margin) so partial
  repaints cover the shadow. The expanded bounds may exceed the surface
  (an edge widget's glow spills past it); shells intersect the reported
  region with the buffer before blitting. Wireframe skips shadows.
  `Graphics::corner_chord` (the fill chord formula, integer-only) is
  public so the widget-level shadow span masks reuse it;
  `Graphics::clip_surface_safe` is the
  surface-bounded clip the shadow pass escapes nested box clips
  through; `Graphics::inscribed_radius` is the single corner-radius
  clamp (pixel-count semantics: 50% of a 54px circle is 27) that every
  rounded primitive and span helper goes through.
- HTML page box: `html_page` (per-axis width/height/background presence)
  is filled by `parse_html(text, ok, page)`; the consumer resolves the
  initial screen size document → shell → app default (warn on default)
  before materializing. Init-path only, like the parse itself.
- `build(host, root)`: the root node itself is the document (the host is
  the real container; the root tag does not instantiate a widget); the
  root configures the host by PRESENCE: spacing/padding/wrap transfer to
  a FlexPanel host (spacing/padding to a Panel host) only when the root
  actually declares them, a row/column root transfers its direction by
  its tag and justify/align only when declared — pre-configured host
  state survives for everything the root omits (the same presence rule
  as the geometry props and the child margins below). Root's children
  are materialized into the host one
  by one. The root's own box dress — solid/gradient background, border,
  radius, text color — styles the host (a styled document root paints
  its frame on the host); geometry never transfers (width/height,
  position, visibility, text, flex-grow stay host-owned — a fixed host
  buffer always wins, §H-8).
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
  (setting explicit); an absent axis defaults to 0. A `width`/`height`
  prop may also be a percent string `N%` (1..100): that axis is declared
  as a percentage of the FlexPanel parent's content box instead of
  explicit pixels (§3 percentage sizes; the resolution algorithm lives
  there — this section only fixes the prop form). Mixed declarations
  apply the integer axes through `set_size` first, then the percent
  declarations (which clear their axis's explicitness). The fluent
  helpers `.width_pct(pct)` / `.height_pct(pct)` produce the same string
  form, so both feeders share one representation.
  Corresponding parse
  side: `id=` accepts an unquoted integer literal and stores its decimal
  string (batch K/N9; grammar defined in `docs/design-file.md`).
- Contract boundary (must not enter the description layer): dynamic models
  (ListBox's ItemText function pointer), event subscriptions (Event<>
  wiring), font/glyph content (font bytes, family names — the `font_size`
  metric is allowed, §2.4), runtime-generated text. The description
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
  setters (set_size, set_size_auto, set_text, set_glyph_provider, set_font_size; container add_child/remove_child,
  set_orientation/set_spacing/set_padding/set_padding_sides)
  call `mark_layout_dirty()`
  which bubbles to the root along the parent chain (zero allocation). A
  fresh tree starts dirty (`layout_dirty_ = true`). Layout clears the
  flag when done, so a given state triggers at most one layout.
- **Auto-layout is a gated, host-opt-in behavior**
  (`CanvasWindow::set_auto_layout(true)`, default off): when on,
  `paint()` runs `if (root_->is_layout_dirty()) root_->layout();` before
  the damage walk. When off, current behavior (explicit
  set_position/set_size preserved verbatim, manual layout idempotent) is
  unchanged. UI-description hosts (ui_preview) must opt in explicitly.
  The preview host is also the font-family install point for the html
  path (contract 2.4): under `USE_TTF_RUNTIME` it installs the vendored
  Inter as the process family before building screens
  (`IM_PREVIEW_TTF_FONT` build default, `UI_PREVIEW_FONT` runtime
  override; load failure degrades to the 5x7 bitmap with a warning),
  which is what makes per-widget `font_size` resolve at all.
- **The in-paint order (layout → damage → draw) is defined
  architecturally in ARCHITECTURE.md §4.1**; API obligation: mark_dirty
  calls triggered inside layout must be picked up by the subsequent
  damage walk, so geometry and drawing agree within the frame.
- Invalidation propagation only sets the layout flag and does not
  additionally mark render dirty (geometry changes produce damage
  naturally when layout writes sizes back through its setters).
- **Absolute positioning (P-3)**: `position: relative` lays out in flow
  and establishes the containing block for abs descendants (its own
  offsets are ignored); `position: absolute` takes the child out of flow.
  The containing block is the nearest ancestor with relative/absolute
  position, else the direct parent's content box (narrowing). A FlexPanel
  skips abs children in measure/packing/lines (zero demand, no spacing)
  and resolves them after normal layout against the anchor content box
  (`max(0, size - padding sums per side)`, the same base as L-4
  percents): width =
  left+right+auto → stretch, else declared (explicit/percent), else
  `measure()` fallback; x = left ?? right-computed ?? padding origin
  (y likewise); `translate` shifts after (percent of self). Resolve
  writes through the auto setters but restores a declared axis's
  explicitness, so H-9 re-passes keep reading the declaration (an
  emptied abs box such as `.knob-dot` would otherwise collapse to
  its zero demand on the second pass). Paint order
  is document order   (no z-index); the anchor does not clip
  (`overflow` unsupported); abs inside a Panel lays out as normal
  (FlexPanel-only feature). Storage is a heap side struct only for
  positioned widgets (init-path alloc; bare-widget ctor stays
  zero-alloc): +8 bytes 64-bit / +4 NDS per Widget. Anchor lookup is
  `Widget::positioned_ancestor()` (nearest positioned ancestor or
  null); offset/translate readers serve the FlexPanel resolver, anchor
  content boxes read through `content_inset()` (padding, else 0 — the
  uniform value; FlexPanel per-side padding resolves its own abs box
  from `pad_t/r/b/l` directly). FlexPanel padding is per-side
  (`set_padding_sides(t, r, b, l)`; `set_padding` writes all four — the
  `.ui`/programmatic uniform shape): measure, available space, packing
  origin, and percent/abs containing boxes use the axis-appropriate
  side sums.
- **Convergent passes (H-9)**: one `FlexPanel::layout()` pass cannot fit
  an auto size to a child whose input only settles top-down during that
  same pass (an aspect-derived height whose cross width resolves in the
  pass; the ancestor measured first, the child derives after). So
  `layout()` re-runs its pass while any child size, position, or
  `measure()` changed, bounded at 3 rounds. Each level converges its own
  subtree before returning, so the bound covers arbitrary depth; trees
  without derived sizes settle after the first pass (every demand is a
  pure function of settled inputs) and pay one compare. The flag still
  clears when done — convergence happens inside one `layout()` call, so
  a given state still triggers at most one layout per paint. Wrapped
  labels (§2 Wrapped text) ride the same rule: the first pass measures
  the single-line advance, the assigned width lands, the next round
  re-reads the multi-line height.
- Text advance cache (batch J4) invalidation duty: any setter that
  changes glyph content (set_text/set_glyph_provider) must reset
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
  - Log macros construct a stringstream at their default level (debug in
    a DEBUG-defining build — the battery runs Debug; info elsewhere,
    2026-09-06 ruling: no DEBUG definition, no debug logs): hot-path LD
    allocations are a **known,
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
    Rebuild observability (DLL-boundary observability, formerly "batch S1"
    — do not confuse with Theme's "batch S1" or backlog's Batch S render
    modes): `rasterization_count()` is a
    monotonic count of row rasterizations (cache misses); the
    invalidation gates assert its deltas because allocation deltas are
    not portable proof — on hosts where imcore is a shared library
    (Windows DLL) the test binary's operator-new replacement cannot see
    imcore-side allocations; ELF interposition sees them on Linux. The
    zero-allocation (warm-path) gates stay allocation-based: they lock
    the test-binary side, which is exactly where hot-path allocations
    are forbidden.
  - Init paths (construction, resource loading) are outside this budget
    (§1).
- Shared obligation: the process-wide shared BitmapProvider (batch J6)
  rests on it having no per-instance state; adding state to it requires
  removing the sharing first.
- The FreeType text-path allocation bullet (A-8, 2026-08-28) left the
  contract with the removed `USE_FONT` path (2026-09-06, L-5); the
  runtime provider carries the same zero-allocation duty forward (next
  bullet).
- **Runtime glyph cache (L-5)**: `TtfFamily`'s cache is bounded (default
  budget 64 KB, constructor parameter) with drop-all eviction (the
  ListBox rule). The first draw of a `(size, code unit)` rasterizes and
  may allocate; a warm draw allocates nothing. `test_runtime_ttf` gates
  warm `write` at zero allocations and pins miss deltas through
  `rasterization_count()` — allocation deltas are not portable proof
  across the imcore DLL boundary (same conclusion as the ListBox bullet
  above), while the zero-allocation gate locks the test-binary side.
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
  an early exit). `draw_image` (plain and tinted), `fill_gradient` and
  the round-rect pair all plot per-pixel through `draw_pixel`/`draw_line`
  and inherit the same clipping; the AA primitives (`draw_line_aa` /
  `draw_circle_aa` / `draw_arc_aa` / `fill_circle_aa` /
  `fill_round_rect_aa` / `draw_round_rect_aa`) write through `plot_aa`
  — the same offset/bounds/damage gate, then a coverage-weighted
  source-over blend that runs regardless of the `alpha_enabled` switch. Direct drawing
  without `clip_safe` is therefore also safe in damage mode
  (`test_raster_damage` pins this).
- **Render modes (S-1)**: `Graphics::render_mode::{full,wireframe,
  sketch}` (`set_render_mode`, default `full` = current behavior).
  `wireframe` turns the six shape fills (`fill_rect/circle/ellipse/
  triangle/gradient/round_rect`) into their 1px outlines (gradient uses
  its `from` color); the AA fills (`fill_circle_aa` /
  `fill_round_rect_aa`) degrade to their AA outlines (`draw_circle_aa` /
  `draw_round_rect_aa` instead of the aliased ones, so the bones stay
  smooth); strokes, AA strokes, text, images, damage regions,
  and hit-testing are unchanged. `fill()` is the mode-immune clear
  primitive (window clear, dialog mask); widget and pressed backgrounds
  go through `fill_rect` so they degrade. `sketch` is reserved and
  renders as `full` until S-2. Opt-in `draw_wireframe_grid(spacing,
  color)` dots the draw area (mode-independent). `ListBox` rows bypass
  the image cache in wireframe (a cached face would blit opaque):
  glyphs write straight to the screen, so flips need no invalidation
  and the miss counter only tracks FULL-mode cache misses.
- **Continuous rounded geometry (rim gate)**: every rounded-rect raster
  (the fills' row spans, the translucent border stroke band, the inset
  shadow's hole and clip, the outer shadow's silhouette mask rows)
  evaluates the box on the pixel-center grid in 1/4-px fixed point —
  inclusive pixel indices are pixels, arc centers
  sit at `(left + r, top + r)` and `(right + 1 - r, bottom + 1 - r)`
  continuously, and tangent rows keep their real chord instead of
  collapsing a row early (the index-space chords made even-sized boxes'
  bottom arc drop out: the model500 knob's bright leak ring between the
  border ring and the inset shadow). The fills keep the center-sampling
  span convention (a pixel is full when its center is inside the chord,
  one partial fringe neighbor); on corner arcs that fringe neighbor's
  coverage is the pixel's true AREA overlap with the rounded rect (an
  8x8 subsample of the integer indicator, squared-distance corner test),
  not the 1D chord fraction — the chord lands on a pixel boundary at
  different sub-pixel positions row by row, and a translucent border
  over the fill then read the PAGE through the band's outer tail instead
  of the face (the model500 knob rim's speckle; the browser's
  background-clip paints the face anti-aliased to the border-box edge).
  Straight-run rows keep the exact 1D fraction. The shadow masks (inset
  hole and outer
  silhouette, the inputs of a coverage blur) convert BOTH edges to
  nearest with explicit partial pixels on either side — area-sampling,
  so the mask is mirror-symmetric and the blur cannot leak a one-sided
  quarter-pixel bias into the spill (the disc mirror lock). The
  translucent 1px border itself
  (per-channel blend, r ≥ 2) takes a signed-distance stroke band on the
  same grid — the ring one pixel inside the box edge, linear 1px
  coverage ramp, midline half a pixel in — with corner-arc coverage
  taken from an 8x8 subsample of the band indicator (mass-conserving
  through the curve; the point-sampled tent is exact on straight edges
  and stays there) — instead of the polyline
  (which leaves under-covered seams at the diagonals and tangents);
  opaque and binary borders keep the polyline. `draw_line_aa`'s
  `skip_first` (arc joints) skips the CALLER's start pixel through the
  endpoint normalization, so clockwise and counter-clockwise arcs
  rasterize identically.
- **`draw_arc_aa` angular contract (V-5)**: integer degrees in the math
  convention, `start_deg` measured from +x (3 o'clock), positive
  `sweep_deg` counter-clockwise — on the raster's screen coordinates
  (y down) that is visually clockwise, matching SVG/Canvas arc angles.
  `sweep_deg == 0` draws nothing, `|sweep_deg| >= 360`
  draws the full circle (the two degenerate arcs clamp to it, no
  overlap). The arc is a polyline of circle points sampled every
  `max(1, ceil(57.3 / radius))` degrees (±0.5px chord spacing), each
  segment drawn through `draw_line_aa` so endpoints plot solid and
  every write stays on `plot_aa`. **Two trig paths** (both
  deterministic): with `USE_INTEGER_GEOMETRY` OFF, IEEE float
  `sin`/`cos`; with it ON (NDS toolchain forces it), a
  compile-time-generated 1-degree lookup table over 0–90° expanded by
  symmetry — no runtime floating point. Both paths agree on each sample
  to within ±0.5px at radius ≤ 128, so the same call renders
  sub-pixel-identically on desktop and FPU-less targets.
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

## 10. Theme (batch S1 contract — the Theme workstream; not backlog Batch S / not the rebuild observability "batch S1")

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
  Storage follows the shared BitmapProvider rationale (docs/backlog.md
  A-4.1): the active theme is an intentionally leaked singleton,
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
