// Snapshot helper (code-contract §12, Batch G P2): the deterministic-
// test story as a linkable helper. Drive your surface headlessly —
// input/paint through the public API, the §4.11 automation pattern —
// then persist or compare frame hashes with these functions. The hash
// runs over the per-pixel r/g/b channel values (A-19 accessors), not
// the memory bytes, so it is layout- and endian-independent within one
// pixel class; alpha is excluded (the 32bpp 4th byte is non-contract).
#ifndef IM_APP_SNAPSHOT_HPP
#define IM_APP_SNAPSHOT_HPP

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "core/color.hpp"
#include "core/error.hpp"
#include "codec/gif.hpp"
#include "iwindow.hpp"

#if defined(USE_PNG)
#include <vector>
#include "codec/image.hpp"
#endif

namespace zb::snap
{
    // FNV-1a (64-bit) over the window's per-pixel r/g/b values.
    // Zero when the window has no buffer yet.
    inline uint64_t framebuffer_hash(zb::app::IWindow &win)
    {
        if (win.data() == nullptr || win.width() <= 0 || win.height() <= 0)
        {
            return 0;
        }
        auto *pixels = static_cast<const zb::ui::core::Color *>(win.data());
        uint64_t h = 1469598103934665603ull;
        const std::size_t n = static_cast<std::size_t>(win.width()) *
                              static_cast<std::size_t>(win.height());
        for (std::size_t i = 0; i < n; ++i)
        {
            const uint32_t rgb = (static_cast<uint32_t>(pixels[i].r()) << 16) |
                                 (static_cast<uint32_t>(pixels[i].g()) << 8) |
                                 static_cast<uint32_t>(pixels[i].b());
            for (int b = 0; b < 3; ++b)
            {
                h ^= (rgb >> (b * 8)) & 0xffu;
                h *= 1099511628211ull;
            }
        }
        return h;
    }

    // Single-frame GIF dump through the always-compiled GIF writer.
    // Init path: an unwritable path throws.
    inline void save_gif(zb::app::IWindow &win, const std::string &path)
    {
        if (win.data() == nullptr || win.width() <= 0 || win.height() <= 0)
        {
            throw zb::ui::error("snapshot: no framebuffer to save to '" + path + "'");
        }
        zb::ui::GifWriter writer(
            path.c_str(), static_cast<std::size_t>(win.width()),
            static_cast<std::size_t>(win.height()), 1);
        writer.add_frame(static_cast<const zb::ui::core::Color *>(win.data()));
        std::ifstream probe(path, std::ios::binary);
        if (!probe)
        {
            throw zb::ui::error("snapshot: GIF writer produced no file at '" + path + "'");
        }
    }

    namespace detail
    {
        inline std::string baseline_path(const std::string &name,
                                         const std::string &dir)
        {
            return dir + "/" + name + ".zbsnap";
        }

        inline std::string hash_hex(const uint64_t h)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%016llx",
                          static_cast<unsigned long long>(h));
            return buf;
        }
    }

    // Writes <dir>/<name>.zbsnap and returns the hash. The caller owns
    // the directory; the helper never creates one. Init path: an
    // unwritable path throws zb::ui::error.
    inline uint64_t record(zb::app::IWindow &win, const std::string &name,
                           const std::string &dir)
    {
        const uint64_t h = framebuffer_hash(win);
        const std::string path = detail::baseline_path(name, dir);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            throw zb::ui::error("snapshot: cannot write baseline '" + path + "'");
        }
        f << "imprint-snapshot v1\n" << detail::hash_hex(h) << "\n";
        f.close();
        if (!f)
        {
            throw zb::ui::error("snapshot: cannot write baseline '" + path + "'");
        }
        return h;
    }

    struct check_result
    {
        enum class status
        {
            ok,
            mismatch,
            missing
        } status = status::missing;
        uint64_t expected = 0;
        uint64_t actual = 0;
    };

    // Compares the current frame hash against the recorded baseline.
    // actual is always filled; missing baseline is an expected outcome
    // (the record-before-verify workflow), a corrupt one is init-path
    // loud. On mismatch the artifact <dir>/<name>.actual.gif lands
    // beside the baseline.
    inline check_result check(zb::app::IWindow &win, const std::string &name,
                              const std::string &dir)
    {
        const std::string path = detail::baseline_path(name, dir);
        std::ifstream f(path, std::ios::binary);
        check_result r;
        r.actual = framebuffer_hash(win);
        if (!f)
        {
            return r;  // missing
        }
        std::string magic;
        std::string hex;
        std::getline(f, magic);
        std::getline(f, hex);
        if (magic != "imprint-snapshot v1" || hex.size() != 16)
        {
            throw zb::ui::error("snapshot: corrupt baseline '" + path + "'");
        }
        r.expected = std::strtoull(hex.c_str(), nullptr, 16);
        if (r.actual == r.expected)
        {
            r.status = check_result::status::ok;
            return r;
        }
        r.status = check_result::status::mismatch;
        save_gif(win, dir + "/" + name + ".actual.gif");
        return r;
    }

#if defined(USE_PNG)
    // Single-frame PNG dump (USE_PNG builds only). Returns the codec
    // error code (0 = OK), the codec-family contract (§1.3).
    inline int save_png(zb::app::IWindow &win, const std::string &path)
    {
        if (win.data() == nullptr || win.width() <= 0 || win.height() <= 0)
        {
            return 1;
        }
        const int32_t w = win.width();
        const int32_t h = win.height();
        auto *pixels = static_cast<const zb::ui::core::Color *>(win.data());
        zb::ui::image_info info{static_cast<uint32_t>(w),
                                static_cast<uint32_t>(h),
                                static_cast<uint32_t>(w) * 3u, 3u};
        std::size_t row = 0;
        zb::ui::Image img;
        return img.write_png_file(
            path, info,
            [&](std::vector<unsigned char> &out)
            {
                out.resize(static_cast<std::size_t>(w) * 3u);
                for (int32_t x = 0; x < w; ++x)
                {
                    const zb::ui::core::Color &c = pixels[row * w + x];
                    out[x * 3 + 0] = c.r();
                    out[x * 3 + 1] = c.g();
                    out[x * 3 + 2] = c.b();
                }
                ++row;
                return true;
            });
    }
#endif
}

#endif
