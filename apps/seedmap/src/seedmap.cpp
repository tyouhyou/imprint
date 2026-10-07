#include "seedmap.hpp"

#include <cstdio>
#include <cstring>

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "seedmap_font.gen.hpp"  // bytes_embed blob (wasm; desktop USE_TTF_RUNTIME=ON)
#endif

using namespace zb::app::seedmap;
using namespace zb::ui;

namespace
{
    constexpr int clamp_i(const int v, const int lo, const int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    core::Color tile_color(const unsigned char t)
    {
        switch (t)
        {
        case MapCanvas::wall:  return core::Color::from(0x3A, 0x3E, 0x4A);
        case MapCanvas::water: return core::Color::from(0x2C, 0x5A, 0x8C);
        case MapCanvas::door:  return core::Color::from(0xB0, 0x7A, 0x38);
        case MapCanvas::tree:  return core::Color::from(0x2A, 0x6B, 0x2F);
        default:               return core::Color::from(0xC8, 0xC2, 0xB0);
        }
    }
}

MapCanvas::MapCanvas(const int scale)
    : scale_(scale)
{
    set_size(canvas_w(), canvas_h());
    set_background_color(core::colors::Black);
}

void MapCanvas::set_tile(const int col, const int row, const unsigned char t)
{
    tiles_[row][col] = t;
}

void MapCanvas::draw_at(core::Graphics &area) const
{
    for (int r = 0; r < kRows; ++r)
    {
        for (int c = 0; c < kCols; ++c)
        {
            const int x = c * scale_;
            const int y = r * scale_;
            const core::Color col = tile_color(tiles_[r][c]);
            area.fill_rect(x, y, x + scale_ - 1, y + scale_ - 1, col);
            // a 1px darker rim so walls read as blocks
            if (tiles_[r][c] == wall || tiles_[r][c] == water)
            {
                area.draw_rect(x, y, x + scale_ - 1, y + scale_ - 1,
                               core::Color::from(0x20, 0x24, 0x2C));
            }
        }
    }
}

unsigned Seedmap::next_rand()
{
    state_ = state_ * 1103515245u + 12345u;
    return (state_ >> 8) & 0xffffffu;
}

void Seedmap::place_room(const int x, const int y, const int w, const int h)
{
    for (int r = y; r < y + h && r < MapCanvas::kRows; ++r)
    {
        for (int c = x; c < x + w && c < MapCanvas::kCols; ++c)
        {
            canvas_->set_tile(c, r, MapCanvas::floor);
        }
    }
    // doorways on two random sides
    canvas_->set_tile(x + static_cast<int>(next_rand() % static_cast<unsigned>(w)),
                      y == 0 ? 0 : y - 1, MapCanvas::door);
    canvas_->set_tile(x + w < MapCanvas::kCols ? x + w : x + w - 1,
                      y + static_cast<int>(next_rand() % static_cast<unsigned>(h)),
                      MapCanvas::door);
}

void Seedmap::carve_corridor(const int x1, const int y1, const int x2, const int y2)
{
    int x = x1;
    int y = y1;
    while (x != x2)
    {
        canvas_->set_tile(x, y, MapCanvas::floor);
        x += x < x2 ? 1 : -1;
    }
    while (y != y2)
    {
        canvas_->set_tile(x, y, MapCanvas::floor);
        y += y < y2 ? 1 : -1;
    }
    canvas_->set_tile(x, y, MapCanvas::floor);
}

void Seedmap::generate()
{
    // FNV-1a over the seed text (UTF-16 code units, fixed width) -> LCG
    state_ = 2166136261u;
    const std::u16string seed = seed_box_->get_text();
    for (const char16_t ch : seed)
    {
        state_ = (state_ ^ (static_cast<unsigned>(ch) & 0xffu)) * 16777619u;
        state_ = (state_ ^ (static_cast<unsigned>(ch) >> 8)) * 16777619u;
    }
    if (state_ == 0)
    {
        state_ = 1;
    }

    // border walls + all-water start, land is carved out
    for (int r = 0; r < MapCanvas::kRows; ++r)
    {
        for (int c = 0; c < MapCanvas::kCols; ++c)
        {
            const bool border = r == 0 || c == 0 ||
                                r == MapCanvas::kRows - 1 || c == MapCanvas::kCols - 1;
            canvas_->set_tile(c, r, border ? MapCanvas::wall : MapCanvas::water);
        }
    }

    constexpr int kRooms = 9;
    int centers[kRooms][2];
    for (int i = 0; i < kRooms; ++i)
    {
        const int w = 5 + static_cast<int>(next_rand() % 7u);
        const int h = 4 + static_cast<int>(next_rand() % 5u);
        const int x = 2 + static_cast<int>(next_rand() %
            static_cast<unsigned>(MapCanvas::kCols - w - 3));
        const int y = 2 + static_cast<int>(next_rand() %
            static_cast<unsigned>(MapCanvas::kRows - h - 3));
        place_room(x, y, w, h);
        centers[i][0] = x + w / 2;
        centers[i][1] = y + h / 2;
        if (i > 0)
        {
            carve_corridor(centers[i - 1][0], centers[i - 1][1],
                           centers[i][0], centers[i][1]);
        }
    }
    // scattered trees on floor tiles, deterministic sprinkles
    for (int i = 0; i < 40; ++i)
    {
        const int x = static_cast<int>(next_rand() % MapCanvas::kCols);
        const int y = static_cast<int>(next_rand() % MapCanvas::kRows);
        if (canvas_->tile_at(x, y) == MapCanvas::floor && next_rand() % 4u == 0u)
        {
            canvas_->set_tile(x, y, MapCanvas::tree);
        }
    }

    // the fingerprint doubles as the determinism check: same seed text,
    // same number, on every platform
    char buf[48];
    std::snprintf(buf, sizeof(buf), "MAP #%05u", state_ % 100000u);
    status_->set_text(buf);
    // tiles changed wholesale: report the damage (the Widget contract) --
    // without it the damage tracker repaints only the header label
    canvas_->mark_dirty();
}

void Seedmap::input(const zb::input::input_event &ev) noexcept
{
    if (window_)
    {
        window_->input(ev);
    }
}

void Seedmap::install_font()
{
#if defined(IMCORE_HAS_TTF_RUNTIME)
    if (zb::ui::has_font_family())
    {
        return;
    }
    // function-level static: the family must outlive every paint (the
    // showcase install_font shape)
    static const zb::ui::TtfFamily family =
        zb::ui::TtfFamily::from_memory(seedmap_font, seedmap_font_len);
    zb::ui::set_font_family(family);
#endif
}

void Seedmap::create_window(const uint32_t max_client_width,
                            const uint32_t max_client_height, void *buffer)
{
    install_font();

    window_ = zb::make_shared<CanvasWindow>();
    window_->create(max_client_width, max_client_height, buffer);

    const int w = static_cast<int>(max_client_width);
    const int h = static_cast<int>(max_client_height);
    // ui scale from the host buffer (the g2048 shape): header, hint and
    // fonts multiply by it; the classic tier keeps u == 1
    const int u = clamp_i(h / 200, 1, 8);
    const int header_h = kHeaderH * u;

    // Canvas scale from the host buffer, same derivation as life: the
    // biggest whole-cell scale that fits under the seed row. At the
    // classic 320x240 tier this is 4px tiles, canvas 256x192 at (32, 40)
    // -- byte-identical to the old hardcoded layout.
    const int fit = (w - 64) / MapCanvas::kCols < (h - header_h - 8) / MapCanvas::kRows
                        ? (w - 64) / MapCanvas::kCols
                        : (h - header_h - 8) / MapCanvas::kRows;
    scale_ = clamp_i(fit, MapCanvas::kScale, 12);

    auto &root = window_->root();
    root.set_background_color(core::Color::from(0x0d, 0x14, 0x20));

    auto seed_box = std::make_unique<TextInput>();
    seed_box->set_text("imprint");
    seed_box->set_size(90 * u, 22 * u);
    seed_box->set_position(8 * u, 8 * u);
    seed_box->set_font_size(7 * u);
    // TextInput paints only the frame -- the field is transparent, so on
    // the dark portal background the default (theme) text vanishes
    seed_box->set_text_color(core::Color::from(0xe4, 0xe6, 0xeb));
    seed_box_->set_text_gap(2);
    seed_box_ = seed_box.get();
    sub_submit_ = seed_box->submitted.subscribe([this](const std::string &) { generate(); });
    root.add_child(std::move(seed_box));

    auto draw_btn = std::make_unique<Button>();
    draw_btn->set_text("DRAW");
    draw_btn->set_text_color(core::Color::from(0xc9, 0xd3, 0xe0));
    draw_btn->set_size(48 * u, 22 * u);
    draw_btn->set_position(106 * u, 8 * u);
    draw_btn->set_font_size(7 * u);
    sub_draw_ = draw_btn->clicked.subscribe([this] { generate(); });
    root.add_child(std::move(draw_btn));

    auto status = std::make_unique<Label>();
    status->set_size(90 * u, 20 * u);
    status->set_position(162 * u, 8 * u);
    status->set_font_size(7 * u);
    status->set_text("SEED");
    status->set_text_color(core::Color::from(0xc9, 0xd3, 0xe0));
    status_ = status.get();
    root.add_child(std::move(status));

    auto canvas = std::make_unique<MapCanvas>(scale_);
    const int cw = canvas->canvas_w();
    const int ch = canvas->canvas_h();
    canvas_x_ = (w - cw) / 2;
    canvas_y_ = header_h + (h - header_h - ch > 16 ? (h - header_h - ch) / 2 - 8
                                                   : 0);
    canvas->set_position(canvas_x_, canvas_y_);
    canvas_ = canvas.get();
    root.add_child(std::move(canvas));

    // how to play: a dim line riding the map's bottom border (added after
    // the canvas, so it paints on top -- the life GEN-label pattern)
    auto hint = std::make_unique<Label>();
    hint->set_size(cw - 8 * u, 14 * u);
    hint->set_position(canvas_x_ + 4 * u, canvas_y_ + ch - 16 * u);
    hint->set_text("TYPE A SEED + DRAW: SAME SEED = SAME MAP");
    hint->set_text_color(core::Color::from(0x8a, 0x93, 0xa3));
    hint->set_font_size(7 * u);
    root.add_child(std::move(hint));

    generate();
}
