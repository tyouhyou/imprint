#pragma once

#include <string>

#include "flex_panel.hpp"

namespace zb::ui
{
    /*
     * Group box container with an optional title label and border frame.
     *
     * Non-interactive container: groups child widgets visually.
     * Integer math only (embedded-safe).
     */
    class GroupBox : public FlexPanel
    {
    public:
        GroupBox();

        void set_title(const std::string &t);
        [[nodiscard]] const std::string &get_title() const { return title; }

        // natural size: default container size
        [[nodiscard]] core::imsize_t measure() const override { return {120, 80}; }

    protected:
        void draw_at(core::Graphics &area) const override;

    private:
        std::string title;
    };
}  // namespace zb::ui
