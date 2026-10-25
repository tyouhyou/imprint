# Imprint — Backlog

> Living backlog document. Tracks active, condition-triggered, and unscheduled
> work items across architecture and product layers. Release/promotion status
> is operator state, not roadmap — it lives in the local handoff notes, not
> here.
> Architecture contracts live in [`docs/ARCHITECTURE.md`](ARCHITECTURE.md);
> API contracts live in [`docs/code-contract.md`](code-contract.md).
> Completed items are removed upon completion (A-numbering is stable, gaps
> represent finished work; history lives in `git log`).

## 0. Execution Order (2026-09-10 map; completed steps removed; A-26 added 2026-10-03)

Agreed sequence — a map through the backlog, not a new state machine.
Dependency-driven: each tier unlocks what follows.

1. **A-29. fps consumption-report absorption** — R1 (zero-copy
   `draw_surface` alias) first, then R6 (deterministic frame contract),
   R8 (input deltas), then re-evaluate R12/R13 against the absorbed
   `imraster` module; each item contract-first (§1 A-29).
2. **A-26. imcore primitive throughput** — the open item is ② (SIMD
   inner loops); its decision point is a 1080p-class consumer
   re-measure against the R1 zero-copy build. fps is paused (2026-10-24
   ruling); when it resumes it rebuilds against current imprint anyway
   (the deleted-`bind_actions` migration folds into that), and any
   1080p wasm consumer can carry the measurement. Method: the fps A/B
   discipline (interleaved, rotating arm order, min-of-N, null A/B for
   host resolution — fps IMPRINT-RUNTIME R15/R18), never
   `cpu.pressure`.
3. **Explicitly NOT now**: F-1/F-2, I-1, V-4, A-4/A-21, D-*, A-27, and
   Batch W (audit follow-ups, decision-gated). Condition-triggered
   items stay trigger-gated. Batch G's P1–P4 roadmap is landed
   (2026-09-26 through 2026-10-01; history in `git log`).
3. **Batch X** (2026-10-15 independent audit): all items landed
   (X-20 2026-10-25: `-Wconversion` / `-Wsign-conversion` triage —
   308 findings fixed or proven benign and both flags added to the X-8
   set in `cmake/imprint-warnings.cmake`; the MSVC `/wd4244` family
   suppression removed).
   X-7 and X-9 landed (2026-10-19: a real X11 loop under Xvfb in CI, and
   one shared 64-bit geometry helper); **X-8 landed the same day** (a
   per-compiler warning set with `-Werror` in-tree, clean across the
   whole build matrix; `cmake/imprint-warnings.cmake` is its home), and
   **X-21 landed the same day** (the mingw-w64 cross probe that puts the
   Windows-only shell TUs under that set: `win-probe` locally, the
   `mingw-probe` CI job, plus the failure-only MSVC warning inventory in
   `windows-desktop`). **X-10 and X-11 landed 2026-10-20**: the 16bpp
   presenter refusal is now symmetric across the desktop shells and the
   fb fast path compares channel descriptors instead of byte counts
   (`core::native_panel_layout()`), and the Python host decodes abgr1555
   with a check that takes its ground truth from the pixel traits.
   **X-12 landed 2026-10-24**: a per-declaration 64 KiB var() expansion
   budget plus cycle rejection, 1024-command / 4096-point path caps, and
   a 64-byte CSI accumulation bound. **X-13 landed the same day**: the
   sixel register-budget diagnostic latches per frame instead of firing
   once per dropped pixel, and the header/ARCHITECTURE §5 now state the
   real drop behavior instead of the old nearest-color promise.
   **X-14 landed the same day**: SOVERSION on both shared libraries, a
   project version that no longer trails the released tags, an installed
   LICENSE, and a pkg-config recipe — all asserted by the installed
   smoke. **X-15 landed the same day**: the chrome-bar fill and the
   painted→present region read move through two shared seams
   (`for_each_bar`, `painted_region`), and the U-1 key-decoration rule
   has one home (`decorate_printable_key`) that every C++ translator
   calls instead of re-deriving it; the §3.1 "~100–200 lines of glue"
   claim is now backed by the seams rather than aspirational.
   **X-16 landed the same day**: the container/accessibility gaps and
   the design-file image gap are stated as boundaries in ARCHITECTURE
   §5 instead of being left for a reader to discover.
   **X-17 landed the same day**: `subscribe()` is `[[nodiscard]`, the
   pixel word no longer puns through a union (memcpy of the object
   representation), the dev container's rebuildable files are committed
   so a clone can reproduce the environment, all three READMEs carry a
   CI badge, and CONTRIBUTING / SECURITY / CHANGELOG exist.
   **X-18 landed the same day**: every SVG rasterizer walk is
   intersected with the draw area at the entry, so a design file
   declaring a huge stroke width or radius walks the surface, not the
   declared geometry; `walk_pixel_count()` is the deterministic cost
   bound the battery asserts on. **X-19 landed the same day**: the
   whole first-party tree is formatted to `.clang-format`
   (clang-format 18.1.3, one mechanical commit, renders byte-identical
   before and after), and `tools/format-check.sh` gates it with the
   version pinned so a runner shipping a newer clang-format cannot
   rewrite the tree on its own. Nothing in the batch reopens a
   ratified ruling; sequencing stays the operator's call.

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

