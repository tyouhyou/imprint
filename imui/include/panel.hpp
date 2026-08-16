#pragma once

#include <memory>
#include <vector>

#include "widget.hpp"

namespace zb::ui
{
    /*
     * Container widget.
     *
     * Holds child widgets and renders them at their positions. Children are
     * owned by the panel; the whole widget tree is destroyed when the root
     * panel is destroyed.
     *
     * A simple linear layout is applied by layout(): children are placed
     * top-to-bottom (vertical) or left-to-right (horizontal) with the given
     * padding and spacing. Child sizes are not modified by the layout.
     */
    class Panel : public Widget
    {
        friend class Dialog;

    public:
        enum class orientation
        {
            vertical,
            horizontal
        };

        Panel() = default;

        void set_orientation(const orientation o) { orient = o; }
        void set_spacing(const int s) { spacing = s; }
        void set_padding(const int p) { padding = p; }

        void add_child(std::unique_ptr<Widget> child)
        {
            child->parent = this;
            children.push_back(std::move(child));
        }

        [[nodiscard]] const std::vector<std::unique_ptr<Widget>> &get_children() const { return children; }

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

        void layout() override;

    protected:
        void draw_at(core::Graphics &area) const override;
        Widget *pick(const int x, const int y) override;
        size_t child_count() const override { return children.size(); }
        Widget *child_at(const size_t i) override
        {
            return (i < children.size()) ? children[i].get() : nullptr;
        }

    private:
        orientation orient = orientation::vertical;
        int spacing = 0;
        int padding = 0;

        std::vector<std::unique_ptr<Widget>> children;
    };
}
