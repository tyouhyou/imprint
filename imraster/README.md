# imraster — the software raster core, as a module

**Namespace** `zb::raster`. Home since 2026-10-25: absorbed into imprint
(A-29 Phase 1, closing fps's R20) as the optional `imraster/` component
beside `imcore`/`imui`. The contracts live in imprint's
`docs/ARCHITECTURE.md` §4.12 and `docs/code-contract.md` §13; the module
builds under `IMPRINT_WITH_IMRASTER` (default OFF, 32bpp `rgba32`/`le`
only) and its suite runs via the `raster-test` verify target. Upstream
source: the fps repo's `imraster/` (its `docs/IMPRINT-RUNTIME.md` holds
the R20 consumption report); the fps golden frames remain the upstream
regression baseline for this module's arithmetic.

## What is in it

| File | Contents |
|---|---|
| `include/imraster/pixel.hpp` | packed-pixel model: `uint32` words, `pack`/`unpack_*`, byte offsets taken from imprint's `IM_PIXEL_TRAITS` macro (static_asserts rgba32/le — the same macro `Color` is built from, so the two cannot drift) |
| `include/imraster/canvas.hpp` + `src/canvas.cpp` | `Canvas`: the consumer-owned pixel block — low-res world grid + full-res surface, the rep resolution scale, span primitives (flat / gradient / texture ramp / **src-over blend**), circles, bullet holes, glow, the per-column wall depth buffer, the per-pixel mesh depth buffer, the sky mask + per-column wall spans, the tent upscale |
| `include/imraster/raster.hpp` + `src/raster.cpp` | `Camera`/`project`, the column-based quad filler (`raster_quad`), the lit box painter (`draw_box`) with per-face hemisphere + rim terms and back-face culling |
| `include/imraster/mesh.hpp` + `src/mesh.cpp` | the per-pixel triangle path: `MeshBuf` (capsule/sphere builders with **self-healing winding**), `draw_mesh` — per-pixel depth + interpolated normals + wall occlusion, `MeshPaint` (the caller's lighting model) |
| `test/test_raster.cpp` (in imprint's `test/`) | the module's own contract tests (see below) |

## The boundary contract

1. **The module owns no "look".** The tone curve is the HOST's: a 256-entry
   LUT injected per paint (`BoxPaint`/`FaceShade`/`MeshPaint::tone`,
   `nullptr` = identity). This line was drawn by the goldens, not by taste:
   when grade was unconditional inside the box path, the contact shadow —
   which builds its `FaceShade` by hand — silently lost the curve and every
   shadow pixel drifted (GOLDEN_PLAY caught 295 pixels; see IMPRINT-RUNTIME
   R20). **Anyone adding a hand-built shading struct must decide the tone
   question explicitly.**
2. **The module knows no entities.** No enemies, no kits, no skeletons —
   only capsules, spheres, triangles, part ids and a lighting model. fps's
   trooper (`game/src/render/mesh.cpp`) is one consumer.
3. **The hand-off to the UI framework is unchanged.** The block reaches the
   framework in one call (`draw_surface`, imprint's U-8). This module is the
   3D viewport's pixel producer, not a second UI stack.

## Tests

- `raster-test` (imprint verify target; native, ~2s, no game, no wasm;
  the `raster` suite rides `test_imui` in the `IMPRINT_WITH_IMRASTER=ON`
  rgba32/le config): 27 assertions — projection against the docstring
  formula, blend against the src-over expression, builder count
  contracts, the outward-normal invariant of the self-healing winding,
  unlit paint resolving to the EXACT part color, per-pixel
  self-occlusion in one pass, per-column wall occlusion, the hemisphere
  term's direction.
- The game's own regression suite doubles as integration proof:
  after the extraction, both web-smoke golden frames and both model contact
  sheets are **pixel-identical** to the pre-extraction build (AE = 0).

## Bit-identity note

`canvas.cpp`/`raster.cpp` are the game's old `target.cpp`/`raster.cpp` with
only the namespace, the class name (`Target` → `Canvas`) and the tone
injection changed — every arithmetic expression is the one that was measured
and golden-blessed in fps phases 1–8. Keep it that way: changes to this
module's arithmetic move fps's goldens, and any intentional move must be
re-pinned with a reason (fps's F10 convention).
