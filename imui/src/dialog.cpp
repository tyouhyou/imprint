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

        // title at the top; a zero-width title stretches to the frame width.
        // The auto height follows the title's declared font size when it
        // has one (U-4: the 16px default box clips sized glyphs); +4 keeps
        // 2px of breathing room per edge.
        if (0 == title_label->get_size().width)
        {
            const int px = title_label->font_size();
            const int h = (px > 0) ? px + 4 : default_title_height;
            title_label->set_size(f.width - 2 * pad, h);
        }
        title_label->set_position(pad, pad);

        // buttons at the bottom: row size derived from the buttons.
        // On overflow the button widths shrink proportionally to fit the
        // frame inner width (U-5) — the row never overflows into clipping.
        // The spacing gaps come out of the budget first, so the shrunk
        // sum + gaps stays within inner and the pass is idempotent;
        // widths are baked like add_button's sizes, so a later-widened
        // frame does not restore them (re-add to resize).
        const int inner = f.width - 2 * pad;
        const auto &btns = buttons->get_children();
        if (!btns.empty())
        {
            const int gaps = buttons->spacing * static_cast<int>(btns.size() - 1);
            int buttons_total = 0;
            for (const auto &b : btns)
            {
                buttons_total += b->get_size().width;
            }
            const int avail = inner - gaps;
            if (buttons_total + gaps > inner && avail > 0)
            {
                for (const auto &b : btns)
                {
                    const int w = b->get_size().width;
                    const int shrunk = w * avail / buttons_total;
                    b->set_size(shrunk > 0 ? shrunk : 1, b->get_size().height);
                }
            }
        }
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
        // the frame's own Panel::layout is deliberately bypassed (its
        // linear placement would overwrite the dialog geometry), so its
        // flag is cleared here instead (batch K / N7)
        frame->clear_layout_dirty();
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
        const core::Color mask = mask_override_.value_or(theme().mask);
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
            area.fill(mask);
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
