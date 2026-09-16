# Imprint — Backlog

> Living backlog document. Tracks active, condition-triggered, and unscheduled
> work items across architecture and product layers. Release/promotion status
> is operator state, not roadmap — it lives in the local handoff notes, not
> here.
> Architecture contracts live in [`docs/ARCHITECTURE.md`](ARCHITECTURE.md);
> API contracts live in [`docs/code-contract.md`](code-contract.md).
> Completed items are removed upon completion (A-numbering is stable, gaps
> represent finished work; history lives in `git log`).

## 0. Execution Order (2026-09-10)

Agreed sequence — a map through the backlog, not a new state machine.
Dependency-driven: each tier unlocks what follows.

1. **A-24 — done** (2026-09-10): the `hit()`/`on_input()` override
   contract is documented in ARCHITECTURE §4.3 + code-contract §3 and
   locked by `test_hit_override` + `test_on_input_custom`; zero interface
   change. The A-24 source analysis (no new Widget methods, Shape is
   YAGNI, GaugeDial/Knob as first overriding subclasses) is the seeded
   basis for V-5 step 2.
2. **V-5 composition widgets + dashboard** (3–5 days) — `draw_arc_aa`
   (done), GaugeDial/Knob/TrendLine/toggle (done), factory-console
   dashboard + self-benchmark panel (done). Highest ROI right now: this is what turns
   the recent industrial mockups (Series 7 / Model 500, `design/htmldemo/`)
   into something real running in the framework, and the promotion
   material for the repo.
3. **V-3 re-record & re-shoot** — **done** (2026-09-11): GIF
   re-recorded with the factory-console dashboard (188 frames);
   320×240 layout fits the recorder and real window. Montage-refresh
   and per-platform screenshot refresh (win/mac/linux/nds) pending
   maintainer — the hero page changed only by adding the CONSOLE
   button, so the existing montage is one click stale. Next step is
   Batch H core.
4. **Batch H core** (3–5 days) — HTML/CSS rendering path; the
   tag-mapping table now has `<gauge>`/`<knob>` available because of
   A-24 + the V-5 widgets.
5. **Quick wins** (<1 day each, opportunistic): **L-3** list_box width
   trap (real bug fix), **S-1 WIREFRAME** (layout-debug view + low-power
   mode), **I-2b** device overlay (pairs naturally with S-1 for
   presentation).
6. **Explicitly NOT now**: H-1 text wrapping (until a real `<p>` need),
   H-6 SVG & S-2 SKETCH (recorded, unscheduled), F-1/F-2, I-1, V-4,
   A-4/A-21/A-23, D-*, Batch G. Condition-triggered items stay
   trigger-gated.

### Batch V — Visual Presentation (Active; started 2026-09-05)

Goal: close the "works but looks primitive" gap against other UI
frameworks. Principle: modern look = rasterizer primitives (imcore
Graphics) + themed demo content; widgets stay theme-token driven, no
widget redesign, no animation system. V-5 + V-3 are execution order
steps 2–3.

- **V-1. Rasterizer primitives** — **done** (2026-09-05): linear
  gradient fill, rounded rect draw/fill, tinted `draw_image`, AA
  line/circle as opt-in `*_aa` calls (never a global default switch);
  see `git log` for the exact contracts.
- **V-2. Showcase content & assets** — **done** (2026-09-05): dark-boot
  hero with the animated chart (the F-2 preview tween reveals it one
  step per paint), compact rows keeping the 256x192 embedded fit;
  gallery alpha-asset row (9-slice shadow card, accent-tinted ball).
  Assets are PROCEDURAL, generated at build time by
  `tools/asset_gen` into RGBA8 arrays (the `ttf_subset` precedent:
  build-time materialization, no runtime decode, no USE_PNG
  dependency); the shadow blur lives in the generator — no framework
  blur. A plain `set_background_image()` panel cannot show soft alpha
  (background draws run with the alpha switch off); apps opt in via
  their own `draw_at()` (`AlphaImage` in the showcase).
- **V-4. codec: memory-source image decode** (condition-triggered):
  `Image::read_png*` accepts only file names; embedded targets shipping
  compressed art need `read_png_memory(bytes, n, ...)` (stb offers
  `stbi_load_from_memory`). Gated together with the USE_PNG
  default-OFF question — decide both when the first real
  compressed-asset use case appears; until then the procedural
  generator covers the demo.
