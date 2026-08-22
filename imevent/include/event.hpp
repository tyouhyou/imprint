#ifndef EVENT_HPP
#define EVENT_HPP

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace zb::event
{
    template <class... TArgs>
    class Event;

    template <class... TArgs>
    class Subscription;

    /*
     * A publish/subscribe event with zero-allocation hot path.
     *
     * Handlers are stored in a flat vector of (id, handler) pairs; invoke()
     * iterates by index so subscriptions made during invocation are not
     * called until the next invoke, and unsubscribing during invocation
     * only tombstones the entry (the id's std::function is cleared), which
     * is compacted away when the outermost invoke finishes. No copy of the
     * handler table is ever taken, so firing an event on a hot path (e.g.
     * per-frame) performs no heap allocation.
     *
     * Subscribers that need to live with the event should hold the RAII
     * Subscription returned by subscribe(); both objects detach safely on
     * destruction in either order.
     *
     * Exception contract (see docs/code-contract.md section 1.5): a
     * handler must not throw -- an escaping exception would be swallowed
     * by the C-ABI boundary's catch(...) while the event still owned the
     * call. The invoke depth is guarded with RAII anyway, so a throwing
     * handler can never leave the event permanently "invoking": the depth
     * unwinds and the tombstone compaction still runs.
     */
    template <class... TArgs>
    class Event
    {
    public:
        using EventHandler = std::function<void(const TArgs &...args)>;

    public:
        static constexpr uint32_t INVALID_EVENT_ID = 0;

        Event() = default;

        // detaches every live subscription so their destructors are no-ops
        ~Event()
        {
            for (Subscription<TArgs...> *s : attached_subscriptions)
            {
                s->event_ = nullptr;
            }
            attached_subscriptions.clear();
            handlers.clear();
        }

        // non-copyable, non-movable: the handler table and id counter must
        // stay consistent (a moved-from Event would share subscribers)
        Event(const Event &) = delete;
        Event &operator=(const Event &) = delete;

        uint32_t operator+=(const EventHandler handler)
        {
            return sub(handler);
        }

        void operator-=(const uint32_t &id)
        {
            unsub(id);
        }

        // true when at least one handler is subscribed (dead entries that
        // were only tombstoned during an invoke do not count)
        operator bool() const
        {
            for (const auto &entry : handlers)
            {
                if (entry.second)
                {
                    return true;
                }
            }
            return false;
        }

        // registers a handler; returns its id for manual unsubscribe()
        uint32_t sub(EventHandler handler)
        {
            if (!handler)
            {
                return INVALID_EVENT_ID;
            }
            // wrap-around skips 0 (INVALID_EVENT_ID): a subscriber must
            // never collide with the "no id" sentinel, however long the
            // event lives (e.g. a long-running industrial display)
            do
            {
                ++next_id;
            } while (next_id == INVALID_EVENT_ID);
            handlers.push_back(std::make_pair(next_id, std::move(handler)));
            return next_id;
        }

        // RAII registration: the subscription unsubscribes on destruction
        Subscription<TArgs...> subscribe(const EventHandler &handler)
        {
            return Subscription<TArgs...>(*this, sub(handler));
        }

        void unsub(const uint32_t &id)
        {
            for (std::size_t i = 0; i < handlers.size(); ++i)
            {
                if (handlers[i].first == id)
                {
                    if (invoke_depth > 0)
                    {
                        // during invoke: tombstone; the final compaction
                        // below removes the dead entries
                        handlers[i].second = EventHandler();
                    }
                    else
                    {
                        handlers.erase(handlers.begin() + static_cast<std::ptrdiff_t>(i));
                    }
                    break;
                }
            }
        }

        void operator()(const TArgs &...args)
        {
            invoke(args...);
        }

        void invoke(const TArgs &...args)
        {
            // iterate by index over the size captured on entry: handlers
            // added during the invoke are not called until the next one,
            // and tombstoned entries are skipped by the empty check. The
            // guard unwinds the depth and compacts even when a handler
            // throws (unsupported, see the exception contract above).
            InvokeGuard guard(*this);
            const std::size_t n = handlers.size();
            for (std::size_t i = 0; i < n; ++i)
            {
                if (handlers[i].second)
                {
                    handlers[i].second(args...);
                }
            }
        }

        inline void emit(const TArgs &...args)
        {
            invoke(args...);
        }

    private:
        /*
         * Depth counter RAII: increments on entry; on exit decrements and
         * compacts the tombstoned entries of the outermost invoke. Even
         * an exception escaping a handler (unsupported, see the class
         * comment) unwinds through it, so the event is never left
         * permanently "invoking" with unsub() stuck in tombstone mode.
         */
        struct InvokeGuard
        {
            Event &event;

            explicit InvokeGuard(Event &e) : event(e)
            {
                ++event.invoke_depth;
            }

            ~InvokeGuard()
            {
                if (--event.invoke_depth == 0 && !event.handlers.empty())
                {
                    event.compact();
                }
            }
        };

        // Subscription registers/unregisters itself here; the Event
        // destructor detaches all of them so neither object can dangle
        void attach(Subscription<TArgs...> &s) { attached_subscriptions.push_back(&s); }
        void detach(Subscription<TArgs...> &s)
        {
            for (auto it = attached_subscriptions.begin(); it != attached_subscriptions.end(); ++it)
            {
                if (*it == &s)
                {
                    attached_subscriptions.erase(it);
                    break;
                }
            }
        }

        // drops tombstoned entries after the outermost invoke
        void compact()
        {
            std::size_t write = 0;
            for (std::size_t read = 0; read < handlers.size(); ++read)
            {
                if (handlers[read].second)
                {
                    if (write != read)
                    {
                        handlers[write] = std::move(handlers[read]);
                    }
                    ++write;
                }
            }
            handlers.resize(write);
        }

        using Entry = std::pair<uint32_t, EventHandler>;
        std::vector<Entry> handlers;
        uint32_t next_id = 0;
        uint32_t invoke_depth = 0;
        std::vector<Subscription<TArgs...> *> attached_subscriptions;

        friend class Subscription<TArgs...>;
    };

    /*
     * RAII guard for an Event subscription: unsubscribes on destruction.
     * Safe in either order of destruction relative to the Event -- an Event
     * being destroyed first detaches all its subscriptions, and a dead
     * Subscription becomes a no-op. Movable, not copyable.
     */
    template <class... TArgs>
    class Subscription
    {
    public:
        Subscription() = default;

        Subscription(Event<TArgs...> &owner, const uint32_t id)
            : event_(&owner), id_(id)
        {
            owner.attach(*this);
        }

        ~Subscription()
        {
            reset();
        }

        Subscription(const Subscription &) = delete;
        Subscription &operator=(const Subscription &) = delete;

        Subscription(Subscription &&other) noexcept
            : event_(other.event_), id_(other.id_)
        {
            other.event_ = nullptr;
            other.id_ = 0;
            if (event_ != nullptr)
            {
                event_->detach(other);
                event_->attach(*this);
            }
        }

        Subscription &operator=(Subscription &&other) noexcept
        {
            if (this != &other)
            {
                reset();
                event_ = other.event_;
                id_ = other.id_;
                other.event_ = nullptr;
                other.id_ = 0;
                if (event_ != nullptr)
                {
                    event_->detach(other);
                    event_->attach(*this);
                }
            }
            return *this;
        }

        // unsubscribes and leaves the guard empty
        void reset()
        {
            if (event_ != nullptr)
            {
                Event<TArgs...> *e = event_;
                event_ = nullptr;
                e->detach(*this);
                e->unsub(id_);
                id_ = 0;
            }
        }

        explicit operator bool() const { return event_ != nullptr; }
        uint32_t id() const { return id_; }

    private:
        Event<TArgs...> *event_ = nullptr;
        uint32_t id_ = 0;

        friend class Event<TArgs...>;
    };
} // namespace zb::event

#endif // end of EVENT_HPP
