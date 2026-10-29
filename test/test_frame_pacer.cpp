// R6 deterministic frame stepping: the shell-side pacer (code-contract
// §11.4). The pacer is deliberately pure -- no clock, no platform handle,
// no allocation -- so the contract's semantics are proven here and every
// shell keeps only its event pump and its blit (the same split as the A-2
// input translators). test_shell_loop drives the real X11 loop through the
// same object.
#include "test.hpp"

#include <cstdint>

#include "iapp.hpp"
#include "shell/frame_pacer.hpp"
#include "shell/run.hpp"

namespace
{
    // counts the steps the pacer delivers, in order
    class step_recorder : public zb::app::IApp
    {
    public:
        void create_window() override {}
        void create_window(uint32_t, uint32_t) override {}
        void create_window(uint32_t, uint32_t, void *) override {}
        zb::SharedPtr<zb::app::IWindow> window() noexcept override { return nullptr; }
        void input(const zb::input::input_event &) noexcept override {}
        void paint() noexcept override {}

        void step(std::uint32_t step_micros, std::uint64_t index) noexcept override
        {
            ++calls;
            last_step_micros = step_micros;
            last_index = index;
            if (calls == 1)
            {
                first_index = index;
            }
        }

        [[nodiscard]] bool is_dirty() const noexcept override { return false; }

        zb::event::Subscription<const void *> on_painting(
            zb::event::PAINT_EVENT::EventHandler) noexcept override
        {
            return {};
        }
        zb::event::Subscription<const void *> on_painted(
            zb::event::PAINT_EVENT::EventHandler) noexcept override
        {
            return {};
        }
        zb::event::Subscription<> on_closing(
            zb::event::CLOSE_EVENT::EventHandler) noexcept override
        {
            return {};
        }
        zb::event::Subscription<> on_closed(
            zb::event::CLOSE_EVENT::EventHandler) noexcept override
        {
            return {};
        }

        int calls = 0;
        std::uint32_t last_step_micros = 0;
        std::uint64_t last_index = 0;
        std::uint64_t first_index = 0;
    };
}  // namespace

int test_frame_pacer()
{
    // ---- the default: no step_micros keeps every shell's own scheduling
    {
        const zb::shell::run_options options{};
        const zb::shell::frame_pacer pacer(options);
        EXPECT(!pacer.fixed());
        EXPECT(!pacer.done());
        EXPECT(!pacer.unbounded_speed());
        EXPECT(pacer.index() == 0);
        EXPECT(pacer.step_micros() == 0);
    }

    // ---- a bounded fixed-step run: flat out, exactly max_steps deliveries
    {
        zb::shell::run_options options{};
        options.step_micros = 16667;  // 60 Hz
        options.max_steps = 3;
        zb::shell::frame_pacer pacer(options);

        EXPECT(pacer.fixed());
        EXPECT(pacer.unbounded_speed());
        EXPECT(!pacer.done());

        step_recorder app;
        int guard = 0;
        while (!pacer.done() && guard < 100)
        {
            pacer.deliver(app);
            ++guard;
        }

        EXPECT(guard == 3);       // the bound is the delivery count
        EXPECT(app.calls == 3);
        EXPECT(app.first_index == 0);  // frame indices count from 0
        EXPECT(app.last_index == 2);   // ...and are contiguous
        EXPECT(app.last_step_micros == 16667);
        EXPECT(pacer.index() == 3);
        EXPECT(pacer.done());
    }

    // ---- an unbounded fixed-step run: a game loop, so it paces itself and
    //      never reports done (the app closes the window instead)
    {
        zb::shell::run_options options{};
        options.step_micros = 8333;  // 120 Hz
        zb::shell::frame_pacer pacer(options);

        EXPECT(pacer.fixed());
        EXPECT(!pacer.unbounded_speed());  // max_steps == 0 -> real-time pace
        step_recorder app;
        for (int i = 0; i < 5; ++i)
        {
            EXPECT(!pacer.done());
            pacer.deliver(app);
        }
        EXPECT(app.calls == 5);
        EXPECT(app.last_index == 4);
        EXPECT(!pacer.done());
    }

    // ---- max_steps without step_micros is rejected at the entry: the app
    //      would have no step to advance with, so the bound could only be a
    //      silent no-op (code-contract §1, §11.4)
    {
        zb::shell::run_options options{};
        options.max_steps = 4;
        bool threw = false;
        try
        {
            const zb::shell::frame_pacer pacer(options);
            (void)pacer;
        }
        catch (const zb::ui::error &)
        {
            threw = true;
        }
        EXPECT(threw);
    }

    return 0;
}