### Batch H — HTML/CSS Rendering Path (landed end to end; open remainder only)

An optional declarative smooth-path that renders an HTML/CSS **subset**
(no JS) through the existing widget tree, complementing the `.ui` design
file. Landed end to end — whitelist contract, parser + battery, preview
consumer, `showcase_html` on desktop / NDS / wasm (history in `git log`).

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
  path's own model), no grid /
  multi-column / RTL / bidi — each is a container/scroller enhancement,
  not parser work.
- **CSS flex features**: `justify-content`, `align-items`/`align-self`,
  `flex-basis`, `flex-shrink`, and `flex` min/max constraints map to
  FlexPanel enhancements — landed (H-7, D-1; history in `git log`).
- **`border-bottom` is not whitelisted** (only `border-top` is). A header
  underline therefore has to be drawn as `border-top` on the *following*
  element. Adding the real property needs a second band slot in the
  widget ext struct (`bord_t` today holds one) plus the draw path and its
  16bpp/32bpp variants — widget-side work, not parser work. Found
  2026-10-10 when the build-time gate went live; the showcase design file
  works around it.
- **`flex` grow must be an integer** (`parse_int_value`): `flex:1.2` warns
  and leaves the node's default, which *silently starves the panel*
  rather than approximating. The design file shipped two of these
  (`flex:1.2`, `flex:1.3`) unnoticed. Decide: keep integer-only but make
  the fallback the inherited value (or `flex:1`) instead of the node
  default, or accept fractional grow.

**Placement note (decided 2026-09-12):** no `IMPRINT_WITH_*` switch needed —
per the A-23 precedent the `html` translation unit stays in imui (STATIC);
unreferenced on targets that don't use it, dropped by the static linker at
image build. Consumer-side, not a new C-ABI surface at this stage.

**Open / condition-triggered sub-items:**
- P-2. Paint remainder (gated by a real page): follow-ups that still
  matter: document-width roots are not centered by UiPreview (amp runs
  full-bleed); body gradient backgrounds have no `html_page` carrier
  (H-8 holds colors only); GIF review captures band smooth ramps (216-cube) —
  review from the raw framebuffer.

### Batch I — Tooling & Inspection (Unscheduled)

- **I-1. Hot reload for design file previewer (`apps/ui_preview`)**:
  - Watch `.ui` file changes on disk and reload in-place without restarting the previewer.

### Batch W — Full-repo Audit Deferred Items (added 2026-10-03; decision-gated, not scheduled)

Follow-ups from the 2026-10-03 full-repo audit (`reports/review-1.md`,
local-only). Verified real, deferred by operator ruling; each entry states
the decision it is waiting on. **A missing number is a fixed item** —
history in `git log`.

- **W-1 (IM-RAS-002). Arc shape depends on alpha**: a translucent arc
  (0<a<0xFF) takes the exact-chord octant scan, so its silhouette changes
  with alpha. Fixing means one rasterizer for all alphas — a rendering
  change that re-baselines every arc golden. Waiting on: accept as
  documented deviation vs. unify.
- **W-2 (IM-IN-002). `ZB_INPUT_MOUSE_*_CLICK` are advertised but the
  dispatcher silently drops them**. Waiting on: document as inert host-side
  synonyms vs. remove from `zbapi.h` (C-ABI surface semantics).
