#include "test.hpp"

#include "canvas_window.hpp"
#include "imui.hpp"

/*
 * End-to-end UI automation: a script stands in for the human host.
 *
 * The driver below touches nothing but the public surface a real
 * automation script has: it drives input through CanvasWindow::input,
 * locates widgets by id, computes their rectangle in the input
 * coordinate system, and reads focus/text state back (see
 * docs/ARCHITECTURE.md 4.11). Because the app is deterministic
 * (single-threaded, no timers), the script drives frames explicitly
 * and never sleeps.
 */

using namespace zb::ui;

namespace
{
    struct Driver
    {
        zb::app::CanvasWindow win;
        int frames = 0;

        Driver(const int w, const int h)
        {
            win.create(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            win.painted += [this](const void *) { ++frames; };
        }

        Widget *by_id(const char *id) { return win.root().find_by_id(id); }

        void feed(const zb::input::input_event &ev) { win.input(ev); }

        void press(const int x, const int y)
        {
            zb::input::input_event ev;
            ev.type = zb::input::input_type::mouse_left_down;
            ev.x = x;
            ev.y = y;
            feed(ev);
        }

        void release(const int x, const int y)
        {
            zb::input::input_event ev;
            ev.type = zb::input::input_type::mouse_left_up;
            ev.x = x;
            ev.y = y;
            feed(ev);
        }

        // clicks the center of a widget's rectangle in input coordinates
        void click(Widget &w)
        {
            const auto p = w.get_absolute_position();
            const auto s = w.get_size();
            const int x = p.x + s.width / 2;
            const int y = p.y + s.height / 2;
            press(x, y);
            release(x, y);
        }

        void key(const zb::input::key_code k)
        {
            zb::input::input_event ev;
            ev.type = zb::input::input_type::key_down;
            ev.key = static_cast<int>(k);
            feed(ev);
        }

        // types ASCII text into the focused widget (the ch channel)
        void type(const char *ascii)
        {
            for (const char *c = ascii; *c != '\0'; ++c)
            {
                zb::input::input_event ev;
                ev.type = zb::input::input_type::key_down;
                ev.ch = static_cast<unsigned char>(*c);
                feed(ev);
            }
        }
    };
}

int test_automation()
{
    // geometry discovery: id -> absolute rect -> click -> focus + click
    {
        Driver d(200, 100);
        auto ok = std::make_unique<Button>();
        ok->set_id("ok");
        ok->set_size(60, 20);
        ok->set_position(10, 10);
        auto *ok_ptr = ok.get();
        int ok_clicks = 0;
        ok->clicked += [&ok_clicks]() { ++ok_clicks; };
        d.win.root().add_child(std::move(ok));

        auto name = std::make_unique<TextInput>();
        name->set_id("name");
        name->set_size(120, 20);
        name->set_position(10, 40);
        d.win.root().add_child(std::move(name));

        d.win.paint();
        EXPECT(d.frames == 1);

        EXPECT(d.by_id("ok") == ok_ptr);
        EXPECT(d.by_id("missing") == nullptr);

        // the discovered rectangle lives in the input coordinate system
        const auto p = ok_ptr->get_absolute_position();
        const auto s = ok_ptr->get_size();
        EXPECT(p.x == 10 && p.y == 10);
        EXPECT(s.width == 60 && s.height == 20);

        const int before = d.frames;
        d.click(*ok_ptr);
        EXPECT(ok_ptr->is_focused());
        EXPECT(ok_clicks == 1);
        EXPECT(d.frames > before);  // the click owed frames; no wait needed
    }

    // determinism: input that changed nothing produces no frame
    {
        Driver d(100, 50);
        d.win.paint();
        const int before = d.frames;

        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_move;
        ev.x = 5;
        ev.y = 5;
        d.feed(ev);  // a bare move with no press changes nothing
        EXPECT(d.frames == before);
        EXPECT(!d.win.is_dirty());
    }

    // typing: a click focuses the editor, characters arrive via ch
    {
        Driver d(200, 100);
        auto in = std::make_unique<TextInput>();
        in->set_id("name");
        in->set_size(120, 20);
        in->set_position(10, 10);
        auto *tp = in.get();
        d.win.root().add_child(std::move(in));
        d.win.paint();

        d.click(*tp);
        EXPECT(tp->is_focused());
        d.type("hi");
        EXPECT(tp->get_text() == u"hi");
    }

    // keyboard navigation and activation (no coordinates at all)
    {
        Driver d(200, 100);
        auto a = std::make_unique<Button>();
        a->set_id("a");
        a->set_size(20, 20);
        auto *ap = a.get();
        auto b = std::make_unique<Button>();
        b->set_id("b");
        b->set_size(20, 20);
        b->set_position(30, 0);
        auto *bp = b.get();
        int b_clicks = 0;
        b->clicked += [&b_clicks]() { ++b_clicks; };
        d.win.root().add_child(std::move(a));
        d.win.root().add_child(std::move(b));
        d.win.paint();

        d.key(zb::input::key_code::tab);
        EXPECT(ap->is_focused());
        d.key(zb::input::key_code::tab);
        EXPECT(bp->is_focused());
        d.key(zb::input::key_code::enter);
        EXPECT(b_clicks == 1);
    }

    // modal scope: events stay inside the dialog, Tab cannot walk out
    {
        Driver d(200, 100);
        auto under = std::make_unique<Button>();
        under->set_id("under");
        under->set_size(40, 20);
        under->set_position(10, 10);
        auto *up = under.get();
        int under_clicks = 0;
        under->clicked += [&under_clicks]() { ++under_clicks; };
        d.win.root().add_child(std::move(under));

        auto dlg = std::make_unique<Dialog>();
        dlg->set_id("dlg");
        dlg->set_frame_size(80, 60);
        Button &dbtn = dlg->add_button("OK");
        int dlg_clicks = 0;
        dbtn.clicked += [&dlg_clicks]() { ++dlg_clicks; };
        auto *dp = dlg.get();
        d.win.root().add_child(std::move(dlg));
        d.win.paint();

        d.click(*up);  // reachable while no modal is set
        EXPECT(under_clicks == 1);
        EXPECT(up->is_focused());

        dp->open();
        d.win.set_modal(dp);

        d.click(*up);  // blocked by the modal
        EXPECT(under_clicks == 1);
        EXPECT(up->is_focused());  // the blocked click changed nothing

        d.key(zb::input::key_code::tab);  // focus moves into the dialog
        EXPECT(!up->is_focused());
        EXPECT(dbtn.is_focused());
        d.key(zb::input::key_code::enter);
        EXPECT(dlg_clicks == 1);

        dp->close();
        d.win.set_modal(nullptr);
        d.click(*up);  // reachable again
        EXPECT(under_clicks == 2);
    }

    return test::report("automation");
}
