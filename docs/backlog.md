# Imprint — Backlog

> Living backlog document. Tracks active, condition-triggered, and unscheduled
> work items across architecture and product layers. Release/promotion status
> is operator state, not roadmap — it lives in the local handoff notes, not
> here.
> Architecture contracts live in [`docs/ARCHITECTURE.md`](ARCHITECTURE.md);
> API contracts live in [`docs/code-contract.md`](code-contract.md).
> Completed items are removed upon completion (A-numbering is stable, gaps
> represent finished work; history lives in `git log`).

## 0. Execution Order (2026-09-10 map, Batch U added 2026-09-27; completed steps removed)

Agreed sequence — a map through the backlog, not a new state machine.
Dependency-driven: each tier unlocks what follows.

1. **Batch H core / HTML path** — landed end to end (whitelist +
   parser + preview consumer + three-platform showcase; H-6 closed
   2026-10-02, see below).
2. **Quick wins** (<1 day each, opportunistic): S-2, I-2b, H-9 tail,
   L-3, H-4 — landed 2026-10-02, entries removed (history in `git log`).
3. **P1 external consumability** (2026-09-26 roadmap, Batch G) — landed
   2026-09-26 (closes A-23); P1.5 install/export landed 2026-10-01.
4. **Batch U — external-consumer feedback** — landed 2026-10-01,
   complete (contracts: code-contract §9, ARCHITECTURE §4.4; history in
   `git log`).
5. **Explicitly NOT now**: F-1/F-2, I-1, V-4, A-4/A-21, D-*, and Batch W
   (audit follow-ups, decision-gated). Batch G is
   unfrozen (2026-09-26) — its P1–P4 roadmap is the active product map.
   Condition-triggered items stay trigger-gated.

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
  path's own model), no grid /
  multi-column / RTL / bidi — each is a container/scroller enhancement,
  not parser work.
- **CSS flex features**: `justify-content`, `align-items`/`align-self`,
  `flex-basis`, `flex-shrink`, and `flex` min/max constraints map to
  FlexPanel enhancements — see H-7 (partially landed).

**Status:** the batch is landed end to end — whitelist contract
(`docs/html-path.md`, the single source), property tables, `parse_html`
parser + battery, preview consumer, and the `showcase_html` story on
desktop / NDS / wasm. Completed sub-items are removed from this file
(H-1 wrapping, H-2 border, H-6 closed 2026-10-02 with the static
geometry subset + DrawPath + canvas transform, H-8 page box, H-10
pseudo-elements, P-1 paint, P-3 positioning; L-2 alignment attributes
via external PR #4). What remains is listed below.

**Open / condition-triggered sub-items:**
- P-2. Paint remainder (gated by a real page): follow-ups that still
  matter: document-width roots are not centered by UiPreview (amp runs
  full-bleed); body gradient backgrounds have no `html_page` carrier
  (H-8 holds colors only); GIF review captures band smooth ramps (216-cube) —
  review from the raw framebuffer. (Outer box-shadow on opaque boxes was
  fixed with `clip_surface_safe` — removed from this list 2026-09-26.)

**Cost estimate (discussion, 2026-09-10):** the core version ≈ 1500–2000
lines C++ estimate predates the landings and is historical; the shipped
parser is the reference. Main risk remains **semantic drift** — users hit
"why isn't this CSS attribute supported", so the whitelist in
`docs/html-path.md` is the released boundary (off-table constructs warn
and render nothing, never a wrong structure).

**Placement note (decided 2026-09-12):** no `IMPRINT_WITH_*` switch needed —
per the A-23 precedent the `html` translation unit stays in imui (STATIC);
unreferenced on targets that don't use it, dropped by the static linker at
image build. Consumer-side, not a new C-ABI surface at this stage.

### Batch I — Tooling & Inspection (Unscheduled)

- **I-1. Hot reload for design file previewer (`apps/ui_preview`)**:
  - Watch `.ui` file changes on disk and reload in-place without restarting the previewer.

### Batch W — Full-repo Audit Deferred Items (added 2026-10-03; decision-gated, not scheduled)

Follow-ups from the 2026-10-03 full-repo audit (`reports/review-1.md`,
local-only). Verified real, deferred by operator ruling; each entry states
the decision it is waiting on. Fixed the same day: the P0/P1 set, the P2
majority, and the audit's own "housekeeping" quick wins (history in
`git log`).

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
- **W-4 (IM-TXT-003). `decode_utf8_next` has no end bound** though the
  header advertises slice use — 1-byte over-read. Fix needs an end-pointer
  parameter (API shape change across text call sites).
