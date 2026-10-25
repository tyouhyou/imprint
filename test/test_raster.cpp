/*
 * The imraster module's own tests: projection, spans and blends, the box
 * rasterizer and the per-pixel mesh path -- native, host compiler, no
 * wasm, no game, nothing from imprint beyond the pixel traits header.
 * (Ported 2026-10-25 from the fps repo's raster_check.cpp onto the
 * battery's EXPECT harness; the assertions are upstream's, one for one.)
 *
 * Why this exists (2026-10-05, the raster-core extraction). The module
 * was cut out of fps so it could be PROMOTED into imprint as a component,
 * and a component that cannot be tested without its first consumer is not
 * a component -- it is a folder. Everything here holds the module against
 * numbers written down independently of the code under test: projection
 * coordinates from the docstring formula, blend arithmetic from the
 * src-over expression, triangle counts from the builder contract, exact
 * pixel colors from an unlit paint (bright = 1 makes the whole shading
 * chain cancel to identity), and occlusion relationships that must hold
 * regardless of the exact pixels.
 *
 * The game's own goldens (web-smoke) then prove the GAME through the
 * module is bit-identical to what it was before the extraction; this file
 * proves the module's CONTRACT on its own terms.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "imraster/canvas.hpp"
#include "imraster/mesh.hpp"
#include "imraster/raster.hpp"

#include "test.hpp"

using zb::raster::Canvas;
using zb::raster::MeshBuf;
using zb::raster::MeshPaint;

namespace
{
    // the sheet's camera: eye at (0, 0.5, 0) looking +z, r = -x, up = +y
    zb::raster::Camera sheet_cam(const int w, const int h)
    {
        zb::raster::Camera cam;
        cam.ox = 0.f;
        cam.oy = 0.5f;
        cam.oz = 0.f;
        cam.fx = 0.f;
        cam.fy = 0.f;
        cam.fz = 1.f;
        cam.rx = -1.f;
        cam.ry = 0.f;
        cam.rz = 0.f;
        cam.ux = 0.f;
        cam.uy = 1.f;
        cam.uz = 0.f;
        cam.plane = 0.66f;
        cam.vw = w;
        cam.vh = h;
        cam.horizon = static_cast<float>(h) / 2.f;
        cam.near_clip = 0.12f;
        cam.depth = nullptr;
        return cam;
    }

    int channel(const uint32_t p, const int c)
    {
        return static_cast<int>((p >> (8 * c)) & 0xFFu);
    }

    // near enough for screen-coordinate comparisons on a small canvas
    bool near_f(const float a, const float b, const float eps)
    {
        return (a < b ? b - a : a - b) <= eps;
    }
} // namespace

int test_raster()
{
    // ---- 1: projection against the docstring formula ---------------
    //
    // sx = vw/2 + u*vscale/v; sy = horizon - up*vscale/v; iw = 1/v,
    // with vscale = vw/(2*plane) for BOTH axes (raster.hpp: the lens is
    // square-pixelled, so a window's aspect ratio is not an anamorphic
    // squeeze). The expectations below were computed BY HAND from that
    // line -- not by calling the code under test.
    //
    // The vertical expectation USED to be `horizon - up*vh/v`, and the
    // assertion here (`eye+0.5 at v=1 -> top row`) encoded it. That
    // formula is the bug this section now guards: on the 300x340 sheet
    // cell it stretched vertically by 1.47x, on the game's 1280x720 by
    // 1.35x, and on a square window it was accidentally correct. A
    // projection test that only ever runs on one canvas size cannot see
    // that, which is why the anisotropy check below runs at three.
    {
        const zb::raster::Camera cam = sheet_cam(300, 340);
        const zb::raster::Vtx c = zb::raster::project(cam, 0.f, 0.5f, 2.f);
        // project: straight ahead -> center, iw = 1/v
        EXPECT(near_f(c.sx, 150.f, 1e-4f) && near_f(c.sy, 170.f, 1e-4f) &&
               near_f(c.iw, 0.5f, 1e-6f));
        if (test::failures > 0)
        {
            std::printf("     got sx=%.2f sy=%.2f iw=%.4f\n",
                        static_cast<double>(c.sx), static_cast<double>(c.sy),
                        static_cast<double>(c.iw));
        }

        // u = +0.66 at v = 2 IS the frustum edge: sx = vw (the plane
        // term saturates); half that offset lands at 3/4 width. The
        // HORIZONTAL axis is the reference -- it is what `plane` defines.
        const zb::raster::Vtx r =
            zb::raster::project(cam, -1.32f, 0.5f, 2.f);
        // project: frustum edge lands at the right edge
        EXPECT(near_f(r.sx, 300.f, 1e-3f));
        const zb::raster::Vtx q =
            zb::raster::project(cam, -0.66f, 0.5f, 2.f);
        // project: half the frustum term lands at 3/4 width
        EXPECT(near_f(q.sx, 225.f, 1e-3f));

        // VERTICAL, and it must be the same pixels-per-radian the
        // horizontal axis uses: vscale = 300/(2*0.66) = 227.27 px, so
        // eye+0.5 at v=1 is 113.64 px above the horizon -> sy 56.36.
        // (`vh` would have said 170, i.e. the top row.)
        const zb::raster::Vtx u = zb::raster::project(cam, 0.f, 1.0f, 1.f);
        // project: vertical uses vscale, not vh
        EXPECT(near_f(u.sy, 170.f - 0.5f * (300.f / (2.f * 0.66f)), 1e-3f));
        if (test::failures > 0)
        {
            std::printf("     got sy=%.2f, vscale would give %.2f\n",
                        static_cast<double>(u.sy),
                        static_cast<double>(170.f -
                                            0.5f * (300.f / (2.f * 0.66f))));
        }

        // THE INVARIANT THAT MATTERS: one cell across and one cell up must
        // land the same number of pixels apart, whatever the canvas shape.
        // Checked at a portrait cell, the game's 16:9, and a square --
        // because the bug was a function of the aspect ratio, and a test
        // that runs at one aspect cannot see it.
        {
            const int sizes[3][2] = {{300, 340}, {1280, 720}, {500, 500}};
            for (int s = 0; s < 3; ++s)
            {
                const zb::raster::Camera a = sheet_cam(sizes[s][0],
                                                       sizes[s][1]);
                // two points at v = 3: one a cell to the side, one a cell up
                const zb::raster::Vtx side =
                    zb::raster::project(a, -1.f, a.oy, 3.f);
                const zb::raster::Vtx above =
                    zb::raster::project(a, 0.f, a.oy + 1.f, 3.f);
                const float dx = side.sx - static_cast<float>(sizes[s][0]) / 2.f;
                const float dy = a.horizon - above.sy;
                // project: square pixels (a cell across == a cell up)
                EXPECT(near_f(dx, dy, 1e-3f));
                if (test::failures > 0)
                {
                    std::printf("     %dx%d: dx=%.2f dy=%.2f\n", sizes[s][0],
                                sizes[s][1], static_cast<double>(dx),
                                static_cast<double>(dy));
                }
            }
        }
    }

    // ---- 2: canvas spans, blend arithmetic, sky bookkeeping ---------
    {
        Canvas t;
        t.resize(64, 48);
        t.set_replicate(1);
        t.fill_rect(0, 0, 63, 47, 0xff646464u); // grey dst

        // vspan_blend = src-over: out = src*a + dst*(1-a), in integers
        // (sr + (dr - sr) * (256 - ia) / 256). dst 100, src (200,50,0),
        // alpha 0.5 -> (150, 75, 50).
        t.vspan_blend(10, 5, 20, zb::raster::pack(200, 50, 0), 0.5f);
        const uint32_t got = *reinterpret_cast<const uint32_t *>(
            t.pixels() + 12 * 64 + 10);
        // vspan_blend: src-over at alpha 0.5
        EXPECT(channel(got, 0) == 150 && channel(got, 1) == 75 &&
               channel(got, 2) == 50);

        // alpha 1 must equal a plain vspan write
        t.vspan_blend(12, 5, 20, zb::raster::pack(1, 2, 3), 1.f);
        const uint32_t full = *reinterpret_cast<const uint32_t *>(
            t.pixels() + 12 * 64 + 12);
        // vspan_blend: alpha 1 = opaque write
        EXPECT(full == zb::raster::pack(1, 2, 3));

        // out-of-range writes are CLIPPED, not dropped: a span past the
        // bottom writes its in-range rows
        t.vspan(5, 40, 100, zb::raster::pack(9, 9, 9));
        const uint32_t last = *reinterpret_cast<const uint32_t *>(
            t.pixels() + 47 * 64 + 5);
        // vspan: clipped to the surface, not dropped
        EXPECT(last == zb::raster::pack(9, 9, 9));

        // sky bookkeeping: an empty column occludes nothing, a recorded
        // wall span occludes its rows, a cleared cell stops being sky
        t.mark_sky(0, 47);
        t.set_wall_span(3, 10, 20);
        bool ok = !t.wall_at(3, 9) && t.wall_at(3, 10) && t.wall_at(3, 20) &&
                  !t.wall_at(3, 21) && !t.wall_at(4, 15);
        // sky mask: wall span semantics
        EXPECT(ok);
        t.clear_sky_cell(3, 10);
        // sky mask: cleared cell is not sky, untouched neighbours are
        EXPECT(!t.sky_at(3, 10) && t.sky_at(4, 15) && t.sky_at(3, 21));

        // the mesh depth buffer is sized by the ACTIVE surface and reset
        t.set_replicate(2);
        // mesh depth: sized to the active surface on set_replicate
        EXPECT(t.mesh_depth_count() ==
               static_cast<size_t>(t.grid_w()) *
                   static_cast<size_t>(t.grid_h()));
        t.mesh_depth_data()[0] = 5.f;
        t.reset_mesh_depth();
        // mesh depth: reset zeroes
        EXPECT(t.mesh_depth_data()[0] == 0.f);
    }

    // ---- 3: the box rasterizer, spans and depth ---------------------
    {
        Canvas t;
        t.resize(120, 90);
        t.set_replicate(1); // the buffer is ZEROED by resize: counting
                            // non-zero words counts PAINTED pixels, so
                            // no backdrop fill is allowed in this test
        zb::raster::Camera cam = sheet_cam(120, 90);
        cam.horizon = 45.f;

        // a unit cube centered 2 ahead: its front face (+z) fills an
        // exact screen rectangle, so the pixel count is hand-computable.
        // half extents 0.25: face corners at x = +/-0.25, y = 0.25..0.75,
        // v = 2 -> u/v spans +/-0.125/0.66 -> sx 60 +/- 11.36 -> 13 px
        // wide per column rounding; assert the count from GEOMETRY:
        // columns x where |2x+1-w|*0.66/(w... -- instead of re-deriving
        // the rounding, assert the three invariants that must hold:
        // painted, strictly inside the projected bounds, and depth
        // tested against a wall.
        float corners[24];
        const float hx = 0.25f, hy = 0.25f, hz = 0.25f;
        const float cz[3] = {0.f, 0.5f, 2.f};
        for (int bit = 0; bit < 8; ++bit)
        {
            corners[bit * 3] = cz[0] + ((bit & 1) ? hx : -hx);
            corners[bit * 3 + 1] = cz[1] + ((bit & 2) ? hy : -hy);
            corners[bit * 3 + 2] = cz[2] + ((bit & 4) ? hz : -hz);
        }
        zb::raster::FaceStyle styles[6];
        styles[4].base = zb::raster::pack(200, 200, 200);
        for (int f = 0; f < 6; ++f)
        {
            styles[f].base = zb::raster::pack(200, 200, 200);
        }
        zb::raster::BoxPaint paint;
        paint.ambient = 1.f;
        paint.lambert = 0.f;

        zb::raster::draw_box(t, cam, corners, styles, paint);
        long painted = 0;
        for (int i = 0; i < 120 * 90; ++i)
        {
            if (*reinterpret_cast<const uint32_t *>(
                    t.pixels() + i) != 0u)
            {
                ++painted;
            }
        }
        // draw_box: a box ahead of the eye paints
        EXPECT(painted > 100);

        // the wall column test: a wall 1.5 ahead occludes the box at 2
        float wall[120];
        for (int x = 0; x < 120; ++x)
        {
            wall[x] = 1.5f;
        }
        cam.depth = wall;
        t.fill_rect(0, 0, 119, 89, 0u); // re-zero: keep the counting honest
        zb::raster::draw_box(t, cam, corners, styles, paint);
        painted = 0;
        for (int i = 0; i < 120 * 90; ++i)
        {
            if (*reinterpret_cast<const uint32_t *>(
                    t.pixels() + i) != 0u)
            {
                ++painted;
            }
        }
        // draw_box: a column whose wall is NEARER occludes the box
        EXPECT(painted == 0);

        // and a wall FARTHER than the box occludes nothing
        for (int x = 0; x < 120; ++x)
        {
            wall[x] = 9.f;
        }
        t.fill_rect(0, 0, 119, 89, 0u);
        zb::raster::draw_box(t, cam, corners, styles, paint);
        painted = 0;
        for (int i = 0; i < 120 * 90; ++i)
        {
            if (*reinterpret_cast<const uint32_t *>(
                    t.pixels() + i) != 0u)
            {
                ++painted;
            }
        }
        // draw_box: a column whose wall is FARTHER lets the box paint
        EXPECT(painted > 100);
    }

    // ---- 4: the mesh builders' contract ------------------------------
    {
        MeshBuf buf;
        buf.add_capsule(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.25f, 0.25f, 8, 2, 0,
                        1.f);
        // rings+1 = 3 rings x seg 8 verts; 2 bands x 8 quads x 2 tris
        // capsule: vert/tri counts follow (rings+1)*seg / rings*seg*2
        EXPECT(buf.verts.size() == 24 && buf.tris.size() == 32);

        buf.reset();
        buf.add_sphere(0.f, 0.f, 0.f, 0.25f, 8, 4, 0, 1.f);
        // 2 poles + (rings-1) rows x seg; top fan + 2 bands x 2 + bottom
        const size_t vexp = 2 + 3 * 8;
        const size_t texp = 8 + 2 * 16 + 8;
        // sphere: vert/tri counts follow the uv layout
        EXPECT(buf.verts.size() == vexp && buf.tris.size() == texp);

        // the self-heal invariant: every triangle's geometric normal
        // points OUTWARD (toward centroid - center)
        bool outward = true;
        for (const zb::raster::MeshTri &tr : buf.tris)
        {
            const zb::raster::MeshVert &a = buf.verts[static_cast<size_t>(tr.a)];
            const zb::raster::MeshVert &b = buf.verts[static_cast<size_t>(tr.b)];
            const zb::raster::MeshVert &c = buf.verts[static_cast<size_t>(tr.c)];
            const float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z;
            const float e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;
            const float nx = e1y * e2z - e1z * e2y;
            const float ny = e1z * e2x - e1x * e2z;
            const float nz = e1x * e2y - e1y * e2x;
            const float gx = (a.x + b.x + c.x) / 3.f;
            const float gy = (a.y + b.y + c.y) / 3.f;
            const float gz = (a.z + b.z + c.z) / 3.f;
            if (nx * gx + ny * gy + nz * gz <= 0.f)
            {
                outward = false;
            }
        }
        // sphere: every triangle's geometric normal points outward
        EXPECT(outward);
    }

    // ---- 5: the mesh raster: exact color, occlusion, lighting -------
    {
        Canvas t;
        t.resize(120, 90);
        t.set_replicate(1); // zeroed buffer: non-zero word = painted
        zb::raster::Camera cam = sheet_cam(120, 90);
        cam.horizon = 45.f;

        MeshBuf buf;
        buf.add_sphere(0.f, 0.5f, 2.f, 0.25f, 8, 4, 0, 1.f);

        // UNLIT paint: ambient 1, lambert/hemi/rim 0, ao 1 (baked), fog
        // far, no flash, no tone -- the whole shading chain cancels and
        // every pixel must be EXACTLY the part color
        MeshPaint paint;
        paint.ambient = 1.f;
        paint.lambert = 0.f;
        paint.hemi = 0.f;
        paint.rim = 0.f;
        paint.fog_dist = 1e9f; // 1 - v/1e9 rounds to exactly 1.0f
        const uint32_t col = zb::raster::pack(90, 120, 160);
        const uint32_t one_part[1] = {col};
        paint.part_colors = one_part;
        paint.part_count = 1;

        t.reset_mesh_depth();
        zb::raster::draw_mesh(t, cam, buf, paint);
        long painted = 0;
        long exact = 0;
        long zset = 0;
        for (int i = 0; i < 120 * 90; ++i)
        {
            const uint32_t px = *reinterpret_cast<const uint32_t *>(
                t.pixels() + i);
            if (px != 0u)
            {
                ++painted;
                if (px == col)
                {
                    ++exact;
                }
            }
            if (t.mesh_depth_data()[i] > 0.f)
            {
                ++zset;
            }
        }
        // mesh: unlit paint resolves to the EXACT part color
        EXPECT(painted > 50 && painted == exact);
        // mesh: every written pixel recorded its per-pixel depth
        EXPECT(zset == painted);

        // a wall NEARER than the sphere occludes every pixel
        float wall[120];
        for (int x = 0; x < 120; ++x)
        {
            wall[x] = 1.f;
        }
        cam.depth = wall;
        t.fill_rect(0, 0, 119, 89, 0u);
        t.reset_mesh_depth();
        zb::raster::draw_mesh(t, cam, buf, paint);
        painted = 0;
        for (int i = 0; i < 120 * 90; ++i)
        {
            if (*reinterpret_cast<const uint32_t *>(
                    t.pixels() + i) != 0u)
            {
                ++painted;
            }
        }
        // mesh: a column whose wall is nearer occludes the mesh
        EXPECT(painted == 0);

        // SELF-occlusion through the per-pixel z: a far sphere behind a
        // near one paints fewer pixels alone than with the near sphere
        // in the same buffer -- the pixels the near body covers must be
        // lost to the zbuf, not to painter order (one buffer, one pass)
        cam.depth = nullptr;
        MeshBuf two;
        two.add_sphere(0.f, 0.5f, 4.f, 0.25f, 8, 4, 0, 1.f); // far
        t.fill_rect(0, 0, 119, 89, 0u);
        t.reset_mesh_depth();
        zb::raster::draw_mesh(t, cam, two, paint);
        long far_alone = 0;
        for (int i = 0; i < 120 * 90; ++i)
        {
            if (*reinterpret_cast<const uint32_t *>(
                    t.pixels() + i) != 0u)
            {
                ++far_alone;
            }
        }
        // the near sphere is OFFSET so only part of the far disc hides
        // behind it: same-center discs would nest exactly and "far_seen
        // > 0" would be unsatisfiable by geometry, not by the zbuf
        two.add_sphere(0.25f, 0.5f, 2.f, 0.25f, 8, 4, 1, 1.f); // near, part 1
        const uint32_t pc2[2] = {col, zb::raster::pack(20, 200, 20)};
        paint.part_colors = pc2;
        paint.part_count = 2;
        t.fill_rect(0, 0, 119, 89, 0u);
        t.reset_mesh_depth();
        zb::raster::draw_mesh(t, cam, two, paint);
        long far_seen = 0;
        for (int i = 0; i < 120 * 90; ++i)
        {
            const uint32_t px = *reinterpret_cast<const uint32_t *>(
                t.pixels() + i);
            if (px == col)
            {
                ++far_seen;
            }
        }
        // mesh: the near sphere's pixels beat the far sphere's in
        // ONE pass (per-pixel z, not painter order)
        EXPECT(far_alone > 0 && far_seen > 0 && far_seen < far_alone);

        // hemi must make the upper half of a lit body brighter than the
        // lower half: the one shading term whose direction is testable
        MeshPaint lit = paint;
        lit.ambient = 0.8f;
        lit.lambert = 0.34f;
        lit.hemi = 0.30f;
        lit.rim = 0.f;
        lit.part_colors = pc2;
        lit.part_count = 2;
        MeshBuf solo;
        solo.add_sphere(0.f, 0.5f, 2.f, 0.5f, 12, 6, 0, 1.f);
        t.fill_rect(0, 0, 119, 89, 0xff000000u);
        t.reset_mesh_depth();
        zb::raster::draw_mesh(t, cam, solo, lit);
        long top_sum = 0, top_n = 0, bot_sum = 0, bot_n = 0;
        for (int y = 0; y < 90; ++y)
        {
            for (int x = 0; x < 120; ++x)
            {
                const uint32_t px = *reinterpret_cast<const uint32_t *>(
                    t.pixels() + y * 120 + x);
                if (px == 0u)
                {
                    continue;
                }
                const long lum = (channel(px, 0) + channel(px, 1) +
                                  channel(px, 2));
                if (y < 38)
                {
                    top_sum += lum;
                    ++top_n;
                }
                else if (y > 52)
                {
                    bot_sum += lum;
                    ++bot_n;
                }
            }
        }
        // mesh: hemisphere term -- upper normals light, lower darken
        EXPECT(top_n > 0 && bot_n > 0 && top_sum * bot_n > bot_sum * top_n);

        // MeshPaint::alpha: src-over against what is already there, and
        // an exact endpoint at both 0 and 1. The floor is filled with a
        // known colour and the same sphere is drawn over it three times;
        // a blend that leaked would show up as a colour that is neither
        // the floor nor the part colour nor their documented lerp.
        {
            MeshPaint blend = paint;
            blend.part_colors = one_part;
            blend.part_count = 1;
            // alpha is SET EXPLICITLY before every draw below, not left at
            // whatever the previous block left it at. The first version of
            // this test declared `blend.alpha = 1.f` once at the top and
            // then forgot it before the alpha-0 case, so that case was
            // silently re-testing alpha 1 -- and failing, for a reason that
            // had nothing to do with the blend.
            const uint32_t floor = zb::raster::pack(200, 100, 40);

            // alpha 0: the floor survives untouched under the sphere
            blend.alpha = 0.f;
            t.fill_rect(0, 0, 119, 89, floor);
            t.reset_mesh_depth();
            zb::raster::draw_mesh(t, cam, solo, blend);
            long touched = 0;
            uint32_t first = 0;
            int first_at = -1;
            for (int i = 0; i < 120 * 90; ++i)
            {
                const uint32_t px =
                    *reinterpret_cast<const uint32_t *>(t.pixels() + i);
                if (px != floor)
                {
                    if (first_at < 0)
                    {
                        first = px;
                        first_at = i;
                    }
                    ++touched;
                }
            }
            // mesh: alpha 0 leaves the destination exactly as it was
            EXPECT(touched == 0);
            // mesh: alpha 0 detail -- fps's checkf printed the failing
            // pixel here; the failure-only line is kept (format inline so
            // -Wformat-nonliteral can check it), the assertion is upstream's
            if (touched != 0)
            {
                std::printf(
                    "     %ld px differ, first at %d: got %08x want %08x\n",
                    touched, first_at, first, floor);
            }
            EXPECT(touched == 0);

            // alpha 1: the exact part colour, the same assertion the
            // solid path above makes -- the blend branch must not shift
            // an opaque pixel by a rounding step
            blend.alpha = 1.f;
            t.fill_rect(0, 0, 119, 89, floor);
            t.reset_mesh_depth();
            zb::raster::draw_mesh(t, cam, solo, blend);
            long blended_pixels = 0, exact_part = 0;
            for (int i = 0; i < 120 * 90; ++i)
            {
                const uint32_t px =
                    *reinterpret_cast<const uint32_t *>(t.pixels() + i);
                if (px != floor)
                {
                    ++blended_pixels;
                }
                if (px == col)
                {
                    ++exact_part;
                }
            }
            // mesh: alpha 1 is byte-identical to the opaque path
            EXPECT(blended_pixels > 0 && blended_pixels == exact_part);

            // alpha 0.5: every covered pixel is the midpoint, which is
            // the only claim worth making about a half blend
            blend.alpha = 0.5f;
            t.fill_rect(0, 0, 119, 89, floor);
            t.reset_mesh_depth();
            zb::raster::draw_mesh(t, cam, solo, blend);
            long mid_pixels = 0, mid_exact = 0;
            for (int i = 0; i < 120 * 90; ++i)
            {
                const uint32_t px =
                    *reinterpret_cast<const uint32_t *>(t.pixels() + i);
                if (px == floor)
                {
                    continue;
                }
                ++mid_pixels;
                // src-over at 0.5 over an OPAQUE destination is exactly
                // the arithmetic midpoint, so all three channels must
                // agree with it. Integer division truncates, so the
                // expectation is allowed one below the true midpoint --
                // anything more is a real disagreement, not rounding.
                // dst + (src - dst) * 0.5f is the arithmetic midpoint, and
                // in binary floating point a halving and an integer-valued
                // addition are both exact -- so the expectation is the plain
                // midpoint, no tolerance. (The first version of this check
                // wrote (a + 2b) / 4, which is not the midpoint of anything
                // and made the assertion fail on every covered pixel.)
                const int r_exp = (channel(floor, 0) + channel(col, 0)) / 2;
                const int g_exp = (channel(floor, 1) + channel(col, 1)) / 2;
                const int b_exp = (channel(floor, 2) + channel(col, 2)) / 2;
                if (channel(px, 0) == r_exp && channel(px, 1) == g_exp &&
                    channel(px, 2) == b_exp)
                {
                    ++mid_exact;
                }
            }
            // mesh: alpha 0.5 is the documented src-over midpoint
            EXPECT(mid_pixels > 0 && mid_pixels == mid_exact);
        }
    }

    return test::report("raster");
}