- **V-3. Re-record & re-shoot** — **done** (2026-09-11): 188-frame GIF
  (10 s) re-recorded with the V-5 factory-console dashboard; the
  three-page recorder choreography (hero → start → replay → dashboard
  with sim growth, live knob drags, SELF-CHECK PIXELS MATCH → light
  gallery → back to dark) fits the 320×240 canvas the real window and
  WASM use. Dashboard designed to 320 natively (content width 304);
  the one-axis `width=` only bug (`apply_common` zeroed the other
  axis's explicitness, collapsing buttons flat) was caught here and
  fixed with a one-axis setter path in `apply_common` (regression
  locked in `test_builder`). Per-platform screenshot refresh
  (montage/win/mac/nds/linux) deferred to maintainer — the hero page
  changed only by adding the CONSOLE button; existing montage is one
  click stale. READMEs (three languages) updated.
- **V-0. Promote `gif_encoder`** — **done** (2026-09-05):
  `zb::ui::GifWriter` lives in `imcore/codec/gif` beside png/jpeg; input
  is `const core::Color*` through the normalized accessors (A-19, so
  16bpp quantizes through the same path); the recorder glue (frame
  pacing, file driving) stays app-side. `test_gif` pins the block
  structure (GCE terminator included, bb916e7) and determinism; the
  recorder builds against the codec unchanged.
- **V-5. Modern dashboard demo pass** (execution order step 2; added 2026-09-06; the
  buyer-facing showcase pass — also the prototype of the future
  embedded device page, see the device-selection discussion in the
  handoff notes):
  - **Arc primitive** — **done** (2026-09-11): opt-in AA `draw_arc_aa`
    (integer degrees, math convention 0° = +x, positive sweep visually
    clockwise on screen y-down — SVG/Canvas-compatible), a sampled
    polyline through `draw_line_aa`, every write on `plot_aa`
    (clip/damage/16bpp inherited); full circle degenerates to
    `draw_circle_aa`, zero sweep draws nothing. **Two-trig-path
    policy**: desktop (`USE_INTEGER_GEOMETRY` OFF) uses IEEE float
    sin/cos; FPU-less targets (NDS FORCE ON) use a compile-time-generated
    1° lookup table (constexpr Taylor into a 91-entry int16 quarter
    table, symmetry-expanded — no runtime float). Both paths agree
    ±0.5px at r ≤ 128, locked by the four-gated arc expectations in
    test_graphics (32/16bpp × float/int); the CI `linux-nonatomic-ptr`
    job now also configures `USE_INTEGER_GEOMETRY=ON` so the int path
    has a permanent test seat. Contract: ARCHITECTURE §4.4/§4.9 +
    code-contract (AA/damage paragraph). Unlocks the gauge/donut ring
    widgets (V-5 step 2).
  - **Composition widgets** — **done** (2026-09-11): `ToggleSwitch`,
    `GaugeDial`, `Knob`, `TrendLine` landed; first A-24 `hit()`/`on_input()`
    overriding consumers in-tree (GaugeDial/Knob circular hit); public
    `core::point_on_circle` helper (same integer-degree + two-trig-path
    convention as `draw_arc_aa`) supplies needle/tick geometry; no widget
    redesign, no animation system (standing non-goals). Theme-token
    driven; both `changed` events fire on user interaction only
    (programmatic setters are silent). Four-gated test battery passes
    (32/16bpp × float/int); NDS cross-compile verified. Contract:
    ARCHITECTURE §2 + code-contract §3.2. Unlocks the showcase page
    (V-5 step 3).
  - **Showcase page** — **done** (2026-09-11): factory-console themed
    dashboard (trend chart, gauges, toggles, setpoint knob+slider,
    alarm list, status banner, SELF-CHECK panel) mounts from the
    CONSOLE button on the hero page; landing the four widgets as
    design-file tags (`toggle`/`gauge`/`knob`/`trend` in the ui_builder
    table with their property sets) means the page is authored in a
    `.ui` file, and Batch H's tag-mapping table inherits them ready to
    go. Deterministic plant sim advances one sample per input event
    while mounted. Verified: four-gated suites pass (32/16bpp ×
    float/int), the `.nds` showcase ROM builds with the dashboard
    embedded. Unlocks V-3 re-record (execution order step 3).
  - **Self-benchmark panel** — **done** (2026-09-11): SELF-CHECK drives
    the knob with three 2px up/down drag round-trips through the real
    input path, hashes the framebuffer byte-for-byte (FNV-1a) at the
    raised state and the returned state, and stamps `PIXELS MATCH` when
    all three pairs are byte-identical (the deterministic-runtime
    proof); the dirty-region selling point rides along — the painted
    subscription counts frame paints and the flushed (damaged) pixels,
    reported as `RT`/`PX/F`/`FPS`, where sub-10ms wall-clock runs
    (headless tests, the NDS poll) show `--` instead of a meaningless
    frame count. The painted counter is frame-accurate because damage is driven by `frame_dirty`
    at `paint()` (the showcase paints once per frame, not per widget).
    The plant sim freezes while the bench runs so the comparison is
    hash-stable; at 16bpp the hash is byte-based because the per-pixel
    Color is 2 bytes there, not 4.

### Batch L — Layout & Text Enhancements (Unscheduled)

- **L-1. Widget-level margin/padding API** (quick win later:
  - Context: Button `measure()` vs draw padding discrepancy fixed in `65087b8`. A general margin/padding model across widgets and containers remains unscheduled.
- **L-2. `.ui` alignment attributes (`halign` / `valign`)** — **externally claimed** (GitHub issue #2 assigned to @tecnolgd, 2026-09-05; do not implement here — review their PR against `docs/design-file.md` grammar when it lands):
  - Context: Declarative `.ui` alignment syntax. Currently apps use explicit `set_v_align` / `set_h_align` in application code (`f74ab48`). Good-first-issue #2 opened.
  - Review default framework alignment strategy (e.g. text centering vs top-left default).
- **L-3. `list_box rows=` declaration width trap** (execution order step 5):
  - Context: `list_box rows=` implicit `set_size` sets undeclared width to 0 (`685c004`), requiring explicit width declarations in `.ui` files. Needs cleaner auto-width sizing behavior.

### Batch H — HTML/CSS Rendering Path (Medium-high priority — execution order step 4; added 2026-09-09)

Goal: an optional declarative smooth-path that renders an HTML/CSS **subset**
(no JS) through the existing widget tree, complementing the `.ui` design file.
Rationale: lets users with existing HTML/CSS authoring patterns describe
screens without learning the `.ui` grammar or the C++ builder API. No JS, no
CSS cascade engine — a deliberately narrow declarative front-end onto widgets.

**Dependency:** A-24 (Widget custom hit-test / value binding) should
land first or in parallel. Without it, the HTML parser can only map
elements to rectangular widgets (Panel/FlexPanel/Label/Button/Checkbox/
Radio). With A-24, the parser gains `<gauge>`, `<knob>`, `<trend>`,
`<meter>` as first-class custom elements — the Widget subclass handles
its own rendering and interaction, the parser only declares the element
and its style properties. This makes the HTML path significantly more
powerful for industrial instrument pages.

**Scope — what the initial (core) version supports:**
- HTML subset: block containers (`div` mapped to `FlexPanel`/`Panel`), text
  (`p`/`span` → `Label`), `button`, `checkbox`, `radio`, `br`; `id=` attributes
  map to the existing `find_by_id` lookup.
- CSS subset: `display: flex` + `flex-direction`, `width/height` (px and `N%`
  → existing `set_width_percent`/`set_height_percent`), `background-color`,
  `color` (→ `set_text_color`), `padding`, gap/spacing, `font-size`
  (→ `set_font_size`, code-contract §2.4; needs `USE_TTF_RUNTIME` + a
  `TtfFamily`, otherwise documented no-op).
- Styling via inline `style=` and an optional `<style>` block; **no** CSS
  selectors beyond tag/type and simple `#id` matching.
- Font follow-ups (recorded, unscheduled): CJK multi-provider fallback
  (Latin → CJK → 5x7) + `.html` codepoint scanning; `USE_FONT_SIZE`
  default resolving to the vendored OFL font instead of system fonts.

**Hard support boundary — the whitelist is the contract (rewritten 2026-09-12).**
The element/attribute/CSS-property whitelists in `docs/html-path.md` are
the single source of the boundary: **anything not in the table constructs
nothing** (LW + content dropped) — no blacklist to maintain. This doc holds
only the genuinely behavioral decisions, which are layout-engine scope, not
parser scope:

- **`div` block semantics are deliberately simplified**: `div` maps to a
  flex container (content-measuring FlexPanel), not HTML block layout; a
  "block" div does **not** stretch to fill its parent's main axis (flex
  markup drives fill). `display: block` and `display: flex` are treated
  as equivalent in the initial core. (Waiving full block layout keeps the
  parser from growing a second layout engine; FlexPanel is the one
  layout backbone.)
- **No second layout engine**: no `position: absolute/fixed`
  (no offset/z-index/overlap), no `overflow: scroll` (H-4), no grid /
  multi-column / RTL / bidi — each is a container/scroller enhancement,
  not parser work.
- **CSS flex features beyond FlexPanel's layout()**: `justify-content`
  (main-axis end/center/space-between), `align-items`/`align-self`
  (cross-axis alignment — FlexPanel cross-axis is not stretched),
  `flex-basis`, `flex-shrink`, and `flex` min/max constraints are
  **out of scope** for the initial core. They map to FlexPanel
  enhancements, not parser work — see H-7.

**Status (2026-09-12):** core work in progress, contract landed first:
`docs/html-path.md` (the whitelist single source) added; shared
`background`/`color` properties added to `docs/design-file.md` /
`docs/code-contract.md` §4; `parse_html` recorded in ARCHITECTURE
§2/§4.10. Work steps: (1) contract docs — (2) property tables — (3)
parser + tests — (4) preview consumer + NDS cross-compile.

**Phased follow-ups (each a later, independently-reviewable increment — add
support "a little at a time" as the boundary demands):**
- H-1. Text wrapping: certifies paragraph reflow. **Prerequisite for any real
  `<p>` page.** The `GlyphProvider` seam currently measures/draws a whole run
  at once (`measure`/`write`); a wrapping engine needs per-word/per-char
  measurement + greedy line breaking (~250 lines). This is the single
  architecture-relevant gap in the initial core.
- H-2. `BorderWidget` wrapper (~100 lines) + `border` shorthand.
- H-3. `margin` support — **landed 2026-09-15 (model500 footer gap)**:
  per-side non-negative margins in the `ext_` sidecar, honored by the
  FlexPanel/Panel in-flow geometry (pitch/line/measure, never collapsed
  or shrunk) + HTML 1–4-value shorthand and longhands. Spacer-widget
  mapping not needed.
- H-4. `ScrollPanel` + `overflow` (moderate — needs a scroll container).
- H-5. Widened selector support — **landed 2026-09-12 (model500 demo
  demanded it)**: tag/`.class`/`#id` compounds, descendant chains,
  comma groups, specificity cascade, `:root` variables + `var()`.
  Remainder stays out: child/sibling/attribute selectors,
  inheritance, `var()` inside `@media` (no at-rules).
  Pseudo-elements move to H-10 (below).
- H-6. `SvgWidget` — SVG as a widget subclass (recorded 2026-09-10,
  **unscheduled — note only, no priority**):
  - `SvgWidget : Widget` (imui, beside Button/Label). Parses an SVG text
    into a compact `DrawCommand[]` byte array once at construction
    (`.ui`-embed / `asset_gen` precedent); `draw_at()` executes the
    sequence on the existing Graphics primitives at runtime (zero parse
    cost). Default: display-only (rect `hit()`, no `on_input()`), like
    Label. Subclasses may override `hit()`/`on_input()`/`set_value()`
    for interactive or value-driven SVG.
  - Alignment with Batch H: HTML parser's tag-mapping table gains
    `<svg>` → `SvgWidget` — same mechanism as `<gauge>` → GaugeDial.
  - Phased internally: (a) static geometry subset — `rect`, `circle`,
    `ellipse`, `line`, `polyline`, `polygon` + `fill`/`stroke`/
    `stroke-width` + `viewBox` (~400 lines, maps to existing Graphics
    primitives). **Landed first cut (2026-09-12, demo-driven, narrower
    than planned): `SvgCanvas` with `viewBox` + `line`/`text` + `g`
    folding only, 1px strokes, stretch mapping; HTML tags `svg` and
    `vectordial` (alias, shared implementation). Remainder of (a)
    (`rect`/`circle`/`ellipse`/`polyline`/`polygon`/`fill`/widths) and
    all of (b) stay unscheduled;** (b) `path` (Bezier M/L/C/Q/A) + `transform`
    translate/rotate/scale (~600 lines, needs new Bezier rasterization
    primitive); (c) out-of-scope: filters, gradient defs, clipPath,
    symbol/use, animation.
  - Embedded caution: Bezier rasterization at runtime may cost more than
    pre-rendered pixel assets (asset_gen precedent) — SVG suits reusable
    UI-drawing widgets (icons, gauge faces, decoration), not
    pixel-dense assets.
- H-7. Layout alignment & flex fill (FlexPanel enhancements, not parser
  work): `justify-content` (main-axis end/center/space-between/
  space-around — around added for the knob-row; H-7a),
  `align-items`/`align-self` (cross-axis alignment — today FlexPanel does
  not stretch cross-axis), `flex-basis`/`flex-shrink`, and `flex` min/max
  constraints. Each is an additive FlexPanel parameter with a default
  preserving current behavior (left-aligned main axis, unwrapped
  cross-axis), so existing tests and `.ui` files stay green when a
  parameter lands. Sized individually; gated by a real page that needs
  them (see §0 step 6: not now).
  H-7a landed 2026-09-13: `justify-content` start/center/end/
  space-between/space-around (closed-form positions per line, spacing
  as minimum gap, overflow falls back to start) — amp-head
  space-between puts POWER right, knob-row space-around spreads the
  five knobs.
  H-7b landed 2026-09-13: `align-items` (container default, start keeps
  history) + `align-self` (auto inherits) with center/end offsets and
  `stretch` for auto-cross children (explicit/percent axes keep size,
  CSS non-auto rule) — knob labels center under knobs, led labels
  under LEDs, amp-head/footer rows center vertically (2304 px delta,
  all inside the card). The viewport-centering wrapper stays a no-op:
  the document root sizes to content, not the window (root-fill
  follow-up, not H-7).
  H-7 A+B landed 2026-09-14 (model500 full-page fidelity): (A) a flex
  `body` is kept as the document root — host takes its container
  properties and box dress, content width constrains the child (the
  P-2a "amp runs full-bleed" follow-up and the root-fill follow-up
  above both close; body gradient backgrounds land through the dress,
  the H-8 colors-only page carrier is unchanged); (B) HTML containers
  with no `align-items` default to `stretch` (CSS default — VU bank
  fills the chassis, knob-row spreads; `.ui`/programmatic keep
  `start`). Plain bodies hoist exactly as before. `min-height:100vh`
  itself stays unsupported (no viewport units).
- H-9. Convergent layout passes — **landed 2026-09-13**: `layout()`
  re-runs its pass while a child size/position/measure changed (bound 3;
  contract §7), so auto ancestors fit aspect-derived children whose
  cross input settles top-down in the same pass — model500 `.vu`
  110x55/svg 110x42 with all strokes parsed, converged in one call
  (probe-verified; `test_flex` H-9 block fails 15-vs-110 on the old
  single pass). Dial *visibility* still needs paint (dark dial face:
  `background:` gradients) + positioning (`absolute` out-of-flow, so
  `.vubottom` stops stealing 28%→13px from the svg's 100%).
- P-1. Paint dressing — **landed 2026-09-13**: `background` shorthand
  (solid/`rgb()/rgba()` + linear 2-end + radial circle, base-first
  layer rule so `.amp`'s brushed texture falls through to its linear
  base), `border: Npx solid`, `border-radius: Npx|50%`, Widget
  `paint_dress` (24B packed, 64-bit gate 248→264; wasm32 176 / NDS ARM
  184 keep headroom). model500 VU bank + dial faces now paint dark and
  the strokes read. Remainder → P-2.
- P-2. Paint remainder (gated by a real page): `conic-gradient` (knob
  faces), `repeating-linear-gradient` (brushed texture, vubottom
  stripes), element-level `opacity` (LED dimming), `box-shadow` /
  `text-shadow`, side-specific `border-top`, 3+-stop linear mid colors
  (P-1 keeps first+last), `font-weight` / `letter-spacing`.
  P-2a landed 2026-09-13: `letter-spacing` (per-unit tracking),
  `font-weight` 600+/bold (double-strike), `text-shadow` (solid offset
  copy, blur ignored). Frame patch same day: `build()` forwards the
  root box dress (bg/border/radius/text color) to the host, geometry
  never transfers (contract §build). Follow-ups (not P-2): document-width
  roots are not centered by UiPreview (amp runs full-bleed); body
  gradient backgrounds have no `html_page` carrier (H-8 holds colors
  only); GIF review captures band smooth ramps (216-cube) — review from
  the raw framebuffer.
  P-2b landed 2026-09-13: `conic-gradient(from Ndeg, 2–4 deg stops)`
  through `Graphics::fill_conic` (dual-path angle: integer LUT search
  vs atan2, agree 1°; center 50/50; straight-RGB segment lerp) into the
  single `Widget::ext_` heap sidecar (pos+grad+text sections share one
  allocation; bare widgets stay null — the size gates hold at 264/192).
  P-2c landed 2026-09-13: exactly-3-stop linear mid section
  (`fill_gradient3`, bare mid = 50; the sheen shape) + topmost
  repeating layer as a translucent overlay (`fill_repeating`, period =
  last stop, double-position pairs; brushed + vubottom stripes).
  `transparent` is an explicit alpha-0 stop inside stop lists
  (`parse_stop_color`; solid backgrounds still reject it). Binary-alpha
  depths follow the standard any-bit-set rule for every stop.
  P-2d landed 2026-09-13: element `opacity` (fixed-point 0..1000,
  build-time fold into the widget's own paint alphas, ceil so nonzero
  stays nonzero; no subtree compositing) dims the LED faces, and
  `border-top` (own sidecar band) draws the footer divider (row 272
  reads uniform 132 = 189 × 0.7).
  P-2e landed 2026-09-13: `box-shadow` lists (2 outer + 2 inset, extras
  warn-and-drop; malformed entries drop alone). Inset paints inner
  bands (linear falloff, exact on 32bpp, half-coverage keep/drop on
    binary; all-sides via shrinking outlines, split sides via
  chord-clipped lines through the newly public `corner_chord`) — dial
  faces, knob edges, and toggle tracks gain depth. Split-side AA
  landed 2026-09-15 (knob-rim staircase): chord-cut band ends blend
  by coverage in the band color; `plot_aa` public with widget paint
  as second consumer (ARCHITECTURE §4.4). Outer paints its
  silhouette under the box but stays invisible on opaque boxes (no
  overdraw — bulb/LED glow + amp drop recorded as follow-up). Two
  quantized-alpha fixes ride along: inset blend is forced on (falloff
  manufactures translucency; radius-0 outlines overwrote raw), and
  falloff math honors the binary half rule.
  N-stop linear landed 2026-09-15 (series7 case metal, 4–8 stops with
  even distribution through `fill_linear_stops`; >8 stays ends-only).
  Prerequisite found en route: stylesheet `/* */` comments glued to
  the neighbor declaration (series7 `:root` lost every commented
  var's successor) — `parse_declarations` now strips them (H-5
  hardening, html-path.md).
- P-3. Absolute positioning — **landed 2026-09-13**: `position:
  relative/absolute` + `top/left/right/bottom` (`Npx`/`N%`/bare/`auto`)
  + `transform: translate()`; FlexPanel skips abs in measure/lines and
  resolves against the containing block after normal flow (anchor =
  nearest positioned ancestor else parent box; stretch/declared/measure
  width, translate last; contract §7). model500 `.vubottom` overlays at
  28% again (svg back to full height, dB visible), knob-dot centered
  (17,17), toggle thumbs at 1px/13px. Follow-up fixed 2026-09-15
  (knob-dot rendered 0x0): resolve_abs kept consuming explicitness
  through the auto setters so H-9 re-passes fell back to demand
  (invisible for base Widget whose measure defaults to size);
  declared axes now survive, and the pixel gate plus set_*_percent
  honor the L-4 never-explicit promise. Storage is a heap side struct
  for positioned widgets only (bare ctor still zero-alloc).
- H-8. Page-level box (landed 2026-09-12, B2 review): the `<body>` style
  feeds an `html_page` (per-axis Npx size + background) beside the tree;
  `parse_html(text, ok, page)` fills it; consumers resolve document →
  shell → app default (warn on default) before materializing; fixed host
  buffers win on conflict. `parse_color` is the shared resolver
  (declared in `html.hpp` to keep `ui_builder.hpp` light for the
  ui_embed host tool).
- H-10. Pseudo-element correspondence (next up after the knob-roundness
  fix, raised 2026-09-16): `::before` / `::after` (model500 `.knob`
  pointer tick is the driving case — currently inert per the
  `docs/html-path.md` tolerance table, so framework knobs miss the
  pointer). Narrow subset only: static box/line content on the originating
  element, no dynamic behavior; contract change grows the whitelist when
  it lands.

**Cost estimate (discussion):** core version ≈ 1500–2000 lines C++ total, of
which the text-wrapping engine (H-1) is the prerequisite piece; a minimal
flex-only core without H-1 is ~800 lines and covers display-only pages. Main
risk is not code volume but **semantic drift** — users hit "why isn't this CSS
attribute supported", so the whitelist in `docs/html-path.md` is the released
boundary (off-table constructs warn and render nothing, never a wrong
structure).

**Placement note (decided 2026-09-12):** no `IMPRINT_WITH_*` switch needed —
per the A-23 precedent the `html` translation unit stays in imui (STATIC);
unreferenced on targets that don't use it, dropped by the static linker at
image build. Consumer-side, not a new C-ABI surface at this stage.

### Batch S — Render Modes: Sketch & Wireframe (S-2 still unscheduled; S-1 landed as execution order step 5)

Alternative rendering modes for the same widget tree. Not new widget
classes — these are `Graphics`-layer `RenderMode` switches (~150 lines
total) that change how existing draw calls behave. A widget tree
rendered in WIREFRAME mode shows only structural bones; in SKETCH mode
it looks hand-drawn. The widget tree, dispatcher, damage tracking, and
Widget hit-testing are all unchanged.

- **S-1. WIREFRAME mode** (landed, execution order step 5):跳过填充，只画 1px 边框和文字骨架；
  grid/spacing 可选显示。Render-mode switch on `Graphics`
  (`set_render_mode`, default FULL); shape fills degrade to 1px
  outlines, `fill()` stays the immune clear primitive, `ListBox` row
  images follow the screen mode; `sketch` reserved for S-2. Contracts in
  ARCHITECTURE §4.4 + code-contract raster section, locked by
  `test_render_mode`. Use cases:
  - **Layout debug view**：开发者查看界面布局结构，隐藏视觉噪音。
  - **e-ink / low-power mode**：减少像素翻转量，延长 e-ink 屏幕
    寿命；低带宽远程监控只传骨架（省 90%+ 帧数据）。
  - **Accessibility / high-contrast**：极端简化，只保留结构信息。
- **S-2. SKETCH mode**：线条加 jitter 偏移（轻微抖动），填充不完全
  均匀，边缘有"毛刺"感。Use cases:
  - **Product configurator kiosk**：家具/户型选配，手绘风暗示
    "这是草图，还没定稿"，降低用户心理压力。
  - **Education / children's devices**：触摸屏教育玩具，手绘风比
    精确工业风更亲切。
  - **Creative tool UI**：嵌入式绘图板/UI，sketch 模式让 UI 跟
    内容风格统一。

**Architecture note**: both modes live in `imcore` Graphics as a
`set_render_mode(FULL|WIREFRAME|SKETCH)` enum. Each `draw_*` call
branches on mode: WIREFRAME skips fills, SKETCH adds jitter to line
endpoints. FULL is the default (current behavior, zero overhead).
Orthogonal to Widget hit-test/shape (A-24) and to theme (colors/tokens)
— a GaugeDial can render in any mode with any theme.

### Batch I — Tooling & Inspection (Unscheduled)

- **I-1. Hot reload for design file previewer (`apps/ui_preview`)**:
  - Watch `.ui` file changes on disk and reload in-place without restarting the previewer.
- **I-2b. Device overlay** (landed, execution order step 5): bezel/chrome around the presented buffer
  matching target screen constraints (dual NDS 256x192 screens,
  framebuffer 320x240) — pure layout math in
  `shell/device_overlay.hpp` (chrome bars, hinge bar via the region
  ceil mapping, half-open contains + documented input composition);
  shells paint natively, no shell rewired (per-shell adoption needs
  maintainer eyes). Contracts in code-contract §3, locked by
  `test_shell_presenter`.

### Batch F — Event Loop Extension & Frame Automation (Long-term)

- **F-1. Cross-thread message posting `zb::ui::post(closure)`**:
  - Contract-first design before implementation.
  - **Invariants to preserve** (normative in [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) §4.11):
    - Single-threaded driving semantics at the core.
    - Deterministic frame ordering for automation and headless runners.
    - Observable `painted` synchronization signal.

- **F-2. Frame automation helper (`Tween` / pacer)** (added 2026-09-05;
  ruling refined the same day: app-side frame automation is allowed,
  widget-built-in tween / a framework animation scheduler remain
  non-goals):
  - `Tween` is a pure value interpolation (from / to / duration /
    easing), a pure function of the frame index — no threads, no core
    state; the app advances it in its painted callback and invalidates.
  - Pacing belongs to the host (desktop invalidate loop / WASM rAF /
    NDS vblank); the helper owns only the math and invalidation
    requests, keeping the A-2 seams untouched. Single-threaded by
    contract; F-1 remains the separate cross-thread track.
  - Contract-first design before implementation; first lands as
    showcase demo glue (V-2 hero), promoted to an optional helper
    beside `imapp_canvas` when a second user appears (§2 tool-placement
    rule).

### Batch G — Positioning & Go-to-Market (Deferred; recorded 2026-09-06)

Input: a third-party commercial review plus the maintainer assessment
of 2026-09-06 (discussion in the session log; deliberately deferred —
the project stays in technical validation until promotion resumes).
Conclusions recorded so they are not re-derived:

- **Reposition when promotion resumes**: lead with the
  already-delivered differentiator — a deterministic, pixel-testable
  UI runtime (input → deterministic frames → pixel assertions,
  allocation gates, cross-platform identical output; all locked by the
  test battery) — not "another cross-platform C++ GUI framework". Do
  not lead with "embedded": the only embedded target so far is the NDS
  (entertainment-class hardware), so the embedded claim stays partially
  earned until a real MCU-tier footprint exists.
- **Cheapest moves when resumed** (packaging only, zero architecture
  change): README first line — **done** (2026-09-06: the three-language
  READMEs lead with the deterministic-testing contract, NDS demoted
  into the target list; the live gh-pages WASM showcase rebuilt to the
  V-2 dark showcase the same day). Remaining: a landing page assembled
  from existing assets (GIF, four-platform screenshot strip, NDS photos,
  single-file WASM demo) with a "tell us about your device" intake;
  the deterministic-test capability packaged as an explicit feature
  with a CI recipe.
- **First revenue path**: per-target port engagements (display /
  input / font glue for a customer's board) — small, immediate,
  single-customer. SDK/enterprise licensing and any designer product
  are later-stage; a designer would reverse the "no drag-drop
  designer" ruling (an architecture-level decision, not a feature).
- **Rejected directions** (with reasons): Figma import (free-form
  canvas → constraint-layout mapping is unmaintainable; the viable
  variant is LLM-generated `.ui` files, which the grammar already
  supports); AI-agent-friendly runtime as a commercial wedge (circular
  — agents drive pre-existing GUIs; deterministic rendering remains a
  free option for agent-eval sandboxes); hardware-SDK-vendor sales at
  the current validation stage (B2B2B needs support infrastructure and
  bus-factor credibility that do not exist yet).
- **Standing constraint**: single-maintainer bus factor outweighs star
  count for embedded adopters; the first external committer matters
  more than stars.

## 2. Architecture Backlog

### A-4. Smaller Items

- **A-4.1 Singleton lifetime**:
  - `Widget` shares one process-wide `BitmapProvider`, intentionally leaked so widgets outliving `main` never touch a dead provider.
  - Revisit if a target ever needs an explicit teardown/shutdown lifecycle (must stay stateless while shared — see `widget.hpp`).
- **A-4.2 Shared/static duality of `imcore`**:
  - Kernel is built as CMake SHARED library (for dynamic host loading on Linux) yet statically linked into embedded ROMs and tests.
  - A packaging abstraction would simplify new target integration.

### A-21. Retire the non-atomic `SharedPtr` branch (Condition-triggered)

- Today every non-embedded build uses `std::shared_ptr` (`zb::SharedPtr` is an alias); the ~150-line non-atomic implementation exists only for targets without atomics (NDS ARM9: devkitARM ships no libatomic). Its semantics are locked by `test_ptr.cpp` (compiled against the custom branch on the host) and the CI non-atomic matrix job runs the whole battery against it.
- **What is deferred:** Collapsing the duality — either `std::shared_ptr` on the NDS too (needs a toolchain decision: `__atomic` support on arm926ej-s / shipping a libatomic) or an intrusive refcount owned by the objects themselves. Both are ABI-adjacent changes with no current payoff.
- **Trigger:** Act when the custom branch needs a real fix again, or when a second non-atomic target appears; until then the tests keep it cheap to carry.

### A-23. Selective build/package switches (Condition-triggered)

- The whole tree always configures and builds; there is no `IMPRINT_WITH_*` switch to trim the configure. Binary granularity is already right — static linking drops unreferenced objects, the Linux host ships only `libimcore.so` + `zbapi.so`, the NDS ROM is fully static — and `zbapi.so` statically embeds imui + the story app, which is inherent to the current C-ABI contract (a foreign host drives a whole app).
- **Trigger:** Add configure-time module switches only when a real distribution case appears that must ship or withhold specific modules at configure time; until then the whole-tree build is the cheaper representation.

### Unscheduled Design Debt (Batch K Triage)

- **D-1**: `FlexPanel` min/max size constraints.
- **D-3**: Focus navigation history.
- **D-4**: `Event` once-handler and priority handlers.
- **D-9**: Centralized resource management.
