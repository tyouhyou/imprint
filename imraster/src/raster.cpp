#include "imraster/raster.hpp"

#include <algorithm>
#include <cmath>

namespace zb::raster
{
    namespace
    {
        // Box corners indexed by bits (1 = +x, 2 = +y, 4 = +z); the faces
        // are wound so the cross product of the first two edges points out
        // of the box, which is what the back-face test below reads.
        constexpr int kFaces[6][4] = {
            {1, 3, 7, 5}, // +x
            {4, 6, 2, 0}, // -x
            {6, 7, 3, 2}, // +y (top)
            {0, 1, 5, 4}, // -y (bottom)
            {4, 5, 7, 6}, // +z (front)
            {1, 0, 2, 3}, // -z (back)
        };

        uint32_t shade_row(const FaceShade &sh, const float row_bright)
        {
            const int r0 = unpack_r(sh.base);
            const int g0 = unpack_g(sh.base);
            const int b0 = unpack_b(sh.base);
            // X-20: the int->float promotions of the int*float products
            // were always implicit; spelled out here without changing them
            int r = static_cast<int>(static_cast<float>(r0) * row_bright);
            int g = static_cast<int>(static_cast<float>(g0) * row_bright);
            int b = static_cast<int>(static_cast<float>(b0) * row_bright);
            // hit flash washes the surface toward white
            r += static_cast<int>(static_cast<float>(245 - r) * sh.flash);
            g += static_cast<int>(static_cast<float>(245 - g) * sh.flash);
            b += static_cast<int>(static_cast<float>(245 - b) * sh.flash);
            // the host's tone curve, carried on the face. A look is the
            // HOST's property, not the rasterizer's: the module resolves
            // colors, the host decides what curve they went through. The
            // viewmodel is shaded on the SURFACE at 1:1 rather than on the
            // grid, so it does not pick the curve up for free -- and if it
            // did not pick it up at all it would read as pasted into a
            // graded photograph: lit to a different curve than the room
            // behind it.
            const uint8_t *tone = sh.tone;
            if (tone != nullptr)
            {
                r = tone[r > 255 ? 255 : (r < 0 ? 0 : r)];
                g = tone[g > 255 ? 255 : (g < 0 ? 0 : g)];
                b = tone[b > 255 ? 255 : (b < 0 ? 0 : b)];
            }
            return pack(r > 255 ? 255 : r, g > 255 ? 255 : g,
                        b > 255 ? 255 : b);
        }
    } // namespace

    Vtx project(const Camera &cam, const float x, const float y,
                const float z)
    {
        const float rel_x = x - cam.ox;
        const float rel_y = y - cam.oy;
        const float rel_z = z - cam.oz;
        float v = rel_x * cam.fx + rel_y * cam.fy + rel_z * cam.fz;
        if (v < cam.near_clip)
        {
            v = cam.near_clip;
        }
        const float u = rel_x * cam.rx + rel_y * cam.ry + rel_z * cam.rz;
        const float up = rel_x * cam.ux + rel_y * cam.uy + rel_z * cam.uz;
        Vtx p;
        // ONE scale for both axes (raster.hpp: the lens is square
        // pixelled). The vertical axis used to read `vh`, which turned the
        // frame's aspect ratio into an anamorphic squeeze -- at 16:9 it
        // shrank the vertical by 1.35x, so the renderer drew a 4:3
        // frustum and stretched it across a 16:9 window.
        const float vs = vscale(cam);
        p.sx = 0.5f * static_cast<float>(cam.vw) + u * vs / v;
        p.sy = cam.horizon - up * vs / v;
        p.iw = 1.f / v;
        return p;
    }

