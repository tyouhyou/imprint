/*
 * ImageView suite (FI-09, code-contract §3.5): owned pixels, the
 * content-id decode cache, the letterbox draw geometry, and the
 * zero-allocation frame path.
 *
 * Codec entry points are gated on USE_PNG / USE_JPEG: the default build
 * registers the suite but runs only the view/geometry assertions.
 */

#include "test.hpp"

#include "test_alloc_count.hpp"

#include "codec/image.hpp"
#include "imapp.hpp"
#include "imui.hpp"
#include "logging.hpp"

#include <cstdio>
#include <vector>

using namespace zb::ui;

namespace
{
    // whole-file bytes (only used by the USE_PNG / USE_JPEG blocks)
    [[maybe_unused]] std::vector<unsigned char> read_file_bytes(const char *path)
    {
        std::vector<unsigned char> bytes;
        FILE *f = std::fopen(path, "rb");
        if (!f)
        {
            return bytes;
        }
        unsigned char chunk[4096];
        size_t got;
        while ((got = std::fread(chunk, 1, sizeof(chunk), f)) > 0)
        {
            bytes.insert(bytes.end(), chunk, chunk + got);
        }
        std::fclose(f);
        return bytes;
    }

#if defined(USE_PNG)
    // encode a 2x2 RGBA image into png bytes through the codec writer
    std::vector<unsigned char> make_png_bytes(const core::Color &a, const core::Color &b,
                                              const core::Color &c, const core::Color &d)
    {
        const char *path = "test_image_view_tmp.png";
        const image_info inf{2, 2, 8, 4};
        const core::Color px[4] = {a, b, c, d};
        size_t pos = 0;
        auto write_row = [&pos, &px](std::vector<unsigned char> &row)
        {
            row.clear();
            for (int i = 0; i < 2; i++)
            {
                const core::Color &col = px[pos++];
                row.push_back(col.r());
                row.push_back(col.g());
                row.push_back(col.b());
#if COLOR_DEPTH == 16
                // color16's single alpha bit expands to full opacity
                row.push_back(col.a() != 0 ? 0xFF : 0x00);
#else
                row.push_back(col.a());
#endif
            }
            return true;
        };
        Image codec;
        EXPECT(0 == codec.write_png_file(path, inf, write_row));
        std::vector<unsigned char> bytes = read_file_bytes(path);
        std::remove(path);
        return bytes;
    }
#endif
}  // namespace

