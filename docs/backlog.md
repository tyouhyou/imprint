# Imprint — Backlog

> Living backlog document. Tracks active, condition-triggered, and unscheduled
> work items across architecture and product layers. Release/promotion status
> is operator state, not roadmap — it lives in the local handoff notes, not
> here.
> Architecture contracts live in [`docs/ARCHITECTURE.md`](ARCHITECTURE.md);
> API contracts live in [`docs/code-contract.md`](code-contract.md).
> Completed items are removed upon completion (A-numbering is stable, gaps
> represent finished work; history lives in `git log`).

## 1. Product & Feature Backlog

### Batch V — Visual Presentation (Active; started 2026-09-05)

Goal: close the "works but looks primitive" gap against other UI
frameworks. Principle: modern look = rasterizer primitives (imcore
Graphics) + themed demo content; widgets stay theme-token driven, no
widget redesign, no animation system.

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
- **V-3. Re-record & re-shoot**: GIF + per-platform static frames
  (win / X11 / mac), README hero layout, three-language READMEs aligned.
- **V-0. Promote `gif_encoder`** (showcase app) into `imcore/codec/gif`
  per the §2 tool-placement rule (+ battery suite); recorder glue stays
  app-side until a second user appears.

### Batch L — Layout & Text Enhancements (Unscheduled)

- **L-1. Widget-level margin/padding API**:
  - Context: Button `measure()` vs draw padding discrepancy fixed in `65087b8`. A general margin/padding model across widgets and containers remains unscheduled.
- **L-2. `.ui` alignment attributes (`halign` / `valign`)** — **externally claimed** (GitHub issue #2 assigned to @tecnolgd, 2026-09-05; do not implement here — review their PR against `docs/design-file.md` grammar when it lands):
  - Context: Declarative `.ui` alignment syntax. Currently apps use explicit `set_v_align` / `set_h_align` in application code (`f74ab48`). Good-first-issue #2 opened.
  - Review default framework alignment strategy (e.g. text centering vs top-left default).
- **L-3. `list_box rows=` declaration width trap**:
  - Context: `list_box rows=` implicit `set_size` sets undeclared width to 0 (`685c004`), requiring explicit width declarations in `.ui` files. Needs cleaner auto-width sizing behavior.

### Batch I — Tooling & Inspection (Unscheduled)

- **I-1. Hot reload for design file previewer (`apps/ui_preview`)**:
  - Watch `.ui` file changes on disk and reload in-place without restarting the previewer.
- **I-2. Target screen simulation**:
  - Desktop-hosted simulation / emulation overlay matching target screen constraints (e.g., dual NDS 256x192 screens, framebuffer 320x240).

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
