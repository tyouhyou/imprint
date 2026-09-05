# Imprint — Backlog

> Living backlog document. Tracks active, condition-triggered, and unscheduled
> work items across architecture and product layers.
> Architecture contracts live in [`docs/ARCHITECTURE.md`](ARCHITECTURE.md);
> API contracts live in [`docs/code-contract.md`](code-contract.md).
> Completed items are removed upon completion (A-numbering is stable, gaps
> represent finished work; history lives in `git log`).

## 1. Active & Promotion Work

- **Promotion & Release Follow-up (Immediate)**:
  - Promotion material and drafts: [`reports/promotion_drafts.md`](../reports/promotion_drafts.md) (Show HN, Reddit r/cpp + r/NDShomebrew, Chinese communities, awesome-cpp PR, blog draft).
  - Version v0.1 was tagged at `667a2e5`; Version v0.1.1 tagged at `da64ed6` with the following fixes packaged into release assets:
    - melonDS touch glitch debounce (`6f60658`)
    - `ttf_subset` advance calculation on glyph index (`5ccc508`)
    - GIF encoder missing GCE block terminator (`bb916e7`)
    - Button text vertical centering (`65087b8`)
    - Showcase GIF re-recorded with platform default font (`872cdc9`)
    - Backlog documentation separated into `docs/backlog.md` (`da64ed6`)
  - Status: `main` pushed, v0.1.1 Release published on GitHub (2026-09-02) with both assets. Remaining: community promotion only (priority lowered 2026-09-05). Before any promotion copy mentions macOS support, re-verify the mac shell on a real macOS 13+ machine (layer-backed path, see CONTEXT.md verify matrix).

## 2. Product & Feature Backlog

### Batch L — Layout & Text Enhancements (Unscheduled)

- **L-1. Widget-level margin/padding API**:
  - Context: Button `measure()` vs draw padding discrepancy fixed in `65087b8`. A general margin/padding model across widgets and containers remains unscheduled.
- **L-2. `.ui` alignment attributes (`halign` / `valign`)**:
  - Context: Declarative `.ui` alignment syntax. Currently apps use explicit `set_v_align` / `set_h_align` in application code (`f74ab48`). Good-first-issue #2 opened.
  - Review default framework alignment strategy (e.g. text centering vs top-left default).
- **L-3. `list_box rows=` declaration width trap**:
  - Context: `list_box rows=` implicit `set_size` sets undeclared width to 0 (`685c004`), requiring explicit width declarations in `.ui` files. Needs cleaner auto-width sizing behavior.

### Batch I — Tooling & Inspection (Unscheduled)

- **I-1. Hot reload for design file previewer (`apps/ui_preview`)**:
  - Watch `.ui` file changes on disk and reload in-place without restarting the previewer.
- **I-2. Target screen simulation**:
  - Desktop-hosted simulation / emulation overlay matching target screen constraints (e.g., dual NDS 256x192 screens, framebuffer 320x240).

### Batch F — Asynchronous Posting & Event Loop Extension (Long-term)

- **F-1. Cross-thread message posting `zb::ui::post(closure)`**:
  - Contract-first design before implementation.
  - **Invariants to preserve** (normative in [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) §4.11):
    - Single-threaded driving semantics at the core.
    - Deterministic frame ordering for automation and headless runners.
    - Observable `painted` synchronization signal.

## 3. Architecture Backlog

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
