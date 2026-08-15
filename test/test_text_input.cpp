#include "test.hpp"

#include "imui.hpp"

using namespace zb::ui;

namespace
{
    zb::input::input_event char_ev(const int ch, const int key = 0)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::key_down;
        ev.ch = ch;
        ev.key = key;
        return ev;
    }

    zb::input::input_event key_ev(const int key)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::key_down;
        ev.key = key;
        return ev;
    }

    zb::input::input_event press_at(const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = x;
        ev.y = y;
        return ev;
    }
}  // namespace

int test_text_input()
{
    // typing inserts characters and fires changed with UTF-8
    {
        Panel root;
        root.set_size(200, 100);
        auto in = std::make_unique<TextInput>();
        in->set_size(120, 20);
        in->set_position(10, 10);
        auto *input = in.get();
        root.add_child(std::move(in));

        InputDispatcher d;
        int changes = 0;
        std::string last;
        input->changed += [&](const std::string &t) { ++changes; last = t; };

        d.dispatch(root, press_at(15, 20));  // focus + place caret
        d.dispatch(root, char_ev('a'));
        d.dispatch(root, char_ev('b'));
        EXPECT(input->get_text() == u"ab");
        EXPECT(changes == 2);
        EXPECT(last == "ab");
    }

    // space via the key field (shells map it that way)
    {
        Panel root;
        root.set_size(200, 100);
        auto in = std::make_unique<TextInput>();
        in->set_size(120, 20);
        in->set_position(10, 10);
        auto *input = in.get();
        root.add_child(std::move(in));

        InputDispatcher d;
        d.dispatch(root, press_at(15, 20));
        d.dispatch(root, key_ev(static_cast<int>(zb::input::key_code::space)));
        d.dispatch(root, char_ev('x'));
        EXPECT(input->get_text() == u" x");
    }

    // backspace erases before the caret, del after it
    {
        Panel root;
        root.set_size(200, 100);
        auto in = std::make_unique<TextInput>();
        in->set_size(120, 20);
        in->set_position(10, 10);
        auto *input = in.get();
        root.add_child(std::move(in));

        InputDispatcher d;
        d.dispatch(root, press_at(15, 20));
        d.dispatch(root, char_ev('a'));
        d.dispatch(root, char_ev('b'));
        // move the caret between a and b
        d.dispatch(root, key_ev(static_cast<int>(zb::input::key_code::left)));
        // del erases b (at the caret)
        d.dispatch(root, key_ev(static_cast<int>(zb::input::key_code::del)));
        EXPECT(input->get_text() == u"a");
        // backspace erases a (before the caret, now at the end)
        d.dispatch(root, key_ev(static_cast<int>(zb::input::key_code::backspace)));
        EXPECT(input->get_text().empty());
    }

    // programmatic set_text is silent and placed at the start; the caret
    // math uses the text slots of the base widget
    {
        Panel root;
        root.set_size(200, 100);
        auto in = std::make_unique<TextInput>();
        in->set_size(120, 20);
        in->set_position(10, 10);
        auto *input = in.get();
        root.add_child(std::move(in));

        int changes = 0;
        input->changed += [&](const std::string &) { ++changes; };
        input->set_text("hi");
        EXPECT(changes == 0);
        EXPECT(input->get_text() == u"hi");
    }

    // enter submits the whole text and is consumed by the focused input
    {
        Panel root;
        root.set_size(200, 100);
        auto in = std::make_unique<TextInput>();
        in->set_size(120, 20);
        in->set_position(10, 10);
        auto *input = in.get();
        root.add_child(std::move(in));

        InputDispatcher d;
        std::string submitted;
        input->submitted += [&](const std::string &t) { submitted = t; };

        d.dispatch(root, press_at(15, 20));
        d.dispatch(root, char_ev('o'));
        const bool consumed = d.dispatch(root, key_ev(static_cast<int>(zb::input::key_code::enter)));
        EXPECT(consumed);
        EXPECT(submitted == "o");
    }

    return test::report("text_input");
}