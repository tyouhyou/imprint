#pragma once

#include <algorithm>
#include <memory>
#include <string>

#include "igui.hpp"
#include "imui.hpp"
#include "iwindow.hpp"

namespace zb::app
{
    /*
     * Default window implementation for widget-tree apps.
     *
     * Owns the framebuffer (a zb::ui::core::Graphics), a root widget panel
     * and an input dispatcher. The app builds its widget tree on root()
     * and connects signals; input() routes events into the tree and
     * paint() renders the tree and emits the painted signal.
     *
     * A modal dialog is enforced by passing it to set_modal() (or nullptr
     * to release); see InputDispatcher.
     *
     * All state lives in the widget tree, so apps using this class write
     * no platform-specific code.
     */
    class CanvasWindow : public IWindow, public IGui
    {
    public:
        CanvasWindow() = default;
        ~CanvasWindow() override = default;

        // creates the framebuffer and the root panel; `buffer` is used
        // directly (wrapper mode) when non-null and must be writable
        void create(const uint32_t &width, const uint32_t &height, void *buffer = nullptr)
        {
            graphics_ = zb::ui::core::Graphics::make_ptr(width, height, buffer);
            root_ = std::make_unique<zb::ui::Panel>();
            root_->set_size(static_cast<int>(width), static_cast<int>(height));
        }

        // IWindow (null/0 before create(), matching paint()'s guards)
        [[nodiscard]] void *data() const noexcept override
        {
            return graphics_ ? graphics_->data() : nullptr;
        }
        [[nodiscard]] int32_t width() const noexcept override
        {
            return graphics_ ? graphics_->size().width : 0;
        }
        [[nodiscard]] int32_t height() const noexcept override
        {
            return graphics_ ? graphics_->size().height : 0;
        }
        [[nodiscard]] std::string title() const noexcept override { return "myapp"; }

        // IGui
        void close() noexcept override
        {
            closing();
            closed();
        }

        // root of the widget tree (valid after create())
        [[nodiscard]] zb::ui::Panel &root() { return *root_; }

        // routes an input event into the widget tree and repaints when the
        // event actually changed something (moves within the press slop
        // that cross no widget boundary do not repaint); the tree is always
        // up to date when the shell presents the next frame
        void input(const zb::input::input_event &ev) noexcept
        {
            if (dispatcher_.dispatch(*root_, ev))
            {
                dirty_ = true;
                paint();
            }
        }

        // events only reach widgets inside this subtree (nullptr = no modal)
        void set_modal(zb::ui::Widget *m) { dispatcher_.set_modal(m); }

        // renders the widget tree and emits the painted signal
        void paint() noexcept
        {
            if (graphics_ == nullptr || root_ == nullptr)
            {
                return;
            }
            // the damage: the union of every widget's reported rect
            const bool requested = dirty_;  // a repaint was owed
            int l = width(), t = height(), r = -1, b = -1;
            root_->walk_damage(&l, &t, &r, &b);
            if (r < l || b < t)
            {
                // nothing reported: a full-frame repaint was requested but
                // no widget claimed damage (e.g. an app drawing outside
                // the widget tree) -- cover the whole buffer; a repaint
                // that was never owed reports an empty region instead
                if (requested)
                {
                    l = 0;
                    t = 0;
                    r = width();
                    b = height();
                }
                else
                {
                    // a repaint that was never owed: nothing was drawn,
                    // the region stays empty (a blank frame)
                    dirty_ = false;
                    damage_l_ = 0;
                    damage_t_ = 0;
                    damage_r_ = -1;
                    damage_b_ = -1;
                    return;
                }
            }
            // the repainted region starts from the surface's default
            // background: moved widgets must not leave their old pixels
            // behind on a partial repaint
            graphics_->set_damage(l, t, r, b);
            {
                auto guard = graphics_->clip_safe(l, t, r - l, b - t);
                if (guard)
                {
                    graphics_->fill(zb::ui::core::colors::White);
                }
            }
            root_->draw(*graphics_);
            graphics_->clear_damage();
            root_->walk_clear_damage();
            damage_l_ = l;
            damage_t_ = t;
            damage_r_ = r;
            damage_b_ = b;
            dirty_ = false;
            painted(data());
        }

        // true until the next paint(): the initial frame is dirty so a
        // freshly created app is rendered once before idle
        [[nodiscard]] bool is_dirty() const noexcept override { return dirty_; }

        /*
         * The region that the last paint() repainted, in pixels, in the
         * same coordinate space as the framebuffer ([0..w) x [0..h)).
         * Empty (w == 0) when nothing was repainted since the previous
         * query. Presenting shells copy only this region (batch C); a
         * repaint that reported no damage covers the full frame.
         */
        void dirty_region(int &out_x, int &out_y, int &out_w, int &out_h) const
        {
            if (damage_r_ < damage_l_ || damage_b_ < damage_t_)
            {
                out_x = out_y = out_w = out_h = 0;
                return;
            }
            out_x = damage_l_;
            out_y = damage_t_;
            out_w = damage_r_ - damage_l_;
            out_h = damage_b_ - damage_t_;
        }

    protected:
        zb::ui::core::Graphics::ptr graphics_;
        std::unique_ptr<zb::ui::Panel> root_;
        zb::ui::InputDispatcher dispatcher_;
        bool dirty_ = true;

        // half-open damage rect of the last paint(), in buffer pixels
        int damage_l_ = 0;
        int damage_t_ = 0;
        int damage_r_ = -1;
        int damage_b_ = -1;
    };
}
