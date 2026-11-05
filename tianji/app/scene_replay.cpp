#include "scene_replay.hpp"

#include "logging.hpp"

namespace zb::app::tianji
{
    namespace
    {
        // the unchecked-narrowing guard (the TJ-NEW/TJ-C2 precedent):
        // wire integers land in canvas coordinates saturated, then
        // clamp into the canvas
        int clamp_canvas(const long long v, const int hi)
        {
            long long c = v;
            if (c > hi)
            {
                c = hi;
            }
            if (c < 0)
            {
                c = 0;
            }
            return static_cast<int>(c);
        }

        core::Color parse_color(const std::string &hex)
        {
            // the schema's "#rrggbb"; unparseable -> the theme blue
            if (hex.size() == 7 && hex[0] == '#')
            {
                const auto nib = [](const char c) -> int
                {
                    if (c >= '0' && c <= '9')
                    {
                        return c - '0';
                    }
                    if (c >= 'a' && c <= 'f')
                    {
                        return c - 'a' + 10;
                    }
                    if (c >= 'A' && c <= 'F')
                    {
                        return c - 'A' + 10;
                    }
                    return -1;
                };
                const int r0 = nib(hex[1]);
                const int r1 = nib(hex[2]);
                const int g0 = nib(hex[3]);
                const int g1 = nib(hex[4]);
                const int b0 = nib(hex[5]);
                const int b1 = nib(hex[6]);
                if (r0 >= 0 && r1 >= 0 && g0 >= 0 && g1 >= 0 && b0 >= 0 &&
                    b1 >= 0)
                {
                    return core::Color::from(
                        static_cast<uint8_t>(r0 * 16 + r1),
                        static_cast<uint8_t>(g0 * 16 + g1),
                        static_cast<uint8_t>(b0 * 16 + b1), 255);
                }
            }
            return core::Color::from(0, 80, 200, 255);
        }
    }  // namespace

    bool parse_stage(const zb::tianji::Json &msg, StageState &out)
    {
        const zb::tianji::Json *sv = msg.find("stage_view");
        if (sv == nullptr || sv->type() != zb::tianji::Json::Type::Object)
        {
            return false;
        }
        StageState s;
        s.scene_id = sv->get_str("scene_id");
        s.revision = sv->get_int("revision");
        if (const zb::tianji::Json *props = sv->find("props"))
        {
            for (const zb::tianji::Json &p : props->items())
            {
                StageProp prop;
                prop.id = p.get_str("id");
                prop.x = clamp_canvas(p.get_int("x"), kStageWidth);
                prop.y = clamp_canvas(p.get_int("y"), kStageHeight);
                prop.w = clamp_canvas(p.get_int("w"), kStageWidth);
                prop.h = clamp_canvas(p.get_int("h"), kStageHeight);
                prop.color = parse_color(p.get_str("color"));
                s.props.push_back(std::move(prop));
            }
        }
        if (const zb::tianji::Json *overlays = sv->find("overlays"))
        {
            for (const zb::tianji::Json &o : overlays->items())
            {
                StageOverlay ov;
                ov.id = o.get_str("id");
                ov.x = clamp_canvas(o.get_int("x"), kStageWidth);
                ov.y = clamp_canvas(o.get_int("y"), kStageHeight);
                ov.r = clamp_canvas(o.get_int("r"), kStageHeight);
                ov.color = parse_color(o.get_str("color"));
                s.overlays.push_back(std::move(ov));
            }
        }
        out = std::move(s);
        return true;
    }

    void compose_stage(const StageState &state, std::vector<core::Color> &buf)
    {
        buf.assign(static_cast<size_t>(kStageWidth) * kStageHeight,
                   core::Color::from(34, 38, 46, 255));
        core::Graphics g(kStageWidth, kStageHeight, buf.data());
        // the props: solid rects (sprite art is the FI-04 ask); the
        // corner form is inclusive, so a w/h rect spans [x, x+w)
        for (const StageProp &p : state.props)
        {
            if (p.w > 0 && p.h > 0)
            {
                g.fill_rect(p.x, p.y, p.x + p.w - 1, p.y + p.h - 1,
                            p.color);
            }
        }
        // the overlays: the schema's circles (圈画 preview), 2px ring
        for (const StageOverlay &o : state.overlays)
        {
            g.draw_circle(o.x, o.y, o.r, o.color);
        }
    }
}  // namespace zb::app::tianji
