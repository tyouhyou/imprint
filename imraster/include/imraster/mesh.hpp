#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "imraster/canvas.hpp"
#include "imraster/raster.hpp"

namespace zb::raster
{
    /*
     * The per-pixel triangle path: organic meshes built from capsules and
     * spheres, rasterized with a per-pixel depth buffer and per-pixel
     * interpolated normals -- the two things the column-based box path
     * (raster.hpp) cannot express, and the two things a body needs before
     * it stops reading as cardboard.
     *
     * The module knows NOTHING about what the mesh depicts: the caller
     * builds world-space vertices and normals, assigns each triangle a
     * part id and a crevice-occlusion term at build time, and hands in
     * its own lighting model through MeshPaint. (fps's trooper -- the
     * pose state machine, the kit carries, the fall -- is one consumer
     * and lives in the game, not here.)
     *
     * Depth is a hybrid, like everything in this module: the mesh's own
     * pixels z-test per pixel (Canvas's mesh depth buffer), then each
     * pixel tests the camera's per-column wall distance, which is what
     * makes a body standing in a room clip against the walls for free.
     */

    // one mesh vertex: position and outward normal, world space, unit
    struct MeshVert
    {
        float x, y, z;
        float nx, ny, nz;
    };

    // one triangle: three vertex indices, the part id it is painted with,
    // and the crevice-occlusion multiplier baked in at build time (the
    // caller's per-part number -- joints darker, shells at 1 or over)
    struct MeshTri
    {
        int a, b, c;
        unsigned char part;
        float ao;
    };

    /*
     * The mesh being built for one body. Scratch is heap-owned (the wasm
     * stack is a fixed 64 KB; a body's working set is not). The builders
     * below append; reset() starts a new body.
     *
     * Winding is SELF-HEALING: every triangle is stored so its geometric
     * normal points at the outward reference point the builder computes
     * off the ring's radial. That is what lets the capsule and sphere
     * builders be written without ever debugging an inside-out body --
     * winding errors become culling errors, and a culled-front body is
     * obvious in the first frame it renders.
     */
    class MeshBuf
    {
    public:
        void reset();

        // verts and tris are public on purpose: the caller transforms the
        // model into world space in place between build and draw
        std::vector<MeshVert> verts;
        std::vector<MeshTri> tris;

        /*
         * UVs, as a SIDECAR rather than a seventh and eighth field on
         * MeshVert: textured sampling is the exception (a body whose parts
         * are shaded by normal alone pays nothing for it), and 8 bytes a
         * vertex is 8 bytes a vertex of memory traffic in the one pass
         * that was measured at 36 ms a frame not long ago. Stride 2,
         * parallel to verts; EMPTY means "this body has no UVs", and
         * draw_mesh then skips the fetch entirely.
         *
         * It is a sidecar and not an attribute list because there is one
         * attribute. When a second one arrives (vertex colour, skin
         * weights) the honest move is to generalize then, not now.
         */
        std::vector<float> uv;

        int add_vert(float x, float y, float z, float nx, float ny, float nz);
        void add_tri(int a, int b, int c, int part, float ao, float ref_x,
                     float ref_y, float ref_z);

        /*
         * A tapered capsule segment between two joint centers, open-ended
         * -- the ends are meant to live inside joint spheres, so no caps
         * are built and no cap seam can show. (Exposed ends -- a boot's
         * toe, a muzzle -- need the caller to cap them with spheres.)
         * `seg` sides around the axis, `rings` bands along it. The side
         * normal tilts for the taper (a cone's normal leans away from its
         * wide end): the one thing that makes a tapered limb shade as a
         * cone instead of as a stepped cylinder.
         */
        void add_capsule(float ax, float ay, float az, float bx, float by,
                         float bz, float r0, float r1, int seg, int rings,
                         int part, float ao);

        /*
         * A UV sphere: two poles, `rings` latitude rows between them.
         * The vertex normal IS the radial -- a sphere is the one shape
         * where the normal is free.
         */
        void add_sphere(float cx, float cy, float cz, float r, int seg,
                        int rings, int part, float ao);

    private:
        // ring stitching scratch, shared by the two builders
        std::vector<int> ring_prev_, ring_curr_;
    };

