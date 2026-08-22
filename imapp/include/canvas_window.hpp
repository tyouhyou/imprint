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

        /*
         * Automatic layout (batch J5): when enabled, paint() runs the
         * root layout whenever a layout-affecting setter fired since the
         * last pass (set_size / set_text / container setters). Off by
         * default: hosts that place their widgets explicitly keep their
         * geometry untouched, and their own layout() calls stay
         * idempotent with the flag protocol. Design-file hosts
         * (ui_preview) enable it.
         */
        void set_auto_layout(const bool on) { auto_layout_ = on; }

        /*
         * Removes a child from a panel with the tree-mutation protocol
         * (batch J3): the widget is evicted from the input dispatcher
         * first (active press cancelled, held focus released, hosted
         * modal dropped), then ownership moves to the caller. Use this
         * for trees that ever saw input; Panel::remove_child is the
         * direct, coordination-free call.
         */
        std::unique_ptr<zb::ui::Widget> remove_from(zb::ui::Panel &panel, zb::ui::Widget *w)
        {
            dispatcher_.evict(w);
            return panel.remove_child(w);
        }

        // removes and destroys every root child (dispatcher evicted first)
        void clear_root_children()
        {
            dispatcher_.evict(root_.get());
            root_->clear_children();
        }

        // renders the widget tree and emits the painted signal
        void paint() noexcept
        {
            if (graphics_ == nullptr || root_ == nullptr)
            {
                return;
            }
            // a pending layout runs before the damage walk when automatic
            // layout is enabled (set_auto_layout); the walk then collects
            // the geometry changes the layout just made (order: layout ->
            // damage -> draw, see docs/code-contract.md 5)
            if (auto_layout_ && root_->is_layout_dirty())
            {
                root_->layout();
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
            // walk_damage consumed the rects as it read them; anything
            // reported during the draw stays pending for the next frame
            damage_l_ = l;
            damage_t_ = t;
            damage_r_ = r;
            damage_b_ = b;
            dirty_ = false;
            painted(data());
        }

        // true while a frame is owed: until the first paint, after input
        // changed something, after any widget setter reported damage
        // (the tree's pending flag), or after invalidate(). Idle-polling
        // shells (linux-fb, NDS) skip paint() entirely while false
        [[nodiscard]] bool is_dirty() const noexcept override
        {
            return dirty_ || (root_ != nullptr && root_->is_subtree_dirty());
        }

        /*
         * Owes a full-frame repaint. For changes the widget tree cannot
         * report (drawing outside the tree, external buffer contents);
         * widget setters do not need this -- their damage reaches
         * is_dirty() through the tree automatically.
         */
        void invalidate() noexcept { dirty_ = true; }

        /*
         * The region that the last paint() repainted, in pixels, in the
         * same coordinate space as the framebuffer ([0..w) x [0..h)).
         * Empty (w == 0) when nothing was repainted since the previous
         * query. Presenting shells copy only this region (batch C); a
         * repaint that reported no damage covers the full frame.
         */
        [[nodiscard]] bool dirty_region(int &out_x, int &out_y, int &out_w, int &out_h) const noexcept override
        {
            if (damage_r_ < damage_l_ || damage_b_ < damage_t_)
            {
                out_x = out_y = out_w = out_h = 0;
                return true;
            }
            out_x = damage_l_;
            out_y = damage_t_;
            out_w = damage_r_ - damage_l_;
            out_h = damage_b_ - damage_t_;
            return true;
        }

    protected:
        zb::ui::core::Graphics::ptr graphics_;
        std::unique_ptr<zb::ui::Panel> root_;
        zb::ui::InputDispatcher dispatcher_;
        bool dirty_ = true;
        bool auto_layout_ = false;

        // half-open damage rect of the last paint(), in buffer pixels
        int damage_l_ = 0;
        int damage_t_ = 0;
        int damage_r_ = -1;
        int damage_b_ = -1;
    };
}
