#include "test.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

#include "codec/gif.hpp"
#include "imcore.hpp"

using namespace zb::ui;

namespace
{
    std::vector<uint8_t> slurp(const char *path)
    {
        std::vector<uint8_t> bytes;
        std::FILE *f = std::fopen(path, "rb");
        EXPECT(f != nullptr);
        if (f == nullptr)
        {
            return bytes;
        }
        uint8_t buf[4096];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        {
            bytes.insert(bytes.end(), buf, buf + n);
        }
        std::fclose(f);
        return bytes;
    }

    /*
     * Structural walk of the emitted GIF (no decoder dependency):
     * counts image descriptors, verifies every graphic control
     * extension ends with its block terminator (a missing terminator
     * made strict decoders reject the file, bb916e7) and that the
     * trailer closes the stream.
     */
    void walk(const std::vector<uint8_t> &b, int &frames,
              bool &gce_terminators, bool &trailer)
    {
        frames = 0;
        gce_terminators = true;
        trailer = false;
        std::size_t i = 0;
        EXPECT(b.size() > 13 + 256 * 3);
        EXPECT(b[0] == 'G' && b[1] == 'I' && b[2] == 'F' &&
               b[3] == '8' && b[4] == '9' && b[5] == 'a');
        EXPECT((b[10] & 0x80) != 0);  // global color table present
        i = 13 + 256 * 3;
        while (i < b.size())
        {
            const uint8_t block = b[i++];
            if (block == 0x3B)
            {
                trailer = true;
                break;
            }
            if (block == 0x21)
            {
                // extension = label + one or more size-prefixed
                // sub-blocks + a 0x00 terminator (NETSCAPE2.0 carries
                // two sub-blocks, so a single-sub-block skip desyncs)
                const uint8_t label = b[i++];
                if (label == 0xF9)
                {
                    // the GCE has exactly one 4-byte sub-block; the
                    // byte after its data must be the terminator
                    if (i + 1 + 4 >= b.size() || b[i + 1 + 4] != 0x00)
                    {
                        gce_terminators = false;
                    }
                }
                while (i < b.size())
                {
                    const uint8_t sz = b[i++];
                    if (sz == 0)
                    {
                        break;
                    }
                    i += sz;
                }
            }
            else if (block == 0x2C)
            {
                ++frames;
                i += 8;  // x, y, w, h
                const uint8_t flags = b[i];
                ++i;
                if (flags & 0x80)
                {
                    i += 3 * (1 << ((flags & 0x07) + 1));  // local color table
                }
                ++i;  // LZW minimum code size
                while (i < b.size())
                {
                    const uint8_t sz = b[i++];
                    if (sz == 0)
                    {
                        break;
                    }
                    i += sz;
                }
            }
            else
            {
                break;  // unknown block: stop walking
            }
        }
    }
}

int test_gif()
{
    // two solid frames: the writer quantizes through the palette and
    // the stream carries both image descriptors
    {
        auto g = core::Graphics::make_ptr(8, 6);
        g->fill(core::colors::Red);
        {
            GifWriter w("test_gif_out.gif", 8, 6, 5);
            w.add_frame(g->data());
            g->fill(core::colors::Blue);
            w.add_frame(g->data());
            EXPECT(w.frames() == 2);
        }  // destructor closes: the trailer must be present after scope

        int frames = 0;
        bool term = false, trailer = false;
        const auto bytes = slurp("test_gif_out.gif");
        walk(bytes, frames, term, trailer);
        EXPECT(frames == 2);
        EXPECT(term);
        EXPECT(trailer);

        // red quantizes to the (5,0,0) cube entry; the global palette
        // stores it at index 180 as exactly (255, 0, 0)
        const std::size_t red_idx = 5 * 36;
        EXPECT(bytes[13 + red_idx * 3] == 255 &&
               bytes[13 + red_idx * 3 + 1] == 0 &&
               bytes[13 + red_idx * 3 + 2] == 0);
    }

    // determinism: the same frames produce byte-identical streams
    {
        core::Color frame1[2] = {core::colors::White, core::colors::Black};
        core::Color frame2[2] = {core::colors::Black, core::colors::White};
        {
            GifWriter w("test_gif_a.gif", 2, 1, 5);
            w.add_frame(frame1);
            w.add_frame(frame2);
        }
        {
            GifWriter w("test_gif_b.gif", 2, 1, 5);
            w.add_frame(frame1);
            w.add_frame(frame2);
        }
        const auto a = slurp("test_gif_a.gif");
        const auto b = slurp("test_gif_b.gif");
        EXPECT(a.size() == b.size());
        bool same = a.size() == b.size();
        for (std::size_t i = 0; same && i < a.size(); ++i)
        {
            same = a[i] == b[i];
        }
        EXPECT(same);
        int frames = 0;
        bool term = false, trailer = false;
        walk(a, frames, term, trailer);
        EXPECT(frames == 2 && term && trailer);
    }

    std::remove("test_gif_out.gif");
    std::remove("test_gif_a.gif");
    std::remove("test_gif_b.gif");

    return test::report("gif");
}