- **W-3 (IM-IN-005). Terminal translator routes Space only through `ch`,
  so Space can never activate the focused widget on the SIXEL target**.
  The fix is a translator behavior change needing a fresh NDS/SIXEL
  verification pass; bundle with the next melonDS re-verify.
- **W-7 (IM-BLD-007). `demo/wasm/build.sh` hand-maintains its source
  list** (stale vs. `imui/CMakeLists.txt`, different font-subset inputs).
  Currently sufficient (the smoke passes); regenerating the list needs a
  maintain-vs-generate decision.
- **W-8 (IM-BLD-008). No symbol-visibility control** —
  `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS` exports the whole imcore surface,
  not just the `zbapi.h` stable ABI. Waiting on: a visibility policy
  (export macros vs. `--exclude-libs` vs. accept).
- **W-9 (IM-BLD-009). No `CMAKE_MSVC_RUNTIME_LIBRARY` pin; the
  installed-package smoke is Linux-only** — the Windows install path
  (classic LNK2038) is unverified. Needs a Windows CI runner decision.
- **W-10 (IM-BLD-011). The ASan job does not instrument the
  shared-library link line.** Waiting on: a CI touch to add the
  shared-link sanitizer flags.
- **W-11 (IM-BLD-013). `asset_gen` is built by every configuration and
  consumed by nothing; committed PNG/GIF assets have no documented
  regeneration path.** Waiting on: wire asset_gen, drop it, or document
  the regen recipe.
- **W-12 (IM-DOC-004). §8's alloc exemption is premised on "the battery
  runs Debug"** — no Linux CI job sets a build type, so `DEBUG` is
  undefined there. Waiting on: add `-DCMAKE_BUILD_TYPE=Debug` to the
  Linux jobs vs. amend §8's premise.
- **W-14 (IM-HOST-003 remainder). Shell/ABI hardening, five sub-items**:
  `run_win` assigns
  global `g_app` before a throwing `CreateWindowEx`; the C-ABI
  `catch(...)` handlers log via the allocating `LE` path (OOM can throw
  out of `extern "C"`); no DPI
  awareness on win/mac; `maps_pointer()` omits `touch_*`; no C-ABI
  re-entrancy guard. Each is small; the set needs a hardening-vs-accept
  ruling (some, like allocating loggers, are the documented design).
  (The sixth — "shell `main`s catch only `zb::ui::error`" — landed with
  X-5, 2026-10-17: every framework-mode `main` now runs through
  `zb::shell::guarded_main`.)

### Batch X — Independent Audit (2026-10-15; audit-ordered, mostly decision-free)

Origin: a full-repo independent audit at `efe684a`, verified by one clean
`desktop-test` run of its own (60/60 suites, 361 s) — so every item below
sits outside what the current battery covers or exercises. Detail ledger:
`reports/audit-2026-10-15.md` (local-only, the same convention as Batch W),
which also carries the audit's strengths list, the positioning/demo
proposals and one *withdrawn* finding. Ordered by the audit's recommended
priority; no item reopens a ratified ruling.

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

### Batch G — Positioning & Go-to-Market (active roadmap; unfrozen 2026-09-26)

Origin: a third-party commercial review plus the maintainer assessment
of 2026-09-06 (deliberately deferred then — technical validation first).
Unfrozen 2026-09-26 after a positioning brainstorm (third-party advisory
fragments + a full-code re-read); the rulings below supersede/extend the
09-06 record, whose discussion history lives in `git log`.

**Standing rulings (2026-09-06, unchanged):**

- Lead with the already-delivered differentiator — a deterministic,
  pixel-testable UI runtime — not "another cross-platform C++ GUI
  framework"; do not lead with "embedded" (NDS is entertainment-class;
  the embedded claim stays partially earned until a real MCU-tier
  footprint exists). Executed at README level 2026-09-22 (three
  languages).
