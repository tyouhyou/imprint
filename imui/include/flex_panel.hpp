#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "widget.hpp"

namespace zb::ui
{
    /*
     * Flex container widget (a separate component, Panel keeps its simple
     * linear layout). Children are placed along the main axis -- row:
     * left to right, column: top to bottom -- with optional wrapping, and
     * children with a nonzero flex grow value share the leftover main-axis
     * space proportionally inside their own line.
     *
     * Sizing: a child that was never explicitly sized (set_size) is sized
     * by its natural measure() along both axes; an explicit set_size wins.
     * Cross-axis sizes are not stretched. Wrap breaks a line when the
     * fixed demands exceed the available main-axis space. Flex-assigned
     * sizes never mark a child as explicitly sized, so re-layout keeps
     * measuring.
     */
    class FlexPanel : public Widget
    {
    public:
        enum class flex_direction
        {
            row,
            column
        };

        struct flex_item
        {
            std::unique_ptr<Widget> child;
            int flex_grow = 0;
        };

        FlexPanel() = default;

        void set_direction(const flex_direction d)
        {
            direction = d;
            mark_layout_dirty();
        }
        void set_spacing(const int s)
        {
            spacing = s;
            mark_layout_dirty();
        }
        void set_padding(const int p)
        {
            padding = p;
            mark_layout_dirty();
        }
        void set_wrap(const bool w)
        {
            wrap = w;
            mark_layout_dirty();
        }

        /* flex_grow > 0 makes the child share the leftover main-axis space */
        void add_child(std::unique_ptr<Widget> child, const int flex_grow = 0)
        {
            child->parent = this;
            items.push_back({std::move(child), flex_grow});
            mark_layout_dirty();
        }

        [[nodiscard]] const std::vector<flex_item> &get_items() const { return items; }

        /*
         * Removes the child from the tree and transfers its ownership to
         * the caller; the child's parent is reset to null. Returns null
         * when `w` is not a child (no partial state).
         *
         * Tree-mutation protocol: a widget that ever reached a dispatcher
         * must be evicted first (InputDispatcher::evict /
         * CanvasWindow::remove_from, batch J3), or the dispatcher keeps a
         * dangling pointer. Direct calls are for hosts that never fed the
         * tree to a dispatcher.
         */
        std::unique_ptr<Widget> remove_child(Widget *w);

        // removes and destroys every child (parents reset first); same
        // eviction duty as remove_child
        void clear_children();

        // container marker for generic host code (ui_builder, layout)
        [[nodiscard]] bool is_flex_container() const override { return true; }

        void layout() override;

    protected:
        void draw_at(core::Graphics &area) const override;
        Widget *pick(const int x, const int y) override;
        size_t child_count() const override { return items.size(); }
        Widget *child_at(const size_t i) override
        {
            return (i < items.size()) ? items[i].child.get() : nullptr;
        }

    private:
        flex_direction direction = flex_direction::column;
        int spacing = 0;
        int padding = 0;
        bool wrap = false;

        std::vector<flex_item> items;
    };
}
