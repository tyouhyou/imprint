#include "zbapi.h"

#include <memory>

#include "app_maker.hpp"
#include "input.hpp"
#include "logging.hpp"

/* completes the opaque type declared in zbapi.h */
struct zb_app
{
    zb::SharedPtr<zb::app::IApp> app;
    zb_painted_cb painted_fn = nullptr;
    void *painted_userdata = nullptr;
    bool painted_hooked = false;
    zb_closed_cb closed_fn = nullptr;
    void *closed_userdata = nullptr;
    bool closed_hooked = false;
};

/*
 * C-ABI boundary: no exception may cross into the host. The C++ side
 * throws on the init path (bad_alloc, zb::ui::error, ...) and must not
 * propagate through the extern "C" frame -- that is UB (a trap in WASM).
 * Logging itself must not throw here, so failures are reported via LE.
 */

extern "C" void zb_set_log_callback(zb_log_cb cb)
{
    try
    {
        zb::Logging::set_log_handle(
            [cb](const zb::Logging_Level &level, const std::string &message)
            {
                if (cb != nullptr)
                {
                    cb(static_cast<int>(level), message.c_str());
                }
            });
    }
    catch (...)
    {
        LE << "zb_set_log_callback failed.";
    }
}

extern "C" zb_app *zb_app_create(uint32_t width, uint32_t height)
{
    auto *self = new zb_app();
    try
    {
        self->app = zb::app::make_app();
        self->app->create_window(width, height);
        return self;
    }
    catch (...)
    {
        // the node (and whatever the app constructed before throwing)
        // must not leak across the failed create
        delete self;
        LE << "zb_app_create failed.";
        return nullptr;
    }
}

extern "C" void zb_app_destroy(zb_app_t *self)
{
    // the only export without a body that can throw: wrapped for
    // consistency with the file-wide no-exception-crossing rule
    try
    {
        delete self;
    }
    catch (...)
    {
        LE << "zb_app_destroy failed.";
    }
}

extern "C" void zb_input(zb_app_t *self, int type, int x, int y, int key, int ch, int touch_id)
{
    if (self == nullptr || self->app == nullptr)
    {
        return;
    }
    // the type comes from an untrusted host: reject values outside the
    // enum range so a future dispatch that indexes by type cannot go OOB
    const int first = static_cast<int>(zb::input::input_type::none);
    const int last = static_cast<int>(zb::input::input_type::key_up);
    if (type < first || type > last)
    {
        return;
    }
    try
    {
        zb::input::input_event ev;
        ev.type = static_cast<zb::input::input_type>(type);
        ev.x = x;
        ev.y = y;
        ev.key = key;
        ev.ch = ch;
        ev.touch_id = touch_id;
        // the type encodes the button (the mouse_* family is left/right
        // by name); without this the C-ABI clicks always reported
        // button == none. mouse_move carries no button, exactly like
        // the native shells (win/x11).
        switch (ev.type)
        {
            case zb::input::input_type::mouse_left_down:
            case zb::input::input_type::mouse_left_up:
            case zb::input::input_type::mouse_left_click:
                ev.button = zb::input::mouse_button_t::left;
                break;
            case zb::input::input_type::mouse_right_down:
            case zb::input::input_type::mouse_right_up:
            case zb::input::input_type::mouse_right_click:
                ev.button = zb::input::mouse_button_t::right;
                break;
            default:
                break;
        }
        // the documented C-ABI contract carries the wheel delta in `key`
        // (zbapi.h); the widgets read ev.delta -- without this mapping
        // every wheel tick scrolled the same direction
        if (ev.type == zb::input::input_type::mouse_wheel)
        {
            ev.delta = key;
        }
        self->app->input(ev);
    }
    catch (...)
    {
        LE << "zb_input failed.";
    }
}

extern "C" void zb_paint(zb_app_t *self)
{
    if (self == nullptr || self->app == nullptr)
    {
        return;
    }
    try
    {
        self->app->paint();
    }
    catch (...)
    {
        LE << "zb_paint failed.";
    }
}

extern "C" const uint8_t *zb_buffer(zb_app_t *self, uint32_t *out_width, uint32_t *out_height)
{
    if (self == nullptr || self->app == nullptr || self->app->window() == nullptr)
    {
        return nullptr;
    }
    try
    {
        const auto win = self->app->window();
        if (out_width != nullptr)
        {
            *out_width = static_cast<uint32_t>(win->width());
        }
        if (out_height != nullptr)
        {
            *out_height = static_cast<uint32_t>(win->height());
        }
        return static_cast<const uint8_t *>(win->data());
    }
    catch (...)
    {
        LE << "zb_buffer failed.";
        return nullptr;
    }
}

extern "C" int zb_buffer_bpp()
{
    // build-time constant: hosts size their pixel loops with it instead
    // of assuming 4 bytes (a 16bpp build over-read by 2x before)
    return static_cast<int>(sizeof(zb::ui::core::Color));
}

extern "C" int zb_buffer_format()
{
#if COLOR_DEPTH == 16
    return ZB_FORMAT_ABGR1555;
#else
    return ZB_FORMAT_BGRA8;
#endif
}

extern "C" int zb_version()
{
    return ZB_API_VERSION;
}

extern "C" void zb_set_painted_callback(zb_app_t *self, zb_painted_cb cb, void *userdata)
{
    if (self == nullptr || self->app == nullptr)
    {
        return;
    }
    try
    {
        self->painted_fn = cb;
        self->painted_userdata = userdata;
        if (!self->painted_hooked)
        {
            // on_painted appends a handler; the closure must be
            // registered once for the app's lifetime, otherwise a host
            // calling zb_set_painted_callback twice fires the callback
            // twice per frame (one closure per call)
            self->painted_hooked = true;
            self->app->on_painted(
                [self](const void *)
                {
                    if (self->painted_fn != nullptr)
                    {
                        self->painted_fn(self->painted_userdata);
                    }
                });
        }
    }
    catch (...)
    {
        LE << "zb_set_painted_callback failed.";
    }
}

extern "C" void zb_set_closed_callback(zb_app_t *self, zb_closed_cb cb, void *userdata)
{
    if (self == nullptr || self->app == nullptr)
    {
        return;
    }
    try
    {
        self->closed_fn = cb;
        self->closed_userdata = userdata;
        if (!self->closed_hooked)
        {
            self->closed_hooked = true;
            self->app->on_closed(
                [self]()
                {
                    if (self->closed_fn != nullptr)
                    {
                        self->closed_fn(self->closed_userdata);
                    }
                });
        }
    }
    catch (...)
    {
        LE << "zb_set_closed_callback failed.";
    }
}