int test_image_view()
{
    // measure(): natural size, 0x0 while empty
    {
        ImageView iv;
        EXPECT(!iv.has_image());
        EXPECT(iv.measure().width == 0 && iv.measure().height == 0);
        EXPECT(iv.get_content_id().empty());
        EXPECT(iv.get_src().empty());
    }

    // set_image copies the view (owned pixels, source buffer freed after)
    {
        std::vector<core::Color> px = {
            core::colors::Red, core::colors::Green,
            core::colors::Blue, core::colors::White};
        ImageView iv;
        iv.set_size(2, 2);
        iv.set_image(core::image_t{px.data(), 2, 2, 0});
        EXPECT(iv.has_image());
        EXPECT(iv.measure().width == 2 && iv.measure().height == 2);
        // a view-set image reports an empty content id
        EXPECT(iv.get_content_id().empty());

        // the copy survives the source dying
        std::vector<core::Color> *leak = new std::vector<core::Color>(px);
        ImageView iv2;
        iv2.set_size(2, 2);
        iv2.set_image(core::image_t{leak->data(), 2, 2, 0});
        delete leak;
        core::Graphics g(2, 2, nullptr);
        iv2.draw(g);
        EXPECT(test::pixel_at(g, 0, 0) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 1, 1) == core::colors::White.pixel);
    }

    // set_image with a wider stride copies row-wise (no bleed)
    {
        std::vector<core::Color> wide = {
            core::colors::Red, core::colors::Green, core::colors::Black,
            core::colors::Blue, core::colors::White, core::colors::Black};
        ImageView iv;
        iv.set_size(2, 2);
        iv.set_image(core::image_t{wide.data(), 2, 2, 3});
        EXPECT(iv.measure().width == 2 && iv.measure().height == 2);
        core::Graphics g(2, 2, nullptr);
        iv.draw(g);
        EXPECT(test::pixel_at(g, 0, 0) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 1, 0) == core::colors::Green.pixel);
        EXPECT(test::pixel_at(g, 0, 1) == core::colors::Blue.pixel);
        EXPECT(test::pixel_at(g, 1, 1) == core::colors::White.pixel);
    }

    // malformed views: exactly one LW each, no change (a held image
    // survives)
    {
        std::vector<core::Color> px(4, core::colors::Red);
        ImageView iv;
        iv.set_image(core::image_t{px.data(), 2, 2, 0});
        EXPECT(iv.has_image());

        int warns = 0;
        zb::Logging::set_log_handle(
            [&](zb::Logging_Level, const std::string &)
            { ++warns; });
        iv.set_image(core::image_t{nullptr, 2, 2, 0});     // null pixels
        iv.set_image(core::image_t{px.data(), 2, 2, 1});   // stride < width
        iv.set_image(core::image_t{px.data(), 0, 2, 0});   // zero width
        iv.set_image(core::image_t{px.data(), 2, 0, 0});   // zero height
        iv.set_image(core::image_t{px.data(), -1, 2, 0});  // negative width
        zb::Logging::set_log_handle();
        EXPECT(warns == 5);

        EXPECT(iv.has_image());
        EXPECT(iv.measure().width == 2 && iv.measure().height == 2);
    }

    // letterbox draw: a 2x2 image in a 6x4 box letterboxes to 4x4
    // centered at (1,0); the margins stay the widget background
    {
        std::vector<core::Color> px = {
            core::colors::Red, core::colors::Green,
            core::colors::Red, core::colors::Green};
        ImageView iv;
        iv.set_size(6, 4);
        iv.set_background_color(core::colors::Black);
        iv.set_image(core::image_t{px.data(), 2, 2, 0});

        core::Graphics g(6, 4, nullptr);
        g.fill(core::colors::Black);
        iv.draw(g);

        // image quadrants (nearest-neighbor 2x upsampling)
        EXPECT(test::pixel_at(g, 1, 0) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 2, 1) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 3, 0) == core::colors::Green.pixel);
        EXPECT(test::pixel_at(g, 4, 3) == core::colors::Green.pixel);
        // uncovered margins: the widget background, never the image
        EXPECT(test::pixel_at(g, 0, 0) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 5, 3) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 0, 3) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 5, 0) == core::colors::Black.pixel);
    }

    // letterbox draw: a 4x2 image in a 4x8 box letterboxes to 4x2
    // centered vertically (y 3..4); all margins stay background
    {
        std::vector<core::Color> px = {
            core::colors::Red, core::colors::Green, core::colors::Blue, core::colors::White,
            core::colors::White, core::colors::Blue, core::colors::Green, core::colors::Red};
        ImageView iv;
        iv.set_size(4, 8);
        iv.set_background_color(core::colors::Black);
        iv.set_image(core::image_t{px.data(), 4, 2, 0});

        core::Graphics g(4, 8, nullptr);
        g.fill(core::colors::Black);
        iv.draw(g);

        EXPECT(test::pixel_at(g, 0, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 3, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(g, 0, 4) == core::colors::White.pixel);
        EXPECT(test::pixel_at(g, 3, 4) == core::colors::Red.pixel);
        // rows above and below the centered image
        EXPECT(test::pixel_at(g, 1, 2) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 2, 5) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 0, 0) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 3, 7) == core::colors::Black.pixel);
    }

    // two paints are byte-identical (deterministic draw)
    {
        std::vector<core::Color> px = {
            core::colors::Red, core::colors::Green,
            core::colors::Blue, core::colors::White};
        ImageView iv;
        iv.set_size(6, 4);
        iv.set_background_color(core::colors::Black);
        iv.set_image(core::image_t{px.data(), 2, 2, 0});

        core::Graphics g(6, 4, nullptr);
        iv.draw(g);
        std::vector<uint32_t> snapshot;
        for (int y = 0; y < 4; y++)
        {
            for (int x = 0; x < 6; x++)
            {
                snapshot.push_back(test::pixel_at(g, x, y));
            }
        }

        g.fill(core::colors::Black);
        iv.draw(g);
        for (int y = 0; y < 4; y++)
        {
            for (int x = 0; x < 6; x++)
            {
                EXPECT(test::pixel_at(g, x, y) ==
                       snapshot[static_cast<size_t>(y) * 6 + static_cast<size_t>(x)]);
            }
        }
    }

    // the frame path allocates nothing (draw_at rides the in-place
    // scaled blit; 10 repaints of the same content)
    {
        std::vector<core::Color> px = {
            core::colors::Red, core::colors::Green,
            core::colors::Blue, core::colors::White};
        zb::app::CanvasWindow w;
        w.create(20, 20);
        auto holder = std::make_unique<ImageView>();
        holder->set_size(6, 4);
        holder->set_position(2, 2);
        holder->set_image(core::image_t{px.data(), 2, 2, 0});
        w.root().add_child(std::move(holder));
        w.paint();  // consume the initial frame

        test::scoped_alloc_count c;
        for (int i = 0; i < 10; ++i)
        {
            w.paint();
        }
        EXPECT(c.delta() == 0);
    }

    // clear_image resets everything; clearing an empty view is silent
    {
        std::vector<core::Color> px(4, core::colors::Red);
        ImageView iv;
        iv.clear_image();  // already empty: no repaint obligation
        iv.set_image(core::image_t{px.data(), 2, 2, 0});
        EXPECT(iv.has_image());
        iv.clear_image();
        EXPECT(!iv.has_image());
        EXPECT(iv.measure().width == 0 && iv.measure().height == 0);
        EXPECT(iv.get_content_id().empty());
    }

