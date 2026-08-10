// Exercises the USE_NON_ATOMIC_PTR branch of zb::SharedPtr -- the build the
// NDS uses. The define must precede every include so this TU resolves
// zb::SharedPtr to the non-atomic implementation (host CI otherwise never
// compiles it). Nothing here crosses the ABI into the linked libraries.
#define USE_NON_ATOMIC_PTR

#include "test.hpp"

namespace
{
    struct PtrBase
    {
        virtual ~PtrBase() = default;
        int tag = 0;
    };

    struct PtrDerived : PtrBase
    {
        static int alive;
        PtrDerived() { ++alive; }
        ~PtrDerived() override { --alive; }
    };

    int PtrDerived::alive = 0;
}

int test_ptr()
{
    // aliased cross-type move assignment: source must end up empty. This was
    // a latent bug in the non-atomic branch -- the `ptr_ != o.ptr_` guard
    // skipped the whole block when both variables already shared the object.
    {
        zb::SharedPtr<PtrDerived> d = zb::make_shared<PtrDerived>();
        zb::SharedPtr<PtrBase> b = d;   // b aliases d (refcount == 2)
        b = std::move(d);               // move the aliased source
        EXPECT(static_cast<bool>(b));   // target still owns the object
        EXPECT(!d);                     // source must be empty afterwards
        b->tag = 7;
        EXPECT(b->tag == 7);
    }
    EXPECT(PtrDerived::alive == 0);     // destroyed exactly once, no leak

    // ordinary cross-type move from a freshly created object
    {
        zb::SharedPtr<PtrBase> b;
        b = zb::make_shared<PtrDerived>();
        EXPECT(static_cast<bool>(b));
    }
    EXPECT(PtrDerived::alive == 0);

    // moving an empty source empties the target
    {
        zb::SharedPtr<PtrBase> b = zb::make_shared<PtrDerived>();
        zb::SharedPtr<PtrDerived> d;
        b = std::move(d);
        EXPECT(!b);
    }
    EXPECT(PtrDerived::alive == 0);

    // same-type self-move is a safe no-op
    {
        zb::SharedPtr<PtrDerived> d = zb::make_shared<PtrDerived>();
        d = std::move(d);
        EXPECT(static_cast<bool>(d));
    }
    EXPECT(PtrDerived::alive == 0);

    // converting move ctor and aliased converting copy assignment still work
    {
        zb::SharedPtr<PtrDerived> d = zb::make_shared<PtrDerived>();
        zb::SharedPtr<PtrBase> b = std::move(d);  // converting move ctor
        EXPECT(!d);
        EXPECT(static_cast<bool>(b));
        zb::SharedPtr<PtrBase> c;
        c = b;                                    // converting copy assignment
        EXPECT(static_cast<bool>(c));
        EXPECT(c.get() == b.get());
    }
    EXPECT(PtrDerived::alive == 0);

    // raw-pointer ownership transfer via ctor / reset / reset()
    {
        zb::SharedPtr<PtrDerived> d(new PtrDerived());
        EXPECT(static_cast<bool>(d));
        EXPECT(PtrDerived::alive == 1);
        d.reset();
        EXPECT(!d);
        EXPECT(PtrDerived::alive == 0);

        PtrDerived *raw = new PtrDerived();
        d.reset(raw);
        EXPECT(d.get() == raw);
    }
    EXPECT(PtrDerived::alive == 0);

    return test::report("ptr");
}
