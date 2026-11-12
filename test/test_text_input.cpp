#include "test.hpp"

#include "imui.hpp"
#include "text/utf8.hpp"

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
        bool covers(const char32_t) const override { return true; }
        text_metrics measure(const char32_t *, const int len) const override
        {
            return {6 * len, 7, 7};
        }
        text_metrics line_metrics() const override { return {0, 7, 7}; }
        void write(core::Graphics &, const char32_t *, const int, const int,
                   const int, const core::Color &) const override
        {
        }
    };

    // BMP code points 5px; a non-BMP code point is ONE 9px glyph. The
    // widget groups same-provider code points into one measure call, so
    // the width accumulates per code point (the grouped call still sees
    // the emoji's own 9px)
    struct WideCpProvider : GlyphProvider
    {
        bool covers(const char32_t) const override { return true; }
        text_metrics measure(const char32_t *s, const int len) const override
        {
            int w = 0;
            for (int i = 0; i < len; ++i)
            {
                w += (s[i] >= 0x10000) ? 9 : 5;
            }
            return {w, 7, 7};
        }
        text_metrics line_metrics() const override { return {0, 7, 7}; }
        void write(core::Graphics &, const char32_t *, const int, const int,
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

    // two editors for the focus-loss test: pressing the second moves
    // focus (and with it clears the first one's history)
    struct UndoPair
    {
        Panel root;
        TextInput *first = nullptr;
        TextInput *second = nullptr;
        InputDispatcher d;

        UndoPair()
        {
            root.set_size(200, 100);
            auto a = std::make_unique<TextInput>();
            a->set_size(120, 20);
            a->set_position(10, 10);
            a->set_glyph_provider(zb::make_shared<UnitProvider>());
            first = a.get();
            root.add_child(std::move(a));
            auto b = std::make_unique<TextInput>();
            b->set_size(120, 20);
            b->set_position(10, 40);
            b->set_glyph_provider(zb::make_shared<UnitProvider>());
            second = b.get();
            root.add_child(std::move(b));
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
        EXPECT(input->get_text() == "ab");
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
        EXPECT(input->get_text() == " x");
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
        EXPECT(input->get_text() == "a");
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
        EXPECT(input->get_text() == "hi");
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
        EXPECT(input->get_text() == std::string("\xe4\xbd\xa0"));
        EXPECT(last == "\xE4\xBD\xA0");  // changed carries UTF-8 bytes
        // é (Latin-1 supplement, above the C1 block) also inserts
        d.dispatch(root, char_ev(0xE9));
        EXPECT(input->get_text() == std::string("\xe4\xbd\xa0\xc3\xa9"));

        // backspace erases one code unit
        d.dispatch(root, key_ev(static_cast<int>(zb::input::key_code::backspace)));
        EXPECT(input->get_text() == std::string("\xe4\xbd\xa0"));

        // DEL (0x7F) and the C1 block drop; a non-BMP code point stores
        // as its UTF-8 byte sequence (A-25)
        d.dispatch(root, char_ev(0x7F));
        d.dispatch(root, char_ev(0x9F));
        d.dispatch(root, char_ev(0x1F600));
        EXPECT(input->get_text() ==
               std::string("\xe4\xbd\xa0\xf0\x9f\x98\x80"));

        // a surrogate value and a value past U+10FFFF are not code
        // points: the T-2 gate drops them (a C-ABI host can put any int
        // in ch — the untrusted-input rule; append_utf8 would encode
        // garbage otherwise)
        d.dispatch(root, char_ev(0xD800));
        d.dispatch(root, char_ev(0xDE00));
        d.dispatch(root, char_ev(0x110000));
        d.dispatch(root, char_ev(0x7FFFFFFF));
        EXPECT(input->get_text() ==
               std::string("\xe4\xbd\xa0\xf0\x9f\x98\x80"));
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
        EXPECT(t.input->get_text() == "X");
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
        EXPECT(t.input->get_text() == std::string("apq\xe4\xbd\xa0"
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
        EXPECT(t.input->get_text() == std::string("az\xe4\xbd\xa0"
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
        EXPECT(t.input->get_text() == std::string("abcdef"));
        EXPECT(t.input->selected_text() == "bc");
    }

    // a typed non-BMP code point (ch > U+FFFF: the terminal translator
    // assembles 4-byte UTF-8 into one full ch) stores as its UTF-8 byte
    // sequence and edits as ONE atom (code-contract §2 code-point
    // atomicity)
    {
        SelInput t;
        // a click first: char events ride focus
        t.d.dispatch(t.root, press_at(20, 20));
        t.d.dispatch(t.root, release_at(20, 20));
        EXPECT(t.d.dispatch(t.root, char_ev(0x1F600)));
        EXPECT(t.input->get_text() ==
               std::string("\xf0\x9f\x98\x80"));  // one code point, 4 bytes

        // two lefts step over 'B' and the whole code point; the typed
        // 'x' lands BEFORE it — a mid-sequence landing would split the
        // bytes
        t.d.dispatch(t.root, char_ev('B'));
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::left))));
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::left))));
        t.d.dispatch(t.root, char_ev('x'));
        EXPECT(t.input->get_text() ==
               std::string("x\xf0\x9f\x98\x80"
                           "B"));

        // Ctrl+A selects the whole text — whole code points out, valid
        // UTF-8 back
        t.d.dispatch(t.root, key_mod('a', zb::input::mod_ctrl));
        EXPECT(t.input->selected_text() == "x\xF0\x9F\x98\x80"
                                           "B");
    }

    // backspace and delete remove one whole code point (a multi-byte
    // sequence never splits)
    {
        SelInput t;
        t.d.dispatch(t.root, press_at(20, 20));
        t.d.dispatch(t.root, release_at(20, 20));
        t.d.dispatch(t.root, char_ev(0x1F600));
        t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::backspace)));
        EXPECT(t.input->get_text().empty());  // all 4 bytes gone

        t.d.dispatch(t.root, char_ev(0x1F600));
        t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::left)));
        t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::del)));
        EXPECT(t.input->get_text().empty());  // not a half-sequence
    }

    // paste rides the same acceptance (whole code points; an invalid
    // byte decodes to U+FFFD); a click lands on a code-point boundary
    // and shift+right selects the next whole code point
    {
        TestSink sink;
        SinkGuard guard(&sink);
        SelInput t;
        t.d.dispatch(t.root, press_at(20, 20));
        t.d.dispatch(t.root, release_at(20, 20));
        sink.canned = "a\xF0\x9F\x98\x80"
                      "b";
        t.d.dispatch(t.root, key_mod('v', zb::input::mod_ctrl));
        EXPECT(t.input->get_text() ==
               std::string("a\xF0\x9F\x98\x80"
                           "b"));  // whole code points, nothing dropped
        EXPECT(t.input->get_text().size() == 6);

        // uniform 6px advances: the emoji is one 6px code point — a
        // click just before its span (xx 8) places the caret before it,
        // and shift+right selects the emoji whole
        t.d.dispatch(t.root, press_at(20, 20));
        t.d.dispatch(t.root,
                     key_mod(static_cast<int>(zb::input::key_code::right),
                             zb::input::mod_shift));
        EXPECT(t.input->selected_text() ==
               std::string("\xf0\x9f\x98\x80"));
    }

    // a non-BMP code point fills one glyph through its provider — one
    // advance, letter-spacing once per code point (observed through
    // Label::measure, the public single-line advance)
    {
        Label l;
        l.set_glyph_provider(zb::make_shared<WideCpProvider>());
        l.set_text(std::string("A\xf0\x9f\x98\x80"));
        EXPECT(l.measure().width == 14);  // 5 + 9: one glyph
        l.set_letter_spacing(2);
        EXPECT(l.measure().width == 18);  // spacing once per code point
    }

    // a click landing on a covered non-BMP glyph snaps the caret to a
    // code-point boundary — the emoji has real width (prefixes 5/14/19)
    {
        Panel root;
        root.set_size(200, 100);
        auto in = std::make_unique<TextInput>();
        in->set_size(120, 20);
        in->set_position(10, 10);
        in->set_glyph_provider(zb::make_shared<WideCpProvider>());
        TextInput *inp = in.get();
        root.add_child(std::move(in));
        InputDispatcher d;
        inp->set_text(std::string("A\xf0\x9f\x98\x80"
                                  "B"));
        d.dispatch(root, press_at(22, 20));  // local 12: inside the emoji
        d.dispatch(root, release_at(22, 20));
        d.dispatch(root,
                   key_mod(static_cast<int>(zb::input::key_code::right),
                           zb::input::mod_shift));
        EXPECT(inp->selected_text() == "B");  // [5, 6), a whole code point
    }

    // read-only (code-contract §2): typing/paste/backspace drop
    // silently and changed stays quiet; copy and selection still work;
    // submitted still fires on enter; clearing the flag restores editing
    {
        SelInput t;
        t.d.dispatch(t.root, press_at(20, 20));
        t.d.dispatch(t.root, release_at(20, 20));
        t.input->set_text(std::string("seed"));
        t.input->set_read_only(true);
        EXPECT(t.input->is_read_only());

        int submits = 0;
        std::string submitted_text;
        t.input->submitted += [&](const std::string &s)
        {
            ++submits;
            submitted_text = s;
        };

        t.d.dispatch(t.root, char_ev('x'));
        t.d.dispatch(t.root,
                     key_ev(static_cast<int>(zb::input::key_code::space)));
        t.d.dispatch(t.root,
                     key_ev(static_cast<int>(zb::input::key_code::backspace)));
        EXPECT(t.input->get_text() == std::string("seed"));
        EXPECT(t.last.empty());  // changed never fired

        TestSink sink;
        sink.canned = "zz";
        SinkGuard guard(&sink);
        t.d.dispatch(t.root, key_mod('v', zb::input::mod_ctrl));
        t.d.dispatch(t.root,
                     key_mod(static_cast<int>(zb::input::key_code::insert),
                             zb::input::mod_shift));
        t.d.dispatch(t.root,
                     key_ev(static_cast<int>(zb::input::key_code::del)));
        EXPECT(t.input->get_text() == std::string("seed"));

        // enter still submits a read-only field (an affordance, not an edit)
        EXPECT(t.d.dispatch(t.root,
                            key_ev(static_cast<int>(zb::input::key_code::enter))));
        EXPECT(submits == 1);
        EXPECT(submitted_text == "seed");

        // copy works
        t.d.dispatch(t.root, key_mod('a', zb::input::mod_ctrl));
        t.d.dispatch(t.root, key_mod('c', zb::input::mod_ctrl));
        EXPECT(sink.sent == "seed");

        // collapse the standing selection with a click past the text end
        // (typing replaces a selection, FI-02), then re-enable editing
        t.d.dispatch(t.root, press_at(110, 20));  // local 100: past "seed"
        t.d.dispatch(t.root, release_at(110, 20));
        t.input->set_read_only(false);
        EXPECT(t.d.dispatch(t.root, char_ev('!')));
        EXPECT(t.input->get_text() == std::string("seed!"));
    }

    // undo/redo (code-contract §2 "Undo/redo"): a typing run is one
    // step; Ctrl+Z restores text + caret and fires changed; Ctrl+Y
    // and Ctrl+Shift+Z redo
    {
        SelInput t;
        t.d.dispatch(t.root, press_at(20, 20));
        t.d.dispatch(t.root, release_at(20, 20));

        t.d.dispatch(t.root, char_ev('a'));
        t.d.dispatch(t.root, char_ev('b'));
        t.d.dispatch(t.root, char_ev('c'));
        EXPECT(t.input->get_text() == std::string("abc"));
        EXPECT(t.input->can_undo());
        EXPECT(!t.input->can_redo());
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->get_text().empty());
        EXPECT(t.last.empty());  // changed fired with the restored text
        EXPECT(t.input->can_redo());
        t.d.dispatch(t.root, char_ev('x'));  // caret was restored to 0
        EXPECT(t.input->get_text() == std::string("x"));

        // redo, then undo again; Ctrl+Shift+Z redoes too
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->get_text().empty());
        t.d.dispatch(t.root, key_mod('y', zb::input::mod_ctrl));
        EXPECT(t.input->get_text() == std::string("x"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->get_text().empty());
        t.d.dispatch(t.root,
                     key_mod('z', zb::input::mod_ctrl | zb::input::mod_shift));
        EXPECT(t.input->get_text() == std::string("x"));
    }

    // a caret move breaks the run: undo takes back only the last edit
    {
        SelInput t;
        t.d.dispatch(t.root, press_at(20, 20));
        t.d.dispatch(t.root, release_at(20, 20));
        t.d.dispatch(t.root, char_ev('a'));
        t.d.dispatch(t.root, char_ev('b'));
        t.d.dispatch(t.root,
                     key_ev(static_cast<int>(zb::input::key_code::left)));
        t.d.dispatch(t.root, char_ev('c'));  // "acb"
        EXPECT(t.input->get_text() == std::string("acb"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->get_text() == std::string("ab"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->get_text().empty());
    }

    // a backspace run merges; a paste and a selection replace are
    // their own steps
    {
        SelInput t;
        t.d.dispatch(t.root, press_at(20, 20));
        t.d.dispatch(t.root, release_at(20, 20));
        t.d.dispatch(t.root, char_ev('a'));
        t.d.dispatch(t.root, char_ev('b'));
        t.d.dispatch(t.root, char_ev('c'));
        t.d.dispatch(t.root,
                     key_ev(static_cast<int>(zb::input::key_code::backspace)));
        t.d.dispatch(t.root,
                     key_ev(static_cast<int>(zb::input::key_code::backspace)));
        EXPECT(t.input->get_text() == std::string("a"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->get_text() == std::string("abc"));

        // typing replaces a selection (FI-02): its own undo step; the
        // restore brings the recorded selection anchor back too
        t.d.dispatch(t.root, key_mod('a', zb::input::mod_ctrl));
        t.d.dispatch(t.root, char_ev('x'));
        EXPECT(t.input->get_text() == std::string("x"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->get_text() == std::string("abc"));
        EXPECT(t.input->selected_text() == "abc");  // the select-all range

        // paste is its own step: collapse the standing selection with
        // a click past the text end first
        t.d.dispatch(t.root, press_at(110, 20));  // local 100: past "abc"
        t.d.dispatch(t.root, release_at(110, 20));
        TestSink sink;
        sink.canned = "XY";
        SinkGuard guard(&sink);
        t.d.dispatch(t.root, key_mod('v', zb::input::mod_ctrl));
        EXPECT(t.input->get_text() == std::string("abcXY"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->get_text() == std::string("abc"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->get_text().empty());
    }

    // a non-BMP typed char (one 4-byte code point) is one undo step
    {
        SelInput t;
        t.d.dispatch(t.root, press_at(20, 20));
        t.d.dispatch(t.root, release_at(20, 20));
        t.d.dispatch(t.root, char_ev(0x1F600));
        EXPECT(t.input->get_text() == std::string("\xf0\x9f\x98\x80"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->get_text().empty());
    }

    // the undo stack caps at 5 steps: the oldest drops
    {
        SelInput t;
        t.d.dispatch(t.root, press_at(20, 20));
        t.d.dispatch(t.root, release_at(20, 20));
        // six runs: each typed char is separated by a caret move
        for (const int ch : {'a', 'b', 'c', 'd', 'e', 'f'})
        {
            t.d.dispatch(t.root, char_ev(ch));
            t.d.dispatch(t.root,
                         key_ev(static_cast<int>(zb::input::key_code::left)));
        }
        EXPECT(t.input->get_text() == std::string("fedcba"));
        for (int i = 0; i < 5; ++i)
        {
            EXPECT(t.input->can_undo());
            t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        }
        EXPECT(!t.input->can_undo());
        EXPECT(t.input->get_text() == std::string("a"));
    }

    // a new edit clears the redo stack
    {
        SelInput t;
        t.d.dispatch(t.root, press_at(20, 20));
        t.d.dispatch(t.root, release_at(20, 20));
        t.d.dispatch(t.root, char_ev('a'));
        t.d.dispatch(t.root, char_ev('b'));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->can_redo());
        t.d.dispatch(t.root, char_ev('c'));
        EXPECT(!t.input->can_redo());
        EXPECT(t.input->get_text() == std::string("c"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->get_text().empty());
    }

    // empty stacks: the chord is consumed but nothing changes
    {
        SelInput t;
        t.d.dispatch(t.root, press_at(20, 20));
        t.d.dispatch(t.root, release_at(20, 20));
        EXPECT(!t.input->can_undo());
        EXPECT(!t.input->can_redo());
        EXPECT(t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl)));
        EXPECT(t.d.dispatch(t.root, key_mod('y', zb::input::mod_ctrl)));
        t.input->undo();
        t.input->redo();
        EXPECT(t.input->get_text().empty());
    }

    // a mid-focus programmatic set_text records no step; the next
    // undo restores the recorded text wholesale (contract §2)
    {
        SelInput t;
        t.d.dispatch(t.root, press_at(20, 20));
        t.d.dispatch(t.root, release_at(20, 20));
        t.d.dispatch(t.root, char_ev('a'));
        t.input->set_text(std::string("XY"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->get_text().empty());
    }

    // the history lives for one focus session: focus loss clears it
    {
        UndoPair t;
        t.d.dispatch(t.root, press_at(20, 20));  // the first input
        t.d.dispatch(t.root, release_at(20, 20));
        t.d.dispatch(t.root, char_ev('a'));
        t.d.dispatch(t.root, char_ev('b'));
        t.d.dispatch(t.root, press_at(30, 50));  // the second input
        t.d.dispatch(t.root, release_at(30, 50));
        // Ctrl+Z now lands on the second (empty stack, consumed)
        EXPECT(t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl)));
        EXPECT(t.first->get_text() == std::string("ab"));
        t.d.dispatch(t.root, press_at(20, 20));  // back to the first
        t.d.dispatch(t.root, release_at(20, 20));
        EXPECT(!t.first->can_undo());
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.first->get_text() == std::string("ab"));
    }

    // read-only gates undo/redo like every other edit primitive
    {
        SelInput t;
        t.d.dispatch(t.root, press_at(20, 20));
        t.d.dispatch(t.root, release_at(20, 20));
        t.d.dispatch(t.root, char_ev('a'));
        t.d.dispatch(t.root, char_ev('b'));
        t.input->set_read_only(true);
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->get_text() == std::string("ab"));
        EXPECT(t.last == "ab");       // the gated undo never fired changed
        EXPECT(t.input->can_undo());  // the history is intact, just gated
        t.input->set_read_only(false);
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.input->get_text().empty());
    }

    return test::report("text_input");
}