    void raster_quad(Canvas &t, const Camera &cam, const Vtx corners[4],
                     const FaceShade &sh)
    {
        // Shading depends on the row, not the column, so the ramp is
        // resolved once per COLUMN and the span is written in one pass: a
        // per-pixel color evaluation is what made the viewmodel cost more
        // than the whole rest of the frame (DESIGN F10, same lesson).
        //
        // The ramp is sampled from the FACE's own vertical extent
        // (face_top / face_bot), which is what makes it a property of the
        // face rather than of the fill. Filled as a triangle pair instead,
        // each triangle ramped across its own extent and the two disagreed
        // along the diagonal they share -- a seam across the face of every
        // box in the game (DESIGN F26).
        const uint32_t col_flat = shade_row(sh, sh.bright);
        const float face_span = sh.face_bot - sh.face_top;
        const bool ramp = sh.grad > 0.f && face_span > 1e-3f;
        // the ramp's color at an absolute screen row
        const auto ramp_at = [&](const float y) {
            if (!ramp)
            {
                return col_flat;
            }
            float u = (y - sh.face_top) / face_span;
            if (u < 0.f)
            {
                u = 0.f;
            }
            else if (u > 1.f)
            {
                u = 1.f;
            }
            return shade_row(sh, sh.bright *
                                      (1.f + sh.grad - 2.f * sh.grad * u));
        };

        float min_x = corners[0].sx, max_x = corners[0].sx;
        for (int i = 1; i < 4; ++i)
        {
            min_x = corners[i].sx < min_x ? corners[i].sx : min_x;
            max_x = corners[i].sx > max_x ? corners[i].sx : max_x;
        }
        int x0 = static_cast<int>(min_x) - 1;
        int x1 = static_cast<int>(max_x) + 1;
        if (x0 < 0)
        {
            x0 = 0;
        }
        if (x1 > cam.vw - 1)
        {
            x1 = cam.vw - 1;
        }

        // One pass over the four edges: a convex quad's span in a column is
        // the union of the crossings, so there is no seam to disagree about
        // and no second depth test to pay for.
        for (int x = x0; x <= x1; ++x)
        {
            float y_lo = 0.f, w_lo = 0.f, y_hi = 0.f, w_hi = 0.f;
            bool any = false;
            for (int e = 0; e < 4; ++e)
            {
                const Vtx &p = corners[e];
                const Vtx &q = corners[(e + 1) & 3];
                if ((p.sx > static_cast<float>(x)) ==
                    (q.sx > static_cast<float>(x)))
                {
                    continue; // edge does not straddle this column
                }
                const float k = (static_cast<float>(x) - p.sx) /
                                (q.sx - p.sx);
                const float y = p.sy + (q.sy - p.sy) * k;
                const float w = p.iw + (q.iw - p.iw) * k;
                if (!any || y < y_lo)
                {
                    y_lo = y;
                    w_lo = w;
                }
                if (!any || y > y_hi)
                {
                    y_hi = y;
                    w_hi = w;
                }
                any = true;
            }
            if (!any)
            {
                continue;
            }

            int top = static_cast<int>(std::ceil(y_lo));
            int bot = static_cast<int>(y_hi);
            if (top < 0)
            {
                top = 0;
            }
            if (bot > cam.vh - 1)
            {
                bot = cam.vh - 1;
            }
            if (bot < top)
            {
                continue;
            }

            // iw is affine along the vertical: re-evaluate at the clamped
            // ends and depth-test with the nearest one
            float w_top = w_lo;
            float w_bot = w_hi;
            const float span = y_hi - y_lo;
            if (span > 1e-3f)
            {
                w_top = w_lo + (w_hi - w_lo) *
                                (static_cast<float>(top) - y_lo) / span;
                w_bot = w_lo + (w_hi - w_lo) *
                                (static_cast<float>(bot) - y_lo) / span;
            }
            const float w_near = w_top > w_bot ? w_top : w_bot;
            if (cam.depth != nullptr &&
                (w_near <= 0.f || 1.f / w_near >= cam.depth[x]))
            {
                continue; // a wall stands in front of this column
            }

            if (sh.alpha < 1.f)
            {
                // the blended write is flat-only: its one caller (the
                // contact shadow) has no ramp, and a ramp re-resolved
                // per row would defeat the point of the span blend
                t.vspan_blend(x, top, bot, col_flat, sh.alpha);
                continue;
            }
            if (!ramp)
            {
                // flat: one color for the whole span, one write
                t.vspan(x, top, bot, col_flat);
                continue;
            }
            // the face's ramp, sampled at this span's two ends
            t.vspan_grad(x, top, bot, ramp_at(static_cast<float>(top)),
                         ramp_at(static_cast<float>(bot) + 1.f));
        }
    }

