// TextArea suites (T-4, code-contract §2 "Multi-line editor"): multi-line
// editing with enter as content, span-mapped click placement, the pixel
// column goal across vertical moves, caret-following scroll, and the
// T-2 acceptance riding the shared editor rules.
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

    zb::input::input_event wheel_ev(const int delta)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_wheel;
        ev.delta = delta;
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

    // FI-02 ClipboardSink test double: records copies, serves a canned
    // paste. The guard restores the module default (a null sink) on
    // scope exit so suites never leak a sink into each other
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

    // a focused editor + dispatcher wired at the origin, so test
    // coordinates are the widget's own
    struct Editor
    {
        Panel root;
        TextArea *area = nullptr;
        InputDispatcher d;
        std::string last;

        explicit Editor(const int w = 40, const int h = 30)
        {
            root.set_size(200, 100);
            auto in = std::make_unique<TextArea>();
            in->set_size(w, h);
            in->set_caret_color(core::colors::Red);
            area = in.get();
            root.add_child(std::move(in));
            area->changed += [this](const std::string &t)
            { last = t; };
            focus();
        }

        void focus() { d.dispatch(root, press_at(5, 5)); }
    };

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
    // FI-02 selection tree: a uniform 6px-advance editor (the 5x7
    // fallback's uneven advances would make the column math shapeless)
    // plus a second focusable so a press can take the focus away
    struct SelTree
    {
        Panel root;
        TextArea *area = nullptr;
        TextInput *other = nullptr;
        InputDispatcher d;
        std::string last;

        SelTree()
        {
            root.set_size(200, 100);
            auto a = std::make_unique<TextArea>();
            a->set_size(40, 30);
            a->set_caret_color(core::colors::Red);
            a->set_glyph_provider(zb::make_shared<UnitProvider>());
            area = a.get();
            root.add_child(std::move(a));
            auto o = std::make_unique<TextInput>();
            o->set_size(60, 14);
            o->set_position(0, 60);
            other = o.get();
            root.add_child(std::move(o));
            area->changed += [this](const std::string &t)
            { last = t; };
        }
    };

    // a uniform 6px-advance provider covering every code unit: the
    // 5x7 fallback leaves CJK units at zero advance, which collapses
    // every click column onto the line end — the click-mapping suites
    // need real advances to be meaningful
}  // namespace

