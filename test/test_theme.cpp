#include "test.hpp"

#include "imapp.hpp"
#include "imui.hpp"

using namespace zb::app;
using namespace zb::ui;

namespace
{
    // RAII: restore the light theme so suite order never leaks state
    struct theme_guard
    {
        ~theme_guard() { set_theme(light_theme()); }
    };

    core::Color px(const CanvasWindow &w, const int x, const int y)
    {
        const auto *p = static_cast<const core::Color *>(w.data());
        return p[y * w.width() + x];
    }

    bool has_pixel(const CanvasWindow &w, const core::Color c)
    {
        const auto *p = static_cast<const core::Color *>(w.data());
        const int n = w.width() * w.height();
        for (int i = 0; i < n; ++i)
        {
            if (p[i].pixel == c.pixel)
            {
                return true;
            }
        }
        return false;
    }
}  // namespace

int test_theme()
{
    using core::Color;

    // the light preset is the pre-theme look, pixel-exactly (contract 10.2)
    {
        const Theme &t = light_theme();
        EXPECT(t.background.pixel == core::colors::White.pixel);
        EXPECT(t.text.pixel == core::colors::Black.pixel);
        EXPECT(t.text_inverted.pixel == core::colors::White.pixel);
        EXPECT(t.border.pixel == core::colors::Black.pixel);
        EXPECT(t.accent.pixel == core::colors::Blue.pixel);
        EXPECT(t.selection.pixel == Color::from(0, 80, 200).pixel);
        EXPECT(t.field_bg.pixel == Color::from(240, 240, 240).pixel);
        EXPECT(t.scroll_track.pixel == Color::from(200, 200, 200).pixel);
        EXPECT(t.scroll_thumb.pixel == Color::from(120, 120, 120).pixel);
        EXPECT(t.focus_mark.pixel == core::colors::Red.pixel);

        // the active theme starts as light
        EXPECT(theme().background.pixel == light_theme().background.pixel);
    }

    // the dark preset differs from light on the visible tokens
    {
        const Theme &d = dark_theme();
        const Theme &l = light_theme();
        EXPECT(d.background.pixel != l.background.pixel);
        EXPECT(d.text.pixel != l.text.pixel);
        EXPECT(d.field_bg.pixel != l.field_bg.pixel);
    }

    // a theme switch recolors an existing tree on the next paint, with
    // no input and no manual invalidation (contract 10.4)
    {
        theme_guard keep;
        CanvasWindow w;
        w.create(60, 40);
        auto b = std::make_unique<Button>();
        b->set_size(30, 20);
        b->set_position(10, 10);
        w.root().add_child(std::move(b));

        w.paint();
        EXPECT(px(w, 0, 0).pixel == light_theme().background.pixel);
        EXPECT(px(w, 10, 10).pixel == light_theme().border.pixel);

        const unsigned g0 = theme_generation();
        set_theme(dark_theme());
        EXPECT(theme_generation() == g0 + 1);
        EXPECT(w.is_dirty());  // the generation bump owes a frame
        w.paint();
        EXPECT(px(w, 0, 0).pixel == dark_theme().background.pixel);
        EXPECT(px(w, 10, 10).pixel == dark_theme().border.pixel);

        // switching back restores the light pixels
        set_theme(light_theme());
        EXPECT(w.is_dirty());
        w.paint();
        EXPECT(px(w, 0, 0).pixel == light_theme().background.pixel);
        EXPECT(px(w, 10, 10).pixel == light_theme().border.pixel);
        EXPECT(!w.is_dirty());  // the owed frame is consumed
    }

    // draw-time lookup also covers widget text (contract 10.3)
    {
        theme_guard keep;
        CanvasWindow w;
        w.create(40, 20);
        auto l = std::make_unique<Label>();
        l->set_text("A");
        l->set_size(20, 14);
        w.root().add_child(std::move(l));

        w.paint();
        EXPECT(has_pixel(w, light_theme().text));

        set_theme(dark_theme());
        w.paint();
        EXPECT(has_pixel(w, dark_theme().text));
        EXPECT(!has_pixel(w, light_theme().text));
    }

    // per-widget overrides win over the active theme (contract 10.3)
    {
        theme_guard keep;
        CanvasWindow w;
        w.create(40, 30);
        auto b = std::make_unique<Button>();
        b->set_size(20, 16);
        b->set_border_color(core::colors::Red);
        w.root().add_child(std::move(b));

        w.paint();
        EXPECT(px(w, 0, 0).pixel == core::colors::Red.pixel);

        set_theme(dark_theme());
        w.paint();
        // the override survives the theme switch
        EXPECT(px(w, 0, 0).pixel == core::colors::Red.pixel);
    }

    return test::report("theme");
}
