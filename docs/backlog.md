# Imprint — Backlog

> Living backlog document. Tracks active, condition-triggered, and unscheduled
> work items across architecture and product layers. Release/promotion status
> is operator state, not roadmap — it lives in the local handoff notes, not
> here.
> Architecture contracts live in [`docs/ARCHITECTURE.md`](ARCHITECTURE.md);
> API contracts live in [`docs/code-contract.md`](code-contract.md).
> Completed items are removed upon completion (A-numbering is stable, gaps
> represent finished work; history lives in `git log`).

## 0. Execution Order (2026-09-10 map; completed steps removed)

Agreed sequence — a map through the backlog, not a new state machine.
Dependency-driven: each tier unlocks what follows.

1. **Batch H core / HTML path** — whitelist landed (`docs/html-path.md`);
   remaining open sub-items below (H-2 border wrapper, H-4 ScrollPanel,
   H-10 pseudo-elements, font follow-ups).
2. **Quick wins** (<1 day each, opportunistic): **L-1** widget-level
   margin/padding API (general model; Button measure padding already fixed
   in `65087b8`), **S-2 SKETCH** (recorded, unscheduled), **I-2b per-shell
   adoption** (device_overlay math + tests landed; shells paint natively —
   per-shell adoption needs maintainer eyes).
3. **Explicitly NOT now**: F-1/F-2, I-1, V-4, A-4/A-21/A-23, D-*,
   Batch G. Condition-triggered items stay trigger-gated.

### Batch V — Visual Presentation (open remainder only)

Goal: close the "works but looks primitive" gap against other UI
frameworks. Completed rasterizer/showcase/gif/dashboard work: see `git log`.

- **V-4. codec: memory-source image decode** (condition-triggered):
  `Image::read_png*` accepts only file names; embedded targets shipping
  compressed art need `read_png_memory(bytes, n, ...)` (stb offers
  `stbi_load_from_memory`). Gated together with the USE_PNG
  default-OFF question — decide both when the first real
  compressed-asset use case appears; until then the procedural
  generator covers the demo.

### Batch L — Layout & Text Enhancements (Unscheduled)

- **L-1. Widget-level margin/padding API** (quick win later):
  - Context: Button `measure()` vs draw padding discrepancy fixed in `65087b8`. A general margin/padding model across widgets and containers remains unscheduled.
- **L-2. `.ui` alignment attributes (`halign` / `valign`)** — **externally claimed** (GitHub issue #2 assigned to @tecnolgd, 2026-09-05; do not implement here — review their PR against `docs/design-file.md` grammar when it lands):
  - Context: Declarative `.ui` alignment syntax. Currently apps use explicit `set_v_align` / `set_h_align` in application code (`f74ab48`). Good-first-issue #2 opened.
  - Review default framework alignment strategy (e.g. text centering vs top-left default).
- **L-3. `list_box rows=` declaration width trap** (execution order step 5):
  - Context: `list_box rows=` implicit `set_size` sets undeclared width to 0 (`685c004`), requiring explicit width declarations in `.ui` files. Needs cleaner auto-width sizing behavior.

### Batch H — HTML/CSS Rendering Path (Medium-high priority)

Goal: an optional declarative smooth-path that renders an HTML/CSS **subset**
(no JS) through the existing widget tree, complementing the `.ui` design file.
Rationale: lets users with existing HTML/CSS authoring patterns describe
screens without learning the `.ui` grammar or the C++ builder API. No JS, no
CSS cascade engine — a deliberately narrow declarative front-end onto widgets.

**Dependency:** A-24 (Widget custom hit-test / value binding) landed
2026-09-10. Without it, the HTML parser can only map
elements to rectangular widgets (Panel/FlexPanel/Label/Button/Checkbox/
Radio). With A-24, the parser gains `<gauge>`, `<knob>`, `<trend>`,
`<meter>` as first-class custom elements — the Widget subclass handles
its own rendering and interaction, the parser only declares the element
and its style properties.

**Hard support boundary — the whitelist is the contract (rewritten 2026-09-12).**
The element/attribute/CSS-property whitelists in `docs/html-path.md` are
the single source of the boundary: **anything not in the table constructs
nothing** (LW + content dropped) — no blacklist to maintain. This doc holds
only the genuinely behavioral decisions, which are layout-engine scope, not
parser scope:

- **`div` block semantics are deliberately simplified**: `div` maps to a
  flex container (content-measuring FlexPanel), not HTML block layout; a
  "block" div does **not** stretch to fill its parent's main axis (flex
  markup drives fill). See **`docs/html-path.md`** for `display: block` /
  `display: flex` (`block` keeps `column`; bare `flex` selects `row` —
  not treated as equivalent).
