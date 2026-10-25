#include "imraster/mesh.hpp"

#include <algorithm>
#include <cmath>

namespace zb::raster
{
    namespace
    {
        /*
         * The Gouraud cut-over, in pixels of bounding box (see the long
         * comment in draw_mesh): at or above this a triangle is lit at its
         * vertices and the colour interpolated, because that is where the
         * exact per-pixel path's two square roots per pixel dominate. A
         * body -- a trooper metres away -- stays under it and keeps exact
         * per-pixel normals everywhere.
         */
        constexpr int kGouraudPixels = 2500;

        // front-to-back triangle order scratch (see draw_mesh). File
        // scope, not function statics: a lambda cannot capture a static,
        // and clang (the wasm build) rejects the attempt where gcc lets
        // it through -- so the sort key lives here.
        std::vector<int> g_tri_order;
        std::vector<float> g_tri_key;
    } // namespace

    void MeshBuf::reset()
    {
        verts.clear();
        tris.clear();
        // uv too: it is a sidecar of verts, and leaving it behind would
        // both leak it across frames and break the size check the texture
        // path uses to decide whether this body has UVs at all
        uv.clear();
    }

    int MeshBuf::add_vert(const float x, const float y, const float z,
                          const float nx, const float ny, const float nz)
    {
        verts.push_back(MeshVert{x, y, z, nx, ny, nz});
        return static_cast<int>(verts.size()) - 1;
    }

    /*
     * One triangle with self-healing winding: (ref_x, ref_y, ref_z) is a
     * point that must lie OUTSIDE the closed surface this triangle belongs
     * to (a point pushed out along the ring's radial, say). If the
     * geometric normal points away from it, the winding is flipped before
     * it is stored. That is what lets the capsule and sphere builders
     * below be written without ever debugging an inside-out body: winding
     * errors become culling errors, and a culled-front body is obvious in
     * the first frame it renders.
     */
    void MeshBuf::add_tri(const int a, const int b, const int c,
                          const int part, const float ao, const float ref_x,
                          const float ref_y, const float ref_z)
    {
        MeshTri tr{a, b, c, static_cast<unsigned char>(part), ao};
        const MeshVert &A = verts[static_cast<size_t>(a)];
        const MeshVert &B = verts[static_cast<size_t>(b)];
        const MeshVert &C = verts[static_cast<size_t>(c)];
        const float e1x = B.x - A.x, e1y = B.y - A.y, e1z = B.z - A.z;
        const float e2x = C.x - A.x, e2y = C.y - A.y, e2z = C.z - A.z;
        const float nx = e1y * e2z - e1z * e2y;
        const float ny = e1z * e2x - e1x * e2z;
        const float nz = e1x * e2y - e1y * e2x;
        if (nx * (ref_x - A.x) + ny * (ref_y - A.y) + nz * (ref_z - A.z) < 0.f)
        {
            const int tmp = tr.b;
            tr.b = tr.c;
            tr.c = tmp;
        }
        tris.push_back(tr);
    }

    void MeshBuf::add_capsule(const float ax, const float ay, const float az,
                              const float bx, const float by, const float bz,
                              const float r0, const float r1, const int seg,
                              const int rings, const int part, const float ao)
    {
        float dx = bx - ax, dy = by - ay, dz = bz - az;
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 1e-5f || seg < 3 || rings < 1)
        {
            return;
        }
        dx /= len;
        dy /= len;
        dz /= len;
        // ring frame: world up crossed with the axis, unless the axis
        // is world up, in which case world x
        float rfx = 0.f, rfy = 1.f, rfz = 0.f;
        if (dy > 0.9f || dy < -0.9f)
        {
            rfx = 1.f;
            rfy = 0.f;
        }
        float ux = rfy * dz - rfz * dy;
        float uy = rfz * dx - rfx * dz;
        float uz = rfx * dy - rfy * dx;
        const float ul = std::sqrt(ux * ux + uy * uy + uz * uz);
        ux /= ul;
        uy /= ul;
        uz /= ul;
        const float vx = dy * uz - dz * uy;
        const float vy = dz * ux - dx * uz;
        const float vz = dx * uy - dy * ux;

        const float tilt = std::atan2(r1 - r0, len);
        const float ct = std::cos(tilt), st = std::sin(tilt);

