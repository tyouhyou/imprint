#pragma once

namespace zb::app::showcase
{
    /*
     * F-2 preview glue: the smallest frame-automation helper the hero
     * chart needs. Frame-index deterministic by contract -- one step()
     * per paint() request, no timers, no threads, no core state (the
     * refined ruling: app-side frame automation is allowed; a framework
     * animation scheduler is not). Easing and host pacing land with the
     * real F-2 design; this only proves the shape.
     */
    class Tween
    {
    public:
        void start(const int from, const int to, const int step_per_frame)
        {
            from_ = from;
            to_ = to;
            step_ = step_per_frame > 0 ? step_per_frame : 1;
            value_ = from;
            active_ = from != to;
        }

        // advances one frame; returns true while another frame is owed
        bool step()
        {
            if (!active_)
            {
                return false;
            }
            if (value_ < to_)
            {
                value_ = to_ - value_ > step_ ? value_ + step_ : to_;
            }
            else
            {
                value_ = value_ - to_ > step_ ? value_ - step_ : to_;
            }
            active_ = value_ != to_;
            return active_;
        }

        [[nodiscard]] int value() const { return value_; }
        [[nodiscard]] bool active() const { return active_; }

    private:
        int from_ = 0;
        int to_ = 255;
        int step_ = 8;
        int value_ = 255;
        bool active_ = false;
    };
}
