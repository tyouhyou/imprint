#include "sandbox.hpp"

#include <cstdio>

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "sandbox_font.gen.hpp"  // bytes_embed blob (wasm; desktop USE_TTF_RUNTIME=ON)
#endif

using namespace zb::app::sandbox;
using namespace zb::ui;

namespace
{
    constexpr int clamp_i(const int v, const int lo, const int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // board and material palette (warm dark brown theme, board bg first)
    const core::Color kBoard = core::Color::from(0x22, 0x1a, 0x12);
    const core::Color kWall = core::Color::from(0x5a, 0x4a, 0x3a);
    const core::Color kWallRim = core::Color::from(0x3a, 0x2e, 0x22);
    const core::Color kSand = core::Color::from(0xe0, 0xb0, 0x60);
    const core::Color kWater = core::Color::from(0x3a, 0x7a, 0xc0);
    const core::Color kOil = core::Color::from(0x5f, 0x4a, 0x28);
    const core::Color kFire = core::Color::from(0xff, 0x78, 0x30);
    const core::Color kFireCore = core::Color::from(0xff, 0xd0, 0x50);
    const core::Color kAcid = core::Color::from(0x9c, 0xe0, 0x3a);
    const core::Color kLava = core::Color::from(0xff, 0x50, 0x28);
    const core::Color kLavaCore = core::Color::from(0xff, 0xb0, 0x40);
    const core::Color kSteam = core::Color::from(0xb8, 0xc4, 0xcc);
    const core::Color kStone = core::Color::from(0x48, 0x50, 0x5a);

    core::Color material_color(const unsigned char m)
    {
        switch (m)
        {
        case SandCanvas::WALL:
            return kWall;
        case SandCanvas::SAND:
            return kSand;
        case SandCanvas::WATER:
            return kWater;
        case SandCanvas::OIL:
            return kOil;
        case SandCanvas::FIRE:
            return kFire;
        case SandCanvas::ACID:
            return kAcid;
        case SandCanvas::LAVA:
            return kLava;
        case SandCanvas::STEAM:
            return kSteam;
        case SandCanvas::STONE:
            return kStone;
        default:
            return kBoard;
        }
    }

    // chrome palette (the seq303 warm browns)
    const core::Color kAmber = core::Color::from(0xff, 0xb4, 0x54);
    const core::Color kCream = core::Color::from(0xf2, 0xe6, 0xd0);
    const core::Color kDim = core::Color::from(0x9a, 0x86, 0x6e);
    const core::Color kTrack = core::Color::from(0x4a, 0x3a, 0x2c);

    // palette row: 9 materials + CLEAR (texts and the material each paints)
    const char *const kBtnText[Sandbox::kBtnCount] = {
        "WALL", "SAND", "WATER", "OIL", "FIRE",
        "ACID", "LAVA", "STEAM", "ERASE", "CLEAR"};
    const unsigned char kBtnMat[Sandbox::kBtnCount] = {
        SandCanvas::WALL, SandCanvas::SAND, SandCanvas::WATER,
        SandCanvas::OIL,  SandCanvas::FIRE, SandCanvas::ACID,
        SandCanvas::LAVA, SandCanvas::STEAM, SandCanvas::EMPTY, 0};

    // FIRE / STEAM lifetime knobs (in generations)
    constexpr int kFireLife = 24;         // dies past this age
    constexpr int kFireWaterPenalty = 8;  // one age chunk per water contact
    constexpr int kSteamLife = 60;        // condenses past this age
}  // namespace

SandCanvas::SandCanvas(const int cell)
    : cell_(cell)
{
    set_size(canvas_w(), canvas_h());
    set_background_color(kBoard);
}

void SandCanvas::clear_all()
{
    for (int y = 0; y < kRows; ++y)
    {
        for (int x = 0; x < kCols; ++x)
        {
            cells_[y][x] = EMPTY;
            ages_[y][x] = 0;
        }
    }
    mark_dirty();
}

void SandCanvas::preload()
{
    // two wall shelves
    for (int x = 18; x < 74; ++x)
    {
        for (int y = 64; y < 67; ++y)
        {
            cells_[y][x] = WALL;
        }
    }
    for (int x = 104; x < 166; ++x)
    {
        for (int y = 88; y < 91; ++y)
        {
            cells_[y][x] = WALL;
        }
    }
    // basin walls rising from the right shelf, water held between them
    for (int y = 80; y < 88; ++y)
    {
        cells_[y][104] = WALL;
        cells_[y][105] = WALL;
        cells_[y][164] = WALL;
        cells_[y][165] = WALL;
    }
    for (int x = 106; x < 164; ++x)
    {
        for (int y = 80; y < 88; ++y)
        {
            cells_[y][x] = WATER;
        }
    }
    // a sand pyramid resting on the left shelf
    for (int r = 0; r < 9; ++r)
    {
        for (int x = 46 - r; x <= 46 + r; ++x)
        {
            cells_[63 - r][x] = SAND;
        }
    }
    // top-right staged reaction: a short ledge holds a lava blob above
    // the basin pool -- the lava creeps off the ledge, drips into the
    // water and crusts into stone within seconds of load
    for (int x = 138; x < 164; ++x)
    {
        for (int y = 40; y < 43; ++y)
        {
            cells_[y][x] = WALL;
        }
    }
    for (int x = 141; x < 151; ++x)
    {
        for (int y = 36; y < 40; ++y)
        {
            cells_[y][x] = LAVA;
        }
    }
    mark_dirty();
}

unsigned SandCanvas::cell_hash(const int x, const int y) const
{
    // deterministic integer hash over (position, frame) -- the CA's only
    // source of "randomness", identical on every platform
    return static_cast<unsigned>(x) * 73856093u ^
           static_cast<unsigned>(y) * 19349663u ^ frame_ * 83492791u;
}

bool SandCanvas::find_neighbor(const int x, const int y, const unsigned char m,
                               int &nx, int &ny) const
{
    // fixed order (up, down, left, right): the first match wins, so the
    // outcome is a function of the cell index, not of scan order
    static constexpr int kDx[4] = {0, 0, -1, 1};
    static constexpr int kDy[4] = {-1, 1, 0, 0};
    for (int i = 0; i < 4; ++i)
    {
        const int tx = x + kDx[i];
        const int ty = y + kDy[i];
        if (tx < 0 || tx >= kCols || ty < 0 || ty >= kRows)
        {
            continue;
        }
        if (cells_[ty][tx] == m)
        {
            nx = tx;
            ny = ty;
            return true;
        }
    }
    return false;
}

void SandCanvas::swap_cells(const int ax, const int ay, const int bx,
                            const int by)
{
    const unsigned char m = cells_[ay][ax];
    const unsigned char a = ages_[ay][ax];
    cells_[ay][ax] = cells_[by][bx];
    ages_[ay][ax] = ages_[by][bx];
    cells_[by][bx] = m;
    ages_[by][bx] = a;
}

void SandCanvas::set_cell(const int x, const int y, const unsigned char m)
{
    cells_[y][x] = m;
    ages_[y][x] = 0;
}

/*
 * One CA generation. DETERMINISM CONTRACT: every "random" decision below
 * comes from cell_hash() over (x, y, frame) -- no host RNG anywhere --
 * and the sweep direction derives from the frame counter alone. The whole
 * run is therefore a pure function of the input stream: the same event
 * sequence always produces the same world, byte for byte.
 */
void SandCanvas::step()
{
    for (int y = kRows - 1; y >= 0; --y)
    {
        // alternate the x sweep with frame parity + row parity; a falling
        // particle always lands in an already-scanned row, and the sweep
        // flip keeps left/right bias symmetric over time
        const bool ltr = ((frame_ + static_cast<unsigned>(y)) & 1u) == 0u;
        for (int i = 0; i < kCols; ++i)
        {
            const int x = ltr ? i : kCols - 1 - i;
            switch (cells_[y][x])
            {
            case SAND:
                do_sand(x, y);
                break;
            case WATER:
                do_liquid(x, y, true);
                break;
            case OIL:
                do_liquid(x, y, false);
                break;
            case FIRE:
                do_fire(x, y);
                break;
            case ACID:
                do_acid(x, y);
                break;
            case LAVA:
                do_lava(x, y);
                break;
            case STEAM:
                do_steam(x, y);
                break;
            default:
                break;  // EMPTY / WALL / STONE are static
            }
        }
    }
}

void SandCanvas::do_sand(const int x, const int y)
{
    // straight down first: sand swaps into air and sinks through liquids
    if (y + 1 < kRows && sinkable(cells_[y + 1][x]))
    {
        swap_cells(x, y, x, y + 1);
        return;
    }
    // else the diagonals, side order from the per-frame hash
    const bool left_first = (cell_hash(x, y) & 1u) == 0u;
    for (int s = 0; s < 2; ++s)
    {
        const int dx = (s == 0) == left_first ? -1 : 1;
        const int nx = x + dx;
        if (nx < 0 || nx >= kCols || y + 1 >= kRows)
        {
            continue;
        }
        if (sinkable(cells_[y + 1][nx]))
        {
            swap_cells(x, y, nx, y + 1);
            return;
        }
    }
    // else rests
}

void SandCanvas::do_liquid(const int x, const int y, const bool oil_swap)
{
    if (y + 1 < kRows)
    {
        // down into air/steam; water (and acid/lava, the heavier liquids)
        // additionally swap with the oil below them -- water sinks, oil
        // floats. The swap lives on this side only, so a water/oil pair
        // never negotiates it twice.
        const unsigned char below = cells_[y + 1][x];
        if (liquid_sinkable(below) || (oil_swap && below == OIL))
        {
            swap_cells(x, y, x, y + 1);
            return;
        }
        // diagonals, side order from the per-frame hash
        const bool left_first = (cell_hash(x, y) & 1u) == 0u;
        for (int s = 0; s < 2; ++s)
        {
            const int dx = (s == 0) == left_first ? -1 : 1;
            const int nx = x + dx;
            if (nx < 0 || nx >= kCols)
            {
                continue;
            }
            if (liquid_sinkable(cells_[y + 1][nx]))
            {
                swap_cells(x, y, nx, y + 1);
                return;
            }
        }
    }
    // horizontal spread: one cell per frame into air, hash side order
    const bool left_first = (cell_hash(x, y) & 2u) == 0u;
    for (int s = 0; s < 2; ++s)
    {
        const int nx = x + ((s == 0) == left_first ? -1 : 1);
        if (nx < 0 || nx >= kCols)
        {
            continue;
        }
        if (cells_[y][nx] == EMPTY)
        {
            swap_cells(x, y, nx, y);
            return;
        }
    }
}

void SandCanvas::do_fire(const int x, const int y)
{
    if (ages_[y][x] < 0xff)
    {
        ++ages_[y][x];
    }
    // expiry: a burnt-out flame leaves steam when water is near, air else
    if (ages_[y][x] > kFireLife)
    {
        int nx, ny;
        set_cell(x, y, find_neighbor(x, y, WATER, nx, ny) ? STEAM : EMPTY);
        return;
    }
    int nx, ny;
    // water contact: the water boils and the flame loses one age chunk
    // (the neighbor scan is fixed-order, so the outcome is a function of
    // the cell index)
    if (find_neighbor(x, y, WATER, nx, ny))
    {
        set_cell(nx, ny, STEAM);
        const int aged = ages_[y][x] + kFireWaterPenalty;
        ages_[y][x] = static_cast<unsigned char>(aged > 0xff ? 0xff : aged);
    }
    // hash-gated ignition of a neighboring oil film
    if (cell_hash(x, y) % 4u == 0u && find_neighbor(x, y, OIL, nx, ny))
    {
        set_cell(nx, ny, FIRE);
    }
    // occasional flicker: the flame rises into air
    if (cell_hash(x, y) % 3u == 0u && y > 0 && cells_[y - 1][x] == EMPTY)
    {
        swap_cells(x, y, x, y - 1);
    }
}

void SandCanvas::do_acid(const int x, const int y)
{
    // hash-gated corrosion: the acid eats an adjacent wall or sand grain
    // and consumes itself
    if (cell_hash(x, y) % 3u == 0u)
    {
        int nx, ny;
        if (find_neighbor(x, y, WALL, nx, ny) ||
            find_neighbor(x, y, SAND, nx, ny))
        {
            set_cell(nx, ny, EMPTY);
            set_cell(x, y, EMPTY);
            return;
        }
    }
    do_liquid(x, y, true);
}

void SandCanvas::do_lava(const int x, const int y)
{
    int nx, ny;
    // water quench: the lava solidifies, the water flashes to steam
    if (find_neighbor(x, y, WATER, nx, ny))
    {
        set_cell(x, y, STONE);
        set_cell(nx, ny, STEAM);
        return;
    }
    // lava sets neighboring oil alight
    if (find_neighbor(x, y, OIL, nx, ny))
    {
        set_cell(nx, ny, FIRE);
    }
    // viscous: the flow advances only every third frame
    if (frame_ % 3u == 0u)
    {
        do_liquid(x, y, true);
    }
}

void SandCanvas::do_steam(const int x, const int y)
{
    if (ages_[y][x] < 0xff)
    {
        ++ages_[y][x];
    }
    // condensation: old vapor disappears
    if (ages_[y][x] > kSteamLife)
    {
        set_cell(x, y, EMPTY);
        return;
    }
    // rise: straight up first, then the diagonals in hash order; the
    // bottom-up scan reaches a risen cell again this generation, so vapor
    // climbs (deterministically) faster than sand falls
    if (y > 0 && cells_[y - 1][x] == EMPTY)
    {
        swap_cells(x, y, x, y - 1);
        return;
    }
    const bool left_first = (cell_hash(x, y) & 1u) == 0u;
    for (int s = 0; s < 2; ++s)
    {
        const int nx = x + ((s == 0) == left_first ? -1 : 1);
        if (nx < 0 || nx >= kCols || y == 0)
        {
            continue;
        }
        if (cells_[y - 1][nx] == EMPTY)
        {
            swap_cells(x, y, nx, y - 1);
            return;
        }
    }
}

void SandCanvas::stamp_disc(const int col, const int row)
{
    const int r = brush_;
    for (int dy = -r; dy <= r; ++dy)
    {
        for (int dx = -r; dx <= r; ++dx)
        {
            if (dx * dx + dy * dy > r * r)
            {
                continue;
            }
            const int x = col + dx;
            const int y = row + dy;
            if (x < 0 || x >= kCols || y < 0 || y >= kRows)
            {
                continue;
            }
            // erasing clears anything; a material never paints into the
            // static solids -- remove those with ERASE deliberately
            if (material_ != EMPTY &&
                (cells_[y][x] == WALL || cells_[y][x] == STONE))
            {
                continue;
            }
            set_cell(x, y, material_);
        }
    }
}

void SandCanvas::stamp_line(const int c0, const int r0, const int c1,
                            const int r1)
{
    // Bresenham: a fast drag arrives as one long move; stamping the whole
    // run keeps the stroke gap-free
    int x0 = c0;
    int y0 = r0;
    const int dx = c1 > c0 ? c1 - c0 : c0 - c1;
    const int dy = r1 > r0 ? r1 - r0 : r0 - r1;
    const int sx = c0 < c1 ? 1 : -1;
    const int sy = r0 < r1 ? 1 : -1;
    int err = dx - dy;
    for (;;)
    {
        stamp_disc(x0, y0);
        if (x0 == c1 && y0 == r1)
        {
            break;
        }
        const int e2 = err * 2;
        if (e2 > -dy)
        {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void SandCanvas::draw_at(core::Graphics &area) const
{
    const int cw = canvas_w();
    const int ch = canvas_h();
    area.fill_rect(0, 0, cw - 1, ch - 1, kBoard);
    for (int y = 0; y < kRows; ++y)
    {
        for (int x = 0; x < kCols; ++x)
        {
            const unsigned char m = cells_[y][x];
            if (m == EMPTY)
            {
                continue;
            }
            const int px = x * cell_;
            const int py = y * cell_;
            area.fill_rect(px, py, px + cell_ - 1, py + cell_ - 1,
                           material_color(m));
            if (m == WALL)
            {
                // a 1px darker rim so walls read as blocks
                area.draw_rect(px, py, px + cell_ - 1, py + cell_ - 1,
                               kWallRim);
            }
            else if (m == FIRE || m == LAVA)
            {
                // brighter flicker core, hash-gated per cell per frame
                if (cell_hash(x, y) % 3u == 0u)
                {
                    const int cx = px + cell_ / 2;
                    const int cy = py + cell_ / 2;
                    area.fill_rect(cx, cy, cx, cy,
                                   m == FIRE ? kFireCore : kLavaCore);
                }
            }
        }
    }
}

bool SandCanvas::on_input(const zb::input::input_event &ev)
{
    // event coordinates are in root space; translate into the grid
    const core::impoint_t p = get_position();
    const int lx = ev.x - p.x;
    const int ly = ev.y - p.y;
    const int col = lx / cell_;
    const int row = ly / cell_;
    switch (ev.type)
    {
    case zb::input::input_type::touch_down:
        // claim only presses that land inside the grid
        if (lx < 0 || ly < 0 || col >= kCols || row >= kRows)
        {
            return false;
        }
        pressed_ = true;
        last_col_ = col;
        last_row_ = row;
        stamp_disc(col, row);
        mark_dirty();
        return true;
    case zb::input::input_type::touch_move:
        if (!pressed_)
        {
            return false;
        }
        stamp_line(last_col_, last_row_, col, row);
        last_col_ = col;
        last_row_ = row;
        mark_dirty();
        return true;
    case zb::input::input_type::touch_up:
        if (!pressed_)
        {
            return false;
        }
        // release ends the stroke: no stamp, just reset the run start
        pressed_ = false;
        last_col_ = -1;
        last_row_ = -1;
        return true;
    default:
        return false;
    }
}

void Sandbox::install_font()
{
#if defined(IMCORE_HAS_TTF_RUNTIME)
    if (zb::ui::has_font_family())
    {
        return;
    }
    // function-level static: the family must outlive every paint (the
    // showcase install_font shape)
    static const zb::ui::TtfFamily family =
        zb::ui::TtfFamily::from_memory(sandbox_font, sandbox_font_len);
    zb::ui::set_font_family(family);
#endif
}

void Sandbox::on_button(const int idx)
{
    if (idx == kBtnCount - 1)
    {
        // CLEAR is momentary: wipe the world, leave the selection alone
        canvas_->clear_all();
        return;
    }
    selected_ = idx;
    canvas_->set_material(kBtnMat[idx]);
    sync_palette();
}

void Sandbox::sync_palette()
{
    for (int i = 0; i < kBtnCount; ++i)
    {
        // the active material glows amber; the rest stay cream
        mat_btns_[i]->set_text_color(i == selected_ ? kAmber : kCream);
    }
}

void Sandbox::create_window(const uint32_t max_client_width,
                            const uint32_t max_client_height, void *buffer)
{
    install_font();

    window_ = zb::make_shared<CanvasWindow>();
    window_->create(max_client_width, max_client_height, buffer);

    const int w = static_cast<int>(max_client_width);
    const int h = static_cast<int>(max_client_height);
    // ui scale from the host buffer (the g2048 shape): palette, brush row
    // and fonts multiply by it; the classic tier keeps u == 1
    const int u = clamp_i(h / 240, 1, 6);
    const int header_h = kHeaderH * u;

    // cell size from the host buffer, the life derivation: the biggest
    // whole-cell size that fits under the toolbar, clamped so the fixed
    // 180x110 grid stays readable (cell 3 covers the 800x520 web tier)
    const int fit_x = (w - 24 * u) / SandCanvas::kCols;
    const int fit_y = (h - header_h - 20 * u) / SandCanvas::kRows;
    cell_ = clamp_i(fit_x < fit_y ? fit_x : fit_y,
                    SandCanvas::kMinCell, SandCanvas::kMaxCell);

    auto &root = window_->root();
    root.set_background_color(core::Color::from(0x1a, 0x13, 0x10));

    // --- material palette: one row, CLEAR rides the right end ---
    const int margin = 8 * u;
    const int gap = 4 * u;
    const int btn_w = (w - 2 * margin - (kBtnCount - 1) * gap) / kBtnCount;
    int bx = margin;
    for (int i = 0; i < kBtnCount; ++i)
    {
        auto b = std::make_unique<Button>();
        b->set_text(kBtnText[i]);
        b->set_text_color(kCream);
        b->set_size(btn_w, 22 * u);
        b->set_position(bx, 6 * u);
        b->set_font_size(7 * u);
        subs_[i] = b->clicked.subscribe([this, i]
                                        { on_button(i); });
        mat_btns_[i] = b.get();
        root.add_child(std::move(b));
        bx += btn_w + gap;
    }

    // --- brush row under the palette ---
    auto brush_tag = std::make_unique<Label>();
    brush_tag->set_text("BRUSH");
    brush_tag->set_text_color(kDim);
    brush_tag->set_size(40 * u, 12 * u);
    brush_tag->set_position(margin, 32 * u);
    brush_tag->set_font_size(7 * u);
    root.add_child(std::move(brush_tag));

    auto brush = std::make_unique<Slider>();
    brush->set_range(1, SandCanvas::kMaxBrush);
    brush->set_value(2);
    brush->set_size(120 * u, 12 * u);
    brush->set_position(margin + 46 * u, 32 * u);
    brush->set_thumb_color(kAmber);
    brush->set_track_color(kTrack);
    brush->set_thumb_width(clamp_i(6 * u, 8, 16));
    brush->set_track_height(clamp_i(4 * u, 4, 8));
    sub_brush_ = brush->changed.subscribe([this](const int v)
                                          {
                                              canvas_->set_brush(v);
                                              char buf[8];
                                              std::snprintf(buf, sizeof(buf), "%d", v);
                                              brush_val_->set_text(buf);
                                          });
    root.add_child(std::move(brush));

    auto brush_val = std::make_unique<Label>();
    brush_val->set_text("2");
    brush_val->set_text_color(kCream);
    brush_val->set_size(20 * u, 12 * u);
    brush_val->set_position(margin + 172 * u, 32 * u);
    brush_val->set_font_size(7 * u);
    brush_val_ = brush_val.get();
    root.add_child(std::move(brush_val));

    // --- the CA board, centered between the toolbar and the hint strip ---
    auto canvas = std::make_unique<SandCanvas>(cell_);
    const int cw = canvas->canvas_w();
    const int ch = canvas->canvas_h();
    canvas_x_ = (w - cw) / 2 > 0 ? (w - cw) / 2 : 0;
    const int free_h = h - header_h - 18 * u - ch;
    canvas_y_ = header_h + (free_h > 0 ? free_h / 2 : 0);
    canvas->set_position(canvas_x_, canvas_y_);
    canvas_ = canvas.get();
    root.add_child(std::move(canvas));

    // how to play: a dim line under the board
    auto hint = std::make_unique<Label>();
    const int hint_w = cw < w - 2 * margin ? cw : w - 2 * margin;
    hint->set_size(hint_w, 12 * u);
    hint->set_position((w - hint_w) / 2, h - 15 * u);
    hint->set_text("PICK A MATERIAL AND DRAW - SAME INPUT, SAME WORLD");
    hint->set_text_color(kDim);
    hint->set_font_size(7 * u);
    root.add_child(std::move(hint));

    canvas_->preload();
    sync_palette();
}

void Sandbox::paint() noexcept
{
    ++frames_;
    canvas_->set_frame(frames_);
    // one CA generation per paint frame (the life.cpp animation shape)
    canvas_->step();
    // the animation must own its repaint: without an invalidate the shell
    // stops after the tree settles and the sandbox freezes
    if (window_)
    {
        window_->invalidate();
        window_->paint();
    }
}
