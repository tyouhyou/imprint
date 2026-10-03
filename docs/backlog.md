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

1. **A-26. imcore primitive throughput** — step ① (scalar word-level
   bulk paths) landed 2026-10-03; next decision point is the fps
   re-measure (fps first migrates to `build(host, node, sink)`), which
   decides whether ② (SIMD inner loops) starts.
2. **Explicitly NOT now**: F-1/F-2, I-1, V-4, A-4/A-21, D-*, A-27, and
   Batch W (audit follow-ups, decision-gated). Condition-triggered
   items stay trigger-gated. Batch G's P1–P4 roadmap is landed
   (2026-09-26 through 2026-10-01; history in `git log`).

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
  consumability is itself the revenue enabler.
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

### A-26. imcore primitive throughput (step ① landed 2026-10-03; ② gated on re-measure)

- **Step ① landed — scalar word-level bulk paths, byte-identical:**
  translucent `fill_rect` rows and non-opaque horizontal lines route
  through a clamped `fill_span_blend` (the U-7 gate-as-interval shape
  with the `alpha_blend` mix inlined; sketch wobble and the
  single-column double blend stay pinned on the old paths), and
  `draw_image` (plain + tinted) clamps surface/draw-area/damage to one
  rect then walks rows (the U-8 `draw_surface` shape; contract §9
  A-12/A-13 reworded). Opaque fills were already bulk (U-7). Local
  Release bench (1920×1080, 32bpp): translucent fill with damage on
  38.3 → 2.0 ms, under a widget clip 35.0 → 2.0 ms; `draw_image`
  damage-on 12.4 → 1.6 ms, widget clip 10.0 → 2.1 ms, full-screen
  alpha-on 13.3 → 6.9 ms. Verification: `desktop-test` +
  `desktop-test-16` green, determinism smoke hash unchanged.
- **What remains for ② SIMD:** the no-clip remainder is the per-pixel
  blend/modulate arithmetic itself (fill 5.2 ms, `draw_image` 6.9 ms,
  tinted 13.1 ms per half/full-screen unit) — exactly the SIMD
  territory. wasm simd128 first (where 1080p consumers run), then
  desktop SSE2/NEON. **Trigger:** the fps re-measure (fps must first
  migrate off the deleted `bind_actions` API to
  `build(host, node, sink)`) shows the 1080p frame budget still missed.
- **Deferred within ①:** diagonal `draw_line` gate hoisting — the
  Bresenham walk is inherently per-pixel (15 ms per 1920 lines locally)
  and not the fps bottleneck; revisit only if fps evidence says
  otherwise.
- **Original evidence (fps F9, 2026-10-03):** 1920×1080 native x86:
  `fill_rect` half-screen 8.37 ms, 1920 × `draw_line` 7.62 ms,
  full-screen `draw_image` 13.94 ms vs 0.43 ms raw — the numbers that
  matched the translucent/clip arms, now largely reclaimed. Wasm
  pipeline spends 40–55 ms/frame in sim+paint at MAX pacing; present is
  already a straight `data.set` (fps `docs/DESIGN.md` D6).
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

### Deferred this round (recorded, not blocking)

- **B10-style constant C-ABI exports** (`zb_buffer_bpp` etc.): optional
  `try/catch` for file-wide no-exception-crossing style consistency;
  low priority, no throw path today.
- **"One seed, three screens"** (from the 2026-10-03 gh-pages portal
  round): the seedmap page's map rendered side by side from the same
  seed on wasm + NDS + desktop, as a living determinism proof. Blocked
  on the NDS build environment being up again; the generator itself is
  already byte-deterministic (locked by the seedmap smoke).
- **SIXEL terminal live recording**: a GIF of a P4-terminal shell
  session recorded via `script(1)` pty, hosted on the portal next to
  the wasm pages — shows the Linux SIXEL backend without a terminal
  emulator dependency. Needs a recording pass, no framework change.

### Unscheduled Design Debt (Batch K Triage)

- **D-3**: Focus navigation history.
- **D-4**: `Event` once-handler and priority handlers.
- **D-9**: Centralized resource management.
