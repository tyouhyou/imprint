#pragma once

#include <memory>

#include "button.hpp"
#include "label.hpp"
#include "panel.hpp"

namespace zb::ui
{
    /*
     * Modal dialog.
     *
     * A mask covers the whole dialog area (alpha-blended fill) and a frame
     * panel sits centered on top of it. The frame is laid out manually:
     * title on top, buttons at the bottom, body filling the middle.
     *
     * Modal input interception is handled by the input dispatcher, which
     * must check is_open() before dispatching to the rest of the widget
     * tree.
     *
     * Text rendering follows the Widget base class rules: no text is drawn
     * without a font, so the dialog works on embedded systems with the
     * background-image approach instead.
     */
    class Dialog : public Widget
    {
    public:
        Dialog();

        // frame. Note: the title label is stretched to the frame width
        // once, at layout time -- widening the frame afterwards does not
        // re-stretch an already laid-out title (set it before layout).
        void set_frame_size(const int w, const int h) { frame->set_size(w, h); }
        void set_frame_background_color(const core::Color &c) { frame->set_background_color(c); }
        void set_frame_padding(const int p)
        {
            frame_padding = p;
            mark_layout_dirty();
        }
        void set_mask_color(const core::Color &c)
        {
            mask = c;
            mark_dirty();
        }

        // title
        void set_title(const char *text) { title_label->set_text(text); }
        [[nodiscard]] Label &get_title() { return *title_label; }

        // body: fill with your own widgets (vertical layout inside)
        [[nodiscard]] Panel &get_body() { return *body; }

        // buttons: added in a horizontal row at the bottom of the frame.
        // The size applies when a button is ADDED (add_button bakes the
        // current metrics in); buttons added before a set_button_size
        // call keep their size -- re-adding is the way to resize them.
        Button &add_button(const char *text);
        void set_button_size(const int w, const int h)
        {
            button_width = w;
            button_height = h;
            mark_layout_dirty();
        }

        // a closed dialog must not intercept hits, drawing, or keyboard
        // focus, so visibility follows the open state
        void open()
        {
            open_ = true;
            set_visible(true);
        }
        void close()
        {
            open_ = false;
            set_visible(false);
        }
        [[nodiscard]] bool is_open() const { return open_; }

        void layout() override;

    protected:
        void draw_at(core::Graphics &area) const override;
        Widget *pick(const int x, const int y) override;
        // a closed dialog must not intercept hits from widgets underneath
        bool hit(const int x, const int y) const override
        {
            return is_open() && Widget::hit(x, y);
        }

        // expose the frame subtree so keyboard focus can reach the buttons
        size_t child_count() const override { return 1; }
        Widget *child_at(const size_t i) override
        {
            return (i == 0) ? frame.get() : nullptr;
        }

    private:
        std::unique_ptr<Panel> frame;
        Label *title_label = nullptr;
        Panel *body = nullptr;
        Panel *buttons = nullptr;

        int frame_padding = 8;
        int spacing = 4;
        int default_title_height = 16;
        int button_width = 48;
        int button_height = 18;

        core::Color mask = core::Color::from(0, 0, 0, 128);
        core::Color mask_16 = core::Color::from(50, 50, 50);
        bool open_ = false;
    };
}
