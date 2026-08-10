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
};

/*
 * C-ABI boundary: no exception may cross into the host. The C++ side
 * throws on the init path (bad_alloc, Font::error, ...) and must not
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
    try
    {
        auto *self = new zb_app();
        self->app = zb::app::make_app();
        self->app->create_window(width, height);
        return self;
    }
    catch (...)
    {
        LE << "zb_app_create failed.";
        return nullptr;
    }
}

extern "C" void zb_app_destroy(zb_app_t *self)
{
    delete self;
}

extern "C" void zb_input(zb_app_t *self, int type, int x, int y, int key, int touch_id)
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
        ev.touch_id = touch_id;
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
        self->app->on_painted(
            [self](const void *)
            {
                if (self->painted_fn != nullptr)
                {
                    self->painted_fn(self->painted_userdata);
                }
            });
    }
    catch (...)
    {
        LE << "zb_set_painted_callback failed.";
    }
}