        ring_prev_.clear();
        ring_curr_.clear();
        for (int i = 0; i <= rings; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(rings);
            const float cx = ax + (bx - ax) * t;
            const float cy = ay + (by - ay) * t;
            const float cz = az + (bz - az) * t;
            const float r = r0 + (r1 - r0) * t;
            for (int j = 0; j < seg; ++j)
            {
                const float ang = 6.2831853f * static_cast<float>(j) /
                                  static_cast<float>(seg);
                const float ca = std::cos(ang), sa = std::sin(ang);
                const float radx = ux * ca + vx * sa;
                const float rady = uy * ca + vy * sa;
                const float radz = uz * ca + vz * sa;
                ring_curr_.push_back(add_vert(
                    cx + radx * r, cy + rady * r, cz + radz * r,
                    radx * ct - dx * st, rady * ct - dy * st,
                    radz * ct - dz * st));
            }
            if (i > 0)
            {
                for (int j = 0; j < seg; ++j)
                {
                    const int jn = (j + 1) % seg;
                    const float ang = 6.2831853f * static_cast<float>(j) /
                                      static_cast<float>(seg);
                    const float ca = std::cos(ang), sa = std::sin(ang);
                    // an outward reference point off the ring's radial,
                    // for the winding self-heal
                    const float rr = r * 1.5f + 0.02f;
                    const float rx = cx + (ux * ca + vx * sa) * rr;
                    const float ry = cy + (uy * ca + vy * sa) * rr;
                    const float rz = cz + (uz * ca + vz * sa) * rr;
                    const int p0 = ring_prev_[static_cast<size_t>(j)];
                    const int p1 = ring_prev_[static_cast<size_t>(jn)];
                    const int c0 = ring_curr_[static_cast<size_t>(j)];
                    const int c1 = ring_curr_[static_cast<size_t>(jn)];
                    add_tri(p0, p1, c1, part, ao, rx, ry, rz);
                    add_tri(p0, c1, c0, part, ao, rx, ry, rz);
                }
            }
            ring_prev_.swap(ring_curr_);
            ring_curr_.clear();
        }
    }

    void MeshBuf::add_sphere(const float cx, const float cy, const float cz,
                             const float r, const int seg, const int rings,
                             const int part, const float ao)
    {
        if (seg < 3 || rings < 2)
        {
            return;
        }
        const int top = add_vert(cx, cy + r, cz, 0.f, 1.f, 0.f);
        ring_prev_.clear();
        ring_curr_.clear();
        for (int i = 1; i < rings; ++i)
        {
            const float phi = 3.14159265f * static_cast<float>(i) /
                              static_cast<float>(rings);
            const float sp = std::sin(phi), cp = std::cos(phi);
            for (int j = 0; j < seg; ++j)
            {
                const float ang = 6.2831853f * static_cast<float>(j) /
                                  static_cast<float>(seg);
                const float ca = std::cos(ang), sa = std::sin(ang);
                const float dx = sp * ca, dyy = cp, dz = sp * sa;
                ring_curr_.push_back(add_vert(cx + dx * r, cy + dyy * r,
                                              cz + dz * r, dx, dyy, dz));
            }
            for (int j = 0; j < seg; ++j)
            {
                const int jn = (j + 1) % seg;
                const float ang = 6.2831853f * static_cast<float>(j) /
                                  static_cast<float>(seg);
                const float ca = std::cos(ang), sa = std::sin(ang);
                const float rx = cx + sp * ca * (r * 1.5f + 0.02f);
                const float ry = cy + cp * (r * 1.5f + 0.02f);
                const float rz = cz + sp * sa * (r * 1.5f + 0.02f);
                if (i == 1)
                {
                    add_tri(top, ring_curr_[static_cast<size_t>(jn)],
                            ring_curr_[static_cast<size_t>(j)], part, ao, rx,
                            ry, rz);
                }
                else
                {
                    const int p0 = ring_prev_[static_cast<size_t>(j)];
                    const int p1 = ring_prev_[static_cast<size_t>(jn)];
                    const int c0 = ring_curr_[static_cast<size_t>(j)];
                    const int c1 = ring_curr_[static_cast<size_t>(jn)];
                    add_tri(p0, p1, c1, part, ao, rx, ry, rz);
                    add_tri(p0, c1, c0, part, ao, rx, ry, rz);
                }
            }
            ring_prev_.swap(ring_curr_);
            ring_curr_.clear();
        }
        const int bot = add_vert(cx, cy - r, cz, 0.f, -1.f, 0.f);
        for (int j = 0; j < seg; ++j)
        {
            const int jn = (j + 1) % seg;
            const float ang = 6.2831853f * static_cast<float>(j) /
                              static_cast<float>(seg);
            const float ca = std::cos(ang), sa = std::sin(ang);
            const float sp =
                std::sin(3.14159265f * static_cast<float>(rings - 1) /
                         static_cast<float>(rings));
            const float rx = cx + sp * ca * (r * 1.5f + 0.02f);
            const float ry = cy - (r * 1.5f + 0.02f);
            const float rz = cz + sp * sa * (r * 1.5f + 0.02f);
            add_tri(bot, ring_prev_[static_cast<size_t>(j)],
                    ring_prev_[static_cast<size_t>(jn)], part, ao, rx, ry, rz);
        }
    }

    namespace
    {
        /*
         * Near-plane clipping, as a PRE-PASS and only when something needs
         * it. project() clamps the view depth, so a body that straddles
         * the near plane would otherwise smear: the first-person weapon
         * is the case that needs it (at the hip pose its stock is behind
         * the eye, and a real viewmodel simply is not drawn back to the
         * near plane), but every trooper stands metres out and must not
         * pay for a scan it does not need -- so the answer is "false", and
         * the caller keeps indexing the caller's own buffer.
         *
         * With an orthonormal camera basis the forward axis IS the view
         * depth, which makes the near plane a plain linear inequality:
         * positions interpolate across it exactly, and so do normals
         * (renormalized, because an interpolated normal is not unit
         * length). Clipping a triangle keeps its winding, which is what
         * the back-face test downstream depends on.
         */
        bool clip_near(MeshBuf &out, const Camera &cam, const MeshBuf &in)
        {
            const float near = cam.near_clip;
            const size_t nv = in.verts.size();
            if (near <= 0.f || nv == 0)
            {
                return false;
            }
            static std::vector<float> dep;
            dep.resize(nv);
            bool need = false;
            for (size_t i = 0; i < nv; ++i)
            {
                const MeshVert &v = in.verts[i];
                const float d = (v.x - cam.ox) * cam.fx +
                                (v.y - cam.oy) * cam.fy +
                                (v.z - cam.oz) * cam.fz;
                dep[i] = d;
                if (d < near)
                {
                    need = true;
                }
            }
            if (!need)
            {
                return false;
            }

            // UVs ride through the clip: the sidecar is what the textured
            // path samples, and a clipped body that lost them would draw
            // untextured for exactly the frames where it is closest to the
            // camera (the viewmodel's recoil kick).
            const bool keep_uv = in.uv.size() == nv * 2;
            out.reset();
            out.verts.reserve(nv + 16);
            if (keep_uv)
            {
                out.uv.reserve(nv * 2 + 32);
            }
            static std::vector<int> remap;
            remap.assign(nv, -1);
            const auto lerp_vert = [&](const int i0, const int i1,
                                       const float t) {
                const MeshVert &a = in.verts[static_cast<size_t>(i0)];
                const MeshVert &b = in.verts[static_cast<size_t>(i1)];
                MeshVert m;
                m.x = a.x + (b.x - a.x) * t;
                m.y = a.y + (b.y - a.y) * t;
                m.z = a.z + (b.z - a.z) * t;
                m.nx = a.nx + (b.nx - a.nx) * t;
                m.ny = a.ny + (b.ny - a.ny) * t;
                m.nz = a.nz + (b.nz - a.nz) * t;
                const float l = std::sqrt(m.nx * m.nx + m.ny * m.ny +
                                          m.nz * m.nz);
                if (l > 1e-6f)
                {
                    m.nx /= l;
                    m.ny /= l;
                    m.nz /= l;
                }
                else
                {
                    m.nx = 0.f;
                    m.ny = 0.f;
                    m.nz = 0.f;
                }
                return m;
            };
            const auto lerp_uv = [&](const int i0, const int i1,
                                     const float t) {
                const float u0 = in.uv[static_cast<size_t>(i0) * 2 + 0];
                const float u1 = in.uv[static_cast<size_t>(i1) * 2 + 0];
                const float v0 = in.uv[static_cast<size_t>(i0) * 2 + 1];
                const float v1 = in.uv[static_cast<size_t>(i1) * 2 + 1];
                out.uv.push_back(u0 + (u1 - u0) * t);
                out.uv.push_back(v0 + (v1 - v0) * t);
            };
            const auto copy_uv = [&](const int i) {
                out.uv.push_back(in.uv[static_cast<size_t>(i) * 2 + 0]);
                out.uv.push_back(in.uv[static_cast<size_t>(i) * 2 + 1]);
            };

            for (const MeshTri &tr : in.tris)
            {
                const int corner[3] = {tr.a, tr.b, tr.c};
                int idx[4] = {0, 0, 0, 0};
                int n = 0;
                for (int k = 0; k < 3; ++k)
                {
                    const int i0 = corner[k];
                    const int i1 = corner[(k + 1) % 3];
                    const bool in0 = dep[static_cast<size_t>(i0)] >= near;
                    const bool in1 = dep[static_cast<size_t>(i1)] >= near;
                    // the clipped buffer is its OWN vertex list, so a
                    // corner that survives is copied once and referred to
                    // by its new index (remap[]), not by the input's
                    if (in0 && n < 4)
                    {
                        int &slot = remap[static_cast<size_t>(i0)];
                        if (slot < 0)
                        {
                            slot = static_cast<int>(out.verts.size());
                            out.verts.push_back(
                                in.verts[static_cast<size_t>(i0)]);
                            if (keep_uv)
                            {
                                copy_uv(i0);
                            }
                        }
                        idx[n++] = slot;
                    }
                    if (in0 != in1 && n < 4)
                    {
                        const float d0 = dep[static_cast<size_t>(i0)];
                        const float d1 = dep[static_cast<size_t>(i1)];
                        const float tt = (near - d0) / (d1 - d0);
                        idx[n] = static_cast<int>(out.verts.size());
                        out.verts.push_back(lerp_vert(i0, i1, tt));
                        if (keep_uv)
                        {
                            lerp_uv(i0, i1, tt);
                        }
                        ++n;
                    }
                }
                for (int k = 2; k < n; ++k)
                {
                    out.tris.push_back(
                        MeshTri{idx[0], idx[k - 1], idx[k], tr.part, tr.ao});
                }
            }
            return true;
        }
    } // namespace

    void draw_mesh(Canvas &t, const Camera &cam, const MeshBuf &buf,
                   const MeshPaint &paint)
    {
        const int gw = t.world_w();
        const int gh = t.world_h();
        if (gw <= 0 || gh <= 0 || buf.tris.empty() || buf.verts.empty())
        {
            return;
        }
        // the clipped copy is scratch that outlives the call, and only
        // exists when the body actually crosses the near plane
        static MeshBuf clipped;
        const bool was_clipped = clip_near(clipped, cam, buf);
        if (was_clipped && clipped.tris.empty())
        {
            return; // entirely behind the near plane: nothing to draw
        }
        const MeshBuf &mesh = was_clipped ? clipped : buf;
        if (paint.part_colors == nullptr || paint.part_count <= 0)
        {
            return;
        }
        float *const mesh_z = t.mesh_depth_data();
        if (mesh_z == nullptr ||
            t.mesh_depth_count() !=
                static_cast<size_t>(gw) * static_cast<size_t>(gh))
        {
            return; // the canvas owes a reset_mesh_depth()/resize
        }

        // whether any column has a wall in front of it at all; the inner
        // loop tests it as a flag rather than re-reading the buffer
        const bool have_walls = cam.depth != nullptr;

        /*
         * The world's depth for a body drawn at 1:1 over an upscaled
         * world (MeshPaint::world_depth). The caller owns the buffer and
         * says how big the grid was; all that is needed here is the
         * surface-to-grid ratio, so the per-pixel test is two multiplies
         * and a load. Only the path that asked for it pays anything.
         *
         * The ratio is against `gw`/`gh` -- the resolution THIS draw
         * writes -- not against the grid: when the body is drawn on the
         * grid the two are equal and the ratio is 1, which is the right
         * answer for a caller that hands in the grid's own zbuf.
         */
        const bool have_world_z = paint.world_depth != nullptr &&
                                  paint.world_w > 0 && paint.world_h > 0;
        const float wz_sx = have_world_z
                                ? static_cast<float>(paint.world_w) /
                                      static_cast<float>(gw)
                                : 0.f;
        const float wz_sy = have_world_z
                                ? static_cast<float>(paint.world_h) /
                                      static_cast<float>(gh)
                                : 0.f;

        // per-column ray terms: the eye-to-pixel direction is
        //     dir = forward + right * ax[x] + up * ay[y]
        // which is what project() encodes: sx = vw/2 + u*vscale/v,
        // sy = horizon - up*vscale/v, with vscale() = vw/(2*plane) for
        // BOTH axes (raster.hpp: square pixels). |dir|^2 = 1 + ax^2 + ay^2
        // because the basis is orthonormal, so the per-pixel normalize is
        // one sqrt.
        //
        // ay used to divide by cam.vh, which is the same anamorphic bug
        // project() had: it made the per-pixel ray direction disagree with
        // the vertex projection, so the rim/silhouette term was lit as if
        // the vertical FOV were a 4:3 one.
        const float ivscale = 1.f / vscale(cam);
        static std::vector<float> ax, ax2, iwall;
        ax.resize(static_cast<size_t>(gw));
        ax2.resize(static_cast<size_t>(gw));
        for (int x = 0; x < gw; ++x)
        {
            ax[static_cast<size_t>(x)] =
                cam.plane * (2.f * static_cast<float>(x) + 1.f -
                             static_cast<float>(gw)) /
                static_cast<float>(gw);
            ax2[static_cast<size_t>(x)] =
                ax[static_cast<size_t>(x)] * ax[static_cast<size_t>(x)];
        }
        // walls as 1/depth so the per-pixel test is one compare in iw
        // space: occluded means iw <= 1/wall. A column with NO wall in
        // front of it fills 0 -- "nothing occludes" -- because 0 is what
        // no depth means in iw space; filling 1/0 instead would read as
        // a wall sitting at the eye and skip every pixel in the column.
        iwall.resize(static_cast<size_t>(gw));
        if (cam.depth != nullptr)
        {
            for (int x = 0; x < gw; ++x)
            {
                const float w = cam.depth[x];
                iwall[static_cast<size_t>(x)] =
                    (w > 0.f && w < 1e8f) ? 1.f / w : 0.f;
            }
        }
        else
        {
            std::fill(iwall.begin(), iwall.end(), 0.f);
        }

        // project: screen position plus the perspective term, from the
        // caller's world-space vertices (normals arrive world-space too)
        static std::vector<float> px_buf, nx_buf;
        px_buf.resize(mesh.verts.size() * 3); // screen x/y/iw
        nx_buf.resize(mesh.verts.size() * 3); // world normal
        for (size_t i = 0; i < mesh.verts.size(); ++i)
        {
            const MeshVert &mv = mesh.verts[i];
            const Vtx s = project(cam, mv.x, mv.y, mv.z);
            px_buf[i * 3] = s.sx;
            px_buf[i * 3 + 1] = s.sy;
            px_buf[i * 3 + 2] = s.iw;
            nx_buf[i * 3] = mv.nx;
            nx_buf[i * 3 + 1] = mv.ny;
            nx_buf[i * 3 + 2] = mv.nz;
        }

        const int tri_count = static_cast<int>(mesh.tris.size());
        // Front to back. The per-pixel depth test rejects an already-
        // covered pixel BEFORE any shading work, so painting the near
        // triangles first is what makes a dense body affordable: the
        // first-person weapon is a pile of overlapping boxes that fills
        // a quarter of the screen, and drawn back to front every one of
        // its triangles would shade pixels the next one overwrites.
        // The depth buffer decides the outcome either way, so this is a
        // pure work saving -- only the triangle COUNT pays for the sort.
        if (tri_count > 1)
        {
            g_tri_order.resize(static_cast<size_t>(tri_count));
            g_tri_key.resize(static_cast<size_t>(tri_count));
            for (int i = 0; i < tri_count; ++i)
            {
                g_tri_order[static_cast<size_t>(i)] = i;
                // (renamed from upstream's `t`: it shadowed the Canvas
                // parameter of the same name -- -Wshadow, X-20; no
                // arithmetic touched)
                const MeshTri &tri = mesh.tris[static_cast<size_t>(i)];
                g_tri_key[static_cast<size_t>(i)] =
                    (px_buf[static_cast<size_t>(tri.a) * 3 + 2] +
                     px_buf[static_cast<size_t>(tri.b) * 3 + 2] +
                     px_buf[static_cast<size_t>(tri.c) * 3 + 2]) /
                    3.f;
            }
            const std::vector<float> &key = g_tri_key;
            std::sort(g_tri_order.begin(), g_tri_order.end(),
                      [&key](const int a, const int b) {
                          return key[static_cast<size_t>(a)] >
                                 key[static_cast<size_t>(b)];
                      });
        }
        for (int oi = 0; oi < tri_count; ++oi)
        {
            const int ti = tri_count > 1 ? g_tri_order[static_cast<size_t>(oi)]
                                         : oi;
            const MeshTri &tr = mesh.tris[static_cast<size_t>(ti)];
            const int ia = tr.a, ib = tr.b, ic = tr.c;
            const float *pa = &px_buf[static_cast<size_t>(ia) * 3];
            const float *pb = &px_buf[static_cast<size_t>(ib) * 3];
            const float *pc = &px_buf[static_cast<size_t>(ic) * 3];
            const float x0 = pa[0], y0 = pa[1], w0 = pa[2];
            const float x1 = pb[0], y1 = pb[1], w1 = pb[2];
            const float x2 = pc[0], y2 = pc[1], w2 = pc[2];

            // back-face: the winding is outward (self-healed at build),
            // so a visible face's geometric normal points at the eye
            const MeshVert &va = mesh.verts[static_cast<size_t>(ia)];
            const MeshVert &vb = mesh.verts[static_cast<size_t>(ib)];
            const MeshVert &vc = mesh.verts[static_cast<size_t>(ic)];
            const float e1x = vb.x - va.x, e1y = vb.y - va.y,
                        e1z = vb.z - va.z;
            const float e2x = vc.x - va.x, e2y = vc.y - va.y,
                        e2z = vc.z - va.z;
            const float fnx = e1y * e2z - e1z * e2y;
            const float fny = e1z * e2x - e1x * e2z;
            const float fnz = e1x * e2y - e1y * e2x;
            if (fnx * (va.x - cam.ox) + fny * (va.y - cam.oy) +
                    fnz * (va.z - cam.oz) >=
                0.f)
            {
                continue;
            }

            // screen bounding box, clamped to the surface
            float sxmin = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
            float sxmax = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
            float symin = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
            float symax = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
            int ix0 = static_cast<int>(sxmin) - 1;
            int ix1 = static_cast<int>(sxmax) + 2;
            int iy0 = static_cast<int>(symin) - 1;
            int iy1 = static_cast<int>(symax) + 2;
            if (ix0 < 0)
            {
                ix0 = 0;
            }
            if (iy0 < 0)
            {
                iy0 = 0;
            }
            if (ix1 > gw - 1)
            {
                ix1 = gw - 1;
            }
            if (iy1 > gh - 1)
            {
                iy1 = gh - 1;
            }
            if (ix1 < ix0 || iy1 < iy0)
            {
                continue;
            }

            const float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
            if (std::fabs(area) < 1e-10f)
            {
                continue;
            }
            const float inv = 1.f / area;

            // fog at the face: one value per triangle, the same
            // granularity the box path uses per box
            const float iw_c = (w0 + w1 + w2) / 3.f;
            float vtri = iw_c > 1e-6f ? 1.f / iw_c : 1e6f;
            if (vtri < 0.12f)
            {
                vtri = 0.12f;
            }
            float fog_t = 1.f - vtri / paint.fog_dist;
            if (fog_t < paint.fog_min)
            {
                fog_t = paint.fog_min;
            }
            // the lerp mode owns the multiply: it is the complement of the
            // haze weight below, so it is computed from there and the
            // caller's fog_min does not apply (see MeshPaint::haze_lerp)
            float bright_fog = fog_t * tr.ao;
            // haze, fully hoisted: the addend is three floats and the pixel
            // loop pays three adds behind one per-draw-call-predictable
            // branch (same reasoning as the alpha comment below). The
            // textured store clamps the sum (bright texel + haze can exceed
            // 255); the gouraud store already did.
            // Squared, and against haze_dist rather than fog_dist: fog is
            // tuned as a multiply (gentle at mid range), but as an addend
            // driver 1-fog hazes +60 onto a trooper at 5 cells and greys
            // it out by 12. Haze distance is where air perspective lives
            // (~past 10 cells), and characters use a dimmer haze colour on
            // top, so they stay readable far past where the world melts.
            const bool use_haze = paint.haze != 0u;
            float hz_r = 0.f, hz_g = 0.f, hz_b = 0.f;
            if (use_haze)
            {
                float hf;
                if (paint.haze_lerp)
                {
                    // out = lit*(1-h) + haze*h: the surface fades into the
                    // air and the ground's brightness has no trough. The
                    // multiply is read back from the same h, so the two
                    // weights cannot drift apart (MeshPaint::haze_lerp).
                    hf = vtri / paint.fog_dist;
                    hf = hf < 0.f ? 0.f : (hf > 1.f ? 1.f : hf);
                    bright_fog = (1.f - hf) * tr.ao;
                }
                else
                {
                    const float hd = paint.haze_dist > 0.f
                                         ? paint.haze_dist
                                         : paint.fog_dist;
                    hf = vtri / hd;
                    hf = hf < 0.f ? 0.f : (hf > 1.f ? 1.f : hf);
                    hf *= hf;
                }
                hz_r = static_cast<float>(unpack_r(paint.haze)) * hf;
                hz_g = static_cast<float>(unpack_g(paint.haze)) * hf;
                hz_b = static_cast<float>(unpack_b(paint.haze)) * hf;
            }
            const float hz[3] = {hz_r, hz_g, hz_b};
            // a part id past the table paints as the last entry: a stale
            // id degrades to one material instead of reading past the end
            const int pi = tr.part < paint.part_count ? tr.part
                                                      : paint.part_count - 1;
            const uint32_t base = paint.part_colors[pi];
            const float cr = static_cast<float>(unpack_r(base));
            const float cg = static_cast<float>(unpack_g(base));
            const float cb = static_cast<float>(unpack_b(base));
            const uint8_t *tone = paint.tone;
            // the material colour, mutable because a texture modulates it
            // per pixel; untextured it is the same three numbers it always
            // was, and the arithmetic below is unchanged
            float cr_t = cr, cg_t = cg, cb_t = cb;
            float gr = 1.f, ggn = 1.f, gbn = 1.f; // texture gain, per channel

            /*
             * Texture, when the caller gave one AND the body carries UVs.
             * Two things are hoisted out of the pixel loop because they
             * are per-triangle: the three UVs, and the affine step of the
             * texture coordinates across the screen -- u and v are linear
             * in screen x and y across a planar triangle, so the whole
             * per-pixel fetch is two adds, one mask and one load.
             *
             * The steps come straight out of the barycentric gradients
             * rather than from a separate uv-to-screen solve: du/dx is
             * (r0's x-step * u_a + r1's * u_b + r2's * u_c) * inv, the
             * same trick the box path uses for its wall columns.
             */
            const float *ua = nullptr;
            const float *ub = nullptr;
            const float *uc = nullptr;
            float tex_du_dx = 0.f, tex_dv_dx = 0.f;
            float tex_du_dy = 0.f, tex_dv_dy = 0.f;
            const int tmask = paint.tex_size > 0 ? paint.tex_size - 1 : 0;
            const float tscale = paint.contrast;
            const bool textured =
                paint.texels != nullptr && paint.tex_size > 0 &&
                mesh.uv.size() == mesh.verts.size() * 2 &&
                paint.contrast > 0.f;
            // Selecting only when a chain was handed in: a caller that set
            // texels and left mips empty must get byte-identical output to
            // what it got before, and this is what guarantees it.
            const bool tsel = textured && paint.mip_levels > 1 &&
                              paint.mips[0] != nullptr;
            // the mip level this triangle samples, and the mask and scale
            // for it. tlevel 0 is the default, so a caller with no chain
            // (mip_levels < 2) leaves every line below exactly as it was.
            int tlevel = 0;
            int tlmask = tmask;
            float tuscale = 1.f;
            if (textured)
            {
                ua = &mesh.uv[static_cast<size_t>(ia) * 2];
                ub = &mesh.uv[static_cast<size_t>(ib) * 2];
                uc = &mesh.uv[static_cast<size_t>(ic) * 2];
            }

            // edge function deltas. The values themselves are recomputed
            // at each ROW's first pixel center rather than carried down
            // the triangle: three evaluations per row is noise next to
            // the per-pixel work, and an accumulation bug in the row
            // step is the kind of thing that shows up as a one-pixel
            // diagonal seam on exactly one limb of exactly one kit.
            const float e0_dx = y1 - y2, e1_dx = y2 - y0, e2_dx = y0 - y1;
            // inv folded into the per-column steps (see the row loop), and
            // the per-ROW steps of the same edge functions (f0..f2), which
            // the texture path needs
            const float d0 = e0_dx * inv, d1 = e1_dx * inv, d2 = e2_dx * inv;
            const float f0 = (x2 - x1) * inv;
            const float f1 = (x0 - x2) * inv;
            const float f2 = (x1 - x0) * inv;
            if (textured)
            {
                // the UV steps, from the barycentric gradients (see above)
                tex_du_dx =
                    (e0_dx * ua[0] + e1_dx * ub[0] + e2_dx * uc[0]) * inv;
                tex_dv_dx =
                    (e0_dx * ua[1] + e1_dx * ub[1] + e2_dx * uc[1]) * inv;
                tex_du_dy = (f0 * ua[0] + f1 * ub[0] + f2 * uc[0]) * inv;
                tex_dv_dy = (f0 * ua[1] + f1 * ub[1] + f2 * uc[1]) * inv;

                /*
                 * Which mip level this triangle samples, from how many
                 * TEXELS one output pixel covers.
                 *
                 * tex_du_dx and tex_du_dy are the UV gradient in texel
                 * units per pixel, and the steeper of the two decides:
                 * a surface seen edge-on minifies along one axis long
                 * before it does along the other, which is why the max
                 * rather than the average. A gradient under one texel per
                 * pixel is magnification, where the chain would only cost
                 * detail -- so level 0 there.
                 *
                 * Chosen once per triangle, from the gradient at its
                 * steepest, for the two reasons in MeshPaint: the gradient
                 * cannot vary across a triangle by anything like a level,
                 * and a per-pixel branch here would live in the innermost
                 * loop of the most expensive pass in the frame.
                 */
                if (tsel)
                {
                    const float gx = tex_du_dx > 0.f ? tex_du_dx : -tex_du_dx;
                    const float gxi = tex_dv_dx > 0.f ? tex_dv_dx : -tex_dv_dx;
                    const float gy = tex_du_dy > 0.f ? tex_du_dy : -tex_du_dy;
                    const float gyi = tex_dv_dy > 0.f ? tex_dv_dy : -tex_dv_dy;
                    const float g1 = gx > gy ? gx : gy;
                    const float g2 = gxi > gyi ? gxi : gyi;
                    const float grad = g1 > g2 ? g1 : g2;
                    int lv = 0;
                    while (lv + 1 < paint.mip_levels &&
                           grad >= static_cast<float>(1 << (lv + 1)))
                    {
                        ++lv;
                    }
                    if (lv > 0)
                    {
                        tlevel = lv;
                        tlmask = paint.mip_sizes[lv] - 1;
                        // the UVs are authored against level 0's grid, so
                        // reaching a smaller level means scaling the
                        // coordinate, not re-authoring it
                        tuscale = static_cast<float>(paint.mip_sizes[lv]) /
                                  static_cast<float>(paint.tex_size);
                    }
                }
            }
            (void)tex_du_dy;
            (void)tex_dv_dy;
            const float *na = &nx_buf[static_cast<size_t>(ia) * 3];
            const float *nb = &nx_buf[static_cast<size_t>(ib) * 3];
            const float *nc = &nx_buf[static_cast<size_t>(ic) * 3];

            /*
             * Two shading paths, picked by how many pixels the triangle
             * owns. A triangle covering a big area -- the viewmodel's
             * receiver and stock, which between them are most of the
             * weapon's pixels -- is lit at its three VERTICES and the
             * colour interpolated per pixel (Gouraud): that drops the
             * per-pixel normal normalize (a sqrt and three divides) and
             * the rim term's second sqrt, which is the whole cost of
             * the exact path. A small triangle keeps the exact path,
             * because on a curved surface that is where the normal
             * varies across a face and the difference is visible.
             *
             * The threshold is on the bounding box, which over-counts a
             * thin triangle; over-counting only means a few more
             * triangles take the cheap path, and the two paths agree
             * closely wherever a triangle is small enough for the
             * difference to show.
             */
            const int cover = (ix1 - ix0 + 1) * (iy1 - iy0 + 1);
            float vr0 = 0.f, vg0 = 0.f, vb0 = 0.f;
            float vr1 = vr0, vg1 = vg0, vb1 = vb0;
            float vr2 = vr0, vg2 = vg0, vb2 = vb0;
            const bool gouraud = cover >= kGouraudPixels;
            if (gouraud)
            {
                const MeshVert *vv[3] = {&va, &vb, &vc};
                const float *nn[3] = {na, nb, nc};
                float *dst[3][3] = {{&vr0, &vg0, &vb0},
                                     {&vr1, &vg1, &vb1},
                                     {&vr2, &vg2, &vb2}};
                const float pal[3] = {cr, cg, cb};
                for (int k = 0; k < 3; ++k)
                {
                    const float *n = nn[k];
                    float lam = n[0] * paint.light_x + n[1] * paint.light_y +
                                n[2] * paint.light_z;
                    if (lam < 0.f)
                    {
                        lam = 0.f;
                    }
                    float bright = paint.ambient + paint.lambert * lam;
                    bright *= 1.f - paint.hemi +
                              paint.hemi * (0.5f + 0.5f * n[1]);
                    // the silhouette term, at the vertex: the ray to it
                    const float dx = vv[k]->x - cam.ox;
                    const float dy = vv[k]->y - cam.oy;
                    const float dz = vv[k]->z - cam.oz;
                    const float dl = std::sqrt(dx * dx + dy * dy + dz * dz);
                    float ndv = 0.f;
                    if (dl > 1e-6f)
                    {
                        ndv = (n[0] * dx + n[1] * dy + n[2] * dz) / dl;
                        if (ndv < 0.f)
                        {
                            ndv = -ndv;
                        }
                    }
                    const float rv = 1.f - ndv;
                    bright += paint.rim * rv * rv * rv * fog_t;
                    bright *= bright_fog;
                    for (int c = 0; c < 3; ++c)
                    {
                        float ch = pal[c] * bright;
                        if (use_haze)
                        {
                            ch += hz[c];
                        }
                        *dst[k][c] = ch > 255.f ? 255.f
                                                : (ch < 0.f ? 0.f : ch);
                    }
                }
            }

            /*
             * The row's own span. Walking the bounding box instead visits
             * every pixel of the box, and a triangle's box is on average
             * twice its area -- so a first-person weapon, whose triangles
             * are long thin slivers seen close up, spends most of its
             * inner loop on pixels it then throws away. Each edge function
             * is linear in x, so the three of them say exactly where the
             * row starts and ends.
             *
             * Solved per row rather than stepped: an incremental version
             * (three divisions hoisted, the crossing column carried) was
             * measured SLOWER at 1080p (9.6 ms vs 5.7 for the weapon
             * alone, native) -- the accumulator drift widens the spans,
             * and the per-row divides were never the cost.
             *
             * The span is deliberately one pixel WIDER than the arithmetic
             * says: the per-pixel inside test below stays the authority, so
             * a rounding slip costs a wasted pixel and a missing one is
             * impossible.
             */
            for (int y = iy0; y <= iy1; ++y)
            {
                uint32_t *row = t.world_row(y);
                float *zrow =
                    &mesh_z[static_cast<size_t>(y) * static_cast<size_t>(gw)];
                // the eye-to-row ray term and its length's reciprocal:
                // |dir|^2 = 1 + ax^2 + ay^2 on the orthonormal basis.
                // vscale, not vh -- see the per-column note above.
                const float pys = static_cast<float>(y) + 0.5f;
                const float ay = (cam.horizon - pys) * ivscale;
                const float ay2 = ay * ay;
                const float dir_bx = cam.fx + cam.ux * ay;
                const float dir_by = cam.fy + cam.uy * ay;
                const float dir_bz = cam.fz + cam.uz * ay;
                const float pxs = static_cast<float>(ix0) + 0.5f;
                // The edge functions carry inv already, so the inner loop
                // is three adds and no divides or multiplies by inv: at a
                // million shaded pixels a frame (the viewmodel at 1:1) that
                // is three million multiplies that do not happen.
                float r0 = ((x2 - x1) * (pys - y1) - (y2 - y1) * (pxs - x1) -
                            (y1 - y2)) *
                           inv;
                float r1 = ((x0 - x2) * (pys - y2) - (y0 - y2) * (pxs - x2) -
                            (y2 - y0)) *
                           inv;
                float r2 = ((x1 - x0) * (pys - y0) - (y1 - y0) * (pxs - x0) -
                            (y0 - y1)) *
                           inv;
                const float re[3] = {r0, r1, r2};
                const float de[3] = {d0, d1, d2};
                int xs = ix0;
                int xe = ix1;
                bool row_empty = false;
                for (int e = 0; e < 3; ++e)
                {
                    if (de[e] > 0.f)
                    {
                        const int xn =
                            ix0 + static_cast<int>(std::ceil(-re[e] / de[e]));
                        if (xn > xs)
                        {
                            xs = xn;
                        }
                    }
                    else if (de[e] < 0.f)
                    {
                        const int xn = ix0 +
                                       static_cast<int>(std::floor(-re[e] /
                                                                   de[e]));
                        if (xn < xe)
                        {
                            xe = xn;
                        }
                    }
                    else if (re[e] < 0.f)
                    {
                        row_empty = true; // parallel to the row and outside
                        break;
                    }
                }
                if (row_empty || xs > xe)
                {
                    continue;
                }
                if (xs > ix0)
                {
                    --xs;
                }
                if (xe < ix1)
                {
                    ++xe;
                }
                if (xs < ix0)
                {
                    xs = ix0;
                }
                if (xe > ix1)
                {
                    xe = ix1;
                }
                const float skip = static_cast<float>(xs - ix0);
                r0 += d0 * skip;
                r1 += d1 * skip;
                r2 += d2 * skip;
                // the texture coordinate at (xs, this row): the three
                // vertex UVs blended by the barycentrics there, then
                // stepped per pixel -- two adds per pixel instead of the
                // six multiplies a fresh barycentric blend would cost.
                float tu = 0.f, tv = 0.f;
                if (textured)
                {
                    const float l0 = r0, l1 = r1, l2 = r2;
                    tu = (l0 * ua[0] + l1 * ub[0] + l2 * uc[0]) * tuscale;
                    tv = (l0 * ua[1] + l1 * ub[1] + l2 * uc[1]) * tuscale;
                }
                // stepped in the scaled units, so the per-pixel adds below
                // are still plain additions
                const float stu = tex_du_dx * tuscale;
                const float stv = tex_dv_dx * tuscale;
                for (int x = xs; x <= xe; ++x)
                {
                    r0 += d0;
                    r1 += d1;
                    r2 += d2;
                    const float l0 = r0;
                    const float l1 = r1;
                    const float l2 = r2;
                    if (l0 < 0.f || l1 < 0.f || l2 < 0.f)
                    {
                        continue;
                    }
                    const float iw = l0 * w0 + l1 * w1 + l2 * w2;
                    // the per-column wall test only exists when the caller
                    // handed in a depth buffer; the viewmodel never does,
                    // and a load plus a compare per pixel is not free
                    if (iw <= zrow[x] ||
                        (have_walls && iw <= iwall[static_cast<size_t>(x)]))
                    {
                        continue; // behind the body's own surface, or a
                                  // wall stands in front of this column
                    }
                    /*
                     * The world, when this body is drawn SHARPER than it
                     * was (1:1 over an upscaled world). Sampled at the
                     * cell this pixel falls in, in iw space so it is one
                     * compare -- but with a tolerance, because the world's
                     * zbuf holds the value of the cell's CENTRE while the
                     * body's pixel may be anywhere inside that cell. The
                     * epsilon is in 1/depth, so it is looser far away and
                     * tighter close in, which is the direction the
                     * staircase error goes.
                     *
                     * Without this the body would paint over the wall it
                     * is standing behind, because set_replicate(1)
                     * reallocated the zbuf the world wrote.
                     */
                    if (have_world_z)
                    {
                        const int wz_x = static_cast<int>(
                            static_cast<float>(x) * wz_sx);
                        const int wz_y = static_cast<int>(
                            static_cast<float>(y) * wz_sy);
                        const float wz =
                            paint.world_depth[static_cast<size_t>(wz_y) *
                                                  static_cast<size_t>(
                                                      paint.world_w) +
                                              static_cast<size_t>(wz_x)];
                        if (wz > 0.f && iw <= wz + paint.world_iw_eps)
                        {
                            continue; // the world owns this pixel
                        }
                    }
                    zrow[x] = iw;

                    // the surface texture, multiplied into the material
                    // AFTER the lighting: a texel is a gain around 1.0, so
                    // it can darken a highlight but must not brighten past
                    // the same ceiling the untextured path has. The
                    // Gouraud path cannot take it -- its colours are lit
                    // per vertex, and a texture is a per-pixel story -- so
                    // a textured body keeps the exact shading path.
                    if (textured)
                    {
                        const int tu_i = static_cast<int>(tu) & tlmask;
                        const int tv_i = static_cast<int>(tv) & tlmask;
                        const uint32_t texel = tsel
                            ? paint.mips[tlevel][tv_i * (tlmask + 1) + tu_i]
                            : paint.texels[tv_i * paint.tex_size + tu_i];
                        tu += stu;
                        tv += stv;
                        // 128 is the neutral texel: the map is a GAIN
                        // around 1.0, never a replacement for the colour.
                        // Kept as a gain rather than folded into the
                        // material so that it multiplies EITHER shading
                        // path -- light and texture are separable, and a
                        // textured triangle on the Gouraud path is worth
                        // 8 ms a frame on the viewmodel (measured).
                        // (renamed from upstream's tr/tg/tb: `tr` shadowed
                        // the triangle reference `tr` of the enclosing loop
                        // -- -Wshadow, X-20; no arithmetic touched)
                        const float tex_r = static_cast<float>(unpack_r(texel));
                        const float tex_g = static_cast<float>(unpack_g(texel));
                        const float tex_b = static_cast<float>(unpack_b(texel));
                        gr = 1.f + tscale * (tex_r * 0.0078431f - 1.f);
                        ggn = 1.f + tscale * (tex_g * 0.0078431f - 1.f);
                        gbn = 1.f + tscale * (tex_b * 0.0078431f - 1.f);
                        cr_t = cr * gr;
                        cg_t = cg * ggn;
                        cb_t = cb * gbn;
                    }

                    int rr, gg, bbc;
                    if (gouraud)
                    {
                        rr = static_cast<int>((l0 * vr0 + l1 * vr1 + l2 * vr2) *
                                              gr);
                        gg = static_cast<int>((l0 * vg0 + l1 * vg1 + l2 * vg2) *
                                              ggn);
                        bbc = static_cast<int>((l0 * vb0 + l1 * vb1 + l2 * vb2) *
                                               gbn);
                        if (rr > 255)
                        {
                            rr = 255;
                        }
                        if (gg > 255)
                        {
                            gg = 255;
                        }
                        if (bbc > 255)
                        {
                            bbc = 255;
                        }
                    }
                    else
                    {
                        // the interpolated normal, normalized per pixel
                        float nx = l0 * na[0] + l1 * nb[0] + l2 * nc[0];
                        float ny = l0 * na[1] + l1 * nb[1] + l2 * nc[1];
                        float nz = l0 * na[2] + l1 * nb[2] + l2 * nc[2];
                        const float nl =
                            std::sqrt(nx * nx + ny * ny + nz * nz);
                        if (nl < 1e-6f)
                        {
                            continue;
                        }
                        nx /= nl;
                        ny /= nl;
                        nz /= nl;

                        float lam = nx * paint.light_x + ny * paint.light_y +
                                    nz * paint.light_z;
                        if (lam < 0.f)
                        {
                            lam = 0.f;
                        }
                        float bright = paint.ambient + paint.lambert * lam;
                        // hemisphere ambient, as the box path has it
                        bright *= 1.f - paint.hemi +
                                  paint.hemi * (0.5f + 0.5f * ny);
                        // silhouette light: the ray term per pixel,
                        // normalized
                        const float axx = ax[static_cast<size_t>(x)];
                        const float il =
                            1.f / std::sqrt(1.f + axx * axx + ay2);
                        const float dx = dir_bx + cam.rx * axx;
                        const float dyy = dir_by + cam.ry * axx;
                        const float dz = dir_bz + cam.rz * axx;
                        float ndv = (nx * dx + ny * dyy + nz * dz) * il;
                        if (ndv < 0.f)
                        {
                            ndv = -ndv;
                        }
                        const float rv = 1.f - ndv;
                        bright += paint.rim * rv * rv * rv * fog_t;

                        bright *= bright_fog;
                        rr = static_cast<int>(cr_t * bright);
                        gg = static_cast<int>(cg_t * bright);
                        bbc = static_cast<int>(cb_t * bright);
                        if (rr > 255)
                        {
                            rr = 255;
                        }
                        if (gg > 255)
                        {
                            gg = 255;
                        }
                        if (bbc > 255)
                        {
                            bbc = 255;
                        }
                    }
                    // hit flash washes the surface toward white, then the
                    // host's tone curve -- the same order the box path's
                    // shade_row uses, so a mesh body and a box body are
                    // lit by the same pipeline
                    if (paint.flash > 0.f)
                    {
                        // X-20: the int->float promotions were always
                        // implicit; spelled out here without changing them
                        rr += static_cast<int>(static_cast<float>(245 - rr) * paint.flash);
                        gg += static_cast<int>(static_cast<float>(245 - gg) * paint.flash);
                        bbc += static_cast<int>(static_cast<float>(245 - bbc) * paint.flash);
                    }
                    if (tone != nullptr)
                    {
                        rr = tone[rr > 255 ? 255 : (rr < 0 ? 0 : rr)];
                        gg = tone[gg > 255 ? 255 : (gg < 0 ? 0 : gg)];
                        bbc = tone[bbc > 255 ? 255 : (bbc < 0 ? 0 : bbc)];
                    }
                    // haze after tone (it is a screen colour, already
                    // graded at the call site), before the coverage blend
                    // (a shadow dims the hazed surface, not the haze).
                    // Clamped: lit can already sit at 255 (bright texel
                    // under a lamp), and the addend on top wraps pack()'s
                    // channels (&255) into cyan/magenta speckle -- seen as
                    // a band of saturated dots wherever lit+haze crossed
                    // 255. The gouraud path below never had this because
                    // its store already clamps.
                    if (use_haze)
                    {
                        rr = static_cast<int>(static_cast<float>(rr) + hz_r);
                        gg = static_cast<int>(static_cast<float>(gg) + hz_g);
                        bbc =
                            static_cast<int>(static_cast<float>(bbc) + hz_b);
                        rr = rr > 255 ? 255 : rr;
                        gg = gg > 255 ? 255 : gg;
                        bbc = bbc > 255 ? 255 : bbc;
                    }
                    // Coverage. The solid path is the default and stays a
                    // plain store; a caller that asked for a blend (a
                    // contact shadow) gets src-over against the pixel the
                    // world already laid down. One compare on a value the
                    // compiler hoists, and the branch is perfectly
                    // predicted per draw call.
                    if (paint.alpha >= 1.f)
                    {
                        row[x] = pack(rr, gg, bbc);
                    }
                    else
                    {
                        const uint32_t dst = row[x];
                        const float a = paint.alpha;
                        // X-20: the int->float promotions of the int*float
                        // products were always implicit; spelled out here
                        // without changing them
                        row[x] = pack(
                            static_cast<int>(static_cast<float>(unpack_r(dst)) +
                                             static_cast<float>(rr - unpack_r(dst)) * a),
                            static_cast<int>(static_cast<float>(unpack_g(dst)) +
                                             static_cast<float>(gg - unpack_g(dst)) * a),
                            static_cast<int>(static_cast<float>(unpack_b(dst)) +
                                             static_cast<float>(bbc - unpack_b(dst)) * a));
                    }
                    // geometry the stars must not shine through
                    t.clear_sky_cell(x, y);
                }
            }
        }
    }
} // namespace zb::raster
