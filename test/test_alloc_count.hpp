#pragma once

/*
 * Allocation counting for the test binary.
 *
 * Overrides the global operator new/delete so suites can assert on heap
 * activity (the framework's hot-path zero-allocation contract, batch J).
 * The counters are plain static scalars: the tests are single-threaded.
 *
 * The replacement operators live in test_alloc_count.cpp as out-of-line
 * strong definitions. They must NOT stay inline in this header: under
 * -O2/-O3 an inline definition is only emitted in TUs that call it, so
 * allocations made inside the library objects bind to the runtime's own
 * operator new and silently bypass the counter (every allocation gate
 * printed 0 in a Release build before the split). Sized deallocation is
 * replaced as well: optimized builds delete through
 * operator delete(void*, size), and an unreplaced sized delete freed on
 * a different entry point than the counting new allocated on, which
 * ASan reports as alloc-dealloc-mismatch.
 *
 * Not replaced: the aligned (align_val_t) overloads — nothing in the
 * framework or the tests allocates over-aligned types dynamically; such
 * an allocation would run uncounted through the runtime's own new.
 *
 * Visibility boundary: the replacement only covers allocations made by
 * code linked into the test binary. imcore is a SHARED library on
 * Windows, and on MSVC a DLL binds its own CRT allocation entry points
 * — allocations inside imcore.dll (row images, Graphics buffers) are
 * invisible here, so the list-box rebuild gates use
 * ListBox::rasterization_count() instead of allocation deltas
 * (code-contract §8). On ELF hosts the executable's operator new
 * interposes the shared libraries too, so the counter sees everything.
 */

#include <cstddef>

namespace test
{
    inline long long g_alloc_count = 0;

    struct scoped_alloc_count
    {
        const long long base = g_alloc_count;
        [[nodiscard]] long long delta() const { return g_alloc_count - base; }
    };
}  // namespace test