    /*
     * The caller's lighting model, evaluated per pixel. Defaults are the
     * numbers fps's trooper calibrated (its draw budget comment: ambient
     * + lambert must stay under ~1 or a close body clips to white).
     */
    // slot count of MeshPaint::mips / MeshPaint::mip_sizes: the mip level
    // walk clamps to this bound, so an oversized mip_levels degrades to
    // the finest stored level instead of indexing past the member arrays
    inline constexpr int kMipSlots = 8;

    struct MeshPaint
    {
        // the one directional light, world space, roughly normalized
        float light_x = 0.36f, light_y = 0.81f, light_z = 0.46f;
        float ambient = 0.62f;  // base brightness
        float lambert = 0.34f;  // extra where the surface faces the light
        float hemi = 0.30f;     // hemisphere ambient, 0 = off: normals that
                                // point down sit in the darker half of the sky
        float rim = 0.22f;      // silhouette light, 0 = off: (1-|n.v|)^3
        // fog multiplies the final brightness: fog = clamp(1 - v/fog_dist,
        // fog_min, 1), v = view depth of the triangle
        float fog_dist = 15.f;
        float fog_min = 0.08f;
        /*
         * Haze added back after the fog multiply: out = lit * fog +
         * haze * (1 - fog). Without it every distant surface fades toward
         * BLACK (task 4d found this: the opened annex read as a dark hole,
         * and every lane end was a black tunnel, because fog only
         * multiplied). The column path always added its haze; the mesh
         * path never did. Packed RGB, 0 (the default) keeps the old
         * multiply-only behaviour for callers that do not set it.
         */
        uint32_t haze = 0u;
        /*
         * The distance haze reaches full strength at. Decoupled from
         * fog_dist on purpose: fog_dist is tuned as a multiply (it has to
         * stay gentle at mid range or every floor pool blows out), but as
         * an addend driver the same curve hazes +60 onto a trooper at
         * 5 cells and washes it to grey by 12 -- characters must stay
         * readable far past where the world is allowed to melt. 0 means
         * "same as fog_dist" (the old behaviour for callers that set a
         * haze colour without thinking about it).
         */
        float haze_dist = 0.f;
        /*
         * How the haze composes with the fog multiply.
         *
         * false (the historical model, and what every character and the
         * indoor world use): out = lit * fog + haze * (v/haze_dist)^2 --
         * the glow is ADDED on top of an already dimming surface. It is
         * tuned for mood at short range, and because the two curves are
         * independent a surface can be 60% fogged while the addend is
         * still under 20%: the mid distance goes DARK before the air
         * takes over. Indoors that is invisible (a wall ends the lane
         * first). Outdoors it is the grey ridge the range carried until
         * D38 part 5.
         *
         * true: out = lit * (1 - h) + haze * h, h = clamp(v / fog_dist).
         * The surface fades INTO the air colour and the two weights sum
         * to one at every distance -- this header's own "haze * (1 - fog)",
         * evaluated as a lerp. A horizon needs exactly that: the ground
         * brightens monotonically toward the sky it meets and reaches the
         * sky's own colour, or the seam is a line. haze_dist is unused
         * here (fog_dist, with fog_min at 0, is the fade length).
         */
        bool haze_lerp = false;
        float flash = 0.f;  // 0..1 wash toward white (hit reaction)
        /*
         * Coverage, 0..1, src-over against whatever is already in the
         * framebuffer. 1 (the default, and what every solid surface wants)
         * writes the shaded colour; below 1 blends, which is what a contact
         * shadow needs -- a shadow is not a dark object lying on the floor,
         * it is the floor with some of its light taken away. The mesh path
         * had no such mode until the contact shadow came back to it
         * (2026-10-08): the shape was already expressible as a flat disc on
         * the floor plane, and only the blend was missing.
         */
        float alpha = 1.f;
        // per-part material colors (packed), indexed by MeshTri::part;
        // a part id >= part_count paints as the LAST entry
        const uint32_t *part_colors = nullptr;
        int part_count = 0;

        /*
         * An optional surface texture, sampled through MeshBuf::uv.
         *
         * The module knows the SHAPE of a texture (a square block of
         * packed RGBA texels, nearest sampled, wrapped) and not where the
         * pixels come from -- the game builds them procedurally
         * (render::Textures), and a module that wanted files would stop
         * being one. A null `texels` means "no texture", which is the
         * untextured path with no per-pixel cost at all.
         *
         * `contrast` is what a texture is ALLOWED to do to the part
         * colour: the texel is centred on white (128,128,128 = no change)
         * and scaled by contrast/255, so 255 is full-strength detail and
         * 0 erases the texture without erasing the material. The texture
         * never REPLACES the colour -- a trooper's olive stays olive, and
         * what the map adds is grain, wear and camo, which is what the
         * authored models were missing.
         */
        const uint32_t *texels = nullptr;
        int tex_size = 0;      // power of two; wraps with &
        float contrast = 1.f;  // 0..1 of the texel's own range

