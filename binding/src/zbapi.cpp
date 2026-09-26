#include "zbapi.h"

#include <memory>
#include <cstring>
#include <map>
#include <utility>

#include "app_maker.hpp"
#include "canvas_window.hpp"
#include "flex_panel.hpp"
#include "html.hpp"
#include "input.hpp"
#include "logging.hpp"
#include "text/utf8.hpp"
#include "ui_builder.hpp"
#include "ui_file.hpp"

namespace
{
    /*
     * IApp adapter over a CanvasWindow for declarative apps (P3): the
     * story apps are IApp classes delegating to a CanvasWindow member
     * (the hello shape); the binding layer needs that shape without a
     * story. The driving protocol (input/paint/window) is unchanged.
     */
    class ui_host_app final : public zb::app::IApp
    {
    public:
        ui_host_app()
            : window_(zb::make_shared<zb::app::CanvasWindow>()) {}

        void create_window() override { create_window(320, 240); }
        void create_window(uint32_t w, uint32_t h) override
        {
            create_window(w, h, nullptr);
        }
        void create_window(uint32_t w, uint32_t h, void *buffer) override
        {
            window_->create(w, h, buffer);
        }

        zb::SharedPtr<zb::app::IWindow> window() noexcept override
        {
            return window_;
        }
        void input(const zb::input::input_event &ev) noexcept override
        {
            window_->input(ev);
        }
        void paint() noexcept override { window_->paint(); }
        bool is_dirty() const noexcept override { return window_->is_dirty(); }
        bool dirty_region(int &x, int &y, int &w, int &h) const noexcept override
        {
            return window_->dirty_region(x, y, w, h);
        }

        void on_painting(zb::event::PAINT_EVENT::EventHandler h) noexcept override
        {
            window_->painting += h;
        }
        void on_painted(zb::event::PAINT_EVENT::EventHandler h) noexcept override
        {
            window_->painted += h;
        }
        void on_closing(zb::event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            window_->closing += h;
        }
        void on_closed(zb::event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            window_->closed += h;
        }

        // the declarative surface: root/layout/widgets
        zb::app::CanvasWindow *canvas() noexcept { return window_.get(); }

    private:
        zb::SharedPtr<zb::app::CanvasWindow> window_;
    };

    // a screen-size chain with the host as the shell (B2): nonzero host
    // dims, else the HTML page box, else the app default
    void resolve_ui_size(const zb::ui::html_page &page,
                         uint32_t &w, uint32_t &h)
    {
        if (w == 0)
        {
            w = (page.has_width && page.width > 0)
                    ? static_cast<uint32_t>(page.width) : 800;
        }
        if (h == 0)
        {
            h = (page.has_height && page.height > 0)
                    ? static_cast<uint32_t>(page.height) : 600;
        }
    }
}

