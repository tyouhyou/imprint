#pragma once

#include <cstdio>

#include "imcore.hpp"

namespace test
{
    inline int failures = 0;

    inline void check(const bool cond, const char *file, const int line, const char *expr)
    {
        if (!cond)
        {
            ++failures;
            std::printf("FAIL %s:%d: %s\n", file, line, expr);
        }
    }

    // reports the failures of one suite and returns them
    inline int report(const char *suite)
    {
        const int f = failures;
        failures = 0;
        std::printf("[%s] %s (%d failures)\n", suite, f ? "FAILED" : "PASSED", f);
        return f;
    }

    inline uint32_t pixel_at(const zb::ui::core::Graphics &g, const int x, const int y)
    {
        const auto s = g.size();
        return g.data()[y * s.width + x].pixel;
    }
}

#define EXPECT(cond) ::test::check((cond), __FILE__, __LINE__, #cond)