- First revenue path: per-target port engagements (display / input /
  font glue for a customer's board); SDK/enterprise licensing and any
  designer product are later-stage (a designer would reverse the
  "no drag-drop designer" ruling).
- Rejected directions (with reasons): Figma import (free-form canvas →
  constraint-layout mapping is unmaintainable; the viable variant is
  LLM-generated `.ui` files, which the grammar already supports);
  "AI-agent-friendly runtime" as a *commercial wedge* (circular —
  agents drive pre-existing GUIs); hardware-SDK-vendor sales at the
  validation stage.
- Standing constraint: single-maintainer bus factor outweighs star
  count; the first external committer matters more than stars.

**2026-09-26 rulings (positioning brainstorm, user-ratified):**

- Positioning confirmed: "embeddable deterministic UI runtime". Moat
  order (hardest to copy first): determinism-as-contract → zero-
  dependency + C ABI → HTML-subset design file (designer-friendly and
  inherently sandboxed) → any-target portability (NDS as the proof) →
  compile-time pixel model. Do not enter the widget-armament race
  (the LVGL / Slint / Qt dimensions: more widgets, faster, prettier).
- Roadmap P1–P4 — landed 2026-09-26 through 2026-10-01 (external
  consumability incl. P1.5 install/export, deterministic-test story,
  declarative plugin protocol, SIXEL terminal target; contracts in
  ARCHITECTURE §3.2/§4.8/§4.10/§11.3 and code-contract §4/§12; history
  in `git log`). Open remainders:
  - *P3.1 (condition-triggered): typed state read/write through the
    ABI when a real host needs it — the native per-widget events
    already carry the payloads for C++ consumers.*
  - *P4 extension (unscheduled): the Kitty graphics protocol as a
    second presenter for the same input source.*
- Business stance reconfirmed: port engagements first; external
  consumability is itself the revenue enabler. The 09-06 landing page +
  "tell us about your device" intake is closed: the portal at
  `tyouhyou.github.io/imprint/` is the landing page, and lead handling
  is operator state, not roadmap.

**2026-10-15 audit proposals (unratified — operator decides).** The
independent audit's shareability findings, product-side only; promotion and
distribution stay operator state per this doc's header. Evidence and
rationale in `reports/audit-2026-10-15.md` §5.

- *G-a.* An `imprint-agent` MCP server over the existing design-file path
  (render → frame hash/PNG → pixel assert → baseline diff) plus a recording
  of an agent fixing its own layout from the render — turns the README's
  "Made for AI agents" claim into a runnable artifact.
- *G-b.* A six-target byte-identical CI montage plus a determinism badge:
  one action renders the same design file on Linux/FB/mac/Win/WASM/SIXEL/NDS
  and byte-compares — the shortest path from the slogan to visible evidence.
  Adjacent to "one seed, three screens" and the SIXEL recording in §1's
  deferred list.
- *G-c.* README first screen: a GIF in the first three lines (the current
  static hero reads as a mockup until two paragraphs in) and a 30-second
  quickstart that does not need libx11-dev — sequence after X-3.
- *G-d.* A ~200-line port template plus one real board shot end to end —
  the missing proof for the "embedded" tag this batch itself marks as
  partially earned.

## 1. Architecture Backlog

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

### A-25. Host-shaped glyph feed (text "Level 2", condition-triggered)

- The `GlyphProvider` contract is per-`char16_t` (no cluster/advance
  model), so a host cannot feed pre-shaped text (HarfBuzz-grade
  shaping, dynamic complex-script text, emoji) through any seam, and
  the C ABI has no glyph exports at all. Static label text is covered
  by the build-time subset (`tools/font_subset.py` + `ttf_subset`);
  desktop dynamic text by `USE_TTF_RUNTIME`.
- **What is deferred:** extending the glyph contract with a
  cluster/advance (or host glyph-feed) shape — contract-first, touches
  code-contract §2.4 and possibly ARCHITECTURE §4.8.
- **Trigger:** a real embedding needs dynamic complex-script text the
  subset + runtime-TTF paths cannot serve. IME and RTL themselves stay
  declared non-goals (the host owns them).

### A-26. imcore primitive throughput (② SIMD; gated on the fps re-measure)

- **What remains for ② SIMD:** the no-clip remainder is the per-pixel
  blend/modulate arithmetic itself (fill 5.2 ms, `draw_image` 6.9 ms,
  tinted 13.1 ms per half/full-screen unit) — exactly the SIMD
  territory. wasm simd128 first (where 1080p consumers run), then
  desktop SSE2/NEON. **Trigger:** a 1080p-class consumer re-measure
  against the R1 zero-copy build (fps paused 2026-10-24; the method is
  the fps A/B discipline, fps IMPRINT-RUNTIME R15/R18 — see §0 item 2).
- **Deferred within ①:** diagonal `draw_line` gate hoisting — the
  Bresenham walk is inherently per-pixel (15 ms per 1920 lines locally)
  and not the fps bottleneck; revisit only if fps evidence says
  otherwise.
- **Hard constraints (unchanged for ②):** byte-identical determinism —
  every step keeps `zb::snap` `framebuffer_hash` / CI byte-compare
  green **and** passes the 16bpp battery (`.ai/bin/verify.sh
  desktop-test-16`; SIMD rewrites are the classic way to break the
  `plot_aa` quantization rule). Hot paths stay integer
  (8-bit-normalized accessors, no floating point).
- Tile-threaded rasterization stays a declared non-goal (README,
  ARCHITECTURE §5, CONTEXT ruling): internal workers would not break
  determinism (disjoint tiles, integer math) but would re-open three
  ruling texts, need SharedArrayBuffer + COOP/COEP on wasm and a
  compile-out on NDS. Re-evaluate only if ② still misses the native
  1080p frame budget.

### A-27. C-ABI present-region export (`zb_present_region`) (condition-triggered)

- `zb_buffer` (`binding/include/zbapi.h`) exposes the whole frame only,
  so every zbapi host presents full-buffer; the A-2 seams
  (`region_to_present`, `dirty_coalescer` in
  `imshell/include/shell/presenter.hpp`) already compute the present
  rect internally — only the ABI hop is missing.
- **Shape:** contract-first (code-contract §9 + ARCHITECTURE §4.8
  first, then `zbapi.h`): export the current frame's present rect so
  hosts copy/upload only the changed region — the same seam that would
  carry a persistent GPU-texture upload ("GPU-accelerated
  presentation", the permitted form per the ARCHITECTURE §5 ruling).
- Zero benefit for fps (full-frame game repaint; source-level
  composition that does not use zbapi).
- **Trigger:** a zbapi host's present path becomes a measured
  bottleneck (wasm story demo, Python, a future Kitty presenter), or
  the wasm demo wants region-wise upload.

### A-28. Feature inventory & per-target minimal builds (on-demand compilation / feature gating; added 2026-10-25)

- **Motivation:** everything beyond the core raster/kernel is real but
  unenumerated — the HTML/CSS path (`html` TU + `html_embed`), the `.ui`
  design-file path (`ui_builder`/`ui_file` + `ui_embed`), the SVG canvas,
  the stb image codecs, runtime TTF (`USE_TTF_RUNTIME`), the platform font
  (`USE_FONT_SIZE`), the shell backends, the C-ABI binding, the host tools.
  Their controls are scattered: `USE_*` options declared inline in
  `imcore/CMakeLists.txt`, `IMPRINT_WITH_*` bundles in the root CMakeLists,
  FORCE-set entries in `cmake/nds.toolchain.cmake`, and the rest leans on
  the A-23 static dead-strip. A target integrator has no single place that
  says what exists, what depends on what, and how to leave it out. First
  concrete want: an NDS story that ships `.ui`-only (no HTML) — runtime
  HTML→UI generation is slow on the ARM9.
- **Do not re-litigate the A-23 placement note (2026-09-12):** for
  statically linked targets the linker already drops an unreferenced `html`
  TU, so consumer-side size trimming works today. What is missing is
  discoverability, declared dependencies, and gates where dead-stripping
  cannot apply (SHARED imcore / `zbapi` consumers — W-8 adjacent).
- **Shape, three layers:**
  1. *Inventory (decision-free, do first):* one list of every post-core
     feature — source closure, intra-feature dependencies (a small DAG),
     C-ABI/export implications, droppable-by-static-linker vs.
     needs-a-build-gate, and a measured size contribution (ROM / `.wasm` /
     install — evidence, not estimates). Home: the ARCHITECTURE §4.9
     compile-time option table, extended; options touching the C-ABI or
     pixel matrix state their why in code-contract.md per the standing
     rule. The USE_PNG default-OFF question (V-4) becomes a row instead of
     an open thread.
  2. *Build-level gating:* choose the mechanism — per-feature CMake options
     following the existing pattern (default OFF on desktop, FORCE ON in
     the embedded toolchain; ARCHITECTURE §4.9 updated per option) vs.
     staying dead-strip-only with the inventory as the map. If options
     win, `cmake/imprint-features.cmake` is their single home, mirroring
     `cmake/imprint-warnings.cmake` (X-8 precedent); no code-level `#if`
     sprawl beyond the existing define pattern (`IMCORE_HAS_TTF_RUNTIME`).
  3. *Story/target-level selection:* the `${STORY}` composition and the
     `<story>_app` target declare the feature closure they need, so the
     same sources yield an NDS `.ui`-only story and a desktop
     full-feature story.
- **Constraints:** contract-before-API — a dropped feature must not
  silently change the `zbapi.h` surface (A-1's wording-sync discipline);
  every feature-off configuration must compile clean under the X-8 warning
  set and have a battery story (extend the `warnings-survey` config
  matrix; never trust one local build); compile-time only — no runtime
  backend switching (settled non-goal).
- **Trigger:** the inventory can start whenever the integrator need
  arises (the NDS HTML slowness is already on record); the
  gating-mechanism decision waits for a second target or a measured
  size/runtime win.

### A-29. fps consumption-report absorption (Phase 2; added 2026-10-25)

- Source: the fps repo's consumer reports (`docs/IMPRINT-RUNTIME.md`
  R-items; `docs/IMPRINT-3D.md` supply list). **Phase 1 landed with the
  `imraster` module** (ARCHITECTURE §4.12) — that closes R20. The other
  supply items (texture/mip chain, tone/Atmosphere, WorldMesh cache,
  asset pipeline) stay documented as fps-side pointers, not absorbed —
  no imprint consumer today. **R7 is already closed**: the U-6 depth
  plane (`draw_line_depth`/`fill_triangle_depth`, `test_depth_fill`)
  postdates the fps report's pinned snapshot (797878b).
- Agreed order (2026-10-25 user ruling), each item contract-first and
  independently verifiable:
  1. **R1 zero-copy alias** — `draw_surface` (U-8, code-contract §9)
     copies the whole frame (~8.29 MB at 1080p, ~1.56 ms, pure-memcpy
     speed): let the consumer alias its buffer into the lib or hand a
     "unchanged this frame" hint; U-8 semantics unchanged, damage
     accounting unchanged. fps measured it as the largest single
     recoverable item in a 1080p budget.
  2. **R6 deterministic frame contract** — a host-injectable fixed step
     or a "repaint without advancing" call. Today pixel-regression
     consumers can only pin a sim-frozen frame (fps's goldens live on
     the boot frame for exactly this reason).
  3. **R8 input_event deltas** — relative pointer motion + stable
     platform-independent codes for printable keys (down/up pairing;
     "hold to move" needs it). POD shape change = architecture change
     (ARCHITECTURE §4.2): all shells + both demo hosts move together.
  4. **Re-evaluate R12/R13 against the absorbed module** — imraster's
     `Canvas` already ships the consumer-side answer (a full-resolution
     surface beside the low-res grid; `mark_sky` coverage + per-column
     wall spans, maintained by recording instead of clearing). Decide
     whether runtime-side primitives (a full-res composite callback
     between scene resolve and UI; a coverage query) are still wanted,
     or the items close as "solved consumer-side".
- Not absorbed, deliberately: audio (no imprint facility — product
  decision if ever), fps sim/UI/level content (load-side meat, not
  library), fps-side unratified art rulings (C12/C14, thin walls — they
  stay in fps's backlog). fps is paused (2026-10-24): its verification
  matrix re-runs when it resumes; closure of each R item follows its
  channel discipline (`IMPRINT-RUNTIME.md` moves closed items to its
  "已闭环" table once fps verifies against an imprint snapshot that has
  the change).

### Deferred this round (recorded, not blocking)

- **B10-style constant C-ABI exports** (`zb_buffer_bpp` etc.): optional
  `try/catch` for file-wide no-exception-crossing style consistency;
  low priority, no throw path today.
- **"One seed, three screens"** (from the 2026-10-03 gh-pages portal
  round): the seedmap page's map rendered side by side from the same
  seed on wasm + NDS + desktop, as a living determinism proof. The
  generator is already byte-deterministic (locked by the seedmap smoke);
  what remains is the NDS capture and the portal wiring.
- **SIXEL terminal live recording**: a GIF of a P4-terminal shell
  session recorded via `script(1)` pty, hosted on the portal next to
  the wasm pages — shows the Linux SIXEL backend without a terminal
  emulator dependency. Needs a recording pass, no framework change.

### Unscheduled Design Debt (Batch K Triage)

- **D-3**: Focus navigation history.
- **D-4**: `Event` once-handler and priority handlers.
- **D-9**: Centralized resource management.
