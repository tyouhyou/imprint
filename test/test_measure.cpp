#include "test.hpp"

#include <memory>

#include "flex_panel.hpp"
#include "imui.hpp"
#include "text/glyph_provider.hpp"

using namespace zb::ui;

namespace
{
    // exposes the protected text_advance() for the cache probes
    struct ProbeWidget : Widget
    {
        int adv() const { return text_advance(); }
    };

    // provider that counts measure() calls: a cache hit must not reach
    // the provider at all
    struct CountingProvider : GlyphProvider
    {
        mutable int measures = 0;

        bool covers(const char16_t) const override { return true; }
        text_metrics measure(const char16_t *str, const int len) const override
        {
            ++measures;
            return {6 * len, 7, 6};
        }
        text_metrics line_metrics() const override { return {0, 7, 6}; }
        void write(core::Graphics &, const char16_t *, const int, const int,
                   const int, const core::Color &) const override
        {
        }
    };
}  // namespace

// measure(): the contract is natural size by content, explicit size wins
int test_measure()
{
    // Widget default: measure == current size; set_size marks explicit,
    // the flex-internal set_size_auto does not
    {
        Panel p;
        EXPECT(p.measure().width == 0 && p.measure().height == 0);
        p.set_size(30, 40);
        EXPECT(p.measure().width == 30 && p.measure().height == 40);
        EXPECT(p.is_size_explicit());
        p.set_size_auto(99, 99);
        EXPECT(p.get_size().width == 99 && p.get_size().height == 99);
        EXPECT(!p.is_size_explicit());
    }

    // Label: empty measures 0x0, text measures its advance (5x7 bitmap:
    // 6 px per covered glyph) and the bitmap line height (7)
    {
        Label l;
        EXPECT(l.measure().width == 0 && l.measure().height == 0);
        l.set_text("ABC");
        EXPECT(l.measure().width == 18);
        EXPECT(l.measure().height == 7);
    }

    // Checkbox: box only, or box + gap + label (tallest of the two)
    {
        Checkbox c;
        EXPECT(c.measure().width == 14 && c.measure().height == 14);
        c.set_text("X");  // 14 + 4 + 6 = 24 wide, 14 tall (box wins)
        EXPECT(c.measure().width == 24);
        EXPECT(c.measure().height == 14);
    }

    // RadioButton: circle only, or circle + gap + label
    {
        RadioButton r;
        EXPECT(r.measure().width == 12 && r.measure().height == 12);
        r.set_text("On");  // 12 + 4 + 12 = 28 wide, 12 tall (circle wins)
        EXPECT(r.measure().width == 28);
        EXPECT(r.measure().height == 12);
    }

    // Slider: intrinsic 100x20
    {
        Slider s;
        EXPECT(s.measure().width == 100 && s.measure().height == 20);
    }

    // ListBox: 100 wide, visible rows tall
    {
        ListBox l;
        l.set_row_height(16);
        l.set_visible_rows(3);
        EXPECT(l.measure().width == 100 && l.measure().height == 48);
    }

    // flex: auto-sized children use measure(), explicit set_size wins,
    // and flex-assigned sizes stay implicit (re-layout re-measures)
    {
        FlexPanel panel;
        panel.set_direction(FlexPanel::flex_direction::row);
        panel.set_spacing(2);

        auto b = std::make_unique<Button>();
        b->set_size(50, 30);  // explicit
        auto *bptr = b.get();
        panel.add_child(std::move(b));

        auto c = std::make_unique<Checkbox>();
        c->set_text("X");  // auto: 24x14
        auto *cptr = c.get();
        panel.add_child(std::move(c));

        auto s = std::make_unique<Slider>();  // auto: 100x20
        auto *sptr = s.get();
        panel.add_child(std::move(s));

        panel.set_size(200, 60);
        panel.layout();

        EXPECT(bptr->get_position().x == 0);
        EXPECT(cptr->get_position().x == 52);   // 50 + 2 spacing
        EXPECT(sptr->get_position().x == 78);   // + 24 + 2
        EXPECT(bptr->get_position().y == 0);
        EXPECT(cptr->get_position().y == 0);

        // sizes follow the measure along the main axis, heights are the
        // line cross maximum (30 from the button)
        EXPECT(bptr->get_size().width == 50);   // explicit wins
        EXPECT(cptr->get_size().width == 24);   // auto-sized
        EXPECT(sptr->get_size().width == 100);  // auto-sized
        EXPECT(cptr->get_size().height == 14);  // no horizontal stretch

        // flex-assigned sizes are not explicit: relayout after a text
        // change picks up the new measure
        EXPECT(!cptr->is_size_explicit());
        EXPECT(cptr->is_checked() == false);
        cptr->set_text("XXXX");  // 14 + 4 + 24 = 42
        panel.layout();
        EXPECT(cptr->get_position().x == 52);   // unchanged slot start
        EXPECT(sptr->get_position().x == 96);   // 52 + 42 + 2
    }

    // flex column stacks auto-sized children along y
    {
        FlexPanel panel;
        panel.set_direction(FlexPanel::flex_direction::column);
        panel.set_spacing(2);

        auto l = std::make_unique<Label>();
        l->set_text("AB");  // 12x7
        auto *lptr = l.get();
        panel.add_child(std::move(l));

        auto s = std::make_unique<Slider>();  // 100x20
        auto *sptr = s.get();
        panel.add_child(std::move(s));

        panel.set_size(120, 80);
        panel.layout();

        EXPECT(lptr->get_position().y == 0);
        EXPECT(sptr->get_position().y == 9);  // 7 + 2
        EXPECT(sptr->get_size().width == 100);
        EXPECT(lptr->get_size().width == 12);
    }

    // text advance cache (batch J4): a measured text is not measured
    // again until a setter that changes the run fires; set_text and
    // set_glyph_provider are the invalidation points
    {
        auto provider = zb::make_shared<CountingProvider>();
        ProbeWidget w;
        w.set_glyph_provider(provider);
        w.set_text("AB");

        EXPECT(w.adv() == 12);          // cold: measured once
        EXPECT(provider->measures == 1);
        EXPECT(w.adv() == 12);          // warm: cache hit
        EXPECT(provider->measures == 1);
        EXPECT(w.get_text().size() == 2);  // state unchanged by measuring

        w.set_text("ABCD");             // invalidation: re-measured
        EXPECT(w.adv() == 24);
        EXPECT(provider->measures == 2);

        w.set_text("ABCD");             // same text, setter still resets
        EXPECT(w.adv() == 24);
        EXPECT(provider->measures == 3);

        auto other = zb::make_shared<CountingProvider>();
        w.set_glyph_provider(other);    // provider change: invalidated
        EXPECT(other->measures == 0);
        EXPECT(w.adv() == 24);
        EXPECT(other->measures == 1);
        EXPECT(provider->measures == 3);  // the old provider is untouched

        // a 0 advance is a valid cached value (not the "empty" sentinel);
        // an empty run never reaches the provider at all
        w.set_text("");
        EXPECT(w.adv() == 0);
        EXPECT(other->measures == 1);
        EXPECT(w.adv() == 0);
        EXPECT(other->measures == 1);
    }

    // measure() paths run through the same cache (Label)
    {
        auto provider = zb::make_shared<CountingProvider>();
        Label l;
        l.set_glyph_provider(provider);
        l.set_text("XYZ");

        EXPECT(l.measure().width == 18);
        EXPECT(provider->measures == 1);
        EXPECT(l.measure().width == 18);
        EXPECT(provider->measures == 1);
    }

    return test::report("measure");
}