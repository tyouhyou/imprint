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
        EXPECT(e.area->get_text() == std::string("ab\ncd"));
        EXPECT(e.last == "ab\ncd");
        const auto &sp = e.area->wrap_spans(e.area->get_size().width);
        EXPECT(sp.size() == 2);
        if (sp.size() == 2)
        {
            const std::string &t = e.area->get_text();
            EXPECT(t.substr(static_cast<std::size_t>(sp[0].first),
                            static_cast<std::size_t>(sp[0].second)) == "ab");
            EXPECT(t.substr(static_cast<std::size_t>(sp[1].first),
                            static_cast<std::size_t>(sp[1].second)) == "cd");
        }
    }

    // up/down move between the wrap_spans lines keeping the pixel column
    // goal: a shorter target line clamps to its end without re-anchoring
    // the goal, so up/up lands back where the down/down started
    {
        Editor e;
        e.area->set_text(std::string("abcdef\ngh\nijklmno"));
        // click line 0 after three chars (18px column goal)
        e.d.dispatch(e.root, press_at(20, 3));
        e.d.dispatch(e.root, key_ev(static_cast<int>(zb::input::key_code::down)));
        e.d.dispatch(e.root, key_ev(static_cast<int>(zb::input::key_code::down)));
        e.d.dispatch(e.root, key_ev(static_cast<int>(zb::input::key_code::up)));
        e.d.dispatch(e.root, key_ev(static_cast<int>(zb::input::key_code::up)));
        e.d.dispatch(e.root, char_ev('X'));
        EXPECT(e.area->get_text() == std::string("abcXdef\ngh\nijklmno"));
    }

    // the wheel scrolls without moving the caret (delta > 0 = toward the
    // top), and a click maps (x, y) through the scrolled spans. (A
    // following edit scrolls the caret's own line back into view — the
    // caret-follow rule — so the second wheel re-scrolls before the
    // mapped click.)
    {
        Editor e;
        e.area->set_text(std::string("l0\nl1\nl2\nl3\nl4"));
        e.d.dispatch(e.root, press_at(10, 3));  // line 0, after one char
        e.d.dispatch(e.root, wheel_ev(-1));     // one line toward the bottom
        e.d.dispatch(e.root, char_ev('X'));
        // inserted at the pre-wheel caret: the wheel moved no caret
        EXPECT(e.area->get_text() ==
               std::string("lX0\nl1\nl2\nl3\nl4"));
        e.d.dispatch(e.root, wheel_ev(-1));
        // the click now lands on the scrolled-in line (line 1 of the text)
        e.d.dispatch(e.root, press_at(10, 3));
        e.d.dispatch(e.root, char_ev('Y'));
        EXPECT(e.area->get_text() ==
               std::string("lX0\nlY1\nl2\nl3\nl4"));
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
        e.area->set_text(std::string("\xe4\xb8\xad\xe6\x96\x87\n\xe6\x96\x87\xe6\x9c\xac"));
        const auto &sp = e.area->wrap_spans(e.area->get_size().width);
        EXPECT(sp.size() == 2);
        if (sp.size() == 2)
        {
            const std::string &t = e.area->get_text();
            EXPECT(t.substr(static_cast<std::size_t>(sp[0].first),
                            static_cast<std::size_t>(sp[0].second)) == "\xe4\xb8\xad\xe6\x96\x87");
            EXPECT(t.substr(static_cast<std::size_t>(sp[1].first),
                            static_cast<std::size_t>(sp[1].second)) == "\xe6\x96\x87\xe6\x9c\xac");
        }
        // click the start of line 1 and type 你
        e.d.dispatch(e.root, press_at(2, 10));
        e.d.dispatch(e.root, char_ev(0x4F60));
        EXPECT(e.area->get_text() == std::string("\xe4\xb8\xad\xe6\x96\x87\n\xe4\xbd\xa0\xe6\x96\x87\xe6\x9c\xac"));
        EXPECT(e.last == "中文\n你文本");
        e.d.dispatch(e.root,
                     key_ev(static_cast<int>(zb::input::key_code::backspace)));
        EXPECT(e.area->get_text() == std::string("\xe4\xb8\xad\xe6\x96\x87\n\xe6\x96\x87\xe6\x9c\xac"));
    }

    // FI-02 selection (code-contract §3.8): a drag sets an anchor and
    // moves the caret — the selection is [min, max) of the two u16
    // indexes, direction-agnostic
    {
        SelTree t;
        t.area->set_text(std::string("abcdef"));
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
        t.area->set_text(std::string("abcdef"));
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
        t.area->set_text(std::string("abc\ndef"));
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
        t.area->set_text(std::string("abcdef"));
        t.d.dispatch(t.root, press_at(2, 3));  // focus + caret 0
        t.d.dispatch(t.root, release_at(2, 3));
        t.d.dispatch(t.root,
                     key_mod('a', zb::input::mod_ctrl));
        EXPECT(t.area->selected_text() == "abcdef");
        t.d.dispatch(t.root, char_ev('X'));
        EXPECT(t.area->get_text() == std::string("X"));
        EXPECT(t.area->selected_text().empty());  // the edit collapsed it
        EXPECT(t.last == "X");                    // one changed per edit

        // rebuild a selection, then take the focus away
        t.area->set_text(std::string("abcdef"));
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
        t.area->set_text(std::string("\xe4\xb8\xad\xe6\x96\x87\n\xe6\x96\x87\xe6\x9c\xac"));
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
        t.area->set_text(std::string("abcdef"));
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
        t.area->set_text(std::string("abcdef"));
        t.d.dispatch(t.root, press_at(8, 3));
        t.d.dispatch(t.root, move_to(26, 3));  // [1, 4)
        t.d.dispatch(t.root, key_mod('c', zb::input::mod_ctrl));
        EXPECT(sink.sent == "bcd");

        sink.canned = "p\nqX";
        t.d.dispatch(t.root, key_mod('v', zb::input::mod_ctrl));
        // the paste replaced the selection; '\n' is content here
        EXPECT(t.area->get_text() == std::string("ap\nqXef"));
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
        EXPECT(t.area->get_text() == std::string("z\nqXef"));
    }

    // the default null sink: copy drops silently, paste is a no-op;
    // a present-but-set-only sink (empty get_text) pastes nothing too
    {
        SinkGuard guard(nullptr);
        SelTree t;
        t.area->set_text(std::string("abcdef"));
        t.d.dispatch(t.root, press_at(8, 3));
        t.d.dispatch(t.root, move_to(26, 3));
        EXPECT(t.d.dispatch(t.root, key_mod('c', zb::input::mod_ctrl)));
        EXPECT(t.d.dispatch(t.root, key_mod('v', zb::input::mod_ctrl)));
        EXPECT(t.area->get_text() == std::string("abcdef"));

        TestSink set_only;  // canned stays empty
        SinkGuard guard2(&set_only);
        EXPECT(t.d.dispatch(t.root, key_mod('v', zb::input::mod_ctrl)));
        EXPECT(t.area->get_text() == std::string("abcdef"));
        // copy through the set-only sink still works
        t.d.dispatch(t.root, key_mod('c', zb::input::mod_ctrl));
        EXPECT(set_only.sent == "bcd");
    }

    // A-25 code-point atomicity in the multi-line editor: a typed
    // non-BMP code point stores as its UTF-8 byte sequence, edits as
    // one atom, and the wrap spans never split it
    {
        SelTree t;
        // a click first: char events ride focus
        t.d.dispatch(t.root, press_at(2, 3));
        t.d.dispatch(t.root, release_at(2, 3));
        EXPECT(t.d.dispatch(t.root, char_ev(0x1F600)));
        EXPECT(t.area->get_text() ==
               std::string("\xf0\x9f\x98\x80"));  // one code point, 4 bytes

        // two lefts step over 'B' and the whole code point; the typed
        // 'x' lands BEFORE it — a mid-sequence landing would split the
        // bytes
        t.d.dispatch(t.root, char_ev('B'));
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::left))));
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::left))));
        t.d.dispatch(t.root, char_ev('x'));
        EXPECT(t.area->get_text() ==
               std::string("x\xf0\x9f\x98\x80"
                           "B"));

        // walk the caret to the end (the first right hops over the
        // emoji), then three backspaces remove 'B', the emoji (whole
        // bytes, never a half-sequence) and 'x'
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::right))));
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::right))));
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::backspace))));
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::backspace))));
        EXPECT(t.d.dispatch(t.root, key_ev(static_cast<int>(zb::input::key_code::backspace))));
        EXPECT(t.area->get_text().empty());

        // the emoji rides whole through wrap spans: with a break
        // candidate beside it, the forced break lands on the space, not
        // inside the emoji's bytes (indexes: a0 b1 sp2 emoji 3-6 sp7 c8 d9)
        t.area->set_text(std::string("ab \xf0\x9f\x98\x80"
                                     " cd"));
        t.area->set_size(24, 30);  // forces the break after the emoji
        const auto &sp = t.area->wrap_spans(24);
        EXPECT(sp.size() == 2);  // "ab <emoji>" | "cd"
        for (const auto &span : sp)
        {
            // no span ends inside the emoji's 4 bytes (byte offsets 4-6)
            EXPECT(!(span.first <= 3 && span.first + span.second >= 4 &&
                     span.first + span.second <= 6));
        }
    }

    // A-25: paste keeps whole code points next to '\n' content
    {
        TestSink sink;
        SinkGuard guard(&sink);
        SelTree t;
        t.d.dispatch(t.root, press_at(2, 3));
        t.d.dispatch(t.root, release_at(2, 3));
        sink.canned = "x\xF0\x9F\x98\x80\ny";
        t.d.dispatch(t.root, key_mod('v', zb::input::mod_ctrl));
        EXPECT(t.area->get_text() ==
               std::string("x\xF0\x9F\x98\x80"
                           "\ny"));
    }

    // read-only (code-contract §2): user edits are silent no-ops and
    // changed stays quiet
    {
        Editor e;
        e.area->set_read_only(true);
        e.d.dispatch(e.root, char_ev('a'));
        e.d.dispatch(e.root,
                     key_ev(static_cast<int>(zb::input::key_code::enter)));
        e.d.dispatch(e.root,
                     key_ev(static_cast<int>(zb::input::key_code::space)));
        e.d.dispatch(e.root,
                     key_ev(static_cast<int>(zb::input::key_code::backspace)));
        e.d.dispatch(e.root,
                     key_ev(static_cast<int>(zb::input::key_code::del)));
        EXPECT(e.area->get_text().empty());
        EXPECT(e.last.empty());

        // paste drops too (a canned sink is present); both chords
        TestSink sink;
        sink.canned = "zz";
        SinkGuard guard(&sink);
        e.d.dispatch(e.root, key_mod('v', zb::input::mod_ctrl));
        e.d.dispatch(e.root,
                     key_mod(static_cast<int>(zb::input::key_code::insert),
                             zb::input::mod_shift));
        EXPECT(e.area->get_text().empty());
        EXPECT(e.last.empty());
    }

    // read-only keeps the non-edit affordances: selection + copy work,
    // the wheel still scrolls, caret keys still move; programmatic
    // set_text applies throughout and clearing the flag restores editing
    {
        SelTree t;
        t.area->set_text(std::string("l0\nl1\nl2\nl3\nl4"));
        t.d.dispatch(t.root, press_at(2, 3));  // focus + caret 0
        t.d.dispatch(t.root, release_at(2, 3));
        t.area->set_read_only(true);

        TestSink sink;
        SinkGuard guard(&sink);
        t.d.dispatch(t.root, key_mod('a', zb::input::mod_ctrl));
        t.d.dispatch(t.root, key_mod('c', zb::input::mod_ctrl));
        EXPECT(sink.sent == "l0\nl1\nl2\nl3\nl4");

        // the wheel still scrolls: select-all's caret-follow sits at the
        // bottom, so a notch down moves nothing (the H-4 bubble rule) and
        // a notch up scrolls toward the top
        EXPECT(!t.d.dispatch(t.root, wheel_ev(-1)));
        EXPECT(t.d.dispatch(t.root, wheel_ev(1)));
        // caret keys still move (left from the select-all end)
        EXPECT(t.d.dispatch(
            t.root, key_ev(static_cast<int>(zb::input::key_code::left))));

        // programmatic set_text still applies (app-driven, not a user edit)
        t.area->set_text(std::string("seed"));
        EXPECT(t.area->get_text() == std::string("seed"));

        // collapse the standing selection with a click past the line end
        // (typing replaces a selection, FI-02), then re-enable editing
        t.d.dispatch(t.root, press_at(38, 3));
        t.d.dispatch(t.root, release_at(38, 3));
        t.area->set_read_only(false);
        EXPECT(t.d.dispatch(t.root, char_ev('!')));
        EXPECT(t.area->get_text() == std::string("seed!"));
    }

    // undo/redo (code-contract §2 "Undo/redo"): a typing run is one
    // step; Ctrl+Z restores text + caret and fires changed; Ctrl+Y
    // and Ctrl+Shift+Z redo
    {
        Editor t;
        t.d.dispatch(t.root, char_ev('a'));
        t.d.dispatch(t.root, char_ev('b'));
        t.d.dispatch(t.root, char_ev('c'));
        EXPECT(t.area->get_text() == std::string("abc"));
        EXPECT(t.area->can_undo());
        EXPECT(!t.area->can_redo());
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text().empty());
        EXPECT(t.last.empty());  // changed fired with the restored text
        EXPECT(t.area->can_redo());
        t.d.dispatch(t.root, char_ev('x'));  // caret was restored to 0
        EXPECT(t.area->get_text() == std::string("x"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text().empty());
        t.d.dispatch(t.root, key_mod('y', zb::input::mod_ctrl));
        EXPECT(t.area->get_text() == std::string("x"));
        t.d.dispatch(t.root,
                     key_mod('z', zb::input::mod_ctrl | zb::input::mod_shift));
        EXPECT(t.area->get_text() == std::string("x"));
    }

    // enter is its own undo step (T-4: enter is content here)
    {
        Editor t;
        t.d.dispatch(t.root, char_ev('a'));
        t.d.dispatch(t.root, char_ev('b'));
        t.d.dispatch(t.root,
                     key_ev(static_cast<int>(zb::input::key_code::enter)));
        t.d.dispatch(t.root, char_ev('c'));
        t.d.dispatch(t.root, char_ev('d'));
        EXPECT(t.area->get_text() == std::string("ab\ncd"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text() == std::string("ab\n"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text() == std::string("ab"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text().empty());
        EXPECT(!t.area->can_undo());
    }

    // a backspace run merges — including the one that removes the '\n'
    {
        Editor t;
        t.d.dispatch(t.root, char_ev('a'));
        t.d.dispatch(t.root, char_ev('b'));
        t.d.dispatch(t.root,
                     key_ev(static_cast<int>(zb::input::key_code::enter)));
        t.d.dispatch(t.root,
                     key_ev(static_cast<int>(zb::input::key_code::backspace)));
        t.d.dispatch(t.root,
                     key_ev(static_cast<int>(zb::input::key_code::backspace)));
        t.d.dispatch(t.root,
                     key_ev(static_cast<int>(zb::input::key_code::backspace)));
        EXPECT(t.area->get_text().empty());
        EXPECT(t.area->can_undo());
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text() == std::string("ab\n"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text() == std::string("ab"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text().empty());
        EXPECT(!t.area->can_undo());
    }

    // a caret move breaks the run; paste is its own step
    {
        Editor t;
        t.d.dispatch(t.root, char_ev('a'));
        t.d.dispatch(t.root, char_ev('b'));
        t.d.dispatch(t.root,
                     key_ev(static_cast<int>(zb::input::key_code::left)));
        t.d.dispatch(t.root, char_ev('c'));  // "acb"
        EXPECT(t.area->get_text() == std::string("acb"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text() == std::string("ab"));

        TestSink sink;
        sink.canned = "XY";
        SinkGuard guard(&sink);
        t.d.dispatch(t.root, key_mod('v', zb::input::mod_ctrl));
        EXPECT(t.area->get_text() == std::string("aXYb"));  // caret 1
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text() == std::string("ab"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text().empty());
    }

    // a non-BMP typed char (a surrogate pair) is one undo step
    {
        Editor t;
        t.d.dispatch(t.root, char_ev(0x1F600));
        EXPECT(t.area->get_text() == std::string("\xf0\x9f\x98\x80"));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text().empty());
    }

    // the undo stack caps at 5 steps: the oldest drops
    {
        Editor t;
        // six runs: each typed char is separated by a caret move
        for (const int ch : {'a', 'b', 'c', 'd', 'e', 'f'})
        {
            t.d.dispatch(t.root, char_ev(ch));
            t.d.dispatch(t.root,
                         key_ev(static_cast<int>(zb::input::key_code::left)));
        }
        EXPECT(t.area->get_text() == std::string("fedcba"));
        for (int i = 0; i < 5; ++i)
        {
            EXPECT(t.area->can_undo());
            t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        }
        EXPECT(!t.area->can_undo());
        EXPECT(t.area->get_text() == std::string("a"));
    }

    // a new edit clears the redo stack
    {
        Editor t;
        t.d.dispatch(t.root, char_ev('a'));
        t.d.dispatch(t.root, char_ev('b'));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->can_redo());
        t.d.dispatch(t.root, char_ev('c'));
        EXPECT(!t.area->can_redo());
        EXPECT(t.area->get_text() == std::string("c"));
    }

    // empty stacks: the chord is consumed but nothing changes
    {
        Editor t;
        EXPECT(!t.area->can_undo());
        EXPECT(!t.area->can_redo());
        EXPECT(t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl)));
        t.area->undo();
        t.area->redo();
        EXPECT(t.area->get_text().empty());
    }

    // the history lives for one focus session: focus loss clears it
    // (SelTree's second focusable takes the focus away)
    {
        SelTree t;
        t.d.dispatch(t.root, press_at(5, 5));
        t.d.dispatch(t.root, release_at(5, 5));
        t.d.dispatch(t.root, char_ev('a'));
        t.d.dispatch(t.root, char_ev('b'));
        t.d.dispatch(t.root, press_at(5, 65));  // the TextInput below
        t.d.dispatch(t.root, release_at(5, 65));
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text() == std::string("ab"));
        t.d.dispatch(t.root, press_at(5, 5));  // back to the area
        t.d.dispatch(t.root, release_at(5, 5));
        EXPECT(!t.area->can_undo());
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text() == std::string("ab"));
    }

    // read-only gates undo/redo like every other edit primitive
    {
        Editor t;
        t.d.dispatch(t.root, char_ev('a'));
        t.d.dispatch(t.root, char_ev('b'));
        t.area->set_read_only(true);
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text() == std::string("ab"));
        EXPECT(t.last == "ab");      // the gated undo never fired changed
        EXPECT(t.area->can_undo());  // the history is intact, just gated
        t.area->set_read_only(false);
        t.d.dispatch(t.root, key_mod('z', zb::input::mod_ctrl));
        EXPECT(t.area->get_text().empty());
    }

    return test::report("text_area");
}
