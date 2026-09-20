#include "test.hpp"

#include "imui.hpp"

using namespace zb::ui;

namespace
{
    // measure/text metrics are protected on Widget; a probe label lifts
    // them so the H-1 suites can assert the wrapped block geometry
    struct ProbeLabel : Label
    {
        using Label::text_advance;
        using Label::text_height;
    };
}  // namespace

int test_label()
{
    // text handling (no font needed)
    {
        Label l;
        l.set_text("abc");
        EXPECT(l.get_text() == std::u16string(u"abc"));
        l.set_text(nullptr);
        EXPECT(l.get_text().empty());
        l.set_text(std::u16string(u"xyz"));
        EXPECT(l.get_text() == std::u16string(u"xyz"));
    }

    // invalid UTF-8 bytes become U+FFFD (input is UTF-8 per the contract)
    {
        Label l;
        l.set_text("\x80\xFF");
        const auto &t = l.get_text();
        EXPECT(t.size() == 2);
        if (t.size() == 2)
        {
            EXPECT(t[0] == 0xFFFD);
            EXPECT(t[1] == 0xFFFD);
        }
    }

    // multibyte UTF-8 decodes into the right code units
    {
        Label l;
        l.set_text("A\xE4\xB8\xAD");
        const auto &t = l.get_text();
        EXPECT(t.size() == 2);
        if (t.size() == 2)
        {
            EXPECT(t[0] == u'A');
            EXPECT(t[1] == 0x4E2D);
        }
    }

    // background color: drawn once set
    {
        auto g = core::Graphics::make_ptr(8, 8);
        Label l;
        l.set_position(1, 1);
        l.set_size(4, 4);
        l.draw(*g);
        EXPECT(test::pixel_at(*g, 1, 1) == 0);  // no background

        l.set_background_color(core::colors::Red);
        l.draw(*g);
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 0, 0) == 0);
    }

    // background image: blitted at the widget origin, clipped to the area
    {
        const core::Color img_pixels[] = {
            core::Color::from(255, 0, 0), core::Color::from(255, 0, 0),
            core::Color::from(255, 0, 0), core::Color::from(255, 0, 0),
        };
        const core::image_t img{img_pixels, 2, 2, 0};

        auto g = core::Graphics::make_ptr(8, 8);
        Label l;
        l.set_position(2, 2);
        l.set_size(3, 3);
        l.set_background_image(img);
        l.draw(*g);

        EXPECT(test::pixel_at(*g, 2, 2) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 1, 1) == 0);  // outside the label
        EXPECT(test::pixel_at(*g, 4, 4) == 0);  // image is 2x2, label is 3x3
    }

    // image wins over the background color
    {
        const core::Color img_pixels[] = {
            core::Color::from(0, 0, 255), core::Color::from(0, 0, 255),
            core::Color::from(0, 0, 255), core::Color::from(0, 0, 255),
        };
        const core::image_t img{img_pixels, 2, 2, 0};

        auto g = core::Graphics::make_ptr(8, 8);
        Label l;
        l.set_position(0, 0);
        l.set_size(4, 4);
        l.set_background_color(core::colors::Red);
        l.set_background_image(img);
        l.draw(*g);

        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Blue.pixel);  // image area
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Red.pixel);   // color beyond image
    }

    // default alignment values
    {
        Label l;
        EXPECT(l.get_text().empty());
        EXPECT(l.is_visible());
    }

    // H-1: wrap is default-off; the switch lives in the sidecar and the
    // line pitch rides the same allocation (no bare-widget growth)
    {
        Label l;
        EXPECT(!l.text_wrap());
        EXPECT(l.line_height() == 0);
        l.set_text_wrap(true);
        EXPECT(l.text_wrap());
        l.set_line_height(20);
        EXPECT(l.line_height() == 20);
        l.set_line_height(-5);  // clamps to 0 (unset -> provider pitch)
        EXPECT(l.line_height() == 0);
        Label b;
        EXPECT(!b.text_wrap());
    }

    // H-1: greedy wrapping breaks at spaces (the break space drops) and
    // at '\n'; consecutive hard breaks keep their empty lines; leading
    // and trailing spaces trim off a line; a width <= 0 is the single
    // line
    {
        Label l;
        l.set_text("aa bb cc");
        l.set_text_wrap(true);
        const auto &wide = l.wrap_spans(10000);
        EXPECT(wide.size() == 1);
        if (wide.size() == 1)
        {
            EXPECT(wide[0].first == 0 && wide[0].second == 8);
        }
        const auto &narrow = l.wrap_spans(8);
        EXPECT(narrow.size() == 3);
        if (narrow.size() == 3)
        {
            EXPECT(narrow[0].first == 0 && narrow[0].second == 2);
            EXPECT(narrow[2].second == 2);  // "cc"
        }
        const auto &zero = l.wrap_spans(0);
        EXPECT(zero.size() == 1 && zero[0].second == 8);

        Label br;
        br.set_text(std::u16string(u"aa\n bb\n\ncc "));
        br.set_text_wrap(true);
        const auto &sp = br.wrap_spans(10000);
        EXPECT(sp.size() == 4);  // aa | bb | (empty) | cc
        if (sp.size() == 4)
        {
            const std::u16string &t = br.get_text();
            EXPECT(t.substr(sp[0].first, sp[0].second) == u"aa");
            EXPECT(t.substr(sp[1].first, sp[1].second) == u"bb");
            EXPECT(t.substr(sp[2].first, sp[2].second).empty());
            EXPECT(t.substr(sp[3].first, sp[3].second) == u"cc");
        }
    }

    // H-1: the wrapped block height is (lines-1) x pitch + line height;
    // the pitch is the provider line metrics unless line_height wins, and
    // a single line equals the plain text height
    {
        ProbeLabel l;
        l.set_text("www www www");
        l.set_text_wrap(true);
        const int h = l.text_height();
        const int one = l.wrapped_block_height(10000);
        EXPECT(one == h);
        const auto &sp = l.wrap_spans(15);
        EXPECT(sp.size() > 1);
        EXPECT(l.wrapped_block_height(15) == static_cast<int>(sp.size()) * h);

        l.set_line_height(20);
        const auto &sp2 = l.wrap_spans(15);
        EXPECT(l.wrapped_block_height(15) ==
               (static_cast<int>(sp2.size()) - 1) * 20 + h);
    }

    // H-1: measure() reports the box width and wrapped height once a box
    // is assigned (the convergent read), else the single-line demand
    {
        ProbeLabel l;
        l.set_text("aa bb");
        l.set_text_wrap(true);
        EXPECT(l.measure().width == l.text_advance());  // no box: single-line
        l.set_size(7, 7);
        EXPECT(l.measure().width == 7);
        EXPECT(l.measure().height == 2 * l.text_height());
    }

    return test::report("label");
}
