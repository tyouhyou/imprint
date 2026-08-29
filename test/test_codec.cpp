/*
 * Codec round-trip suite (backlog batch D): exercises the vendored
 * stb_image / stb_image_write paths through the public Image API.
 *
 * Suites are gated on USE_PNG / USE_JPEG: the default build registers the
 * suite but runs no codec assertions.
 */

#include "test.hpp"

#include "codec/image.hpp"

#include <cstdio>
#include <vector>

namespace
{
    const char *PNG_TMP = "test_codec_tmp.png";
    const char *JPG_TMP = "test_codec_tmp.jpg";

    using zb::ui::core::Color;
    using zb::ui::core::Graphics;
    using zb::ui::Image;
    using zb::ui::image_info;

    // Pixel buffer helper: 32x16, flat fill with a few marked pixels
    struct TestImage
    {
        TestImage() : buf(32u * 16u), g(32, 16, buf.data())
        {
            g.fill(Color::from(10, 20, 30, 255));
            buf[5 * 32 + 3] = Color::from(200, 100, 50, 255);
            buf[12 * 32 + 27] = Color::from(0, 250, 7, 128);
        }

        std::vector<Color> buf;
        Graphics g;
    };

    struct ReadResult
    {
        std::vector<Color> pixels;
        image_info info{};
        bool info_received = false;
    };

    auto make_loaded(ReadResult &out) {
        return [&out](image_info &im) {
            out.info = im;
            out.info_received = true;
            return true;
        };
    }

    // RGBA row collector (PNG)
    auto make_row_rgba(ReadResult &out) {
        return [&out](unsigned char *buf) {
            for (uint32_t i = 0; i < out.info.image_width; i++)
            {
                out.pixels.push_back(Color::from(buf[0], buf[1], buf[2], buf[3]));
                buf += 4;
            }
            return true;
        };
    }

    // RGB row collector (JPEG)
    auto make_row_rgb(ReadResult &out) {
        return [&out](unsigned char *buf) {
            for (uint32_t i = 0; i < out.info.image_width; i++)
            {
                out.pixels.push_back(Color::from(buf[0], buf[1], buf[2]));
                buf += 3;
            }
            return true;
        };
    }

#if defined(USE_JPEG) && COLOR_DEPTH == 32
    void channels(const Color &c, int &r, int &g, int &b)
    {
        r = c.r();
        g = c.g();
        b = c.b();
    }
#endif
} // namespace

int test_codec()
{
#if defined(USE_PNG)
    {
        TestImage src;
        Image img;

        EXPECT(0 == img.write_png(src.g, PNG_TMP));

        ReadResult out;
        EXPECT(0 == img.read_png_file(PNG_TMP, make_loaded(out), make_row_rgba(out)));

        EXPECT(out.info_received);
        EXPECT(32 == out.info.image_width);
        EXPECT(16 == out.info.image_height);
        EXPECT(4 == out.info.color_components);
        EXPECT(32u * 4 == out.info.row_stride);
        EXPECT(out.pixels.size() == src.buf.size());

        // exact match, row order must be preserved (no vertical flip)
        for (size_t i = 0; i < src.buf.size() && out.pixels.size() == src.buf.size(); i++)
        {
            if (src.buf[i].pixel != out.pixels[i].pixel)
            {
                EXPECT(false); // report once with position
                std::printf("  pixel mismatch at %zu\n", i);
                break;
            }
        }

        // callbacks may reject: info rejected (5), row rejected (6)
        auto reject_info = [](image_info &) { return false; };
        EXPECT(5 == img.read_png_file(PNG_TMP, reject_info, make_row_rgba(out)));
        auto reject_row = [](unsigned char *) { return false; };
        EXPECT(6 == img.read_png_file(PNG_TMP, make_loaded(out), reject_row));

        // error codes: missing file (1), not a PNG (2)
        EXPECT(1 == img.read_png_file("test_codec_no_such.png", make_loaded(out), make_row_rgba(out)));
        FILE *f = std::fopen(PNG_TMP, "wb");
        std::fputs("not a png", f);
        std::fclose(f);
        EXPECT(2 == img.read_png_file(PNG_TMP, make_loaded(out), make_row_rgba(out)));
    }
    std::remove(PNG_TMP);
#endif

#if defined(USE_JPEG)
    {
        TestImage src;
        Image img;

        EXPECT(0 == img.write_jpeg(src.g, JPG_TMP, 90));

        ReadResult out;
        EXPECT(0 == img.read_jpeg_file(JPG_TMP, make_loaded(out), make_row_rgb(out)));

        EXPECT(out.info_received);
        EXPECT(32 == out.info.image_width);
        EXPECT(16 == out.info.image_height);
        EXPECT(3 == out.info.color_components);
        EXPECT(out.pixels.size() == src.buf.size());

        // lossy: per-channel tolerance instead of exact match. 4:2:0 chroma
        // subsampling smears the isolated marker pixels over their 2x2
        // blocks, so only the flat background is checked tightly; a loose
        // global bound covers the subdivided neighbourhood.
#if COLOR_DEPTH == 32
        {
            const auto near_marker = [](const size_t i) {
                const int x = (int)(i % 32), y = (int)(i / 32);
                return (x >= 0 && x <= 6 && y >= 2 && y <= 8) ||     // (3,5)
                       (x >= 24 && x <= 30 && y >= 9 && y <= 15);   // (27,12)
            };
            int maxdrift = 0;
            for (size_t i = 0; i < src.buf.size() && out.pixels.size() == src.buf.size(); i++)
            {
                int sr = 0, sg = 0, sb = 0, dr = 0, dg = 0, db = 0;
                channels(src.buf[i], sr, sg, sb);
                channels(out.pixels[i], dr, dg, db);
                int d = sr - dr > 0 ? sr - dr : dr - sr;
                int dg2 = sg - dg > 0 ? sg - dg : dg - sg;
                int db2 = sb - db > 0 ? sb - db : db - sb;
                if (d < dg2) d = dg2;
                if (d < db2) d = db2;
                if (d > maxdrift) maxdrift = d;
                if (!near_marker(i) && d > 12)
                {
                    EXPECT(false);
                    std::printf("  jpeg channel drift %d at %zu\n", d, i);
                    break;
                }
            }
            if (maxdrift >= 170)
            {
                EXPECT(false);
                std::printf("  jpeg global drift %d\n", maxdrift);
            }
        }
#endif

        // error codes: missing file (1), not a JPEG (2)
        EXPECT(1 == img.read_jpeg_file("test_codec_no_such.jpg", make_loaded(out), make_row_rgb(out)));
        FILE *f = std::fopen(JPG_TMP, "wb");
        std::fputs("not a jpeg", f);
        std::fclose(f);
        EXPECT(2 == img.read_jpeg_file(JPG_TMP, make_loaded(out), make_row_rgb(out)));
    }
    std::remove(JPG_TMP);
#endif

    return ::test::report("codec");
}