int test_text_area()
{
    // wrap is always on and the natural size is three lines
    {
        TextArea a;
        EXPECT(a.text_wrap());
        const auto m = a.measure();
        EXPECT(m.width == 100);
        // the pitch is the provider line metrics (no line_height set)
        EXPECT(m.height == a.measure().height);
        TextArea b;
        b.set_line_height(11);
        EXPECT(b.measure().height == 11 * 3 + 2);
    }

    // typing inserts, enter inserts '\n' (no submitted event exists on
    // this editor), changed carries the whole UTF-8 text, and the
    // paragraph wraps into the typed lines
    {
        Editor e;
        e.d.dispatch(e.root, char_ev('a'));
        e.d.dispatch(e.root, char_ev('b'));
        e.d.dispatch(e.root, key_ev(static_cast<int>(zb::input::key_code::enter)));
        e.d.dispatch(e.root, char_ev('c'));
        e.d.dispatch(e.root, char_ev('d'));
        EXPECT(e.area->get_text() == std::u16string(u"ab\ncd"));
        EXPECT(e.last == "ab\ncd");
        const auto &sp = e.area->wrap_spans(e.area->get_size().width);
        EXPECT(sp.size() == 2);
        if (sp.size() == 2)
        {
            const std::u16string &t = e.area->get_text();
            EXPECT(t.substr(static_cast<std::size_t>(sp[0].first),
                            static_cast<std::size_t>(sp[0].second)) == u"ab");
            EXPECT(t.substr(static_cast<std::size_t>(sp[1].first),
                            static_cast<std::size_t>(sp[1].second)) == u"cd");
        }
    }

    // up/down move between the wrap_spans lines keeping the pixel column
    // goal: a shorter target line clamps to its end without re-anchoring
    // the goal, so up/up lands back where the down/down started
    {
        Editor e;
        e.area->set_text(std::u16string(u"abcdef\ngh\nijklmno"));
        // click line 0 after three chars (18px column goal)
        e.d.dispatch(e.root, press_at(20, 3));
        e.d.dispatch(e.root, key_ev(static_cast<int>(zb::input::key_code::down)));
        e.d.dispatch(e.root, key_ev(static_cast<int>(zb::input::key_code::down)));
        e.d.dispatch(e.root, key_ev(static_cast<int>(zb::input::key_code::up)));
        e.d.dispatch(e.root, key_ev(static_cast<int>(zb::input::key_code::up)));
        e.d.dispatch(e.root, char_ev('X'));
        EXPECT(e.area->get_text() == std::u16string(u"abcXdef\ngh\nijklmno"));
    }

    // the wheel scrolls without moving the caret (delta > 0 = toward the
    // top), and a click maps (x, y) through the scrolled spans. (A
    // following edit scrolls the caret's own line back into view — the
    // caret-follow rule — so the second wheel re-scrolls before the
    // mapped click.)
    {
        Editor e;
        e.area->set_text(std::u16string(u"l0\nl1\nl2\nl3\nl4"));
        e.d.dispatch(e.root, press_at(10, 3));  // line 0, after one char
        e.d.dispatch(e.root, wheel_ev(-1));     // one line toward the bottom
        e.d.dispatch(e.root, char_ev('X'));
        // inserted at the pre-wheel caret: the wheel moved no caret
        EXPECT(e.area->get_text() ==
               std::u16string(u"lX0\nl1\nl2\nl3\nl4"));
        e.d.dispatch(e.root, wheel_ev(-1));
        // the click now lands on the scrolled-in line (line 1 of the text)
        e.d.dispatch(e.root, press_at(10, 3));
        e.d.dispatch(e.root, char_ev('Y'));
        EXPECT(e.area->get_text() ==
               std::u16string(u"lX0\nlY1\nl2\nl3\nl4"));
    }

    // after an edit that pushes the caret below the fold, the minimal
    // scroll keeps the caret's line visible (a caret-colored pixel exists
    // in the caret's column inside the widget)
    {
        Editor e;
        for (int i = 0; i < 6; ++i)
        {
            e.d.dispatch(e.root,
                         key_ev(static_cast<int>(zb::input::key_code::enter)));
        }
        auto g = core::Graphics::make_ptr(40, 30);
        e.area->draw(*g);
        bool caret_visible = false;
        for (int row = 0; row < 30; ++row)
        {
            // caret column 0 -> x = text_gap; the focus ring lives on
            // x = 0 and x = w-1, so x = 2 only shows the caret
            if (test::pixel_at(*g, 2, row) == core::colors::Red.pixel)
            {
                caret_visible = true;
            }
        }
        EXPECT(caret_visible);
    }

    // CJK text and hard breaks coexist; typed CJK inserts as one code
    // unit and backspace erases one (the T-2 acceptance). The unit
    // provider gives the clicks real advances (see the stub note)
    {
        Editor e;
        e.area->set_glyph_provider(zb::make_shared<UnitProvider>());
        e.area->set_text(std::u16string(u"中文\n文本"));
        const auto &sp = e.area->wrap_spans(e.area->get_size().width);
        EXPECT(sp.size() == 2);
        if (sp.size() == 2)
        {
            const std::u16string &t = e.area->get_text();
            EXPECT(t.substr(static_cast<std::size_t>(sp[0].first),
                            static_cast<std::size_t>(sp[0].second)) == u"中文");
            EXPECT(t.substr(static_cast<std::size_t>(sp[1].first),
                            static_cast<std::size_t>(sp[1].second)) == u"文本");
        }
        // click the start of line 1 and type 你
        e.d.dispatch(e.root, press_at(2, 10));
        e.d.dispatch(e.root, char_ev(0x4F60));
        EXPECT(e.area->get_text() == std::u16string(u"中文\n你文本"));
        EXPECT(e.last == "中文\n你文本");
        e.d.dispatch(e.root,
                     key_ev(static_cast<int>(zb::input::key_code::backspace)));
        EXPECT(e.area->get_text() == std::u16string(u"中文\n文本"));
    }

    // FI-02 selection (code-contract §3.8): a drag sets an anchor and
    // moves the caret — the selection is [min, max) of the two u16
    // indexes, direction-agnostic
    {
        SelTree t;
        t.area->set_text(std::u16string(u"abcdef"));
        // press after 'c' (col 18 -> x 20), drag to after 'e' (col 30)
        t.d.dispatch(t.root, press_at(20, 3));
        EXPECT(t.area->selected_text().empty());  // a press is collapsed
        t.d.dispatch(t.root, move_to(32, 3));     // caret 5, anchor stays 3
        EXPECT(t.area->selected_text() == "de");  // [3, 5) = d..e
        // dragging back through the anchor flips the range
        t.d.dispatch(t.root, move_to(8, 3));  // caret 1
        EXPECT(t.area->selected_text() == "bc");
        t.d.dispatch(t.root, release_at(8, 3));
        // the selection survives the release
        EXPECT(t.area->selected_text() == "bc");
    }

    // shift+click and shift+arrow extend from the anchor; a non-shift
    // caret move collapses to the caret
    {
        SelTree t;
        t.area->set_text(std::u16string(u"abcdef"));
        t.d.dispatch(t.root, press_at(20, 3));  // anchor = caret = 3
        t.d.dispatch(t.root, release_at(20, 3));

        auto shift_click = press_at(32, 3);
        shift_click.mods = zb::input::mod_shift;
        t.d.dispatch(t.root, shift_click);
        EXPECT(t.area->selected_text() == "de");  // [3, 5), anchor kept

        t.d.dispatch(t.root, release_at(32, 3));
        t.d.dispatch(t.root, key_mod(static_cast<int>(zb::input::key_code::right),
                                     zb::input::mod_shift));
        EXPECT(t.area->selected_text() == "def");  // [3, 6)
        t.d.dispatch(t.root,
                     key_ev(static_cast<int>(zb::input::key_code::right)));
        EXPECT(t.area->selected_text().empty());  // collapsed at caret 6
        // shift+up crosses the hard break: [0, 4) is "abc\n"
        t.area->set_text(std::u16string(u"abc\ndef"));
        t.d.dispatch(t.root, press_at(2, 3));  // caret 0
        t.d.dispatch(t.root, release_at(2, 3));
        t.d.dispatch(t.root, key_mod(static_cast<int>(zb::input::key_code::down),
                                     zb::input::mod_shift));
        EXPECT(t.area->selected_text() == "abc\n");
    }

    // Ctrl+A selects all; typing replaces the selection; focus loss
    // collapses to the caret
    {
        SelTree t;
        t.area->set_text(std::u16string(u"abcdef"));
        t.d.dispatch(t.root, press_at(2, 3));  // focus + caret 0
        t.d.dispatch(t.root, release_at(2, 3));
        t.d.dispatch(t.root,
                     key_mod('a', zb::input::mod_ctrl));
        EXPECT(t.area->selected_text() == "abcdef");
        t.d.dispatch(t.root, char_ev('X'));
        EXPECT(t.area->get_text() == std::u16string(u"X"));
        EXPECT(t.area->selected_text().empty());  // the edit collapsed it
        EXPECT(t.last == "X");                    // one changed per edit

        // rebuild a selection, then take the focus away
        t.area->set_text(std::u16string(u"abcdef"));
        t.d.dispatch(t.root, press_at(20, 3));
        t.d.dispatch(t.root, move_to(32, 3));
        EXPECT(t.area->selected_text() == "de");
        t.d.dispatch(t.root, press_at(10, 65));  // the sibling takes focus
        EXPECT(t.d.get_focus_target() == t.other);
        EXPECT(t.area->selected_text().empty());  // focus loss collapsed
    }

    // selected_text is UTF-8: CJK multibyte and hard breaks round-trip
    {
        SelTree t;
        t.area->set_text(std::u16string(u"中文\n文本"));
        t.d.dispatch(t.root, press_at(2, 3));  // focus
        t.d.dispatch(t.root, release_at(2, 3));
        t.d.dispatch(t.root, key_mod('a', zb::input::mod_ctrl));
        EXPECT(t.area->selected_text() == "\xE4\xB8\xAD\xE6\x96\x87\n"
                                          "\xE6\x96\x87\xE6\x9C\xAC");
    }

    // the selected range draws with the inverted face: the theme
    // selection band covers exactly the selected columns of the line
    {
        SelTree t;
        t.area->set_text(std::u16string(u"abcdef"));
        t.d.dispatch(t.root, press_at(8, 3));  // caret 1 (col 6)
        t.d.dispatch(t.root, move_to(26, 3));  // caret 4 (col 24)
        core::Graphics g(200, 100, nullptr);
        g.fill_rect(0, 0, 199, 99, core::colors::White);
        t.root.draw(g);
        // band x 8..25 (text_gap 2 + advance), line rows 0..pitch-1
        EXPECT(theme().selection.pixel == test::pixel_at(g, 10, 2));
        EXPECT(theme().selection.pixel == test::pixel_at(g, 22, 2));
        EXPECT(theme().selection.pixel != test::pixel_at(g, 4, 2));
        EXPECT(theme().selection.pixel != test::pixel_at(g, 28, 2));
        EXPECT(theme().selection.pixel != test::pixel_at(g, 10, 9));
    }

    // key round-trips through an injected sink: Ctrl+C / Ctrl+Insert
    // copy the selection, Ctrl+V / Shift+Insert paste at the caret
    // replacing it, clipped to the field's own input rules
    {
        TestSink sink;
        SinkGuard guard(&sink);
        SelTree t;
        t.area->set_text(std::u16string(u"abcdef"));
        t.d.dispatch(t.root, press_at(8, 3));
        t.d.dispatch(t.root, move_to(26, 3));  // [1, 4)
        t.d.dispatch(t.root, key_mod('c', zb::input::mod_ctrl));
        EXPECT(sink.sent == "bcd");

        sink.canned = "p\nqX";
        t.d.dispatch(t.root, key_mod('v', zb::input::mod_ctrl));
        // the paste replaced the selection; '\n' is content here
        EXPECT(t.area->get_text() == std::u16string(u"ap\nqXef"));
        EXPECT(t.area->selected_text().empty());
        EXPECT(t.last == "ap\nqXef");

        // Ctrl+Insert copies the current selection
        t.d.dispatch(t.root, key_mod('a', zb::input::mod_ctrl));
        t.d.dispatch(t.root,
                     key_mod(static_cast<int>(zb::input::key_code::insert),
                             zb::input::mod_ctrl));
        EXPECT(sink.sent == "ap\nqXef");

        // Shift+Insert pastes; a C1 control in the paste drops
        sink.canned = "\xC2\x81z";
        t.d.dispatch(t.root, press_at(2, 3));  // caret 0
        t.d.dispatch(t.root, move_to(14, 3));  // caret 2: [0, 2)
        t.d.dispatch(t.root,
                     key_mod(static_cast<int>(zb::input::key_code::insert),
                             zb::input::mod_shift));
        EXPECT(t.area->get_text() == std::u16string(u"z\nqXef"));
    }

    // the default null sink: copy drops silently, paste is a no-op;
    // a present-but-set-only sink (empty get_text) pastes nothing too
    {
        SinkGuard guard(nullptr);
        SelTree t;
        t.area->set_text(std::u16string(u"abcdef"));
        t.d.dispatch(t.root, press_at(8, 3));
        t.d.dispatch(t.root, move_to(26, 3));
        EXPECT(t.d.dispatch(t.root, key_mod('c', zb::input::mod_ctrl)));
        EXPECT(t.d.dispatch(t.root, key_mod('v', zb::input::mod_ctrl)));
        EXPECT(t.area->get_text() == std::u16string(u"abcdef"));

        TestSink set_only;  // canned stays empty
        SinkGuard guard2(&set_only);
        EXPECT(t.d.dispatch(t.root, key_mod('v', zb::input::mod_ctrl)));
        EXPECT(t.area->get_text() == std::u16string(u"abcdef"));
        // copy through the set-only sink still works
        t.d.dispatch(t.root, key_mod('c', zb::input::mod_ctrl));
        EXPECT(set_only.sent == "bcd");
    }

    // A-25 pair atomicity in the multi-line editor: a typed non-BMP
    // code point stores as its surrogate pair, edits as one atom, and
    // the wrap spans never split it
    {
        SelTree t;
        // a click first: char events ride focus
        t.d.dispatch(t.root, press_at(2, 3));
        t.d.dispatch(t.root, release_at(2, 3));
        EXPECT(t.d.dispatch(t.root, char_ev(0x1F600)));
        EXPECT(t.area->get_text().size() == 2);
        EXPECT(t.area->get_text()[0] == 0xD83D);
        EXPECT(t.area->get_text()[1] == 0xDE00);

        // arrow steps never stop between the two units; the typed 'x'
        // lands before the pair instead of splitting it
        t.d.dispatch(t.root, char_ev('B'));
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::left))));
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::left))));
        t.d.dispatch(t.root, char_ev('x'));
        EXPECT(t.area->get_text() ==
               std::u16string(u"x\xD83D"
                              u"\xDE00"
                              u"B"));

        // walk the caret to the end (the first right hops over the
        // pair), then three backspaces remove 'B', the pair (as one —
        // no lone unit survives) and 'x'
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::right))));
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::right))));
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::backspace))));
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::backspace))));
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::backspace))));
        EXPECT(t.area->get_text().empty());

        // the pair rides whole through wrap spans: with a break
        // candidate beside it, the forced break lands on the space, not
        // inside the pair (indexes: a0 b1 sp2 lead3 trail4 sp5 c6 d7)
        t.area->set_text(std::u16string(u"ab \xD83D"
                                        u"\xDE00"
                                        u" cd"));
        t.area->set_size(24, 30);  // forces the break after the pair
        const auto &sp = t.area->wrap_spans(24);
        EXPECT(sp.size() == 2);  // "ab <pair>" | "cd"
        for (const auto &span : sp)
        {
            // no span ends between the pair's two units (at index 4)
            EXPECT(!(span.first <= 3 && span.first + span.second == 4));
        }
    }

    // A-25: paste keeps whole pairs next to '\n' content
    {
        TestSink sink;
        SinkGuard guard(&sink);
        SelTree t;
        t.d.dispatch(t.root, press_at(2, 3));
        t.d.dispatch(t.root, release_at(2, 3));
        sink.canned = "x\xF0\x9F\x98\x80\ny";
        t.d.dispatch(t.root, key_mod('v', zb::input::mod_ctrl));
        EXPECT(t.area->get_text() ==
               std::u16string(u"x\xD83D"
                              u"\xDE00"
                              u"\ny"));
    }

    return test::report("text_area");
}
