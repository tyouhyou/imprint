#include "crystal.hpp"

#include <cstdio>
#include <cstring>

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "crystal_font.gen.hpp"  // bytes_embed blob (wasm; desktop USE_TTF_RUNTIME=ON)
#endif

using namespace zb::app::crystal;
using namespace zb::ui;

namespace
{
    constexpr int clamp_i(const int v, const int lo, const int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    constexpr unsigned char uc(const int v)
    {
        return static_cast<unsigned char>(v);
    }

    // violet lab palette; crystals run white-cyan, catalyst gold
    const core::Color kTank1 = core::Color::from(0x12, 0x0a, 0x24);
    const core::Color kTank2 = core::Color::from(0x24, 0x14, 0x36);
    const core::Color kCrystalOld = core::Color::from(0x7f, 0xb8, 0xcc);
    const core::Color kCrystalNew = core::Color::from(0xea, 0xff, 0xff);
    const core::Color kCatalyst = core::Color::from(0xd9, 0xc2, 0x5a);
    const core::Color kCatalystDim = core::Color::from(0x8a, 0x7a, 0x3a);
    const core::Color kImpurity = core::Color::from(0x5a, 0x1a, 0x2a);
    const core::Color kImpurityHi = core::Color::from(0x8a, 0x2a, 0x3a);
    const core::Color kAnchor = core::Color::from(0xff, 0x4f, 0xd8);
    const core::Color kBar = core::Color::from(0x6e, 0xe7, 0xff);
    const core::Color kBarTrack = core::Color::from(0x1c, 0x12, 0x30);
    const core::Color kDim = core::Color::from(0x9a, 0x8a, 0xc0);

    // fixed impurity speks (grid coords)
    constexpr int kSpeks[5][2] = {{90, 55}, {130, 110}, {45, 40}, {175, 65}, {110, 15}};
}  // namespace

unsigned TankCanvas::hash(const int a, const int b) const
{
    unsigned h = static_cast<unsigned>(a) * 73856093u ^
                 static_cast<unsigned>(b) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

int TankCanvas::anchors_hit() const
{
    int n = 0;
    for (int i = 0; i < kAnchors; ++i)
    {
        if (anchor_[i])
        {
            ++n;
        }
    }
    return n;
}

void TankCanvas::grow_cell(const int cx, const int cy, const int cost)
{
    cells_[cy][cx] = crystal;
    born_[cy][cx] = static_cast<unsigned short>(frame_ & 0xffffu);
    budget_ -= cost;
    for (int i = 0; i < kAnchors; ++i)
    {
        const int dx = cx - kAnchorX[i];
        const int dy = cy - kAnchorY[i];
        if (dx * dx + dy * dy <= 9)
        {
            anchor_[i] = true;
        }
    }
}

void TankCanvas::set_geometry(const int ox, const int oy, const int scale,
                              const int font_px)
{
    ox_ = ox;
    oy_ = oy;
    s_ = scale;
    set_font_size(font_px);
    mark_dirty();
}

void TankCanvas::reset()
{
    frame_ = 0;
    budget_ = kBudget;
    cat_count_ = 0;
    std::memset(cells_, 0, sizeof cells_);
    std::memset(born_, 0, sizeof born_);
    for (const auto &sp : kSpeks)
    {
        cells_[sp[1]][sp[0]] = impurity;
    }
    // the seed nucleus and its first growth tip
    cells_[60][18] = crystal;
    born_[60][18] = 0;
    tips_[0] = {18, 60, 1, 0};
    tips_n_ = 1;
    for (int i = 0; i < kAnchors; ++i)
    {
        anchor_[i] = false;
    }
    last_cx_ = -1;
    mark_dirty();
}

void TankCanvas::paint_line(const int c0, const int r0, const int c1, const int r1)
{
    // Bresenham on grid cells; catalyst only lands on empty cells
    int x0 = c0, y0 = r0;
    const int dx = c1 > c0 ? c1 - c0 : c0 - c1;
    const int dy = r1 > r0 ? r1 - r0 : r0 - r1;
    const int sx = c0 < c1 ? 1 : -1;
    const int sy = r0 < r1 ? 1 : -1;
    int err = dx - dy;
    for (;;)
    {
        if (x0 >= 0 && x0 < kCols && y0 >= 0 && y0 < kRows &&
            cells_[y0][x0] == empty && cat_count_ < 500)
        {
            cells_[y0][x0] = catalyst;
            ++cat_count_;
        }
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
    mark_dirty();
}

void TankCanvas::tick()
{
    ++frame_;
    if (done() || failed())
    {
        return;
    }
    // one growth round: every live tip advances once (array order --
    // the growth is a function of the tip list, never of scan luck)
    for (int t = 0; t < tips_n_; ++t)
    {
        if (budget_ <= 0)
        {
            break;
        }
        tip &tp = tips_[t];
        // catalyst neighbors win (fixed N/E/S/W order)
        static const int kNx[4] = {0, 1, 0, -1};
        static const int kNy[4] = {-1, 0, 1, 0};
        bool moved = false;
        for (int k = 0; k < 4; ++k)
        {
            const int nx = tp.x + kNx[k];
            const int ny = tp.y + kNy[k];
            if (nx < 0 || nx >= kCols || ny < 0 || ny >= kRows ||
                cells_[ny][nx] != catalyst)
            {
                continue;
            }
            grow_cell(nx, ny, 2);  // crystallizing catalyst costs double
            tp.dx = static_cast<signed char>(kNx[k]);
            tp.dy = static_cast<signed char>(kNy[k]);
            tp.x = static_cast<short>(nx);
            tp.y = static_cast<short>(ny);
            moved = true;
            break;
        }
        if (!moved)
        {
            const int nx = tp.x + tp.dx;
            const int ny = tp.y + tp.dy;
            if (nx < 1 || nx >= kCols - 1 || ny < 1 || ny >= kRows - 1 ||
                cells_[ny][nx] != empty)
            {
                // dead end (or an impurity wall): the tip retires
                tips_[t] = tips_[tips_n_ - 1];
                --tips_n_;
                --t;
                continue;
            }
            grow_cell(nx, ny, 1);
            tp.x = static_cast<short>(nx);
            tp.y = static_cast<short>(ny);
            // branching: occasional perpendicular offspring
            if (tips_n_ < kMaxTips && (hash(t, static_cast<int>(frame_)) & 15u) == 0u)
            {
                const int bx = nx + (tp.dy != 0 ? 1 : 0);
                const int by = ny + (tp.dx != 0 ? 1 : 0);
                if (bx > 0 && bx < kCols - 1 && by > 0 && by < kRows - 1 &&
                    cells_[by][bx] == empty)
                {
                    grow_cell(bx, by, 1);
                    tips_[tips_n_] = {static_cast<short>(bx), static_cast<short>(by),
                                      static_cast<signed char>(tp.dy != 0 ? 0 : -tp.dy),
                                      static_cast<signed char>(tp.dx != 0 ? 0 : tp.dx)};
                    ++tips_n_;
                }
            }
        }
        if (done() || failed())
        {
            break;
        }
    }
    if (tips_n_ == 0 && !done() && budget_ > 0)
    {
        // every branch died short of the anchors: the run is lost even
        // with solution left
        budget_ = 0;
    }
}

bool TankCanvas::on_input(const zb::input::input_event &ev)
{
    const core::impoint_t p = get_position();
    // two virtual px per grid cell
    const int cx = clamp_i((ev.x - p.x - ox_) / (2 * s_), 0, kCols - 1);
    const int cy = clamp_i((ev.y - p.y - oy_) / (2 * s_), 0, kRows - 1);
    switch (ev.type)
    {
    case zb::input::input_type::touch_down:
        paint_line(cx, cy, cx, cy);
        last_cx_ = cx;
        last_cy_ = cy;
        return true;
    case zb::input::input_type::touch_move:
        if (last_cx_ < 0)
        {
            return false;
        }
        paint_line(last_cx_, last_cy_, cx, cy);
        last_cx_ = cx;
        last_cy_ = cy;
        return true;
    case zb::input::input_type::touch_up:
        last_cx_ = -1;
        return true;
    default:
        return false;
    }
}

void TankCanvas::draw_at(core::Graphics &area) const
{
    const core::imsize_t sz = get_size();
    const int W = sz.width;
    const int H = sz.height;
    const auto X = [&](const int v)
    { return ox_ + v * s_; };
    const auto Y = [&](const int v)
    { return oy_ + v * s_; };
    const int cell_px = 2 * s_;

    area.fill_gradient(0, 0, W - 1, H - 1, kTank1, kTank2, false);

    for (int r = 0; r < kRows; ++r)
    {
        for (int c = 0; c < kCols; ++c)
        {
            const unsigned char m = cells_[r][c];
            if (m == empty)
            {
                continue;
            }
            const int px = X(c * 2);
            const int py = Y(r * 2);
            switch (m)
            {
            case crystal:
            {
                // fresh growth is near-white, settling to blue-cyan
                const int age = static_cast<int>(frame_ & 0xffffu) - born_[r][c];
                const int t = age > 300 ? 300 : age;
                area.fill_rect(px, py, px + cell_px - 1, py + cell_px - 1,
                               core::Color::from(
                                   uc(kCrystalNew.r() + (kCrystalOld.r() - kCrystalNew.r()) * t / 300),
                                   uc(kCrystalNew.g() + (kCrystalOld.g() - kCrystalNew.g()) * t / 300),
                                   uc(kCrystalNew.b() + (kCrystalOld.b() - kCrystalNew.b()) * t / 300)));
                break;
            }
            case catalyst:
                area.fill_rect(px, py, px + cell_px - 1, py + cell_px - 1, kCatalyst);
                area.fill_rect(px, py, px, py, kCatalystDim);
                break;
            case impurity:
                area.fill_rect(px, py, px + cell_px - 1, py + cell_px - 1, kImpurity);
                area.fill_rect(px + cell_px / 2, py + cell_px / 2, px + cell_px / 2,
                               py + cell_px / 2, kImpurityHi);
                break;
            default:
                break;
            }
        }
    }

    // anchors: pulsing magenta rings; solid once connected
    for (int i = 0; i < kAnchors; ++i)
    {
        const int ax = X(kAnchorX[i] * 2);
        const int ay = Y(kAnchorY[i] * 2);
        if (anchor_[i])
        {
            area.draw_circle(ax, ay, 6 * s_, kAnchor);
            area.fill_circle(ax, ay, 2 * s_, kAnchor);
        }
        else
        {
            const unsigned h = hash(i, static_cast<int>(frame_ >> 3));
            const int rr = 5 + static_cast<int>(h % 3u);
            area.draw_circle(ax, ay, rr * s_, kAnchor);
        }
    }

    // the solution budget: the run ends when it drains
    const int bar_w = W * budget_ / kBudget;
    area.fill_rect(0, H - 6, W - 1, H - 1, kBarTrack);
    area.fill_rect(0, H - 5, bar_w, H - 2, kBar);

    char16_t hint[48];
    int n = 0;
    const char *msg = done()   ? "LATTICE COMPLETE"
                    : failed() ? "SOLUTION DEPLETED"
                               : "DRAW CATALYST LINES - ROUTE THE GROWTH";
    for (; msg[n] != 0 && n < 47; ++n)
    {
        hint[n] = static_cast<char16_t>(msg[n]);
    }
    draw_text_at(area, hint, n, X(60), Y(4), done() ? kAnchor : kDim);
}

void Crystal::install_font()
{
#if defined(IMCORE_HAS_TTF_RUNTIME)
    if (zb::ui::has_font_family())
    {
        return;
    }
    static const zb::ui::TtfFamily family =
        zb::ui::TtfFamily::from_memory(crystal_font, crystal_font_len);
    zb::ui::set_font_family(family);
#endif
}

void Crystal::sync_status()
{
    const int key = solution_key();
    if (key != last_status_)
    {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "SOLUTION %d%%   ANCHORS %d/3",
                      tank_->solution_pct(), tank_->anchors_hit());
        status_->set_text(buf);
        last_status_ = key;
    }
}

int Crystal::solution_key() const
{
    return tank_->solution_pct() * 10 + tank_->anchors_hit();
}

void Crystal::create_window(const uint32_t max_client_width,
                            const uint32_t max_client_height, void *buffer)
{
    install_font();

    window_ = zb::make_shared<CanvasWindow>();
    window_->create(max_client_width, max_client_height, buffer);

    const int w = static_cast<int>(max_client_width);
    const int h = static_cast<int>(max_client_height);
    const int u = clamp_i(h / 240, 1, 6);
    const int hud_h = 34 * u;

    auto &root = window_->root();
    root.set_background_color(core::Color::from(0x12, 0x0a, 0x24));

    auto title = std::make_unique<Label>();
    title->set_text("CRYSTAL EPOCH");
    title->set_text_color(core::Color::from(0x6e, 0xe7, 0xff));
    title->set_bold(true);
    title->set_size(150 * u, 24 * u);
    title->set_position(10 * u, 6 * u);
    title->set_font_size(9 * u);
    root.add_child(std::move(title));

    auto stat = std::make_unique<Label>();
    stat->set_text("SOLUTION 100%   ANCHORS 0/3");
    stat->set_text_color(core::Color::from(0xd9, 0xcf, 0xf0));
    stat->set_size(210 * u, 20 * u);
    stat->set_position(170 * u, 8 * u);
    stat->set_font_size(8 * u);
    status_ = stat.get();
    root.add_child(std::move(stat));

    auto reset = std::make_unique<Button>();
    reset->set_text("RESET");
    reset->set_size(56 * u, 24 * u);
    reset->set_position(w - 68 * u, 6 * u);
    reset->set_font_size(7 * u);
    reset->set_text_color(core::Color::from(0x24, 0x1b, 0x15));
    reset->set_background_color(core::Color::from(0x6e, 0xe7, 0xff));
    sub_reset_ = reset->clicked.subscribe([this]
                                          { tank_->reset(); });
    root.add_child(std::move(reset));

    auto tank = std::make_unique<TankCanvas>();
    const int ah = h - hud_h;
    const int s = clamp_i(w / TankCanvas::kVW < ah / TankCanvas::kVH
                              ? w / TankCanvas::kVW
                              : ah / TankCanvas::kVH,
                          1, 6);
    tank->set_size(w, ah);
    tank->set_position(0, hud_h);
    tank->set_geometry((w - TankCanvas::kVW * s) / 2,
                       (ah - TankCanvas::kVH * s) / 2, s, clamp_i(5 * s, 8, 30));
    tank_ = tank.get();
    root.add_child(std::move(tank));

    tank_->reset();
}
