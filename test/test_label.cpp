#include "test.hpp"

#include "imui.hpp"

#if defined(USE_FONT)
#include <cstdio>
#endif

using namespace zb::ui;

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

#if defined(USE_FONT)
    // measure smoke test against a system font (skip if unavailable)
    {
        std::FILE *f = std::fopen("C:/Windows/Fonts/arial.ttf", "rb");
        if (nullptr != f)
        {
            std::fclose(f);
            Font font("C:/Windows/Fonts/arial.ttf", 0);
            font.set_char_size_in_px(16);
            const auto m = font.measure(u"Hello", 5);
            EXPECT(m.width > 0);
            EXPECT(m.height > 0);
            EXPECT(m.ascent > 0);
            EXPECT(m.ascent <= m.height);

            // centered text must stay inside the label
            Label l;
            l.set_size(100, 20);
            l.set_font(&font);
            l.set_text(u"Hi");
            l.set_h_align(Widget::h_align::center);
            l.set_v_align(Widget::v_align::center);
            auto g = core::Graphics::make_ptr(100, 20);
            l.draw(*g);  // must not draw outside; no crash
        }
        else
        {
            std::printf("skip font measure test (arial.ttf not found)\n");
        }
    }
#endif

    return test::report("label");
}
