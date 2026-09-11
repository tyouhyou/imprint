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
2. **V-5 composition widgets + dashboard** (3–5 days) — `draw_arc_aa`,
   GaugeDial, Knob, TrendLine pawn, then the factory-console dashboard
   + self-benchmark panel. Highest ROI right now: this is what turns
   the recent industrial mockups (Series 7 / Model 500, `design/htmldemo/`)
   into something real running in the framework, and the promotion
   material for the repo.
3. **V-3 re-record & re-shoot** — only after V-5 exists to record;
   captures the new dashboard, not the old showcase.
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
- **V-3. Re-record & re-shoot** (execution order step 3): GIF + per-platform static frames
  (win / X11 / mac), README hero layout, three-language READMEs aligned.
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
  - **Composition widgets** from existing primitives: toggle switch
    (rounded capsule + dot), gauge ring (arc + ticks), big-numeral
    readouts (build-time TTF subset / vendored stb runtime text —
    no external library). Theme-token driven; no widget redesign,
    no animation system (standing non-goals).
  - **Showcase page**: a factory-console themed dashboard (trend
    chart, gauges, toggles, setpoint slider, alarm list, status
    banner) — the genre its target audience (industrial /
    instrument / kiosk) recognizes; doubles as the embedded device
    page (same source tree, real sensor data on the device).
  - **Self-benchmark panel** (app-side; simulated data on desktop,
    real data once a device port lands): FPS with a render/flush
    split, flush bytes vs full-frame bytes (the dirty-region
    selling point), a steady-state zero-allocation line, and an
    input-tape determinism verify (PIXELS MATCH) — the on-device
    proof of the deterministic-runtime positioning.

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
  `color` (→ `set_text_color`), `padding`, gap/spacing, `font-size` (where the
  GlyphProvider supports it).
- Styling via inline `style=` and an optional `<style>` block; **no** CSS
  selectors beyond tag/type and simple `#id` matching.

**Hard support boundary (documented, do-not-creep):**
- No JS or dynamic behavior — HTML/CSS only as a static declarative front-end;
  event wiring stays in C++ via `find_by_id` (same as `.ui`).
- No CSS cascade engine, no inheritance model, no pseudo-classes/elements, no
  media queries, no CSS variables, no responsive reflow beyond `N%`.
- No `position: absolute/fixed` (no offset/z-index/overlap layout).
- No `margin` in the initial version (only parent `padding`/`spacing`).
- No `overflow: scroll` (no scroll container) in the initial version.
- No CSS grid, no multi-column, no RTL/bidi.
- **`div` block semantics are deliberately simplified**: `div` maps to a
  flex container (content-measuring FlexPanel), not HTML block layout; a
  "block" div does **not** stretch to fill its parent's main axis (flex
  markup drives fill). `display: block` and `display: flex` are treated
  as equivalent in the initial core. (Waiving full block layout keeps the
  parser from growing a second layout engine; FlexPanel is the one
  layout backbone.)
- **CSS flex features beyond FlexPanel's layout()**: `justify-content`
  (main-axis end/center/space-between), `align-items`/`align-self`
  (cross-axis alignment — FlexPanel cross-axis is not stretched),
  `flex-basis`, `flex-shrink`, and `flex` min/max constraints are
  **out of scope** for the initial core. They map to FlexPanel
  enhancements, not parser work — see H-7.

**Phased follow-ups (each a later, independently-reviewable increment — add
support "a little at a time" as the boundary demands):**
- H-1. Text wrapping: certifies paragraph reflow. **Prerequisite for any real
  `<p>` page.** The `GlyphProvider` seam currently measures/draws a whole run
  at once (`measure`/`write`); a wrapping engine needs per-word/per-char
  measurement + greedy line breaking (~250 lines). This is the single
  architecture-relevant gap in the initial core.
- H-2. `BorderWidget` wrapper (~100 lines) + `border` shorthand.
- H-3. `margin` support (or spacer-widget mapping).
- H-4. `ScrollPanel` + `overflow` (moderate — needs a scroll container).
- H-5. Widened selector support (class/descendant) if a real use case demands
  it.
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
    primitives); (b) `path` (Bezier M/L/C/Q/A) + `transform`
    translate/rotate/scale (~600 lines, needs new Bezier rasterization
    primitive); (c) out-of-scope: filters, gradient defs, clipPath,
    symbol/use, animation.
  - Embedded caution: Bezier rasterization at runtime may cost more than
    pre-rendered pixel assets (asset_gen precedent) — SVG suits reusable
    UI-drawing widgets (icons, gauge faces, decoration), not
    pixel-dense assets.
- H-7. Layout alignment & flex fill (FlexPanel enhancements, not parser
  work): `justify-content` (main-axis end/center/space-between),
  `align-items`/`align-self` (cross-axis alignment — today FlexPanel does
  not stretch cross-axis), `flex-basis`/`flex-shrink`, and `flex` min/max
  constraints. Each is an additive FlexPanel parameter with a default
  preserving current behavior (left-aligned main axis, unwrapped
  cross-axis), so existing tests and `.ui` files stay green when a
  parameter lands. Sized individually; gated by a real page that needs
  them (see §0 step 6: not now).

**Cost estimate (discussion):** core version ≈ 1500–2000 lines C++ total, of
which the text-wrapping engine (H-1) is the prerequisite piece; a minimal
flex-only core without H-1 is ~800 lines and covers display-only pages. Main
risk is not code volume but **semantic drift** — users hit "why isn't this CSS
attribute supported", so the boundary above must stay explicit in the released
docs.

**Placement note:** toggle-position in-tree (a library module under an
`IMPRINT_WITH_*`-style switch per A-23 precedent) is undecided — decide when
the first shipped consumer appears. Consumer-side, not a new C-ABI surface at
this stage.

### Batch S — Render Modes: Sketch & Wireframe (Unscheduled; S-1 is execution order step 5; added 2026-09-09)

Alternative rendering modes for the same widget tree. Not new widget
classes — these are `Graphics`-layer `RenderMode` switches (~150 lines
total) that change how existing draw calls behave. A widget tree
rendered in WIREFRAME mode shows only structural bones; in SKETCH mode
it looks hand-drawn. The widget tree, dispatcher, damage tracking, and
Widget hit-testing are all unchanged.

- **S-1. WIREFRAME mode** (execution order step 5):跳过填充，只画 1px 边框和文字骨架；
  grid/spacing 可选显示。Use cases:
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
- **I-2b. Device overlay** (execution order step 5): bezel/chrome around the presented buffer
  matching target screen constraints (e.g., dual NDS 256x192 screens,
  framebuffer 320x240).

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