        /*
         * The mip chain, so a receding surface stops reading as noise.
         *
         * The module still knows the SHAPE of a texture and not where its
         * pixels come from: `texels` stays level 0, and these are the same
         * texture at half, quarter, eighth... linear size, oldest first,
         * with the side length of each level beside it. A null `mips` (or
         * mip_levels < 2) means "no chain": every surface samples level 0,
         * which is what every caller did before 2026-10-09.
         *
         * WHY per-pixel and not per-triangle. A triangle is not a wall
         * column. F31 chose one mip level per wall COLUMN because the hit
         * distance is constant down a column, and it was right; the same
         * trick on a mesh is wrong by construction, because a near corner
         * of a floor triangle can be 2 cells away and its far corner 8, and
         * one level cannot serve both. So the level here comes from the
         * UV gradient -- how many texels one OUTPUT pixel covers -- which
         * is already computed for other reasons, costs one comparison per
         * pixel, and is the same quantity F31 measured in hit distance.
         *
         * The choice is made per triangle from its STEEPEST gradient and
         * used for the whole triangle, not per pixel: the gradient varies
         * little across a triangle next to the difference between choosing
         * a level and not, and a per-pixel level would put a branch inside
         * the innermost loop of the most expensive pass in the frame.
         *
         * mip_levels must not exceed kMipSlots (the slot count of the two
         * arrays below): the level walk clamps to that bound, so a larger
         * value degrades to the finest stored level instead of reading
         * past the member arrays. Review round 1; no in-repo caller
         * exceeds it.
         */
        const uint32_t *mips[kMipSlots] = {};
        int mip_sizes[kMipSlots] = {};
        int mip_levels = 0;

        /*
         * The world's depth, at whatever resolution it was drawn at, for a
         * body drawn at 1:1 over an upscaled world.
         *
         * WHY THIS EXISTS. The world is shaded on a coarse grid (rep 4 by
         * default) and then resampled onto the surface; a trooper drawn at
         * 1:1 afterwards has no depth to test against, because the mesh
         * zbuf was reallocated at the new size and the grid pass's contents
         * are gone. Without a reference the body would simply paint over
         * walls. So the caller hands in the grid's zbuf here and says how
         * big the grid was; the per-pixel test then samples the cell the
         * pixel falls in.
         *
         * It is a NEAREST read of a 4x-coarser buffer, which is exactly
         * right: a body may only be occluded where the world's own cell
         * says there is a wall nearer than the body's pixel, and at cell
         * granularity that is already the answer -- the upscale cannot
         * invent geometry between two cells. The consequence is a 4-pixel
         * staircase along a wall/body silhouette; that is the same
         * staircase the world itself has.
         *
         * Null (the default) means "no world in front of me", which is
         * what the viewmodel and the native test sheets want.
         */
        const float *world_depth = nullptr;
        int world_w = 0;  // the grid the depth buffer was written at
        int world_h = 0;
        float world_iw_eps = 1.5e-3f;  // see the compare in draw_mesh

        // the host's tone curve (256 entries per channel); nullptr = identity
        const uint8_t *tone = nullptr;
    };

    /*
     * Draw one body. The mesh depth buffer must be cleared once per frame
     * before the first body of that frame (Canvas::reset_mesh_depth) --
     * deliberately not per body: a body drawn later depth-tests against
     * the bodies drawn before it, which is how a near body's arm keeps
     * occluding a far body's torso without any whole-body sort. Body-vs-
     * body painter order (far to near) is still the caller's job where it
     * matters, same as the box path.
     *
     * Geometry that crosses Camera::near_clip is CLIPPED, not clamped: a
     * first-person weapon whose stock is behind the eye gets cut at the plane
     * (what every 3D viewmodel does), and a body entirely in front of it pays
     * nothing for the test.
     */
    void draw_mesh(Canvas &t, const Camera &cam, const MeshBuf &buf,
                   const MeshPaint &paint);
}  // namespace zb::raster
