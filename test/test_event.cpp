// Event<TArgs...> / Subscription<TArgs...> unit tests.
// Focus areas: deferred (tombstone) removal during invoke, subscribe-
// during-invoke isolation, reentrancy, and RAII guard lifecycle safety.
#include "test.hpp"

#include "test_alloc_count.hpp"

#include "event.hpp"

using zb::event::Event;
using zb::event::Subscription;

static void test_basic_subscribe_invoke_unsub()
{
    Event<> ev;
    int calls = 0;
    const uint32_t id = ev += [&calls]() { ++calls; };
    EXPECT(id != ev.INVALID_EVENT_ID);
    EXPECT(bool(ev));

    ev();
    EXPECT(calls == 1);

    ev -= id;
    EXPECT(!bool(ev));
    ev();
    EXPECT(calls == 1);
}

static void test_subscribe_within_invoke_is_deferred()
{
    Event<int> ev;
    int late_calls = 0;
    int early_calls = 0;

    ev += [&](const int v)
    {
        ++early_calls;
        // subscribing from a handler must not fire the new handler
        // during this same invoke (id-based sub: fire-and-forget)
        ev += [&late_calls](const int) { ++late_calls; };
    };

    ev(1);
    EXPECT(early_calls == 1);
    EXPECT(late_calls == 0);  // deferred to the next invoke

    ev(2);
    EXPECT(early_calls == 2);
    EXPECT(late_calls == 1);
}

static void test_unsub_within_invoke_is_deferred_but_effective()
{
    Event<> ev;
    int calls = 0;
    const uint32_t doomed = ev += [&calls]() { ++calls; };

    // the second handler unsubscribes the first during the invoke
    ev += [&]() { ev.unsub(doomed); };

    ev();  // doomed already ran (it was registered first)
    EXPECT(calls == 1);

    // the tombstone must be gone: the next invoke only runs the unsubscriber
    ev();
    EXPECT(calls == 1);
    EXPECT(bool(ev));  // the unsubscriber itself is still alive
}

static void test_unsub_all_during_invoke()
{
    Event<> ev;
    int calls = 0;
    const uint32_t a = ev += [&calls]() { ++calls; };
    const uint32_t b = ev += [&calls]() { ++calls; };
    const uint32_t c = ev += [&calls]() { ++calls; };
    (void)a;
    (void)b;
    (void)c;

    ev += [&]()
    {
        ev.unsub(a);
        ev.unsub(b);
        ev.unsub(c);
    };

    ev();
    EXPECT(calls == 3);  // all three fired before the unsubscriber ran

    ev();
    EXPECT(calls == 3);  // and none of them fired again
    EXPECT(bool(ev));    // only the unsubscriber remains
}

static void test_reentrant_invoke()
{
    Event<> outer;
    Event<> inner;
    int outer_calls = 0;
    int inner_calls = 0;

    outer += [&]()
    {
        ++outer_calls;
        inner();
        ++outer_calls;
    };
    inner += [&]() { ++inner_calls; };

    outer();
    EXPECT(outer_calls == 2);
    EXPECT(inner_calls == 1);
}

static void test_unsub_during_reentrant_invoke_compacts_once()
{
    // a handler of a nested event removes an outer handler; the tombstone
    // must survive until the outermost invoke finishes
    Event<> ev;
    int calls = 0;
    const uint32_t doomed = ev += [&calls]() { ++calls; };
    Event<> inner;

    ev += [&]()
    {
        inner += [&]() { ev.unsub(doomed); };
        inner();
    };

    ev();
    EXPECT(calls == 1);  // doomed ran; the unsubscriber ran and removed it

    ev();
    EXPECT(calls == 1);  // doomed is gone after the compaction
}

static void test_subscription_raii()
{
    Event<> ev;
    int calls = 0;

    {
        Subscription<> s = ev.subscribe([&calls]() { ++calls; });
        EXPECT(bool(s));
        ev();
        EXPECT(calls == 1);
    }  // s destructs -> unsubscribed
    EXPECT(!bool(ev));
    ev();
    EXPECT(calls == 1);
}

static void test_subscription_move()
{
    Event<> ev;
    int calls = 0;

    Subscription<> a = ev.subscribe([&calls]() { ++calls; });
    Subscription<> b = std::move(a);
    EXPECT(!bool(a));
    EXPECT(bool(b));

    ev();
    EXPECT(calls == 1);

    Subscription<> c;
    c = std::move(b);
    EXPECT(!bool(b));
    EXPECT(bool(c));
    c.reset();
    EXPECT(!bool(c));
    EXPECT(!bool(ev));
}