- **No second layout engine**: no `position: absolute/fixed`
  (no offset/z-index/overlap — abs/relative landed as P-3 for the HTML
  path's own model), no `overflow: scroll` (H-4), no grid /
  multi-column / RTL / bidi — each is a container/scroller enhancement,
  not parser work.
- **CSS flex features**: `justify-content`, `align-items`/`align-self`,
  `flex-basis`, `flex-shrink`, and `flex` min/max constraints map to
  FlexPanel enhancements — see H-7 (partially landed).

**Status (2026-09-12):** core work in progress, contract landed first:
`docs/html-path.md` (the whitelist single source) added; shared
`background`/`color` properties added to `docs/design-file.md` /
`docs/code-contract.md` §4; `parse_html` recorded in ARCHITECTURE
§2/§4.10. Work steps: (1) contract docs — (2) property tables — (3)
parser + tests — (4) preview consumer + NDS cross-compile.

**Open / condition-triggered sub-items:**
- H-1. Text wrapping: certifies paragraph reflow. **Prerequisite for any real
  `<p>` page.** The `GlyphProvider` seam currently measures/draws a whole run
  at once (`measure`/`write`); a wrapping engine needs per-word/per-char
  measurement + greedy line breaking (~250 lines). This is the single
  architecture-relevant gap in the initial core.
- H-2. `BorderWidget` wrapper (~100 lines) + `border` shorthand.
- H-4. `ScrollPanel` + `overflow` (moderate — needs a scroll container).
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
    translate/rotate/scale (~600 lines, parse-time flattening already
    covers path for the HTML path — see `docs/html-path.md`; widget
    DrawCommand form may still want its own flattener); (c) out-of-scope:
    filters, gradient defs, clipPath, symbol/use, animation.
  - Embedded caution: Bezier rasterization at runtime may cost more than
    pre-rendered pixel assets (asset_gen precedent) — SVG suits reusable
    UI-drawing widgets (icons, gauge faces, decoration), not
    pixel-dense assets.
- H-7. Layout alignment & flex fill (FlexPanel enhancements, not parser
  work): `justify-content` (main-axis end/center/space-between/
  space-around — around added for the knob-row; H-7a),
  `align-items`/`align-self` (cross-axis alignment), `flex-basis`/`flex-shrink`,
  and `flex` min/max constraints. Each is an additive FlexPanel parameter with a default
  preserving current behavior (left-aligned main axis, unwrapped
  cross-axis), so existing tests and `.ui` files stay green when a
  parameter lands. Sized individually; gated by a real page that needs
  them.
  H-7a landed 2026-09-13: `justify-content` start/center/end/
  space-between/space-around.
  H-7b landed 2026-09-13: `align-items` (container default, start keeps
  history) + `align-self` (auto inherits).
  H-7 A+B landed 2026-09-14: flex `body` root + HTML container default
  `stretch`. `min-height:100vh` itself stays unsupported (no viewport units).
- H-9. Convergent layout passes — **landed 2026-09-13**: `layout()`
  re-runs its pass while a child size/position/measure changed (bound 3;
  contract §7). Dial *visibility* still needs paint + positioning follow-ups.
- P-1. Paint dressing — **landed 2026-09-13**: `background` shorthand
  (solid/`rgb()/rgba()` + linear 2-end + radial circle), `border: Npx solid`,
  `border-radius: Npx|50%`, Widget `paint_dress`. Remainder → P-2.
- P-2. Paint remainder (gated by a real page): follow-ups that still
  matter: document-width roots are not centered by UiPreview (amp runs
  full-bleed); body gradient backgrounds have no `html_page` carrier
  (H-8 holds colors only); GIF review captures band smooth ramps (216-cube) —
  review from the raw framebuffer. Outer box-shadow paints under the box
  but stays invisible on opaque boxes (no overdraw — bulb/LED glow + amp
  drop recorded as follow-up).
- P-3. Absolute positioning — **landed 2026-09-13**: `position:
  relative/absolute` + `top/left/right/bottom` + `transform: translate()`.
- H-8. Page-level box (landed 2026-09-12): the `<body>` style
  feeds an `html_page`; `parse_html(text, ok, page)` fills it.
- H-10. Pseudo-element correspondence (raised 2026-09-16): `::before` /
  `::after` (model500 `.knob` pointer tick is the driving case — currently
  inert per the `docs/html-path.md` tolerance table). Narrow subset only:
  static box/line content on the originating element, no dynamic behavior;
  contract change grows the whitelist when it lands.

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

### Deferred this round (recorded, not blocking)

- **B6 NDS tictactoe dialog buttons**: evidence chain in `CONTEXT.md`
  环境注意 — diagnose with `LOGGING_DEBUG=ON` before changing dispatcher
  two-click/slop policy (contract-first if the policy changes).
- **B10-style constant C-ABI exports** (`zb_buffer_bpp` etc.): optional
  `try/catch` for file-wide no-exception-crossing style consistency;
  low priority, no throw path today.

### Unscheduled Design Debt (Batch K Triage)

- **D-1**: `FlexPanel` min/max size constraints.
- **D-3**: Focus navigation history.
- **D-4**: `Event` once-handler and priority handlers.
- **D-9**: Centralized resource management.