- **W-5 (IM-TXT-004). `TtfFamilyState::size` written, never read;
  `from_memory` lacks length validation.** Small provider cleanup.
- **W-6 (IM-TXT-006). Negative glyph advances poison the
  `advance_cache_` -1 sentinel** — permanent cache miss plus a negative
  centering offset. Fix is a clamp at the provider/cache boundary;
  rendering-byte implications confined to fonts with negative advances.
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
  shared-library link line; no UBSan job exists.** Add a UBSan Tier-3
  variant and fix the shared-link sanitizers when touching CI next.
- **W-11 (IM-BLD-013). `asset_gen` is built by every configuration and
  consumed by nothing; committed PNG/GIF assets have no documented
  regeneration path.** Waiting on: wire asset_gen, drop it, or document
  the regen recipe.
- **W-12 (IM-DOC-004). §8's alloc exemption is premised on "the battery
  runs Debug"** — no Linux CI job sets a build type, so `DEBUG` is
  undefined there. Waiting on: add `-DCMAKE_BUILD_TYPE=Debug` to the
  Linux jobs vs. amend §8's premise.
- **W-13 (IM-DOC-005). Eight documentation drift items**: getting-started
  duplicates README build commands and mis-describes the showcase pages;
  ARCHITECTURE §2 omits `ScrollPanel` from the imui widget list;
  `design-file.md` root rule omits `scroll_panel`; a stale "sketch renders
  as FULL for now" comment; README's "six targets" claim names four. Pure
  doc fixes — take together in one docs pass.
- **W-14 (IM-HOST-003 remainder). Shell/ABI hardening, seven sub-items**:
  X11 `Display` leak on a late-throwing init path; `run_win` assigns
  global `g_app` before a throwing `CreateWindowEx`; the C-ABI
  `catch(...)` handlers log via the allocating `LE` path (OOM can throw
  out of `extern "C"`); shell `main`s catch only `zb::ui::error`; no DPI
  awareness on win/mac; `maps_pointer()` omits `touch_*`; no C-ABI
  re-entrancy guard. Each is small; the set needs a hardening-vs-accept
  ruling (some, like allocating loggers, are the documented design).

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
- Roadmap (ratified order; each phase unlocks the next):
  - **P1. External consumability** — landed 2026-09-26 (A-23 closed).
    P1.5 install/export — landed 2026-10-01 (first external consumer:
    fps; contract in ARCHITECTURE §3.2, locked by
    `test/installed_smoke`).
  - **P2. Deterministic-test story** — landed 2026-09-26 (contract:
    code-contract §12; the `imprint-render` CI recipe runs in Tier-1).
    A standalone drives-your-app CLI remains explicitly NOT the form;
    a WASM-app CLI variant is a later option.
  - **P3. Declarative plugin protocol** — landed 2026-09-26 (contracts:
    ARCHITECTURE §4.8/§4.10, code-contract §4; C smoke + Python demo
    drive it). *P3.1 (condition-triggered): typed state read/write
    through the ABI when a real host needs it — the native per-widget
    events already carry the payloads for C++ consumers.*
  - **P4. Terminal graphics demo target** — landed 2026-09-26 (the
    SIXEL backend, §11.3, Tier-2 compile job; the pty smoke was a
    one-time local `script(1)` verification, not a CI gate — the
    sixel/term_input logic itself is unit-tested from the battery).
    *Extension (unscheduled): the Kitty graphics protocol as a second
    presenter for the same input source.*
- Business stance reconfirmed: port engagements first; external
  consumability is itself the revenue enabler (before P1 an external
  project cannot even `add_subdirectory`).
- Still deferred from 09-06: landing page assembled from existing
  assets with a "tell us about your device" intake.

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

### Deferred this round (recorded, not blocking)

- **B10-style constant C-ABI exports** (`zb_buffer_bpp` etc.): optional
  `try/catch` for file-wide no-exception-crossing style consistency;
  low priority, no throw path today.

### Unscheduled Design Debt (Batch K Triage)

- **D-3**: Focus navigation history.
- **D-4**: `Event` once-handler and priority handlers.
- **D-9**: Centralized resource management.
