#pragma once

/*
 * The scene-replay driver (TJ-C5): beat-driven stage states composed
 * onto the AnnotateCanvas. Application content by design — the
 * transition-animation system stays a ratified non-goal; a replay is
 * a plain list of stage states the deterministic step clock walks.
 *
 * A StageState is the v1 `stage` message's StageView slice the client
 * renders today: the background asset is the client's packed
 * placeholder (tianji has zero scene art — the FI-04 ask), props are
 * solid rects (id, rect, color), overlays are circles (the schema's
 * 圈画 preview). compose() renders one state into an 800x600 buffer
 * through the ordinary Graphics primitives — same input, same bytes.
 */

#include <cstdint>
#include <string>
#include <vector>

#include "core/color.hpp"
#include "core/graphics.hpp"

// core = zb::ui::core (the imcore namespace)
namespace core = zb::ui::core;

#include "feed/json.hpp"

namespace zb::app::tianji
{
    constexpr int kStageWidth = 800;
    constexpr int kStageHeight = 600;

    struct StageProp
    {
        std::string id;
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        core::Color color{};
    };

    struct StageOverlay
    {
        std::string id;
        int x = 0;
        int y = 0;
        int r = 0;
        core::Color color{};
    };

    struct StageState
    {
        std::string scene_id;
        long long revision = 0;
        std::vector<StageProp> props;
        std::vector<StageOverlay> overlays;
    };

    /*
     * Parses the v1 `stage` message (envelope + StageView) into a
     * state. Returns false for a message without a usable stage_view;
     * colors are the schema's "#rrggbb" (unparseable -> the theme
     * blue). Untrusted wire numbers clamp into the canvas.
     */
    bool parse_stage(const zb::tianji::Json &msg, StageState &out);

    /*
     * Renders one state into `buf` (kStageWidth x kStageHeight,
     * tightly packed) through core::Graphics: the background fill,
     * one rect per prop, one filled circle per overlay. Deterministic:
     * the same state composes the same bytes.
     */
    void compose_stage(const StageState &state, std::vector<core::Color> &buf);
}  // namespace zb::app::tianji
