#include "shell/input_source.hpp"

#include "iapp.hpp"

namespace zb::shell
{
    void feed_input(zb::app::IApp &app, const zb::input::input_event &ev)
    {
        app.input(ev);
        if (app.is_dirty())
        {
            app.paint();
        }
    }
}
