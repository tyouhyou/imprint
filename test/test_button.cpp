#include "test.hpp"

#include "imui.hpp"

using namespace zb::ui;

int test_button()
{
    // press then release fires clicked once
    {
        Button b;
        int clicks = 0;
        b.clicked += [&clicks]() { ++clicks; };
        b.press();
        b.release();
        EXPECT(clicks == 1);
    }

    // cancel suppresses the click
    {
        Button b;
        int clicks = 0;
        b.clicked += [&clicks]() { ++clicks; };
        b.press();
        b.cancel();
        b.release();
        EXPECT(clicks == 0);
    }

    // release without press does nothing
    {
        Button b;
        int clicks = 0;
        b.clicked += [&clicks]() { ++clicks; };
        b.release();
        EXPECT(clicks == 0);
    }

    // double press still fires once
    {
        Button b;
        int clicks = 0;
        b.clicked += [&clicks]() { ++clicks; };
        b.press();
        b.press();
        b.release();
        EXPECT(clicks == 1);
    }

    // cancel then press again works
    {
        Button b;
        int clicks = 0;
        b.clicked += [&clicks]() { ++clicks; };
        b.press();
        b.cancel();
        b.press();
        b.release();
        EXPECT(clicks == 1);
    }

    // no subscribers: press/release must not crash
    {
        Button b;
        b.press();
        b.release();
    }

    // drawing: pressed state swaps the fill color
    {
        auto g = core::Graphics::make_ptr(8, 8);
        Button b;
        b.set_size(4, 4);
        b.set_background_color(core::colors::Green);
        b.set_pressed_color(core::colors::Red);
        b.set_show_border(false);

        b.draw(*g);
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::Green.pixel);

        b.press();
        b.draw(*g);
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 4, 4) == 0);  // outside the button
    }

    // drawing: border on the outline, fill inside
    {
        auto g = core::Graphics::make_ptr(8, 8);
        Button b;
        b.set_size(4, 4);
        b.set_background_color(core::colors::White);
        b.set_border_color(core::colors::Red);
        b.set_show_border(true);

        b.draw(*g);
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Red.pixel);   // border
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Red.pixel);   // border
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::White.pixel); // inside
    }

    // drawing: pressed image wins over the pressed color
    {
        const core::Color img_pixels[] = {
            core::Color::from(0, 255, 0), core::Color::from(0, 255, 0),
            core::Color::from(0, 255, 0), core::Color::from(0, 255, 0),
        };
        const core::image_t img{img_pixels, 2, 2, 0};

        auto g = core::Graphics::make_ptr(8, 8);
        Button b;
        b.set_size(4, 4);
        b.set_background_color(core::colors::White);
        b.set_pressed_color(core::colors::Red);
        b.set_pressed_image(img);
        b.set_show_border(false);

        b.draw(*g);
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::White.pixel);  // normal

        b.press();
        b.draw(*g);
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::Green.pixel);  // image area
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::White.pixel);  // beyond image: base background
    }

    // text placement: centered clear of the border (a flush top-left
    // placement used to merge the text into the 1px border)
    {
        Button b;
        b.set_glyph_provider(zb::make_shared<BitmapProvider>());
        b.set_text("OK");
        b.set_text_color(core::colors::Black);
        b.set_background_color(core::colors::White);
        b.set_border_color(core::colors::Red);
        const auto s = b.measure();
        b.set_size(s.width, s.height);
        auto g = core::Graphics::make_ptr(s.width, s.height);
        b.draw(*g);
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Red.pixel);   // border
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::White.pixel); // padding ring
        // 'O' top bar of the centered text (5x7: advance 6/char, height 7)
        EXPECT(test::pixel_at(*g, (s.width - 12) / 2 + 1, (s.height - 7) / 2) ==
               core::colors::Black.pixel);
    }

    return test::report("button");
}
