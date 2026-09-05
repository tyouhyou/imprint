/*
 * asset_gen — procedural showcase asset generator (Batch V-2).
 *
 * Usage: asset_gen <out_header>
 *
 * Emits RGBA8 byte arrays for the showcase's demo assets as a C++
 * header. Build-time materialization of a shipping asset — the same
 * idea as ttf_subset rasterizing fonts: the pixels are derived data,
 * the tool is the source of truth. No runtime decode, no USE_PNG
 * dependency, works unchanged on every target including NDS.
 *
 * Assets:
 *   logo_ball   32x32  soft-edged gradient ball with a specular
 *                      highlight (alpha compositing + tint demo)
 *   card_shadow 48x48  pre-blurred rounded-rect shadow (9-slice demo;
 *                      the framework deliberately has no blur — the
 *                      blur happens here, once, at build time)
 *
 * The generated header needs no framework headers (the consumer decides
 * how to convert the bytes into the build's pixel layout).
 */
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    struct rgba
    {
        unsigned char r, g, b, a;
    };

    struct asset
    {
        const char *name;
        const char *symbol;
        int width;
        int height;
        std::vector<rgba> pixels;
    };

    // 32x32 ball: vertical blue gradient, 1px soft edge, white specular
    asset make_ball()
    {
        asset a{"logo_ball", "kLogoBall", 32, 32, {}};
        a.pixels.resize(static_cast<std::size_t>(a.width) * a.height);
        const double cx = (a.width - 1) / 2.0, cy = (a.height - 1) / 2.0;
        const double radius = a.width / 2.0 - 1.5;
        for (int y = 0; y < a.height; ++y)
        {
            for (int x = 0; x < a.width; ++x)
            {
                rgba &p = a.pixels[static_cast<std::size_t>(y) * a.width + x];
                const double d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
                if (d >= radius)
                {
                    p = {0, 0, 0, 0};
                    continue;
                }
                // vertical gradient: light blue top, deep blue bottom
                const double t = y / static_cast<double>(a.height - 1);
                p.r = static_cast<unsigned char>(0x6E + (0x25 - 0x6E) * t);
                p.g = static_cast<unsigned char>(0xA8 + (0x63 - 0xA8) * t);
                p.b = static_cast<unsigned char>(0xFE + (0xEB - 0xFE) * t);
                // specular highlight toward white
                const double hd = std::sqrt((x - 11.5) * (x - 11.5) + (y - 10.0) * (y - 10.0));
                if (hd < 6.0)
                {
                    const double w = (1.0 - hd / 6.0) * 0.85;
                    p.r = static_cast<unsigned char>(p.r + (0xFF - p.r) * w);
                    p.g = static_cast<unsigned char>(p.g + (0xFF - p.g) * w);
                    p.b = static_cast<unsigned char>(p.b + (0xFF - p.b) * w);
                }
                // 1px soft edge: coverage fades over the last pixel
                double cov = radius - d;
                if (cov > 1.0)
                {
                    cov = 1.0;
                }
                p.a = static_cast<unsigned char>(cov * 255.0 + 0.5);
            }
        }
        return a;
    }

    // separable integer box blur on one channel, radius r, two passes
    void blur(std::vector<int> &ch, const int w, const int h, const int r)
    {
        std::vector<int> tmp(static_cast<std::size_t>(w) * h, 0);
        for (int pass = 0; pass < 2; ++pass)
        {
            // horizontal
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    int sum = 0, n = 0;
                    for (int k = -r; k <= r; ++k)
                    {
                        const int xx = x + k;
                        if (xx >= 0 && xx < w)
                        {
                            sum += ch[static_cast<std::size_t>(y) * w + xx];
                            ++n;
                        }
                    }
                    tmp[static_cast<std::size_t>(y) * w + x] = sum / n;
                }
            }
            // vertical
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    int sum = 0, n = 0;
                    for (int k = -r; k <= r; ++k)
                    {
                        const int yy = y + k;
                        if (yy >= 0 && yy < h)
                        {
                            sum += tmp[static_cast<std::size_t>(yy) * w + x];
                            ++n;
                        }
                    }
                    ch[static_cast<std::size_t>(y) * w + x] = sum / n;
                }
            }
        }
    }

    // 48x48 shadow: blurred rounded-rect alpha (black), peak ~43%
    asset make_shadow()
    {
        asset a{"card_shadow", "kCardShadow", 48, 48, {}};
        a.pixels.resize(static_cast<std::size_t>(a.width) * a.height);
        const int m = 10;           // the solid rect inset (blur tail lives outside)
        const int solid = 28;       // [m, m+solid) — the 9-slice center tile
        const int radius = 6;
        std::vector<int> alpha(static_cast<std::size_t>(a.width) * a.height, 0);
        for (int y = 0; y < a.height; ++y)
        {
            for (int x = 0; x < a.width; ++x)
            {
                // rounded-rect coverage: inside the inset box, corners
                // clipped by the circle at (m+radius, m+radius) etc.
                const bool in_x = x >= m && x < m + solid;
                const bool in_y = y >= m && y < m + solid;
                bool inside = false;
                if (in_x && in_y)
                {
                    inside = true;
                    const int cx = x < m + radius ? m + radius
                                 : x >= m + solid - radius ? m + solid - radius - 1 : x;
                    const int cy = y < m + radius ? m + radius
                                 : y >= m + solid - radius ? m + solid - radius - 1 : y;
                    if ((cx == m + radius || cx == m + solid - radius - 1) &&
                        (cy == m + radius || cy == m + solid - radius - 1))
                    {
                        const double dx = x - cx, dy = y - cy;
                        inside = dx * dx + dy * dy <=
                                 static_cast<double>(radius) * radius;
                    }
                }
                alpha[static_cast<std::size_t>(y) * a.width + x] = inside ? 255 : 0;
            }
        }
        blur(alpha, a.width, a.height, 3);
        for (auto &p : a.pixels)
        {
            p = {0, 0, 0, 0};
        }
        for (std::size_t i = 0; i < alpha.size(); ++i)
        {
            // 110/255 peak opacity: a shadow, not a hole
            a.pixels[i].a = static_cast<unsigned char>(alpha[i] * 110 / 255);
        }
        return a;
    }

    void emit(FILE *out, const asset &a)
    {
        std::fprintf(out, "static const unsigned char %s[] =\n{\n", a.symbol);
        for (std::size_t i = 0; i < a.pixels.size(); ++i)
        {
            const rgba &p = a.pixels[i];
            std::fprintf(out, "0x%02x,0x%02x,0x%02x,0x%02x,", p.r, p.g, p.b, p.a);
            if (i % 4 == 3)
            {
                std::fprintf(out, "\n");
            }
        }
        std::fprintf(out, "\n};\n\n");
        std::fprintf(out,
                     "inline const rgba_view %s_view{%s, %d, %d};\n\n",
                     a.name, a.symbol, a.width, a.height);
    }
}  // namespace

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: asset_gen <out_header>\n");
        return 2;
    }
    FILE *out = std::fopen(argv[1], "w");
    if (out == nullptr)
    {
        std::fprintf(stderr, "asset_gen: cannot write %s\n", argv[1]);
        return 2;
    }

    std::fprintf(out, "#pragma once\n");
    std::fprintf(out, "// generated by asset_gen — do not edit\n");
    std::fprintf(out, "// procedural RGBA8 demo assets; the tool is the source of truth\n");
    std::fprintf(out, "#include <cstddef>\n\n");
    std::fprintf(out, "namespace showcase_assets {\n\n");
    std::fprintf(out, "struct rgba_view\n{\n"
                      "    const unsigned char *rgba;\n"
                      "    unsigned width;\n"
                      "    unsigned height;\n"
                      "};\n\n");

    emit(out, make_ball());
    emit(out, make_shadow());

    std::fprintf(out, "}  // namespace showcase_assets\n");
    std::fclose(out);
    return 0;
}
