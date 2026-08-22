#include "test_alloc_count.hpp"

#include <cstdlib>
#include <new>

/*
 * Out-of-line strong definitions (see the header for why inline weak
 * definitions are not enough). Every allocation entry point lands on
 * malloc and every deletion entry point on free, so counted blocks and
 * runtime-allocated blocks share one heap whichever overload the
 * compiler picked at the call site.
 */

void *operator new(std::size_t n)
{
    ++test::g_alloc_count;
    if (void *p = std::malloc(n))
    {
        return p;
    }
    throw std::bad_alloc{};
}

void *operator new[](std::size_t n)
{
    return ::operator new(n);
}

void *operator new(std::size_t n, const std::nothrow_t &) noexcept
{
    ++test::g_alloc_count;
    return std::malloc(n);
}

void *operator new[](std::size_t n, const std::nothrow_t &) noexcept
{
    return ::operator new(n, std::nothrow);
}

void operator delete(void *p) noexcept
{
    std::free(p);
}

void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}

void operator delete[](void *p) noexcept
{
    ::operator delete(p);
}

void operator delete[](void *p, std::size_t) noexcept
{
    ::operator delete(p);
}

void operator delete(void *p, const std::nothrow_t &) noexcept
{
    std::free(p);
}

void operator delete[](void *p, const std::nothrow_t &) noexcept
{
    ::operator delete(p, std::nothrow);
}