#if defined(USE_PNG)
    // the content-id cache: repeated id = no-op whatever the bytes; new
    // id replaces; decode failure = one LW + previous image kept
    {
        const auto good = make_png_bytes(
            core::colors::Red, core::colors::Green,
            core::colors::Blue, core::colors::White);
        EXPECT(!good.empty());

        ImageView iv;
        iv.set_size(2, 2);
        EXPECT(iv.set_image_png("art", good.data(), good.size()));
        EXPECT(iv.has_image());
        EXPECT(iv.get_content_id() == "art");
        EXPECT(iv.measure().width == 2 && iv.measure().height == 2);

        core::Graphics g(2, 2, nullptr);
        iv.draw(g);
        const uint32_t painted0 = test::pixel_at(g, 0, 0);
        const uint32_t painted1 = test::pixel_at(g, 1, 1);

        // same id + corrupt bytes: a behavioral no-op — the first image
        // survives without a decode attempt (returns true, nothing changes)
        const unsigned char corrupt[] = "not a png";
        EXPECT(iv.set_image_png("art", corrupt, sizeof(corrupt) - 1));
        EXPECT(iv.get_content_id() == "art");
        EXPECT(iv.measure().width == 2 && iv.measure().height == 2);
        g.fill(core::colors::Black);
        iv.draw(g);
        EXPECT(test::pixel_at(g, 0, 0) == painted0);
        EXPECT(test::pixel_at(g, 1, 1) == painted1);

        // new id + corrupt bytes: decode failure, one LW, previous kept
        // (only warn-level rows count: the codec's own LE is its separate
        // diagnostic)
        int warns = 0;
        zb::Logging::set_log_handle(
            [&](zb::Logging_Level level, const std::string &)
            {
                if (level == zb::Logging_Level::warn)
                {
                    ++warns;
                }
            });
        EXPECT(!iv.set_image_png("other", corrupt, sizeof(corrupt) - 1));
        zb::Logging::set_log_handle();
        EXPECT(warns == 1);
        EXPECT(iv.has_image());
        EXPECT(iv.get_content_id() == "art");

        // new id + good bytes replaces the cached decode
        const auto good2 = make_png_bytes(
            core::colors::White, core::colors::Black,
            core::colors::Green, core::colors::Red);
        EXPECT(iv.set_image_png("other", good2.data(), good2.size()));
        EXPECT(iv.get_content_id() == "other");
        g.fill(core::colors::Black);
        iv.draw(g);
        EXPECT(test::pixel_at(g, 0, 0) == core::colors::White.pixel);
        EXPECT(test::pixel_at(g, 1, 1) == core::colors::Red.pixel);

        // null/empty bytes fail the same way (one LW, previous kept)
        warns = 0;
        zb::Logging::set_log_handle(
            [&](zb::Logging_Level level, const std::string &)
            {
                if (level == zb::Logging_Level::warn)
                {
                    ++warns;
                }
            });
        EXPECT(!iv.set_image_png("third", nullptr, 0));
        zb::Logging::set_log_handle();
        EXPECT(warns == 1);
        EXPECT(iv.get_content_id() == "other");
    }

    // an empty content id is an unnamed image: it always decodes. The
    // no-op corner must not swallow a png fed after a view-set image
    // (set_image leaves an empty id, and "same id means same image"
    // does not hold across the two paths).
    {
        std::vector<core::Color> px(4, core::colors::Red);
        ImageView iv;
        iv.set_size(2, 2);
        iv.set_image(core::image_t{px.data(), 2, 2, 0});
        EXPECT(iv.has_image());
        EXPECT(iv.get_content_id().empty());

        const auto art = make_png_bytes(
            core::colors::Blue, core::colors::Blue,
            core::colors::Blue, core::colors::Blue);
        EXPECT(!art.empty());
        EXPECT(iv.set_image_png("", art.data(), art.size()));
        EXPECT(iv.get_content_id().empty());  // unnamed stays unnamed
        core::Graphics g(2, 2, nullptr);
        iv.draw(g);
        EXPECT(test::pixel_at(g, 0, 0) == core::colors::Blue.pixel);
        EXPECT(test::pixel_at(g, 1, 1) == core::colors::Blue.pixel);
    }
