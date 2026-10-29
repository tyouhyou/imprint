// R6 deterministic frame stepping -- the shell-side half of the
// contract in code-contract §11.4. Deliberately pure: no clock, no
// platform handle, no allocation, so the battery drives it directly
// (test_frame_pacer) and every shell keeps only its event pump and its
// blit, the same split the A-2 input translators use.
//
// Two modes, chosen by run_options::step_micros:
//   - 0 (default): `fixed()` is false and the shell keeps its own
//     scheduling -- event-driven on the desktop shells, idle-poll on FB.
//     Nothing in this class is consulted beyond `done()`, which is then
//     always false.
//   - nonzero: `fixed()` is true and the shell must deliver exactly one
//     `IApp::step(step_micros, index)` per frame, paint, present, and
//     never block on an event source. `unbounded_speed()` separates the
//     two fixed-step uses: a bounded run (max_steps != 0) runs flat out
//     because a capture wants the frames, not a schedule; an unbounded
//     one paces itself at step_micros because it is a game loop.
#ifndef IM_SHELL_FRAME_PACER_HPP
#define IM_SHELL_FRAME_PACER_HPP

#include <cstdint>

#include "core/error.hpp"
#include "iapp.hpp"
#include "shell/run.hpp"

namespace zb::shell
{
    class frame_pacer
    {
    public:
        // Init-path validation (code-contract §1): a frame bound with no
        // step to advance it would be a silent no-op -- run() would look
        // like it ran four frames and run until the app closed instead.
        // Reject it at the entry, where the caller can see the message.
        explicit frame_pacer(const run_options &options)
            : step_micros_(options.step_micros), max_steps_(options.max_steps)
        {
            if (max_steps_ != 0 && step_micros_ == 0)
            {
                throw zb::ui::error(
                    "run_options: max_steps needs step_micros (a frame bound with no "
                    "fixed step would never advance)");
            }
        }

        // fixed-step mode: one step per frame, no wall clock in the loop
        [[nodiscard]] bool fixed() const noexcept { return step_micros_ != 0; }

        // a bounded run has delivered its last step
        [[nodiscard]] bool done() const noexcept
        {
            return max_steps_ != 0 && index_ >= max_steps_;
        }

        // a bounded fixed-step run does not pace itself: the capture wants
        // the frames, not a real-time schedule
        [[nodiscard]] bool unbounded_speed() const noexcept
        {
            return fixed() && max_steps_ != 0;
        }

        // deliver one fixed step to the app and advance the frame index
        void deliver(zb::app::IApp &app) noexcept
        {
            app.step(step_micros_, index_);
            ++index_;
        }

        [[nodiscard]] std::uint32_t step_micros() const noexcept { return step_micros_; }
        [[nodiscard]] std::uint64_t index() const noexcept { return index_; }

    private:
        std::uint32_t step_micros_ = 0;
        std::uint64_t max_steps_ = 0;
        std::uint64_t index_ = 0;
    };
}  // namespace zb::shell

#endif  // IM_SHELL_FRAME_PACER_HPP
