#pragma once

/*
 * Allocation counting for the test binary.
 *
 * Overrides the global operator new/delete so suites can assert on heap
 * activity (the framework's hot-path zero-allocation contract, batch J).
 * The counters are plain static scalars: the tests are single-threaded.
 *
 * Include this header in exactly one TU chain of the test binary (it
 * defines the replacement operators); every suite that wants a probe
 * includes it and uses test::scoped_alloc_count.
 */

#include <cstddef>
#include <cstdlib>
#include <new>

namespace test
{
    inline long long g_alloc_count = 0;

    struct scoped_alloc_count
    {
        const long long base = g_alloc_count;
        [[nodiscard]] long long delta() const { return g_alloc_count - base; }
    };
}  // namespace test

inline void *operator new(std::size_t n)
{
    ++test::g_alloc_count;
    if (void *p = std::malloc(n))
    {
        return p;
    }
    throw std::bad_alloc{};
}

inline void *operator new[](std::size_t n)
{
    return ::operator new(n);
}

inline void *operator new(std::size_t n, const std::nothrow_t &) noexcept
{
    ++test::g_alloc_count;
    return std::malloc(n);
}

inline void *operator new[](std::size_t n, const std::nothrow_t &) noexcept
{
    return ::operator new(n, std::nothrow);
}

inline void operator delete(void *p) noexcept
{
    std::free(p);
}

inline void operator delete[](void *p) noexcept
{
    ::operator delete(p);
}

inline void operator delete(void *p, const std::nothrow_t &) noexcept
{
    std::free(p);
}

inline void operator delete[](void *p, const std::nothrow_t &) noexcept
{
    ::operator delete(p, std::nothrow);
}