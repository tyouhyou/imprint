// Exercises the USE_NON_ATOMIC_PTR branch of zb::SharedPtr -- the build the
// NDS uses. The define must precede every include so this TU resolves
// zb::SharedPtr to the non-atomic implementation (host CI otherwise never
// compiles it). Nothing here crosses the ABI into the linked libraries.
// The CI non-atomic matrix job defines the macro on the command line for
// the whole framework, so only define it when it is not already set.
#ifndef USE_NON_ATOMIC_PTR
#define USE_NON_ATOMIC_PTR
#endif

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
        zb::SharedPtr<PtrBase> b = d;  // b aliases d (refcount == 2)
        b = std::move(d);              // move the aliased source
        EXPECT(static_cast<bool>(b));  // target still owns the object
        EXPECT(!d);                    // source must be empty afterwards
        b->tag = 7;
        EXPECT(b->tag == 7);
    }
    EXPECT(PtrDerived::alive == 0);  // destroyed exactly once, no leak

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
        // through an alias so the self-move is a runtime fact, not a
        // literal the compiler rejects up front (-Wself-move)
        zb::SharedPtr<PtrDerived> &alias = d;
        d = std::move(alias);
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
        c = b;  // converting copy assignment
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

    // copy semantics: two owners keep the object alive; the last release
    // destroys it (refcount parity with std::shared_ptr)
    {
        zb::SharedPtr<PtrDerived> a = zb::make_shared<PtrDerived>();
        {
            zb::SharedPtr<PtrDerived> b = a;  // copy ctor: two owners
            EXPECT(a.get() == b.get());
            EXPECT(PtrDerived::alive == 1);  // one object, two owners
            b->tag = 3;                      // aliasing writes are visible
            EXPECT(a->tag == 3);
        }
        EXPECT(PtrDerived::alive == 1);  // b died, the object survived
    }
    EXPECT(PtrDerived::alive == 0);  // last owner destroyed it

    // copy assignment releases the previously held object
    {
        zb::SharedPtr<PtrDerived> a = zb::make_shared<PtrDerived>();
        zb::SharedPtr<PtrDerived> b = zb::make_shared<PtrDerived>();
        EXPECT(PtrDerived::alive == 2);
        a = b;
        EXPECT(PtrDerived::alive == 1);  // a's old object released
        EXPECT(a.get() == b.get());
    }
    EXPECT(PtrDerived::alive == 0);

    // runtime self-copy-assignment is a safe no-op (aliased access avoids
    // the compile-time self-assign pattern so no toolchain warns)
    {
        zb::SharedPtr<PtrDerived> d = zb::make_shared<PtrDerived>();
        zb::SharedPtr<PtrDerived> &alias = d;
        d = alias;
        EXPECT(static_cast<bool>(d));
        EXPECT(PtrDerived::alive == 1);
    }
    EXPECT(PtrDerived::alive == 0);

    // dereference, null-state and nullptr comparison surface
    {
        zb::SharedPtr<PtrDerived> d = zb::make_shared<PtrDerived>();
        (*d).tag = 5;
        EXPECT(d->tag == 5);
        EXPECT(d != nullptr);
        EXPECT(!(!d));
        zb::SharedPtr<PtrDerived> e;
        EXPECT(e == nullptr);
        EXPECT(!e);
    }
    EXPECT(PtrDerived::alive == 0);

    // construction from nullptr, both branches. std::shared_ptr accepts it;
    // the non-atomic branch did not until 2026-10-30, so a `return nullptr;`
    // for a SharedPtr return type compiled everywhere except the
    // USE_NON_ATOMIC_PTR config (the CI linux-nonatomic-ptr red). The two
    // branches must accept the same expressions -- this TU always compiles
    // the non-atomic branch (USE_NON_ATOMIC_PTR above), so the lock lives
    // here rather than in a config only CI builds.
    {
        zb::SharedPtr<PtrBase> a = nullptr;  // copy-init from nullptr
        zb::SharedPtr<PtrBase> b(nullptr);   // direct-init from nullptr
        EXPECT(!a);
        EXPECT(!b);
        zb::SharedPtr<PtrDerived> d = nullptr;
        zb::SharedPtr<PtrBase> up = d;  // converting copy of a null handle
        EXPECT(!up);
        EXPECT(up == nullptr);
        a = nullptr;  // assignment through the converting ctor
        EXPECT(!a);
    }

    return test::report("ptr");
}
