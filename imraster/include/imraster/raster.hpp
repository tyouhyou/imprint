#pragma once

#include <cstdint>

#include "imraster/canvas.hpp"

namespace zb::raster
{
    /*
     * The shared software rasterizer: a perspective camera, a column
     * triangle filler with per-column depth testing, and a lit box
     * painter. One implementation serves the world (raycast walls, the
     * box-man enemies) AND the first-person viewmodel, which is what
     * makes "the gun shares the scene's shading path" a structural fact
     * rather than two parallel renderers (docs/DESIGN.md phase 2).
     *
     * Coordinates are metric: map x (east), height (up), map y (south).
     * The eye sits at (ox, oy, oz); `plane` is tan(hfov/2), i.e. the half
     * width of the view plane one unit ahead of the eye, so a point's
     * lateral offset u maps to the screen as vw/2 * (1 + u/(v*plane)).
     * `horizon` is the screen y of eye level.
     *
     * THE LENS IS SQUARE-PIXELLED, AND vh DOES NOT ENTER IT. Both axes
     * scale by the SAME factor -- `vscale()` below, = vw/(2*plane) pixels
     * per unit of (offset/depth) -- so a sphere renders as a circle and a
     * cube's front face as a square at every aspect ratio.
     *
     * The vertical axis used to scale by `vh` instead, which made the
     * anisotropy a function of the WINDOW SHAPE: at 16:9 it compressed the
     * vertical by 1.35x (the vertical half-FOV came out 26.6 degrees
     * against the horizontal's 33.4), i.e. the renderer was drawing a 4:3
     * frustum and stretching it to 16:9. At a square window it stretched
     * instead. Nothing in the picture announces this: a stretched world
     * still looks like a world, and every object is stretched by the same
     * amount, so there is no reference to compare against. Measured on a
     * unit cube at 3 cells: 193x143 px, w/h 1.350. A trooper whose own
     * proportions are 0.348 came out 0.469 -- 35% too wide and too short,
     * which is what "the soldier looks wrong" partly was.
     *
     * Depth is a per-COLUMN distance in world units (the raycaster's
     * wall distance), not a per-pixel z-buffer: walls are vertical spans
     * so one distance per column is exact for them, and every solid in
     * the scene is a vertical prism wide enough that the approximation is
     * invisible. `depth == nullptr` disables the test -- the viewmodel
     * camera uses that, since a first-person weapon is by definition in
     * front of everything in the world.
     */
    struct Camera
    {
        float ox = 0.f, oy = 0.f, oz = 0.f; // eye
        float fx = 1.f, fy = 0.f, fz = 0.f; // forward, unit
        float rx = 0.f, ry = 0.f, rz = 0.f; // right, unit
        float ux = 0.f, uy = 1.f, uz = 0.f; // up, unit
        float plane = 0.66f;                // tan(hfov/2)
        int vw = 0, vh = 0;
        float horizon = 0.f;
        float near_clip = 0.05f; // closer than this, clamp (avoids blowup)
        const float *depth = nullptr;
    };

    // A projected vertex: screen position plus 1/depth (the perspective
    // term, affine along a planar triangle in screen space).
    struct Vtx
    {
        float sx, sy, iw;
    };

    /*
     * Pixels per unit of (offset / depth) -- the ONE scale both screen
     * axes use. It is derived from the horizontal FOV alone because the
     * lens is square-pixelled (see Camera above): the vertical FOV is
     * whatever vh/vw makes of it, and every consumer that needs to place
     * something vertically (projection, the billboards, the decal
     * projection, the sky mask) must use THIS and not vh.
     *
     * Callers that hand-roll a vertical projection -- there are several in
     * scene_view.cpp -- were the second half of the same bug, so the
     * helper exists rather than being a comment telling people to divide
     * by vw and multiply by 2*plane.
     */
    [[nodiscard]] inline float vscale(const Camera &cam) noexcept
    {
        const float w = static_cast<float>(cam.vw);
        return w / (2.f * cam.plane);
    }

    Vtx project(const Camera &cam, float x, float y, float z);

