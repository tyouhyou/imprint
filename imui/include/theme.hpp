#pragma once

#include "imcore.hpp"

namespace zb::ui
{
    /*
     * Palette tokens (batch S1): the framework's color vocabulary. Every
     * color a framework widget draws is either a per-widget override or
     * one of these tokens, read from the active theme at draw time. Form,
     * lifetime, override and invalidation rules: docs/code-contract.md
     * section 10.
     */
    struct Theme
    {
        core::Color background;    // window base fill (CanvasWindow)
        core::Color text;          // default text
        core::Color text_inverted; // text drawn over `selection`
        core::Color border;        // outlines: button frame, checkbox box,
                                   // radio ring, text-input frame, slider track
        core::Color accent;        // interactive fill: pressed button, check
                                   // mark, radio dot, slider thumb, caret,
                                   // focused border
        core::Color selection;     // selected list-row background
        core::Color field_bg;      // list / text-field background
        core::Color scroll_track;
        core::Color scroll_thumb;
        core::Color focus_mark;    // keyboard-focus indicator rect
        core::Color mask;          // modal overlay (alpha meaningful at 32bpp)
    };

    // built-in presets; light reproduces the pre-theme look pixel-exactly
    [[nodiscard]] const Theme &light_theme();
    [[nodiscard]] const Theme &dark_theme();

    // the process-wide active theme (light until set_theme)
    [[nodiscard]] const Theme &theme();

    /*
     * Replaces the active theme and bumps the theme generation; hosting
     * windows (CanvasWindow) force a whole-frame repaint on their next
     * paint. Copies a fixed-size value, never throws.
     */
    void set_theme(const Theme &t);

    // bumped by set_theme; windows compare it to detect theme switches
    [[nodiscard]] unsigned theme_generation();
}  // namespace zb::ui
