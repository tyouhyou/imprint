// Snapshot helper tests (code-contract §12): hash stability, the
// record → check → mismatch workflow with its artifact, the missing-
// baseline status, and the GIF/PNG dumps.
#include "test.hpp"

#include <cstdio>
#include <memory>
#include <string>

#include "canvas_window.hpp"
#include "snapshot.hpp"

#ifdef _WIN32
#include <direct.h>
#define IM_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define IM_MKDIR(p) ::mkdir(p, 0755)
#endif

using namespace zb::app;

namespace
{
    // a distinct widget so a second paint state hashes differently
    std::unique_ptr<zb::ui::Button> make_button(const char *text)
    {
        auto b = std::make_unique<zb::ui::Button>();
        b->set_size(40, 16);
        b->set_text(text);
        return b;
    }
}

int test_snapshot()
{
    const std::string dir = "snapshot_test_out";
    IM_MKDIR(dir.c_str());  // exists is fine

    // hash stability: the same rendered state hashes identically, and
    // a content change changes the hash
    {
        CanvasWindow w;
        w.create(64, 32);
        auto btn = make_button("A");
        w.root().add_child(std::move(btn));
        w.paint();
        const uint64_t h1 = zb::snap::framebuffer_hash(w);
        const uint64_t h2 = zb::snap::framebuffer_hash(w);
        EXPECT(h1 != 0);
        EXPECT(h1 == h2);

        auto btn2 = make_button("B");
        w.root().add_child(std::move(btn2));
        w.paint();
        const uint64_t h3 = zb::snap::framebuffer_hash(w);
        EXPECT(h3 != h1);
    }

    // record → check(ok) → check(mismatch + artifact)
    {
        CanvasWindow w;
        w.create(64, 32);
        auto btn = make_button("A");
        w.root().add_child(std::move(btn));
        w.paint();

        const uint64_t recorded = zb::snap::record(w, "baseline", dir);
        EXPECT(recorded != 0);

        const auto ok = zb::snap::check(w, "baseline", dir);
        EXPECT(ok.status == zb::snap::check_result::status::ok);
        EXPECT(ok.expected == recorded);
        EXPECT(ok.actual == recorded);

        w.root().add_child(make_button("C"));
        w.paint();
        const auto bad = zb::snap::check(w, "baseline", dir);
        EXPECT(bad.status == zb::snap::check_result::status::mismatch);
        EXPECT(bad.expected == recorded);
        EXPECT(bad.actual != recorded);
        // the mismatch artifact lands beside the baseline
        std::FILE *artifact = std::fopen((dir + "/baseline.actual.gif").c_str(), "rb");
        EXPECT(artifact != nullptr);
        if (artifact != nullptr)
        {
            EXPECT(std::fseek(artifact, 0, SEEK_END) == 0);
            EXPECT(std::ftell(artifact) > 0);
            std::fclose(artifact);
        }
    }

    // missing baseline is an expected outcome, not an error
    {
        CanvasWindow w;
        w.create(64, 32);
        w.paint();
        const auto missing = zb::snap::check(w, "no_such_baseline", dir);
        EXPECT(missing.status == zb::snap::check_result::status::missing);
        EXPECT(missing.actual != 0);
    }

    // save_gif produces a non-empty file; an unwritable path throws
    {
        CanvasWindow w;
        w.create(64, 32);
        w.paint();
        zb::snap::save_gif(w, dir + "/frame.gif");
        std::FILE *gif = std::fopen((dir + "/frame.gif").c_str(), "rb");
        EXPECT(gif != nullptr);
        if (gif != nullptr)
        {
            EXPECT(std::fseek(gif, 0, SEEK_END) == 0);
            EXPECT(std::ftell(gif) > 0);
            std::fclose(gif);
        }
        bool threw = false;
        try
        {
            zb::snap::save_gif(w, "no/such/dir/frame.gif");
        }
        catch (const zb::ui::error &)
        {
            threw = true;
        }
        EXPECT(threw);
    }

#if defined(USE_PNG)
    // save_png (USE_PNG builds): 0 = OK, codec-family contract
    {
        CanvasWindow w;
        w.create(64, 32);
        w.paint();
        EXPECT(zb::snap::save_png(w, dir + "/frame.png") == 0);
        std::FILE *png = std::fopen((dir + "/frame.png").c_str(), "rb");
        EXPECT(png != nullptr);
        if (png != nullptr)
        {
            std::fclose(png);
        }
    }
#endif

    return test::report("snapshot");
}