    /*
     * Per-face shading carried into the rasterizer: the unshaded material
     * color plus the brightness the lighting model produced. Keeping it
     * here (rather than pre-multiplying into a color word) is what makes
     * the in-face gradient of docs/DESIGN.md D11 possible: the ramp is
     * evaluated per row from `bright`, so it costs one multiply per row
     * and nothing at all when `grad` is 0.
     *
     *   grad      0 = flat, >0 = rows darken toward the face's bottom
     *             edge (face_top/face_bot = the face's screen y extent)
     *   flash     0..1 wash toward white, applied after the ramp
     */
    struct FaceShade
    {
        uint32_t base = 0u; // unshaded RGB; alpha ignored
        float bright = 1.f;
        float grad = 0.f;
        float flash = 0.f;
        // <1 = src-over blend of the resolved color over what is already
        // in the target (the contact shadow). Flat fills only; a ramped
        // face ignores it.
        float alpha = 1.f;
        // the host's tone curve (256 entries per channel), applied to
        // every resolved color. The module does not own a look; the host
        // that wants one passes its LUT here. nullptr = identity.
        const uint8_t *tone = nullptr;
        float face_top = 0.f;
        float face_bot = 0.f;
    };

    /*
     * Depth-tested fill of one CONVEX QUAD, given its four projected corners
     * in winding order (the camera's column buffer may be null).
     *
     * It is a quad, not two triangles, on purpose. Filling a quad as a
     * triangle pair is the obvious way and it is wrong twice over: the two
     * triangles disagree along the diagonal they share -- each evaluated the
     * in-face ramp over its OWN screen extent, so every box in the game wore
     * a diagonal seam (DESIGN F26) -- and it evaluates everything twice.
     * One pass over four edges gives the union span per column, so the ramp
     * is sampled once per column per FACE and cannot disagree with itself.
     */
    void raster_quad(Canvas &t, const Camera &cam, const Vtx corners[4],
                     const FaceShade &sh);

    // one material color for a face, plus its optional in-face gradient
    struct FaceStyle
    {
        uint32_t base = 0u; // unshaded RGB, alpha ignored
        float grad = 0.f;  // 0 = flat, 0.3 = 30% darker at the bottom edge
    };

    /*
     * Shading parameters shared by every solid in the scene.
     *   light    normalized direction (world space)
     *   ambient  brightness where the face normal is perpendicular to light
     *   lambert  extra brightness where it faces the light head on
     *   hemi     hemisphere term on the ambient, 0 = off: faces whose normal
     *            points down sit in the darker half of the sky. All
     *            per-face constant arithmetic, nothing per pixel.
     *   rim      silhouette light, 0 = off: a face seen nearly edge-on
     *            (|n·view| near 0) brightens, which is what lifts a body
     *            off the background it overlaps. Per-face constant too.
     *   ao       per-object occlusion multiplier (crevices, limb roots)
     *   fog      distance falloff, 1 = near, 0 = fully faded
     *   flash    0..1 wash toward white (hit reaction / muzzle light)
     *   tone     the host's tone curve (256 entries), applied per pixel
     *            to the resolved color; nullptr = identity
     */
    struct BoxPaint
    {
        float light[3] = {0.36f, 0.81f, 0.46f};
        float ambient = 0.62f;
        float lambert = 0.62f;
        float hemi = 0.f;
        float rim = 0.f;
        float ao = 1.f;
        float fog = 1.f;
        float flash = 0.f;
        const uint8_t *tone = nullptr;
    };

    /*
     * One convex box: 8 corners as x,y,z triples in bit order
     * (1 = +x, 2 = +y, 4 = +z, matching the face table inside), six face
     * styles in the order +x, -x, +y, -y, +z, -z. Back faces are culled
     * from the winding, so a convex box needs no depth sort among its own
     * faces; the caller sorts whole boxes back to front.
     */
    void draw_box(Canvas &t, const Camera &cam, const float corners[24],
                  const FaceStyle face[6], const BoxPaint &paint);
} // namespace zb::raster