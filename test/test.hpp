#pragma once

#include <cstdio>
#include <variant>

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

    /*
     * Value access for prop_value variants in the tests. The type-based
     * std::get<T>(variant) is availability-gated "introduced in macOS
     * 10.14" in Xcode 10's libc++ and the macOS deployment target is
     * deliberately unpinned, so the tests go through the ungated
     * pointer form (std::get_if, the same accessor the framework uses).
     * A wrong-alternative probe records a failure and returns a
     * value-initialized fallback instead of dereferencing null.
     */
    template <typename T, typename... Ts>
    const T &vget(const std::variant<Ts...> &v)
    {
        static const T fallback{};
        if (const T *p = std::get_if<T>(&v))
        {
            return *p;
        }
        ++failures;
        std::printf("FAIL (vget): variant does not hold the requested type\n");
        return fallback;
    }
}

#define EXPECT(cond) ::test::check((cond), __FILE__, __LINE__, #cond)
