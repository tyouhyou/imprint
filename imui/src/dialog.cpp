#include "dialog.hpp"

namespace zb::ui
{
    Dialog::Dialog()
    {
        frame = std::make_unique<Panel>();
        frame->parent = this;
        frame->set_orientation(Panel::orientation::vertical);

        auto t = std::make_unique<Label>();
        title_label = t.get();
        frame->add_child(std::move(t));

        auto b = std::make_unique<Panel>();
        b->set_orientation(Panel::orientation::vertical);
        body = b.get();
        frame->add_child(std::move(b));

        auto btns = std::make_unique<Panel>();
        btns->set_orientation(Panel::orientation::horizontal);
        btns->set_spacing(4);
        buttons = btns.get();
        frame->add_child(std::move(btns));
    }

    Button &Dialog::add_button(const char *text)
    {
        auto b = std::make_unique<Button>();
        b->set_text(text);
        b->set_size(button_width, button_height);
        auto *ptr = b.get();
        buttons->add_child(std::move(b));
        return *ptr;
    }

    void Dialog::layout()
    {
        const auto s = get_size();
        const auto f = frame->get_size();

        // center the frame in the dialog area
        frame->set_position((s.width - f.width) / 2, (s.height - f.height) / 2);

        const int pad = frame_padding;

        // title at the top; a zero-width title stretches to the frame width
        if (0 == title_label->get_size().width)
        {
            title_label->set_size(f.width - 2 * pad, default_title_height);
        }
        title_label->set_position(pad, pad);

        // buttons at the bottom: row size derived from the buttons
        int btn_height = 0;
        for (const auto &b : buttons->get_children())
        {
            if (b->get_size().height > btn_height)
            {
                btn_height = b->get_size().height;
            }
        }
        buttons->set_size(f.width - 2 * pad, btn_height);
        buttons->set_position(pad, f.height - pad - btn_height);

        int body_top = pad + title_label->get_size().height + spacing;
        int body_height = buttons->get_position().y - spacing - body_top;
        if (body_height < 0)
        {
            body_height = 0;
        }
        body->set_position(pad, body_top);
        body->set_size(f.width - 2 * pad, body_height);

        title_label->layout();
        body->layout();
        buttons->layout();
        clear_layout_dirty();
    }

    void Dialog::draw_at(core::Graphics &area) const
    {
        if (!open_)
        {
            return;
        }

        // semi-transparent mask over the whole dialog area
        const auto s = get_size();
        if (core::ImColor_Depth == 32)
        {
            const auto bak = area.is_alpha_enabled();
            area.enable_alpha(true);
            area.fill_rect(0, 0, s.width - 1, s.height - 1, mask);
            area.enable_alpha(bak);
        }
        else
        {
            // 16bpp has a single alpha bit, so per-pixel blending is both
            // wrong and slow (it would run every frame of the whole
            // screen); a plain solid fill dims the board just as well
            area.fill(mask_16);
        }

        frame->draw(area);
    }

    Widget *Dialog::pick(const int x, const int y)
    {
        if (!open_)
        {
            return nullptr;
        }
        const auto p = frame->get_position();
        if (frame->hit(x - p.x, y - p.y))
        {
            if (auto *inner = frame->pick(x - p.x, y - p.y))
            {
                return inner;
            }
            return frame.get();
        }
        return nullptr;
    }
}