    void draw_box(Canvas &t, const Camera &cam, const float corners[24],
                  const FaceStyle styles[6], const BoxPaint &paint)
    {
        Vtx s[8];
        for (int i = 0; i < 8; ++i)
        {
            s[i] = project(cam, corners[i * 3], corners[i * 3 + 1],
                           corners[i * 3 + 2]);
        }

        for (int fi = 0; fi < 6; ++fi)
        {
            const int (&f)[4] = kFaces[fi];
            const float *a = corners + f[0] * 3;
            const float *b = corners + f[1] * 3;
            const float *c = corners + f[2] * 3;
            const float *d = corners + f[3] * 3;

            // outward normal from the face winding
            const float e1x = b[0] - a[0], e1y = b[1] - a[1],
                        e1z = b[2] - a[2];
            const float e2x = c[0] - a[0], e2y = c[1] - a[1],
                        e2z = c[2] - a[2];
            float nx = e1y * e2z - e1z * e2y;
            float ny = e1z * e2x - e1x * e2z;
            float nz = e1x * e2y - e1y * e2x;
            // view vector from the eye to the face center
            const float vcx = (a[0] + b[0] + c[0] + d[0]) * 0.25f - cam.ox;
            const float vcy = (a[1] + b[1] + c[1] + d[1]) * 0.25f - cam.oy;
            const float vcz = (a[2] + b[2] + c[2] + d[2]) * 0.25f - cam.oz;
            if (nx * vcx + ny * vcy + nz * vcz >= 0.f)
            {
                continue; // back face: a convex box hides it
            }

            const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (nl < 1e-6f)
            {
                continue;
            }
            nx /= nl;
            ny /= nl;
            nz /= nl;
            float lam = nx * paint.light[0] + ny * paint.light[1] +
                        nz * paint.light[2];
            if (lam < 0.f)
            {
                lam = 0.f;
            }

            float bright = paint.ambient + paint.lambert * lam;
            if (paint.hemi > 0.f)
            {
                // hemisphere ambient: a face whose normal points into the
                // floor sits in the darker half of the sky. The factor is
                // 1.0 for an up-facing face and (1-hemi) for a down-facing
                // one, and it scales the WHOLE sum, lambert included -- an
                // earlier version of this comment said it reweighted the
                // ambient only, which is not what the line below does.
                //
                // What the callers' brightness budget needs is only that
                // the PEAK is untouched, and it is: the brightest face is
                // up-facing, where the factor is exactly 1. So no face can
                // come out brighter than ambient + lambert, and the sum they
                // calibrated against fog reaching 1.0 still holds.
                bright *= 1.f - paint.hemi +
                          paint.hemi * (0.5f + 0.5f * ny);
            }
            if (paint.rim > 0.f)
            {
                // silhouette light: the more edge-on the face (|n·view|
                // near zero), the bigger the boost, cubed so only the
                // grazing faces light up. Per-face constant -- this is
                // the whole rim trick adapted to a box, where "the
                // silhouette" is literally the last visible face.
                const float vl = std::sqrt(vcx * vcx + vcy * vcy +
                                           vcz * vcz);
                if (vl > 1e-6f)
                {
                    float ndv = (nx * vcx + ny * vcy + nz * vcz) / vl;
                    if (ndv < 0.f)
                    {
                        ndv = -ndv;
                    }
                    const float rv = 1.f - ndv;
                    bright += paint.rim * rv * rv * rv * paint.fog;
                }
            }

            FaceShade sh;
            sh.tone = paint.tone;
            sh.base = styles[fi].base;
            sh.bright = bright * paint.fog * paint.ao;
            sh.grad = styles[fi].grad;
            sh.flash = paint.flash;
            sh.face_top = std::min(std::min(s[f[0]].sy, s[f[1]].sy),
                                   std::min(s[f[2]].sy, s[f[3]].sy));
            sh.face_bot = std::max(std::max(s[f[0]].sy, s[f[1]].sy),
                                   std::max(s[f[2]].sy, s[f[3]].sy));

            // the face's four corners in winding order, contiguous for the
            // quad fill (kFaces lists indices, not a run)
            const Vtx quad[4] = {s[f[0]], s[f[1]], s[f[2]], s[f[3]]};
            raster_quad(t, cam, quad, sh);
        }
    }
} // namespace zb::raster