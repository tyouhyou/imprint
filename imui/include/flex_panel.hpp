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

        void set_direction(const flex_direction d) { direction = d; }
        void set_spacing(const int s) { spacing = s; }
        void set_padding(const int p) { padding = p; }
        void set_wrap(const bool w) { wrap = w; }

        /* flex_grow > 0 makes the child share the leftover main-axis space */
        void add_child(std::unique_ptr<Widget> child, const int flex_grow = 0)
        {
            child->parent = this;
            items.push_back({std::move(child), flex_grow});
        }

        [[nodiscard]] const std::vector<flex_item> &get_items() const { return items; }

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
