# Changelog

All notable changes to Imprint UI are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [semantic versioning](https://semver.org/) at the
level its pre-1.0 state allows: `v0.1.x` tags were patch releases of the
0.1 line, `v0.2.0` is the second feature line.

The released tags are summarized below; the full history lives in
`git log`.

## [Unreleased]

## [0.2.0] — 2026-10-28

The first feature release after v0.1.1 (424 commits). The framework now
ships what it demonstrates — an optional 3D rasterizer, a terminal
target, a library-mode host loop, snapshot baselines and runtime TTF
text — and the package is consumable from a foreign CMake project
(`find_package(imprint 0.2)`, `imprint::` targets).

### Added
- **`imraster`, an optional software 3D core** (`IMPRINT_WITH_IMRASTER`,
  default OFF; rgba32/le only, other pixel models rejected at configure
  time). `Canvas` / `Raster` / `MeshBuf` with tone injected by the
  caller: the module owns no look and knows no entity. Three stories are
  built on it (`moonpull`, `fogkeeper`, `hoursmith`). Contract:
  `docs/ARCHITECTURE.md` §4.12.
- **A terminal target** (`-DIM_SHELL_BACKEND=SIXEL`): the same UI drawn
  into a sixel terminal, SGR mouse and key input, `IM_TERM_SIZE=WxH`.
- **Library-mode host loop** (`zb::shell::run`, `docs/code-contract.md`
  §11): a host that owns its event loop hands the shell an `IApp` instead
  of linking a `main()`; the loop resets the module globals it touches,
  so a second call starts clean.
- **Snapshot baselines** (`zb::snap`, `imprint::snapshot`): `check()` /
  `write()` against a baseline directory plus a single-frame GIF dump —
  the headless "draw it, assert it, look at it" workflow in one header.
- **Runtime TTF text** (`USE_TTF_RUNTIME`): proportional platform
  typography on desktop, WASM and NDS, with the 5x7 bitmap fallback kept
  for builds without it.
- **New widgets**: `GaugeDial`, `Knob`, `TrendLine`, `ToggleSwitch`, and
  `SvgCanvas` (vector paths inside the widget tree).
- **A consumable package.** The CMake project is now `imprint` (it was
  `myapp` at v0.1.1), so `find_package(imprint 0.2)` resolves and every
  target travels under the `imprint::` alias. `IMPRINT_WITH_SHELL=OFF`
  packages the headless widget path without inheriting a platform
  dependency. Two smokes lock the surface: a subproject consumer and an
  installed-package consumer, both in CI.
- **Twenty demo stories** in `apps/`, seventeen of them WebAssembly
  pages: eleven browser-playable games (`seq303`, `drift`, `sandbox`,
  `firefly`, `windloom`, `rotafarm`, `comet`, `crystal`, `moonpull`,
  `fogkeeper`, `hoursmith`), the seven framework demos and a design-file
  playground. Portal: https://tyouhyou.github.io/imprint/
- **A warning baseline** (`-Werror` over the set in
  `cmake/imprint-warnings.cmake`, `-Wconversion` /
  `-Wsign-conversion` included; MSVC `/W4` no longer suppresses the
  C4244 family) and a **format gate** (`tools/format-check.sh`,
  clang-format 18.1.3 pinned, run in CI).
- **`zb::input::decorate_printable_key`** — the printable-character half
  of the U-1 key rule, now a single home that every C++ input translator
  calls instead of re-deriving the letter/digit classification.
- **Shared shell seams.** `shell/device_overlay.hpp` gains
  `for_each_bar()` (the chrome-bar iteration every desktop shell
  hand-rolled) and `shell/presenter.hpp` gains `painted_region()` (the
  dirty-region → present-region read, which also mechanizes the
  evaluation-order rule that once shipped as a black X11 window).
- **Package hygiene.** Both shared libraries (`libimcore`, `libzbapi`)
  now carry a `SOVERSION`, so an in-place upgrade cannot silently replace
  a library a linked binary still uses. `cmake --install` also ships
  `LICENSE` under `share/imprint/` and a pkg-config recipe
  (`lib/pkgconfig/imprint.pc`) for non-CMake consumers of the C ABI.
- `CONTRIBUTING.md`, `SECURITY.md`, this changelog, and a CI status
  badge in all three READMEs.

### Changed
- **The HTML/design-file whitelist grew** (`<slider>`, gauge/knob/meter
  range properties, fractional SVG stroke widths that survive as Q10
  values), and design-file parser warnings now fail the build instead of
  scrolling past.
- The project version no longer trails the released tags
  (`find_package(imprint 0.2)` now resolves against this tree).
- `Event::subscribe()` is `[[nodiscard]]`: discarding the guard
  unsubscribes on the same statement, which is never what a caller
  means.
- `core::basic_color` no longer puns the pixel word through a union. The
  32bpp channel byte view goes through `memcpy` of the object
  representation — the same memory byte order, without the one
  load-bearing implementation-defined construct in the tree. Object
  size is unchanged (still exactly `depth/8` bytes).
- **macOS shell**: buffered-window drawing is Y-flipped only when the
  base CTM is y-down (10.13 rendered the buffer upside down), and the
  repaint chain is diagnosable end to end.

### Fixed
- **Untrusted input** reachable from a design file, the C ABI or a
  terminal is bounded: UTF-8 decoding stops at the caller's slice, TTF
  buffers are length-validated, and HTML variables, SVG path data and
  terminal CSI sequences are capped (warn-once, then drop).
- **SIXEL log flood.** The sixel presenter logged one warning per
  *pixel* once a band's 250 palette registers were spent; it now latches
  once per frame. The header and `docs/ARCHITECTURE.md` §5 also state
  the real behavior — a color past the budget has no register and is
  dropped from the band — replacing the old "maps to the nearest
  registered color" promise, which the code never implemented.
- **SVG rasterizer cost.** Every rasterizer walk is now intersected with
  the draw area at the entry, so a design file declaring a huge stroke
  width or radius costs surface-area work instead of its own: one
  `stroke-width:999999999px` line on a 2^20 px canvas used to add ~330 s
  to the HTML suite. `SvgCanvas::walk_pixel_count()` exposes the
  deterministic cost the battery asserts on.
- **CRLF design files.** A Windows checkout's `\r` reached the parser as
  part of the last token; line terminators are stripped before
  tokenizing, and the CRLF shape is in `test_ui_file`.
- **X11 key release mask**, scaled-present scratch indexing, and the
  Windows-only TUs now compile under the project warning set (a mingw
  cross-probe covers the GCC family in CI, where MSVC cannot).
- **Python host key codes.** `demo/python/myapp.py` filled only the
  character channel, so its keys carried no platform-independent key
  code; it now applies the U-1 rule the C++ translators use.

### Documentation
- README rewritten pitch-first (one-liner, hero, feature → pain-point)
  in all three languages.
- `docs/ARCHITECTURE.md` §5 states the widget/container gaps (no grid,
  no table/tree/tab/menu container, no accessibility or text-selection
  surface) and the design-file image gap (PNG/JPEG decode exists in
  `imcore`, but the HTML whitelist has no `<img>` and skips `url()`;
  GIF is encode-only) as boundaries rather than omissions.
- `docs/backlog.md` is the single home for the architecture backlog; the
  0.2-era audit batches are closed there.

## [0.1.1] — 2026-09-02

### Fixed
- Touch glitch debounce.
- TTF advance calculation.
- GIF encoder block terminator (strict decoders rejected the output).

### Changed
- Centered button text.

## [0.1.0] — 2026-08-31

First release: the deterministic software-rasterizer UI runtime with the
C ABI, the design-file path, the vendored stb codecs, and the first
platform shells.

[Unreleased]: https://github.com/tyouhyou/imprint/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/tyouhyou/imprint/compare/v0.1.1...v0.2.0
[0.1.1]: https://github.com/tyouhyou/imprint/compare/v0.1...v0.1.1
[0.1.0]: https://github.com/tyouhyou/imprint/releases/tag/v0.1
