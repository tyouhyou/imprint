#include "theme.hpp"

namespace zb::ui
{
    namespace
    {
        Theme make_light()
        {
            // the pre-theme widget literals, pixel-exactly (locked by the
            // pixel-asserting suites; docs/code-contract.md 10.2)
            Theme t;
            t.background = core::colors::White;
            t.text = core::colors::Black;
            t.text_inverted = core::colors::White;
            t.border = core::colors::Black;
            t.accent = core::colors::Blue;
            t.selection = core::Color::from(0, 80, 200);
            t.field_bg = core::Color::from(240, 240, 240);
            t.scroll_track = core::Color::from(200, 200, 200);
            t.scroll_thumb = core::Color::from(120, 120, 120);
            t.focus_mark = core::colors::Red;
            t.mask = (core::ImColor_Depth == 32) ? core::Color::from(0, 0, 0, 128)
                                                 : core::Color::from(50, 50, 50);
            return t;
        }

        Theme make_dark()
        {
            Theme t;
            t.background = core::Color::from(24, 26, 32);
            t.text = core::Color::from(228, 230, 235);
            t.text_inverted = core::colors::White;
            t.border = core::Color::from(72, 77, 88);
            t.accent = core::Color::from(76, 148, 255);
            t.selection = core::Color::from(45, 100, 190);
            t.field_bg = core::Color::from(36, 39, 46);
            t.scroll_track = core::Color::from(48, 52, 60);
            t.scroll_thumb = core::Color::from(96, 102, 114);
            t.focus_mark = core::Color::from(255, 140, 64);
            t.mask = (core::ImColor_Depth == 32) ? core::Color::from(0, 0, 0, 160)
                                                 : core::Color::from(16, 18, 22);
            return t;
        }

        struct theme_state
        {
            Theme current = make_light();
            unsigned generation = 0;
        };

        // the shared BitmapProvider pattern (A-4.1): leaked on purpose so
        // a static-lifetime widget drawing after main can never touch a
        // dead theme
        theme_state &state()
        {
            static theme_state *p = new theme_state{};
            return *p;
        }
    }  // namespace

    const Theme &light_theme()
    {
        static const Theme t = make_light();
        return t;
    }

    const Theme &dark_theme()
    {
        static const Theme t = make_dark();
        return t;
    }

    const Theme &theme()
    {
        return state().current;
    }

    void set_theme(const Theme &t)
    {
        auto &s = state();
        s.current = t;
        ++s.generation;
    }

    unsigned theme_generation()
    {
        return state().generation;
    }
}  // namespace zb::ui
