#pragma once

/*
 * Shared pointer used across the framework (Graphics surfaces, glyph
 * providers, app/window handles).
 *
 * Default: std::shared_ptr with its (atomic) reference counting, which is
 * what every desktop/WASM toolchain supports. On targets without atomic
 * instructions (e.g. the NDS ARM9, whose devkitARM ships no libatomic and
 * falls back to library calls for every refcount bump), define
 * USE_NON_ATOMIC_PTR to switch to a minimal non-atomic implementation:
 * plain int refcount, no thread-safety (the UI runs on a single thread).
 * See the note in imcore/CMakeLists.txt.
 *
 * Supported surface: make_shared / copy / get / reset / -> / * / bool.
 * No weak_ptr, no aliasing constructors, no custom deleters are used
 * anywhere in this code base.
 */

#include <memory>
#include <utility>

namespace zb
{
#if defined(USE_NON_ATOMIC_PTR)

    template <class T>
    class SharedPtr
    {
    public:
        SharedPtr() = default;
        explicit SharedPtr(T *p) : ptr_(p), count_(make_count(p)) {}

        SharedPtr(const SharedPtr &o) : ptr_(o.ptr_), count_(o.count_) { addref(); }
        SharedPtr &operator=(const SharedPtr &o)
        {
            if (this != &o)
            {
                release();
                ptr_ = o.ptr_;
                count_ = o.count_;
                addref();
            }
            return *this;
        }

        SharedPtr(SharedPtr &&o) noexcept : ptr_(o.ptr_), count_(o.count_)
        {
            o.ptr_ = nullptr;
            o.count_ = nullptr;
        }
        SharedPtr &operator=(SharedPtr &&o) noexcept
        {
            if (this != &o)
            {
                release();
                ptr_ = o.ptr_;
                count_ = o.count_;
                o.ptr_ = nullptr;
                o.count_ = nullptr;
            }
            return *this;
        }

        // converting constructors (upcasts, e.g. Tictactoe -> IApp), mirroring
        // std::shared_ptr's templated ctor/assignment
        template <class U>
        SharedPtr(const SharedPtr<U> &o) : ptr_(o.ptr_), count_(o.count_)
        {
            addref();
        }
        template <class U>
        SharedPtr &operator=(const SharedPtr<U> &o)
        {
            if (ptr_ != o.ptr_)
            {
                release();
                ptr_ = o.ptr_;
                count_ = o.count_;
                addref();
            }
            return *this;
        }
        template <class U>
        SharedPtr(SharedPtr<U> &&o) noexcept : ptr_(o.ptr_), count_(o.count_)
        {
            o.ptr_ = nullptr;
            o.count_ = nullptr;
        }
        // Cross-type move assignment: &o == this is impossible here (U != T),
        // so no self-assignment guard is needed -- unlike the same-type move
        // operator above, whose `this != &o` guard is correct because it only
        // asks "is this literally the same variable?".
        // Do NOT guard on `ptr_ != o.ptr_`: when both variables already alias
        // the same object, skipping this branch would leave o un-cleared,
        // violating the move contract ("source is empty after the move").
        // release() below only drops *this's own reference, which is safe and
        // idempotent even in the aliased case.
        template <class U>
        SharedPtr &operator=(SharedPtr<U> &&o) noexcept
        {
            release();
            ptr_ = o.ptr_;
            count_ = o.count_;
            o.ptr_ = nullptr;
            o.count_ = nullptr;
            return *this;
        }

        ~SharedPtr() { release(); }

        void reset()
        {
            release();
            ptr_ = nullptr;
            count_ = nullptr;
        }
        void reset(T *p)
        {
            int *c = make_count(p);  // throws -> p deleted, *this unchanged
            release();
            ptr_ = p;
            count_ = c;
        }

        T *get() const { return ptr_; }
        T *operator->() const { return ptr_; }
        T &operator*() const { return *ptr_; }
        explicit operator bool() const { return ptr_ != nullptr; }

        friend bool operator==(const SharedPtr &a, std::nullptr_t) { return a.ptr_ == nullptr; }
        friend bool operator!=(const SharedPtr &a, std::nullptr_t) { return a.ptr_ != nullptr; }

    private:
        template <class U>
        friend class SharedPtr;

        // Ownership of p is being transferred to this SharedPtr. If the
        // refcount allocation fails, p is already ours, so delete it before
        // propagating -- the OOM path must not leak the object. Returning a
        // fresh `new int(1)` (instead of in-place `new` in the ctor) keeps the
        // `release()` logic in one place.
        // A-11 (2026-08-28): nullptr does not own a control block — skip
        // the allocation to avoid a stray `new int(1)` for `reset(nullptr)`.
        static int *make_count(T *p)
        {
            if (p == nullptr)
            {
                return nullptr;
            }
            try
            {
                return new int(1);
            }
            catch (...)
            {
                delete p;
                throw;
            }
        }

        void addref()
        {
            if (count_ != nullptr)
            {
                ++*count_;
            }
        }
        void release()
        {
            if (count_ != nullptr && --*count_ == 0)
            {
                delete ptr_;
                delete count_;
            }
        }

        T *ptr_ = nullptr;
        int *count_ = nullptr;
    };

#else

    template <class T>
    using SharedPtr = std::shared_ptr<T>;

#endif

    /*
     * Factory matching std::make_shared; on the non-atomic branch it uses
     * a separate refcount allocation (the savings there are the missing
     * atomics, not the single allocation).
     */
    template <class T, class... Args>
    SharedPtr<T> make_shared(Args &&...args)
    {
#if defined(USE_NON_ATOMIC_PTR)
        return SharedPtr<T>(new T(std::forward<Args>(args)...));
#else
        return std::make_shared<T>(std::forward<Args>(args)...);
#endif
    }
}  // namespace zb
