#include "test.hpp"

#include "imapp.hpp"
#include "imui.hpp"

using namespace zb::ui;

int test_dialog()
{
    // initial state
    {
        Dialog d;
        d.set_size(100, 100);
        d.set_frame_size(60, 80);
        EXPECT(!d.is_open());
        d.open();
        EXPECT(d.is_open());
        d.close();
        EXPECT(!d.is_open());
    }

    // layout: frame centered, title top, buttons bottom, body in the middle
    {
        Dialog d;
        d.set_size(100, 100);
        d.set_frame_size(60, 80);
        d.open();

        d.add_button("OK");  // default button size 48x18
        d.layout();

        // frame centered: (100-60)/2, (100-80)/2 -> the body sits inside it
        const int pad = 8;
        const auto body_pos = d.get_body().get_position();
        const auto body_sz = d.get_body().get_size();
        EXPECT(body_pos.x == pad);
        EXPECT(body_pos.y == pad + 16 + 4);              // title + spacing
        EXPECT(body_sz.width == 60 - 2 * pad);           // 44
        EXPECT(body_sz.height == (80 - pad - 18) - 4 - body_pos.y);  // 22
    }

    // add_button: returned reference is usable
    {
        Dialog d;
        auto &b = d.add_button("Restart");
        b.set_size(60, 20);
        int clicks = 0;
        b.clicked += [&clicks]() { ++clicks; };
        b.press();
        b.release();
        EXPECT(clicks == 1);
    }

    // drawing: closed dialog draws nothing
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::White);
        Dialog d;
        d.set_size(10, 10);
        d.set_frame_size(4, 4);
        d.set_frame_background_color(core::colors::Red);
        d.draw(*g);
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 5, 5) == core::colors::White.pixel);
    }

    // drawing: open dialog draws the mask (alpha-blended) and the frame
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::White);

        Dialog d;
        d.set_size(10, 10);
        d.set_frame_size(4, 4);
        d.set_frame_background_color(core::colors::Red);
        d.open();
        d.layout();
        d.draw(*g);

        // mask area: White blended with Black at alpha 128 -> (127,127,127)
        // (only meaningful at 32bpp; at 16bpp the mask alpha has 1 bit)
        if (core::ImColor_Depth == 32)
        {
            EXPECT(test::pixel_at(*g, 1, 1) == core::Color::from(127, 127, 127).pixel);
            EXPECT(test::pixel_at(*g, 9, 9) == core::Color::from(127, 127, 127).pixel);
        }
        else
        {
            EXPECT(test::pixel_at(*g, 1, 1) != core::colors::White.pixel);
            EXPECT(test::pixel_at(*g, 9, 9) != core::colors::White.pixel);
        }
        // frame area: opaque Red over the mask
        EXPECT(test::pixel_at(*g, 5, 5) == core::colors::Red.pixel);
    }

    // a closed dialog must not block widgets underneath it
    {
        Panel root;
        root.set_size(100, 100);

        auto btn = std::make_unique<Button>();
        btn->set_size(20, 20);
        btn->set_position(35, 40);  // under the dialog area
        auto *pbtn = btn.get();
        int clicks = 0;
        pbtn->clicked += [&clicks]() { ++clicks; };

        auto dlg = std::make_unique<Dialog>();
        dlg->set_size(40, 40);
        dlg->set_position(30, 30);
        dlg->set_frame_size(40, 40);
        dlg->close();

        root.add_child(std::move(btn));
        root.add_child(std::move(dlg));

        InputDispatcher d;
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = 45;
        ev.y = 50;
        d.dispatch(root, ev);
        ev.type = zb::input::input_type::mouse_left_up;
        d.dispatch(root, ev);
        EXPECT(clicks == 1);  // the button below the closed dialog is clickable
    }

    // layout protocol: a Dialog restores its layout flag after a pass;
    // without that, a root dialog is re-laid out on every paint under
    // auto layout (first paint lays out -> flag stays true -> repeat)
    {
        zb::app::CanvasWindow w;
        w.create(200, 150);
        w.set_auto_layout(true);
        auto &root = w.root();
        auto dlg = std::make_unique<Dialog>();
        dlg->set_size(120, 90);
        dlg->set_frame_size(60, 80);
        auto *pd = dlg.get();
        root.add_child(std::move(dlg));
        w.paint();
        EXPECT(!pd->is_layout_dirty());
        w.paint();  // a settled tree stays settled
        EXPECT(!pd->is_layout_dirty());

        // the frame-padding setter invalidates the layout; the next
        // paint performs and clears it
        pd->set_frame_padding(2);
        EXPECT(pd->is_layout_dirty());
        w.paint();
        EXPECT(!pd->is_layout_dirty());
    }

    // runtime mutation: frame padding and button size changed after the
    // first paint are picked up by the next paint
    {
        zb::app::CanvasWindow w;
        w.create(200, 150);
        w.set_auto_layout(true);
        auto &root = w.root();
        auto dlg = std::make_unique<Dialog>();
        dlg->set_size(120, 90);
        dlg->set_frame_size(60, 80);
        dlg->add_button("OK");  // default 48x18
        auto *pd = dlg.get();
        root.add_child(std::move(dlg));
        w.paint();

        // default padding 8: body starts under the 16px title + 4 spacing
        const auto &body = pd->get_body();
        EXPECT(body.get_position().y == 8 + 16 + 4);
        EXPECT(body.get_size().height == (80 - 8 - 18) - 4 - body.get_position().y);

        pd->set_frame_padding(2);
        pd->set_button_size(50, 22);  // applies to later add_button calls
        pd->add_button("NO");         // 50x22: the buttons row grows to 22
        w.paint();

        // the new 2px padding moves the body up; the 22px buttons row
        // shrinks the body bottom: (frame 80) - 2 - 22 - 4 - body_top
        EXPECT(body.get_position().y == 2 + 16 + 4);
        EXPECT(body.get_size().height == (80 - 2 - 22) - 4 - body.get_position().y);
    }

    return test::report("dialog");
}