static void test_subscription_event_destroyed_first()
{
    int calls = 0;
    Subscription<> *s = nullptr;
    {
        Event<> ev;
        s = new Subscription<>(ev.subscribe([&calls]() { ++calls; }));
        ev();
        EXPECT(calls == 1);
        // ev dies first; s is detached and becomes a no-op
    }
    s->reset();  // must not touch the destroyed Event
    delete s;
}

static void test_subscription_unsub_during_invoke_via_raii()
{
    Event<> ev;
    int calls = 0;
    Subscription<> *victim = new Subscription<>(ev.subscribe([&calls]() { ++calls; }));
    Subscription<> keeper = ev.subscribe([&]()
    {
        // destroying the RAII guard from inside an invoke tombstones
        // the victim without invalidating the running iteration
        delete victim;
        victim = nullptr;
    });

    ev();
    EXPECT(calls == 1);
    EXPECT(bool(ev));  // keeper still subscribed

    ev();
    EXPECT(calls == 1);  // victim never ran again
}

static void test_subscription_must_be_held()
{
    // an un-held guard unsubscribes immediately: subscribe() is RAII,
    // use += / sub() when the handler should outlive the expression
    Event<> ev;
    int calls = 0;

    ev.subscribe([&calls]() { ++calls; });  // temporary guard dies here
    EXPECT(!bool(ev));
    ev();
    EXPECT(calls == 0);
}

static void test_handler_exception_keeps_event_consistent()
{
    // an exception escaping a handler is unsupported (code-contract 1.5),
    // but it must not corrupt the event: the depth guard unwinds and
    // compacts, so the table keeps its outside-invoke semantics
    Event<> ev;
    int fires = 0;
    const uint32_t a = ev += [&fires]() { ++fires; };
    bool boom = true;
    const uint32_t b = ev += [&boom]()
    {
        if (boom)
        {
            boom = false;
            throw 1;
        }
    };
    const uint32_t c = ev += [&fires]() { ++fires; };

    try
    {
        ev();
    }
    catch (const int &)
    {
    }
    // the handler before the thrower ran, the one after it did not
    EXPECT(fires == 1);

    ev();  // the thrower is still subscribed and runs again
    EXPECT(fires == 3);

    // unsub from outside an invoke erases immediately: no leftover
    // tombstone, no stuck depth
    ev.unsub(a);
    ev.unsub(b);
    EXPECT(bool(ev));
    ev.unsub(c);
    EXPECT(!bool(ev));
}

static void test_handler_exception_no_table_growth()
{
    // regression gate for the depth guard: without it a throwing handler
    // leaves the event permanently "invoking", every later unsub
    // tombstones instead of erasing, and the handler table grows without
    // bound. Warm the capacity, then hammer subscribe / invoke-throw /
    // unsubscribe cycles: the exception-unwound compaction must keep the
    // table inside its capacity (zero allocation).
    Event<> ev;
    for (int i = 0; i < 4; ++i)
    {
        ev += []() {};
    }
    ev();
    bool boom = true;
    ev += [&boom]()
    {
        if (boom)
        {
            throw 1;
        }
    };

    const int cycles = 200;
    bool caught = true;
    {
        test::scoped_alloc_count c;
        for (int i = 0; i < cycles; ++i)
        {
            const uint32_t id = ev += []() {};
            try
            {
                ev();
                caught = false;  // the expected throw did not happen
            }
            catch (const int &)
            {
            }
            ev.unsub(id);
        }
        std::printf("event subscribe/invoke-throw/unsub allocations (%d cycles): %lld\n",
                    cycles, c.delta());
        EXPECT(c.delta() == 0);
    }
    EXPECT(caught);
    EXPECT(bool(ev));  // the thrower itself stays subscribed
}

int test_event()
{
    test_basic_subscribe_invoke_unsub();
    test_subscribe_within_invoke_is_deferred();
    test_unsub_within_invoke_is_deferred_but_effective();
    test_unsub_all_during_invoke();
    test_reentrant_invoke();
    test_unsub_during_reentrant_invoke_compacts_once();
    test_subscription_raii();
    test_subscription_move();
    test_subscription_event_destroyed_first();
    test_subscription_unsub_during_invoke_via_raii();
    test_subscription_must_be_held();
    test_handler_exception_keeps_event_consistent();
    test_handler_exception_no_table_growth();

    return test::report("event");
}
