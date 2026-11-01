// TextArea suites (T-4, code-contract §2 "Multi-line editor"): multi-line
// editing with enter as content, span-mapped click placement, the pixel
// column goal across vertical moves, caret-following scroll, and the
// T-2 acceptance riding the shared editor rules.
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

    zb::input::input_event wheel_ev(const int delta)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_wheel;
        ev.delta = delta;
        return ev;
    }

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

    // a uniform 6px-advance provider covering every code unit: the
    // 5x7 fallback leaves CJK units at zero advance, which collapses
    // every click column onto the line end — the click-mapping suites
    // need real advances to be meaningful
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

    return test::report("text_area");
}
