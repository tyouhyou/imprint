// A-26 ② gate instrument: the per-pixel blend/modulate arithmetic at
// 1080p. The backlog's trigger for SIMD is "a 1080p-class consumer
// re-measure against the R1 zero-copy build", and the numbers it quotes
// (fill 5.2 ms, draw_image 6.9 ms, tinted 13.1 ms per full-screen unit)
// came from fps. This suite is the in-tree instrument for that gate.
//
// No assertions -- the printout is the evidence, exactly like
// test_perf_walk (batch J9). It must be run from a **Release** build:
// an untyped build is -O0 and inflates every kernel by roughly 10x
// (the A-26 step-0 lesson in CONTEXT.md 环境注意).
//
// Measurement discipline (AGENTS.md, absorbed from fps R15/R18):
//   - interleaved arms with a rotating order, never arm-then-arm
//   - min-of-N is the reported figure; a median is printed for contrast
//     because fps measured medians swinging 17.6 -> 21.5 ms under
//     contention while min-of-N stayed stable
//   - a NULL A/B (both arms run the same kernel) measures the host's
//     noise floor, so "arm A is faster" is only readable above it
//   - the kernels are the real Graphics entry points, not synthetic
//     loops: a probe whose memory ops do not match the kernel measures
//     the compiler instead (fps R18)
#include "test.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "imcore.hpp"

using namespace zb::ui;

namespace
{
    constexpr int kW = 1920;
    constexpr int kH = 1080;
    constexpr int kRounds = 7;  // min-of-N
    constexpr int kItersPerSample = 12;

    using clock_t_ = std::chrono::steady_clock;

    double ms_between(const clock_t_::time_point &a, const clock_t_::time_point &b)
    {
        return std::chrono::duration<double, std::milli>(b - a).count();
    }

    struct sample_stats
    {
        double min_ms = 0.0;
        double median_ms = 0.0;
    };

    sample_stats summarise(std::vector<double> v)
    {
        sample_stats s;
        if (v.empty())
        {
            return s;
        }
        std::sort(v.begin(), v.end());
        s.min_ms = v.front();
        s.median_ms = v[v.size() / 2];
        return s;
    }

    // One kernel invocation over the whole surface. `phase` varies the
    // colour per call so the compiler cannot hoist the work out of the
    // timing loop or collapse the fill into a memset of one value.
    using kernel_fn = void (*)(core::Graphics &, const core::Color &, const core::Color *);

    void kernel_fill_opaque(core::Graphics &g, const core::Color &c, const core::Color *)
    {
        g.fill_rect(0, 0, kW, kH, c);
    }

    void kernel_fill_blend(core::Graphics &g, const core::Color &c, const core::Color *)
    {
        g.fill_rect(0, 0, kW, kH, c);  // the colour carries alpha 128
    }

    void kernel_image_plain(core::Graphics &g, const core::Color &, const core::Color *src)
    {
        g.draw_image(src, kW, kH, kW, 0, 0);
    }

    void kernel_image_tinted(core::Graphics &g, const core::Color &tint, const core::Color *src)
    {
        g.draw_image(src, kW, kH, kW, 0, 0, tint);
    }

    struct kernel_case
    {
        const char *name;
        kernel_fn fn;
        bool uses_tint;
        bool uses_alpha;
    };

    // a stable colour per call, still cheap to compute. Channel accessors
    // only (A-19): the pixel word is never written directly.
    core::Color phase_colour(const int phase, const bool with_alpha)
    {
        core::Color c;
        c.set_r(static_cast<uint8_t>(phase * 17 + 3));
        c.set_g(static_cast<uint8_t>(phase * 29 + 11));
        c.set_b(static_cast<uint8_t>(phase * 41 + 7));
        c.set_a(static_cast<uint8_t>(with_alpha ? 128 : 255));
        return c;
    }
}  // namespace

int test_perf_fill()
{
    auto surface = core::Graphics::make_ptr(kW, kH);
    if (!surface)
    {
        std::printf("[perf_fill] no surface\n");
        return test::report("perf_fill");
    }

    // a source image for the blit kernels (its own block, so the blit
    // reads real memory instead of aliasing the destination)
    std::vector<core::Color> src(static_cast<size_t>(kW) * kH);
    for (size_t i = 0; i < src.size(); ++i)
    {
        src[i].set_r(static_cast<uint8_t>(i));
        src[i].set_g(static_cast<uint8_t>(i >> 3));
        src[i].set_b(static_cast<uint8_t>(i >> 7));
        src[i].set_a(255);
    }

    const kernel_case cases[] = {
        {"fill_rect opaque   ", kernel_fill_opaque, false, false},
        {"fill_rect a=128    ", kernel_fill_blend, false, true},
        {"draw_image plain   ", kernel_image_plain, false, false},
        {"draw_image tinted  ", kernel_image_tinted, true, false},
    };

    std::printf("[perf_fill] %dx%d, min-of-%d, %d iterations per sample, "
                "Release build required\n",
                kW, kH, kRounds, kItersPerSample);
    std::printf("[perf_fill] %-20s %10s %10s %10s %10s\n",
                "kernel", "min ms", "median ms", "null-A ms", "noise ms");

    for (const auto &kc : cases)
    {
        // two arms run the SAME kernel: the difference between them is the
        // host's noise floor, not a result
        std::vector<double> arm_a;
        std::vector<double> arm_b;
        arm_a.reserve(kRounds);
        arm_b.reserve(kRounds);

        for (int round = 0; round < kRounds; ++round)
        {
            const bool a_first = (round % 2) == 0;  // rotating order
            for (int pass = 0; pass < 2; ++pass)
            {
                const bool is_a = (pass == 0) == a_first;
                const auto t0 = clock_t_::now();
                for (int i = 0; i < kItersPerSample; ++i)
                {
                    const core::Color c = phase_colour(round * kItersPerSample + i, kc.uses_alpha);
                    kc.fn(*surface, c, src.data());
                }
                const auto t1 = clock_t_::now();
                const double per_call = ms_between(t0, t1) / kItersPerSample;
                (is_a ? arm_a : arm_b).push_back(per_call);
            }
        }

        const sample_stats a = summarise(arm_a);
        const sample_stats b = summarise(arm_b);
        const double noise = b.min_ms - a.min_ms;
        std::printf("[perf_fill] %-20s %10.3f %10.3f %10.3f %10.3f\n",
                    kc.name, a.min_ms, a.median_ms, b.min_ms, noise);
    }

    std::printf("[perf_fill] a 60 Hz frame budget is 16.67 ms; the no-clip\n"
                "[perf_fill] remainder for a full-screen unit is the number above\n");
    return test::report("perf_fill");
}
