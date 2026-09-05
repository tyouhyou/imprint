#include "test.hpp"

#include "imapp.hpp"
#include "imui.hpp"
#include "showcase.hpp"

using namespace zb::app;
using namespace zb::ui;

namespace
{
    // heap-built: wire_controls subscribes [this]-capturing lambdas, so
    // the object's address must be final before create_window runs (the
    // same guarantee zb::make_shared gives the real app entry)
    zb::SharedPtr<zb::app::showcase::Showcase> make_app(const int w, const int h)
    {
        auto app = zb::make_shared<zb::app::showcase::Showcase>();
        app->create_window(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        app->paint();
        return app;
    }

    // only the mounted page is findable under the root; the parked page
    // is not in the tree by design (Panel stacks children unconditionally)
    Panel &root(zb::app::showcase::Showcase &app)
    {
        return static_cast<CanvasWindow *>(app.window().get())->root();
    }

    void click(zb::app::showcase::Showcase &app, const int x, const int y)
    {
        zb::input::input_event ev = {};
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = x;
        ev.y = y;
        app.input(ev);
        ev.type = zb::input::input_type::mouse_left_up;
        app.input(ev);
    }

    void move(zb::app::showcase::Showcase &app, const int x, const int y)
    {
        zb::input::input_event ev = {};
        ev.type = zb::input::input_type::mouse_move;
        ev.x = x;
        ev.y = y;
        app.input(ev);
    }

    void click_center(zb::app::showcase::Showcase &app, Widget &w)
    {
        const auto pos = w.get_absolute_position();
        const auto s = w.get_size();
        click(app, pos.x + s.width / 2, pos.y + s.height / 2);
    }

    uint32_t px(zb::app::showcase::Showcase &app, const int x, const int y)
    {
        const auto w = app.window();
        core::Graphics view(static_cast<uint32_t>(w->width()),
                            static_cast<uint32_t>(w->height()), w->data());
        return test::pixel_at(view, x, y);
    }

    // every listed widget exists and fits inside the frame
    void expect_fit(zb::app::showcase::Showcase &app, const int w, const int h,
                    std::initializer_list<const char *> ids)
    {
        for (const char *id : ids)
        {
            Widget *wid = root(app).find_by_id(id);
            EXPECT(wid != nullptr);
            if (wid == nullptr)
            {
                continue;
            }
            const auto pos = wid->get_absolute_position();
            const auto s = wid->get_size();
            EXPECT(pos.x >= 0 && pos.y >= 0);
            EXPECT(pos.x + s.width <= w && pos.y + s.height <= h);
        }
    }
}

// the S3 showcase driven headlessly: page switch, event-driven advance,
// theme toggle, and the 256x192 embedded fit (the montage targets)
int test_showcase()
{
    // hero page at desktop size: documents materialized, bar framed
    {
        const auto holder = make_app(640, 480);
        auto &app = *holder;
        auto *cpu = static_cast<ProgressBar *>(root(app).find_by_id("cpu_bar"));
        auto *state = static_cast<Label *>(root(app).find_by_id("state_value"));
        auto *gallery_btn = static_cast<Button *>(root(app).find_by_id("gallery_btn"));
        EXPECT(cpu != nullptr && state != nullptr && gallery_btn != nullptr);
        EXPECT(state->get_text() == u"IDLE");

        // padding corner shows the window background; the bar outline is
        // drawn in the border token (probes vs theme, depth-independent)
        EXPECT(px(app, 3, 3) == theme().background.pixel);
        const auto pos = cpu->get_absolute_position();
        const auto s = cpu->get_size();
        EXPECT(s.width == 100 && s.height == 12);
        EXPECT(px(app, pos.x, pos.y) == theme().border.pixel);
        // value 0: the interior is track
        EXPECT(px(app, pos.x + 1, pos.y + s.height / 2) == theme().field_bg.pixel);
    }

    // page switch: gallery mounts, hero parks, back returns
    {
        const auto holder = make_app(640, 480);
        auto &app = *holder;
        auto *gallery_btn = static_cast<Button *>(root(app).find_by_id("gallery_btn"));
        auto *cpu = static_cast<ProgressBar *>(root(app).find_by_id("cpu_bar"));
        // the chart slot spans nearly the full frame width and only the
        // hero page draws it, so its far end is the repaint-away probe
        // (layout-derived: the old fixed point now lands on gallery
        // content after the V-2 restructure)
        auto *slot = root(app).find_by_id("chart_slot");
        EXPECT(gallery_btn != nullptr && cpu != nullptr && slot != nullptr);
        const auto cp = cpu->get_absolute_position();
        const auto cs = cpu->get_size();
        const auto sp = slot->get_absolute_position();
        const auto ss = slot->get_size();

        click_center(app, *gallery_btn);
        app.paint();
        // the parked hero page is gone from the tree
        EXPECT(root(app).find_by_id("cpu_bar") == nullptr);
        // the gallery widgets resolve now, and the demo bar carries
        // value=60 from the design file
        auto *back_btn = static_cast<Button *>(root(app).find_by_id("back_btn"));
        auto *demo_bar = static_cast<ProgressBar *>(root(app).find_by_id("demo_bar"));
        EXPECT(back_btn != nullptr && demo_bar != nullptr);
        EXPECT(demo_bar->get_value() == 60);
        const auto bp = demo_bar->get_absolute_position();
        EXPECT(px(app, bp.x + 1, bp.y + demo_bar->get_size().height / 2) ==
               theme().accent.pixel);
        // the hero region was repainted away: plain background
        EXPECT(px(app, sp.x + ss.width - 8, sp.y + ss.height / 2) ==
               theme().background.pixel);

        click_center(app, *back_btn);
        app.paint();
        EXPECT(root(app).find_by_id("cpu_bar") == cpu);  // same widget, remounted
        EXPECT(px(app, cp.x, cp.y) == theme().border.pixel);
    }

    // event-driven advance: one deterministic step per input event while
    // running; STOP costs exactly the press+release pair, then freezes
    {
        const auto holder = make_app(640, 480);
        auto &app = *holder;
        auto *start_btn = static_cast<Button *>(root(app).find_by_id("start_btn"));
        auto *stop_btn = static_cast<Button *>(root(app).find_by_id("stop_btn"));
        auto *cpu = static_cast<ProgressBar *>(root(app).find_by_id("cpu_bar"));
        auto *mem = static_cast<ProgressBar *>(root(app).find_by_id("mem_bar"));
        auto *temp = static_cast<ProgressBar *>(root(app).find_by_id("temp_bar"));

        click_center(app, *start_btn);  // the click itself advances nothing
        EXPECT(cpu->get_value() == 0);
        for (int i = 0; i < 5; ++i)
        {
            move(app, 1, 1);
        }
        EXPECT(cpu->get_value() == 5 && mem->get_value() == 10 &&
               temp->get_value() == 15);

        const int before = cpu->get_value();
        click_center(app, *stop_btn);  // down + up each advance once
        EXPECT(cpu->get_value() == before + 2);
        for (int i = 0; i < 3; ++i)
        {
            move(app, 1, 1);
        }
        EXPECT(cpu->get_value() == before + 2);  // frozen
    }

    // DONE transition: all bars saturate, running stops, label updates
    {
        const auto holder = make_app(640, 480);
        auto &app = *holder;
        auto *start_btn = static_cast<Button *>(root(app).find_by_id("start_btn"));
        auto *cpu = static_cast<ProgressBar *>(root(app).find_by_id("cpu_bar"));
        auto *state = static_cast<Label *>(root(app).find_by_id("state_value"));
        click_center(app, *start_btn);
        for (int i = 0; i < 100; ++i)
        {
            move(app, 1, 1);
        }
        EXPECT(cpu->get_value() == cpu->get_max());
        EXPECT(state->get_text() == u"DONE");
        const int v = cpu->get_value();
        move(app, 1, 1);  // stopped: no further advance
        EXPECT(cpu->get_value() == v);
    }

    // 256x192 fit (NDS/WASM): every widget of both pages stays inside
    // the frame — the layout risk the montage depends on
    {
        const auto holder = make_app(256, 192);
        auto &app = *holder;
        expect_fit(app, 256, 192,
                   {"cpu_bar", "mem_bar", "temp_bar", "state_value", "start_btn",
                    "stop_btn", "gallery_btn", "theme_btn"});
        auto *gallery_btn = static_cast<Button *>(root(app).find_by_id("gallery_btn"));
        click_center(app, *gallery_btn);
        app.paint();
        expect_fit(app, 256, 192,
                   {"back_btn", "demo_slider", "demo_bar", "demo_list",
                    "logo_slot", "shadow_slot"});
    }

    // V-2 assets: the procedural ball composites with alpha and the
    // 9-slice shadow card draws behind its card (probes structural,
    // depth-independent facts)
    {
        const auto holder = make_app(640, 480);
        auto &app = *holder;
        auto *gallery_btn = static_cast<Button *>(root(app).find_by_id("gallery_btn"));
        click_center(app, *gallery_btn);
        app.paint();

        // ball: opaque body in the middle, page background outside the
        // soft edge (the slot is exactly the ball size)
        auto *logo = root(app).find_by_id("logo_slot");
        EXPECT(logo != nullptr);
        const auto lp = logo->get_absolute_position();
        EXPECT(px(app, lp.x + 16, lp.y + 16) != theme().background.pixel);
        EXPECT(px(app, lp.x + 1, lp.y + 16) == theme().background.pixel);

        // shadow card: interior is the field token (card over shadow);
        // the ball is tinted by the accent token, not the raw asset
        auto *slot = root(app).find_by_id("shadow_slot");
        EXPECT(slot != nullptr);
        const auto sp = slot->get_absolute_position();
        const auto ss = slot->get_size();
        EXPECT(px(app, sp.x + 20, sp.y + ss.height / 2) == theme().field_bg.pixel);
        EXPECT(px(app, sp.x + ss.width / 2, sp.y + ss.height / 2) !=
               theme().field_bg.pixel);
        // the corner outside card and shadow tail stays the page
        EXPECT(px(app, sp.x + 1, sp.y + 1) == theme().background.pixel);
    }

    // theme toggle repaints the frame; the showcase boots dark (V-2),
    // so the first click goes light and the second restores dark — then
    // the light theme is restored for later suites (theme is
    // process-global)
    {
        const auto holder = make_app(640, 480);
        auto &app = *holder;
        auto *theme_btn = static_cast<Button *>(root(app).find_by_id("theme_btn"));
        EXPECT(theme_btn->get_text() == u"LIGHT");
        click_center(app, *theme_btn);
        app.paint();
        EXPECT(px(app, 3, 3) == light_theme().background.pixel);
        EXPECT(theme_btn->get_text() == u"DARK");

        click_center(app, *theme_btn);
        app.paint();
        EXPECT(px(app, 3, 3) == dark_theme().background.pixel);
        EXPECT(theme_btn->get_text() == u"LIGHT");
        zb::ui::set_theme(light_theme());
    }

    return test::report("showcase");
}
