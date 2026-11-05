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
    zb::input::input_event release_at(const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_up;
        ev.x = x;
        ev.y = y;
        return ev;
    }

    zb::input::input_event move_to(const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_move;
        ev.x = x;
        ev.y = y;
        return ev;
    }

    // a plain key with a modifier mask (FI-02 bindings ride `key` +
    // mods, ch = 0, code-contract §3.3)
    zb::input::input_event key_mod(const int key, const int mods)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::key_down;
        ev.key = key;
        ev.mods = mods;
        return ev;
    }

    // FI-02 ClipboardSink test double (see test_text_area.cpp)
    struct TestSink : ClipboardSink
    {
        std::string sent;
        std::string canned;
        void set_text(const char *utf8, const size_t n) override
        {
            sent.assign(utf8, n);
        }
        std::string get_text() const override
        {
            return canned;
        }
    };

    struct SinkGuard
    {
        explicit SinkGuard(ClipboardSink *s)
        {
            set_clipboard_sink(s);
        }
        ~SinkGuard()
        {
            set_clipboard_sink(nullptr);
        }
    };

    // a uniform 6px-advance provider: the selection click math needs
    // real advances (the 5x7 fallback's vary per glyph)
    struct UnitProvider : GlyphProvider
    {
        bool covers(const char16_t) const override { return true; }
        text_metrics measure(const char16_t *, const int len) const override
        {
            return {6 * len, 7, 7};
        }
        text_metrics line_metrics() const override { return {0, 7, 7}; }
        void write(core::Graphics &, const char16_t *, const int, const int,
                   const int, const core::Color &) const override
        {
        }
    };

    // a single-line editor with uniform advances and a changed recorder
    struct SelInput
    {
        Panel root;
        TextInput *input = nullptr;
        InputDispatcher d;
        std::string last;

        SelInput()
        {
            root.set_size(200, 100);
            auto in = std::make_unique<TextInput>();
            in->set_size(120, 20);
            in->set_position(10, 10);
            in->set_glyph_provider(zb::make_shared<UnitProvider>());
            input = in.get();
            root.add_child(std::move(in));
            input->changed += [this](const std::string &t)
            { last = t; };
        }
    };

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
        input->changed += [&](const std::string &t)
        { ++changes; last = t; };

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
        input->changed += [&](const std::string &)
        { ++changes; };
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
        input->submitted += [&](const std::string &t)
        { submitted = t; };

        d.dispatch(root, press_at(15, 20));
        d.dispatch(root, char_ev('o'));
        const bool consumed = d.dispatch(root, key_ev(static_cast<int>(zb::input::key_code::enter)));
        EXPECT(consumed);
        EXPECT(submitted == "o");
    }

    // T-2: code points >= U+00A0 insert as BMP code units (ch = code
    // point, key = 0, the terminal translator's UTF-8 assembly), C1
    // controls and non-BMP code points drop
    {
        Panel root;
        root.set_size(200, 100);
        auto in = std::make_unique<TextInput>();
        in->set_size(120, 20);
        in->set_position(10, 10);
        auto *input = in.get();
        root.add_child(std::move(in));

        InputDispatcher d;
        std::string last;
        input->changed += [&](const std::string &t)
        { last = t; };

        d.dispatch(root, press_at(15, 20));
        d.dispatch(root, char_ev(0x4F60));  // 你
        EXPECT(input->get_text() == std::u16string(u"\u4F60"));
        EXPECT(last == "\xE4\xBD\xA0");  // changed carries UTF-8 bytes
        // é (Latin-1 supplement, above the C1 block) also inserts
        d.dispatch(root, char_ev(0xE9));
        EXPECT(input->get_text() == std::u16string(u"\u4F60\xE9"));

        // backspace erases one code unit
        d.dispatch(root, key_ev(static_cast<int>(zb::input::key_code::backspace)));
        EXPECT(input->get_text() == std::u16string(u"\u4F60"));

        // DEL (0x7F), the C1 block, and a non-BMP code point drop
        d.dispatch(root, char_ev(0x7F));
        d.dispatch(root, char_ev(0x9F));
        d.dispatch(root, char_ev(0x1F600));
        EXPECT(input->get_text() == std::u16string(u"\u4F60"));
    }

    // FI-02 single-line selection (code-contract §3.8): drag selects,
    // shift+click / shift+arrow extend, a non-shift caret move collapses
    {
        SelInput t;
        t.input->set_text("abcdef");
        t.d.dispatch(t.root, press_at(18, 20));  // local (8,10): col 6 -> caret 1
        EXPECT(t.input->selected_text().empty());
        t.d.dispatch(t.root, move_to(30, 20));  // col 18 -> caret 3
        EXPECT(t.input->selected_text() == "bc");
        t.d.dispatch(t.root, release_at(30, 20));
        EXPECT(t.input->selected_text() == "bc");

        auto shift_click = press_at(36, 20);  // col 24 -> caret 4
        shift_click.mods = zb::input::mod_shift;
        t.d.dispatch(t.root, shift_click);
        EXPECT(t.input->selected_text() == "bcd");
        t.d.dispatch(t.root, release_at(36, 20));

        t.d.dispatch(t.root, key_mod(static_cast<int>(zb::input::key_code::left),
                                     zb::input::mod_shift));
        EXPECT(t.input->selected_text() == "bc");
        t.d.dispatch(t.root,
                     key_ev(static_cast<int>(zb::input::key_code::left)));
        EXPECT(t.input->selected_text().empty());  // collapsed at caret 2
    }

    // Ctrl+A selects all; typing replaces the selection
    {
        SelInput t;
        t.input->set_text("abcdef");
        t.d.dispatch(t.root, press_at(18, 20));  // focus + caret 1
        t.d.dispatch(t.root, release_at(18, 20));
        t.d.dispatch(t.root, key_mod('a', zb::input::mod_ctrl));
        EXPECT(t.input->selected_text() == "abcdef");
        t.d.dispatch(t.root, char_ev('X'));
        EXPECT(t.input->get_text() == u"X");
        EXPECT(t.input->selected_text().empty());
        EXPECT(t.last == "X");
    }

    // key round-trips through an injected sink; the paste is clipped to
    // the field's own rules (no line break, CJK kept)
    {
        TestSink sink;
        SinkGuard guard(&sink);
        SelInput t;
        t.input->set_text("abcdef");
        t.d.dispatch(t.root, press_at(18, 20));  // caret 1
        t.d.dispatch(t.root, move_to(30, 20));   // [1, 3)
        t.d.dispatch(t.root, key_mod('c', zb::input::mod_ctrl));
        EXPECT(sink.sent == "bc");

        sink.canned = "p\nq\xE4\xBD\xA0";
        t.d.dispatch(t.root, key_mod('v', zb::input::mod_ctrl));
        // the '\n' drops (single line), 你 (U+4F60) stays
        EXPECT(t.input->get_text() == std::u16string(u"apq\x4F60"
                                                     "def"));
        EXPECT(t.input->selected_text().empty());
        EXPECT(t.last == "apq\xE4\xBD\xA0"
                         "def");

        // Ctrl+Insert copies; Shift+Insert pastes replacing [0, 2)
        t.d.dispatch(t.root, press_at(18, 20));  // caret 1
        t.d.dispatch(t.root, move_to(30, 20));   // [1, 3)
        t.d.dispatch(t.root,
                     key_mod(static_cast<int>(zb::input::key_code::insert),
                             zb::input::mod_ctrl));
        EXPECT(sink.sent == "pq");  // [1, 3) = p..q
        sink.canned = "z";
        t.d.dispatch(t.root, release_at(30, 20));
        t.d.dispatch(t.root, press_at(18, 20));  // caret 1
        t.d.dispatch(t.root, move_to(30, 20));   // [1, 3)
        t.d.dispatch(t.root,
                     key_mod(static_cast<int>(zb::input::key_code::insert),
                             zb::input::mod_shift));
        EXPECT(t.input->get_text() == std::u16string(u"az\x4F60"
                                                     "def"));
    }

    // the default null sink: copy drops silently, paste is a no-op
    {
        SinkGuard guard(nullptr);
        SelInput t;
        t.input->set_text("abcdef");
        t.d.dispatch(t.root, press_at(18, 20));
        t.d.dispatch(t.root, move_to(30, 20));
        EXPECT(t.d.dispatch(t.root, key_mod('c', zb::input::mod_ctrl)));
        EXPECT(t.d.dispatch(t.root, key_mod('v', zb::input::mod_ctrl)));
        EXPECT(t.input->get_text() == std::u16string(u"abcdef"));
        EXPECT(t.input->selected_text() == "bc");
    }

    return test::report("text_input");
}