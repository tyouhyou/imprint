# Contributing to Imprint UI

Thanks for your interest. This file is the practical half of the
contribution contract: what to read before changing code, what the
project expects from a change, and where each rule lives. The
architecture and API contracts are normative in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and
[`docs/code-contract.md`](docs/code-contract.md) — when they and this
file disagree, they win.

## Before you start

- **Read the contract first.** API behavior lives in
  `docs/code-contract.md`, module boundaries and per-target policy in
  `docs/ARCHITECTURE.md`. Contract-before-API is the rule: if your change
  alters a public surface, change the contract in the same PR (or
  before it).
- **Propose architecture changes in an issue first.** The roadmap and
  the architecture backlog are maintained privately by the maintainer;
  a missing A-/X- number means that item landed — history is in
  `git log`.
- **Pick an issue, or open one first.** For anything larger than a
  clearly-scoped fix, agree on the approach before writing code. The
  `good first issue` entries all have an acceptance path.

## Ground rules

- **Determinism is the contract, not a test trick.** For a fixed build
  and buffer size, the same input must always produce the same
  framebuffer bytes. No time, no environment, no uninitialized reads, no
  floating-point shortcuts in the raster paths. `zb::snap` records and
  checks frame hashes; CI byte-compares renders.
- **No new dependencies.** The tree vendors `third_party/stb` and
  nothing else. FreeType is gone by ruling; the build-time TTF subset and
  the runtime-TTF path both ride the vendored stb_truetype.
- **No warnings.** Every in-tree build compiles with the project warning
  set in `cmake/imprint-warnings.cmake` and `-Werror`
  (`-DIMPRINT_WERROR=OFF` is the escape hatch for a compiler that ships a
  new diagnostic before the tree is cleaned).
- **Formatting is gated.** The whole first-party tree is formatted with
  clang-format 18.1.3 to [`.clang-format`](.clang-format) (`ColumnLimit:
  0`, Allman). Run `sh tools/format-check.sh --write` before a commit
  that touches first-party C++; CI runs the same check without
  `--write` and fails on drift. The gate pins the version, so install
  clang-format 18.1.3 (or let the script tell you it is missing).
- **Tests ride the local `EXPECT` macro** in `test/test.hpp`
  (NDEBUG-safe, no framework) and register in `test/test_main.cpp`. A
  behavior change needs a suite that fails without it.
- **16bpp is a first-class battery.** Coverage and antialiasing code
  quantizes differently at `COLOR_DEPTH=16` (binary alpha, 5-bit
  channels). If you touch rasterization or drawing, run the 16bpp suite
  too — do not weaken a test to 32bpp expectations.
- **One identity, no attribution trailers.** Commit messages carry the
  change only.

## Build and test

The build table in [`README.md`](README.md) is the single source for
every target's exact commands — including the NDS cross build, the
mingw-w64 Windows TU probe, the WASM build and the Python host.

```
cmake -S . -B build/build_linux -DIM_SHELL_BACKEND=X11
cmake --build build/build_linux
./build/build_linux/bin/test_imui          # or: ctest -R test_imui
```

A green run prints `all tests passed`; a `TOTAL:` line only appears on
failure. The battery skips itself on embedded targets.

## Pull requests

1. Branch from `main`, one logical change per PR.
2. Contract/docs first if the API shape moves, then code, then tests.
3. Run the battery for the area you touched — plus `desktop-test-16`
   for rasterization/drawing changes and `zbapi-test` for binding
   changes.
4. Keep the diff free of unrelated reformatting and drive-by fixes.
5. Describe *why* in the PR body; the diff already says *what*.

## Reporting security issues

Please do not open a public issue. See [`SECURITY.md`](SECURITY.md).