/* completes the opaque type declared in zbapi.h */
struct zb_app
{
    zb::SharedPtr<zb::app::IApp> app;
    // non-null only for declarative apps (zb_app_create_from_ui)
    ui_host_app *ui = nullptr;
    struct zb_action
    {
        zb_action_cb fn = nullptr;
        void *userdata = nullptr;
    };
    std::map<std::string, zb_action> actions;
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
    zb_app *self = nullptr;
    try
    {
        self = new zb_app();
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

/* ---------- declarative apps (P3) ------------------------------------- */

extern "C" zb_app_t *zb_app_create_from_ui(const char *ui_text, int is_html,
                                           uint32_t width, uint32_t height)
{
    zb_app_t *self = nullptr;
    try
    {
        if (ui_text == nullptr)
        {
            LE << "zb_app_create_from_ui: null design text.";
            return nullptr;
        }
        bool ok = false;
        zb::ui::ui_node doc;
        zb::ui::html_page page;
        if (is_html != 0)
        {
            doc = zb::ui::parse_html(ui_text, &ok, &page);
        }
        else
        {
            doc = zb::ui::parse_ui_text(ui_text, &ok);
        }
        // unknown tags parse ok but materialize nothing: an app with no
        // widgets is a create failure (the ui_embed validation
        // semantics, not an empty window). Checked on the node tree
        // before anything is allocated.
        if (!ok || !zb::ui::materializes_widget(doc))
        {
            LE << "zb_app_create_from_ui: the design text yields no widget.";
            return nullptr;
        }
        uint32_t w = width;
        uint32_t h = height;
        resolve_ui_size(page, w, h);

        auto adapter = zb::make_shared<ui_host_app>();
        adapter->create_window(w, h);
        zb::app::CanvasWindow *canvas = adapter->canvas();
        canvas->set_auto_layout(true);
        auto screen = std::make_unique<zb::ui::FlexPanel>();
        screen->set_size(static_cast<int>(w), static_cast<int>(h));
        if (page.has_background)
        {
            screen->set_background_color(page.background);
        }
        // the ui_preview materialization path, single document
        zb::ui::build(*screen, doc);
        canvas->root().add_child(std::move(screen));

        self = new zb_app();

        // the binding consults the host's registrations at fire time, so
        // zb_set_event_callback order relative to this create is free
        zb_app_t *sink_app = self;
        zb::ui::bind_actions(
            canvas->root(), doc,
            [sink_app](const std::string &id)
            {
                const auto it = sink_app->actions.find(id);
                if (it != sink_app->actions.end() && it->second.fn != nullptr)
                {
                    it->second.fn(id.c_str(), it->second.userdata);
                }
            });

        self->app = adapter;
        self->ui = adapter.get();
        return self;
    }
    catch (...)
    {
        // the node (and whatever the parse constructed before throwing)
        // must not leak across the failed create
        delete self;
        LE << "zb_app_create_from_ui failed.";
        return nullptr;
    }
}

extern "C" void zb_set_event_callback(zb_app_t *self, const char *widget_id,
                                      zb_action_cb cb, void *userdata)
{
    if (self == nullptr || widget_id == nullptr)
    {
        return;
    }
    try
    {
        if (cb == nullptr)
        {
            self->actions.erase(widget_id);
        }
        else
        {
            self->actions[widget_id] = zb_app::zb_action{cb, userdata};
        }
    }
    catch (...)
    {
        LE << "zb_set_event_callback failed.";
    }
}

extern "C" int zb_widget_text(zb_app_t *self, const char *widget_id,
                              char *out, int cap)
{
    if (self == nullptr || self->ui == nullptr || widget_id == nullptr ||
        out == nullptr || cap <= 0)
    {
        return -1;
    }
    try
    {
        zb::ui::Widget *w = self->ui->canvas()->root().find_by_id(widget_id);
        if (w == nullptr)
        {
            return -1;
        }
        const std::string utf8 = zb::ui::utf16_to_utf8(w->get_text());
        const int n = static_cast<int>(utf8.size());
        const int copy = (n < cap - 1) ? n : cap - 1;
        if (copy > 0)
        {
            std::memcpy(out, utf8.data(), static_cast<std::size_t>(copy));
        }
        out[copy] = '\0';
        return copy;
    }
    catch (...)
    {
        LE << "zb_widget_text failed.";
        return -1;
    }
}

extern "C" void zb_widget_set_text(zb_app_t *self, const char *widget_id,
                                   const char *utf8_text)
{
    if (self == nullptr || self->ui == nullptr || widget_id == nullptr ||
        utf8_text == nullptr)
    {
        return;
    }
    try
    {
        zb::ui::Widget *w = self->ui->canvas()->root().find_by_id(widget_id);
        if (w != nullptr)
        {
            w->set_text(zb::ui::utf8_to_utf16(utf8_text));
        }
    }
    catch (...)
    {
        LE << "zb_widget_set_text failed.";
    }
}
