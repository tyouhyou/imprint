// Event<TArgs...> / Subscription<TArgs...> unit tests.
// Focus areas: deferred (tombstone) removal during invoke, subscribe-
// during-invoke isolation, reentrancy, and RAII guard lifecycle safety.
#include "test.hpp"

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

    return test::report("event");
}
