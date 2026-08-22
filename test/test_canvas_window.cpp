#include "test.hpp"

#include "imapp.hpp"
#include "imui.hpp"

using namespace zb::app;
using namespace zb::ui;

int test_canvas_window()
{
    // creation and basic accessors
    {
        CanvasWindow w;
        w.create(320, 240);
        EXPECT(w.width() == 320);
        EXPECT(w.height() == 240);
        EXPECT(w.data() != nullptr);
        EXPECT(w.title() == "myapp");
    }

    // wrapper mode uses the provided buffer directly
    {
        uint32_t buf[16 * 16] = {0};
        CanvasWindow w;
        w.create(16, 16, buf);
        EXPECT(w.data() == buf);
    }

    // widget tree: a button receives dispatched clicks
    {
        CanvasWindow w;
        w.create(100, 100);
        auto &root = w.root();

        auto b = std::make_unique<Button>();
        b->set_size(20, 20);
        auto *pbtn = b.get();
        int clicks = 0;
        b->clicked += [&clicks]() { ++clicks; };
        root.add_child(std::move(b));

        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = 5;
        ev.y = 5;
        w.input(ev);
        ev.type = zb::input::input_type::mouse_left_up;
        w.input(ev);
        EXPECT(clicks == 1);
    }

    // paint renders the tree and emits the painted signal with the framebuffer
    {
        CanvasWindow w;
        w.create(50, 30);
        int painted_calls = 0;
        const void *painted_data = nullptr;
        w.painted += [&painted_calls, &painted_data](const void *d)
        {
            ++painted_calls;
            painted_data = d;
        };

        w.paint();
        EXPECT(painted_calls == 1);
        EXPECT(painted_data == w.data());
    }

    // paint without create() does not crash
    {
        CanvasWindow w;
        w.paint();
    }

    // dirty flag: owed frames gate idle repaints
    {
        CanvasWindow w;
        w.create(100, 100);
        EXPECT(w.is_dirty());  // nothing rendered yet

        w.paint();
        EXPECT(!w.is_dirty());  // the frame was rendered

        w.paint();  // an explicit paint still renders
        EXPECT(!w.is_dirty());
    }

    // input repaints only when the event changed the tree
    {
        CanvasWindow w;
        w.create(100, 100);
        auto &root = w.root();

        auto b = std::make_unique<Button>();
        b->set_size(20, 20);
        b->set_position(10, 10);
        int clicks = 0;
        b->clicked += [&clicks]() { ++clicks; };
        root.add_child(std::move(b));

        int painted_calls = 0;
        w.painted += [&painted_calls](const void *) { ++painted_calls; };

        // moves without a press do not repaint
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_move;
        ev.x = 15;
        ev.y = 15;
        w.input(ev);
        EXPECT(painted_calls == 0);
        EXPECT(w.is_dirty());  // the initial frame is still owed

        // a claimed press + release repaint (paint clears the flag)
        ev.type = zb::input::input_type::mouse_left_down;
        w.input(ev);
        EXPECT(painted_calls == 1);
        ev.type = zb::input::input_type::mouse_left_up;
        w.input(ev);
        EXPECT(painted_calls == 2);
        EXPECT(clicks == 1);
        EXPECT(!w.is_dirty());

        // a release without a press is a no-op
        w.input(ev);
        EXPECT(painted_calls == 2);
    }

    // widget setters outside the input path owe a frame (Z8): the tree
    // propagates the damage to is_dirty(); paint() consumes it
    {
        CanvasWindow w;
        w.create(100, 100);
        auto lbl = std::make_unique<Label>();
        lbl->set_size(40, 10);
        auto *plbl = lbl.get();
        w.root().add_child(std::move(lbl));

        w.paint();
        EXPECT(!w.is_dirty());

        plbl->set_text("hello");  // no input handler involved
        EXPECT(w.is_dirty());

        w.paint();
        EXPECT(!w.is_dirty());
    }

    // a setter firing during the draw survives into the next frame
    // (walk_damage consumes at read time; the late damage stays pending)
    {
        CanvasWindow w;
        w.create(100, 100);
        auto lbl = std::make_unique<Label>();
        lbl->set_size(40, 10);
        auto *plbl = lbl.get();
        w.root().add_child(std::move(lbl));

        bool mutate = true;
        w.painted += [&](const void *)
        {
            if (mutate)
            {
                mutate = false;
                plbl->set_text("late");
            }
        };
        w.paint();
        EXPECT(w.is_dirty());  // the late change still owes a frame
        w.paint();
        EXPECT(!w.is_dirty());
    }

    // invalidate(): a full frame is owed without any widget damage
    {
        CanvasWindow w;
        w.create(100, 100);
        w.paint();
        EXPECT(!w.is_dirty());
        w.invalidate();
        EXPECT(w.is_dirty());
        w.paint();  // nothing reported: the full-frame fallback covers it
        int x = 0, y = 0, rw = 0, h = 0;
        EXPECT(w.dirty_region(x, y, rw, h));
        EXPECT(rw == 100 && h == 100);
        EXPECT(!w.is_dirty());
    }

    // modal: set_modal blocks clicks outside the dialog
    {
        CanvasWindow w;
        w.create(100, 100);
        auto &root = w.root();

        auto btn = std::make_unique<Button>();
        btn->set_size(20, 20);
        auto *pbtn = btn.get();
        int clicks = 0;
        btn->clicked += [&clicks]() { ++clicks; };

        auto dlg = std::make_unique<Dialog>();
        dlg->set_size(40, 40);
        dlg->set_position(30, 30);
        dlg->set_frame_size(40, 40);
        dlg->set_button_size(20, 18);
        dlg->open();
        auto &ok = dlg->add_button("OK");
        int ok_clicks = 0;
        ok.clicked += [&ok_clicks]() { ++ok_clicks; };
        auto *pdlg = dlg.get();

        root.add_child(std::move(btn));
        root.add_child(std::move(dlg));
        pdlg->layout();

        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;

        // without modal, the outer button works
        ev.x = 5;
        ev.y = 5;
        w.input(ev);
        ev.type = zb::input::input_type::mouse_left_up;
        w.input(ev);
        EXPECT(clicks == 1);

        // with modal, the outer button is blocked; the dialog button works
        w.set_modal(pdlg);
        ev.type = zb::input::input_type::mouse_left_down;
        w.input(ev);
        ev.type = zb::input::input_type::mouse_left_up;
        w.input(ev);
        EXPECT(clicks == 1);

        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = 45;  // dialog button
        ev.y = 50;
        w.input(ev);
        ev.type = zb::input::input_type::mouse_left_up;
        w.input(ev);
        EXPECT(ok_clicks == 1);
    }

    return test::report("canvas_window");
}