#endif

#if defined(USE_JPEG)
    // the jpeg mirror: decode lands the image (content-id cache shared)
    {
        const char *path = "test_image_view_tmp.jpg";
        const image_info inf{2, 2, 6, 3};
        size_t pos = 0;
        const core::Color px[4] = {
            core::colors::Red, core::colors::Green,
            core::colors::Blue, core::colors::White};
        auto write_row = [&pos, &px](std::vector<unsigned char> &row)
        {
            row.clear();
            for (int i = 0; i < 2; i++)
            {
                const core::Color &col = px[pos++];
                row.push_back(col.r());
                row.push_back(col.g());
                row.push_back(col.b());
            }
            return true;
        };
        Image codec;
        EXPECT(0 == codec.write_jpeg_file(path, inf, write_row, 90));
        const std::vector<unsigned char> bytes = read_file_bytes(path);
        std::remove(path);
        EXPECT(!bytes.empty());

        ImageView iv;
        EXPECT(iv.set_image_jpeg("shot", bytes.data(), bytes.size()));
        EXPECT(iv.has_image());
        EXPECT(iv.get_content_id() == "shot");
        EXPECT(iv.measure().width == 2 && iv.measure().height == 2);

        // lossy decode: channels drift, so only structural facts hold —
        // the same id is a no-op (the image survives byte corruption)
        const unsigned char corrupt[] = "not a jpeg";
        EXPECT(iv.set_image_jpeg("shot", corrupt, sizeof(corrupt) - 1));
        EXPECT(iv.get_content_id() == "shot");
        EXPECT(iv.measure().width == 2 && iv.measure().height == 2);
    }
#endif

    return test::report("image_view");
}
