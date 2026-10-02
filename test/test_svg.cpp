#include "test.hpp"

#include "flex_panel.hpp"
#include "svg_canvas.hpp"
#include "theme.hpp"
#include "ui_builder.hpp"

using namespace zb::ui;

int test_svg()
{
    set_theme(light_theme());

    // measure follows the viewBox, 64x64 without one
    {
        SvgCanvas v;
        EXPECT(v.measure().width == 64 && v.measure().height == 64);
        v.set_view_box(0, 0, 100, 50);
        EXPECT(v.measure().width == 100 && v.measure().height == 50);
        EXPECT(v.view_w() == 100 && v.view_h() == 50);
        v.set_view_box(0, 0, 0, 0);
        EXPECT(v.measure().width == 64);
    }

    // viewBox stretch mapping: a 0..100 line spans the widget width
    {
        SvgCanvas v;
        v.set_view_box(0, 0, 100, 50);
        v.set_size(200, 100);
        SvgCanvas::Line l{0, 25, 100, 25, core::colors::Black};
        v.add_line(l);
        EXPECT(v.lines().size() == 1);
        core::Graphics g(200, 100, nullptr);
        g.fill(core::colors::White);
        v.draw(g);
        EXPECT(test::pixel_at(g, 0, 50) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 199, 50) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 100, 50) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 100, 10) == core::colors::White.pixel);
    }

    // pixel units without a viewBox, diagonal stays solid on its spine
    {
        SvgCanvas v;
        v.set_size(40, 40);
        v.add_line({0, 0, 20, 20, core::colors::Black});
        core::Graphics g(40, 40, nullptr);
        g.fill(core::colors::White);
        v.draw(g);
        EXPECT(test::pixel_at(g, 10, 10) == core::colors::Black.pixel);
    }

    // thick strokes: stroke-width scales with the viewBox and fills a
    // solid band (no double-blend seams); butt vs round caps at ends
    {
        SvgCanvas v;
        v.set_view_box(0, 0, 100, 50);
        v.set_size(100, 50);  // 1:1 scale, stroke 3 -> a 3px band
        SvgCanvas::Line butt{10, 25, 90, 25, core::colors::Black};
        butt.width = 3.0;
        v.add_line(butt);
        core::Graphics g(100, 50, nullptr);
        g.fill(core::colors::White);
        v.draw(g);
        // band spans 23.5..26.5: two solid rows, one blended fringe row
        // on each side (32bpp); binary alpha (16bpp) quantizes the
        // half-covered fringe rows to skip (the plot_aa threshold), the
        // stroke narrows to its solid core
        int solid = 0, ink = 0;
        for (int y = 0; y < 50; ++y)
        {
            const uint32_t p = test::pixel_at(g, 50, y);
            if (p != core::colors::White.pixel)
            {
                ++ink;
            }
            if (p == core::colors::Black.pixel)
            {
                ++solid;
            }
        }
#if COLOR_DEPTH == 32
        EXPECT(ink == 4);
        EXPECT(solid == 2);
#else
        EXPECT(ink == 2);
        EXPECT(solid == 2);
#endif
        EXPECT(test::pixel_at(g, 50, 25) == core::colors::Black.pixel);
        // butt cap: nothing beyond the endpoint
        EXPECT(test::pixel_at(g, 9, 25) == core::colors::White.pixel);

        v.clear_vectors();
        SvgCanvas::Line round{10, 25, 90, 25, core::colors::Black};
        round.width = 3.0;
        round.round_caps = true;
        v.add_line(round);
        core::Graphics g2(100, 50, nullptr);
        g2.fill(core::colors::White);
        v.draw(g2);
        // round cap: the disc reaches one radius past the endpoint
        EXPECT(test::pixel_at(g2, 9, 25) == core::colors::Black.pixel);
    }

    // path strokes: a straight polyline matches its line twin, and
    // translucent overdraw stays seamless through a shared joint
    {
        SvgCanvas v;
        v.set_view_box(0, 0, 100, 50);
        v.set_size(100, 50);  // 1:1 scale, stroke 3 -> a 3px band
        SvgCanvas::Path p;
        p.pts = {{10, 25}, {90, 25}};
        p.color = core::colors::Black;
        p.width = 3.0;
        v.add_path(p);
        core::Graphics g(100, 50, nullptr);
        g.fill(core::colors::White);
        v.draw(g);
        // same geometry as the line: solid band + fringe rows, butt end
        int solid = 0, ink = 0;
        for (int y = 0; y < 50; ++y)
        {
            const uint32_t px = test::pixel_at(g, 50, y);
            if (px != core::colors::White.pixel)
            {
                ++ink;
            }
            if (px == core::colors::Black.pixel)
            {
                ++solid;
            }
        }
#if COLOR_DEPTH == 32
        EXPECT(ink == 4);
#else
        EXPECT(ink == 2);
#endif
        EXPECT(solid == 2);
        // butt cap: nothing beyond the endpoint
        EXPECT(test::pixel_at(g, 9, 25) == core::colors::White.pixel);

        // closed triangle: the Z segment joins, the corners stay solid
        v.clear_vectors();
        SvgCanvas::Path tri;
        tri.pts = {{25, 10}, {40, 40}, {10, 40}};
        tri.closed = true;
        tri.color = core::colors::Black;
        tri.width = 2.0;
        v.add_path(tri);
        core::Graphics g2(100, 50, nullptr);
        g2.fill(core::colors::White);
        v.draw(g2);
        // the (25,10) apex: covered only through the closing segment's
        // join — a missing Z leaves it blank
        bool apex_ink = false;
        for (int dy = -1; dy <= 1 && !apex_ink; ++dy)
        {
            for (int dx = -1; dx <= 1 && !apex_ink; ++dx)
            {
                if (test::pixel_at(g2, 25 + dx, 10 + dy) !=
                    core::colors::White.pixel)
                {
                    apex_ink = true;
                }
            }
        }
        EXPECT(apex_ink);

        // round caps on an open polyline reach one radius past the end
        v.clear_vectors();
        SvgCanvas::Path dot;
        dot.pts = {{50, 25}};
        dot.color = core::colors::Black;
        dot.width = 4.0;
        dot.round_caps = true;  // bare M: a dot under round caps
        v.add_path(dot);
        core::Graphics g3(100, 50, nullptr);
        g3.fill(core::colors::White);
        v.draw(g3);
        EXPECT(test::pixel_at(g3, 50, 25) == core::colors::Black.pixel);

        // whole-polyline distance: a right-angle V in one translucent
        // path blends its joint once (a per-segment stroke would
        // double-blend the corner pixel darker than the arms)
        v.clear_vectors();
        SvgCanvas::Path elbow;
        elbow.pts = {{30, 10}, {50, 30}, {70, 10}};
        elbow.color = core::Color::from(0, 0, 0, 128);
        elbow.width = 6.0;
        v.add_path(elbow);
        core::Graphics g4(100, 50, nullptr);
        g4.fill(core::colors::White);
        v.draw(g4);
        // the joint (50,30): the same half-coverage tone as an arm
        // midpoint pixel row, never a stacked double blend
        EXPECT(test::pixel_at(g4, 50, 30) ==
               test::pixel_at(g4, 40, 20));
    }

    // text draws through the seam with middle anchoring
    {
        SvgCanvas v;
        v.set_size(100, 30);
        SvgCanvas::Text t;
        t.text = u"dB";
        t.x = 50;
        t.y = 20;
        t.color = core::colors::Black;
        t.has_color = true;
        t.anchor = 1;
        v.add_text(t);
        EXPECT(v.texts().size() == 1);
        core::Graphics g(100, 30, nullptr);
        g.fill(core::colors::White);
        v.draw(g);
        bool any_ink = false;
        for (int y = 0; y < 30 && !any_ink; ++y)
        {
            for (int x = 0; x < 100; ++x)
            {
                if (test::pixel_at(g, x, y) == core::colors::Black.pixel)
                {
                    any_ink = true;
                    break;
                }
            }
        }
        EXPECT(any_ink);
        v.clear_vectors();
        EXPECT(v.lines().empty() && v.texts().empty());
    }

    // builder: svg nodes materialize with viewBox + items, children consumed
    {
        ui_node doc = column({svg()});
        ui_node &s = doc.children[0];
        s.named("vu");
        s.prop("vb_x", 0LL).prop("vb_y", 0LL).prop("vb_w", 100LL).prop("vb_h", 50LL);
        s.children.push_back(svg_line(6, 38, 12, 35, "#7f9cb0"));
        s.children.push_back(svg_text(50, 48, "dB", "#dce7ee", 1));
        FlexPanel host;
        host.set_size(200, 100);
        build(host, doc);
        host.layout();
        auto *v = static_cast<SvgCanvas *>(host.find_by_id("vu"));
        EXPECT(v != nullptr);
        EXPECT(v->view_w() == 100 && v->view_h() == 50);
        EXPECT(v->lines().size() == 1);
        EXPECT(v->lines()[0].x1 == 6 && v->lines()[0].y2 == 35);
        EXPECT(v->texts().size() == 1);
        EXPECT(v->texts()[0].anchor == 1);
        EXPECT(v->texts()[0].has_color);
        // consumed, not materialized as widgets
        EXPECT(host.get_items().size() == 1);
    }

    // builder: svg_path nodes carry the flattened polyline + stroke form
    {
        ui_node doc = column({svg()});
        ui_node &s = doc.children[0];
        s.named("scope");
        s.prop("vb_x", 0LL).prop("vb_y", 0LL).prop("vb_w", 100LL).prop("vb_h", 50LL);
        s.children.push_back(svg_path("10.5,25 90,25.25", "#57f5a0"));
        FlexPanel host;
        host.set_size(200, 100);
        build(host, doc);
        host.layout();
        auto *v = static_cast<SvgCanvas *>(host.find_by_id("scope"));
        EXPECT(v != nullptr);
        EXPECT(v->paths().size() == 1);
        const SvgCanvas::Path &p = v->paths()[0];
        EXPECT(p.pts.size() == 2);
        EXPECT(p.pts[0].first == 10.5 && p.pts[1].second == 25.25);
        EXPECT(!p.closed && !p.round_caps);
        EXPECT(p.width == 1.0);
        EXPECT(p.color == core::Color::from(0x57, 0xf5, 0xa0, 255));
        // malformed pts drop the path (the shared tolerance)
        ui_node doc2 = column({svg()});
        ui_node &s2 = doc2.children[0];
        s2.named("bad");
        s2.prop("vb_w", 100LL).prop("vb_h", 50LL);
        s2.children.push_back(svg_path("10,25 90", "#57f5a0"));
        FlexPanel host2;
        host2.set_size(200, 100);
        build(host2, doc2);
        host2.layout();
        auto *v2 = static_cast<SvgCanvas *>(host2.find_by_id("bad"));
        EXPECT(v2 != nullptr);
        EXPECT(v2->paths().empty());
    }

    // static shape fills (H-6): rect box test through the viewBox,
    // ellipse implicit test, even-odd polygon with a notch, polyline
    // implicit close, degenerate radius draws nothing
    {
        SvgCanvas v;
        v.set_view_box(0, 0, 100, 50);
        v.set_size(100, 50);  // 1:1 scale
        SvgCanvas::Shape r;
        r.kind = SvgCanvas::Shape::Kind::rect;
        r.cx = 30;
        r.cy = 25;
        r.rx = 20;
        r.ry = 10;
        r.has_fill = true;
        r.fill = core::colors::Black;
        v.add_shape(r);
        EXPECT(v.shapes().size() == 1);
        core::Graphics g(100, 50, nullptr);
        g.fill(core::colors::White);
        v.draw(g);
        // the full-coverage interior spans cx-rx..cx+rx-1 (the edge
        // column lands on the shape side of the 2x2 samples)
        EXPECT(test::pixel_at(g, 30, 25) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 10, 25) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 49, 25) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 9, 25) == core::colors::White.pixel);
        EXPECT(test::pixel_at(g, 50, 25) == core::colors::White.pixel);
        EXPECT(test::pixel_at(g, 30, 15) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 30, 14) == core::colors::White.pixel);

        // ellipse fill: exact inside, the bbox corners stay blank
        v.clear_vectors();
        SvgCanvas::Shape e;
        e.kind = SvgCanvas::Shape::Kind::ellipse;
        e.cx = 50;
        e.cy = 25;
        e.rx = 20;
        e.ry = 10;
        e.has_fill = true;
        e.fill = core::colors::Black;
        v.add_shape(e);
        core::Graphics g2(100, 50, nullptr);
        g2.fill(core::colors::White);
        v.draw(g2);
        EXPECT(test::pixel_at(g2, 50, 25) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g2, 69, 25) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g2, 70, 25) == core::colors::White.pixel);
        EXPECT(test::pixel_at(g2, 50, 34) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g2, 50, 35) == core::colors::White.pixel);
        EXPECT(test::pixel_at(g2, 68, 33) == core::colors::White.pixel);
        // degenerate radius: nothing renders
        v.clear_vectors();
        e.rx = 0;
        v.add_shape(e);
        core::Graphics g3(100, 50, nullptr);
        g3.fill(core::colors::White);
        v.draw(g3);
        EXPECT(test::pixel_at(g3, 50, 25) == core::colors::White.pixel);
    }

    // even-odd polygon fill: the notch stays blank, the lobes fill;
    // an open polyline fill closes implicitly
    {
        SvgCanvas v;
        v.set_size(100, 60);
        SvgCanvas::Path poly;
        poly.pts = {{10, 10}, {40, 10}, {40, 40}, {25, 25}, {10, 40}};
        poly.closed = true;
        poly.has_fill = true;
        poly.fill = core::colors::Black;
        v.add_path(poly);
        core::Graphics g(100, 60, nullptr);
        g.fill(core::colors::White);
        v.draw(g);
        // (15,30): three crossings to the right -> inside; (25,35):
        // two crossings -> the notch is outside
        EXPECT(test::pixel_at(g, 15, 30) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 35, 30) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 25, 35) == core::colors::White.pixel);
        EXPECT(test::pixel_at(g, 25, 12) == core::colors::Black.pixel);

        // polyline (open) fills the implicitly closed region
        v.clear_vectors();
        SvgCanvas::Path tri;
        tri.pts = {{10, 10}, {40, 10}, {10, 40}};
        tri.closed = false;
        tri.has_fill = true;
        tri.fill = core::colors::Black;
        v.add_path(tri);
        core::Graphics g2(100, 60, nullptr);
        g2.fill(core::colors::White);
        v.draw(g2);
        EXPECT(test::pixel_at(g2, 15, 15) == core::colors::Black.pixel);
        // outside the implicit closing edge (the hypotenuse)
        EXPECT(test::pixel_at(g2, 35, 35) == core::colors::White.pixel);
    }

    // fill paints under stroke (SVG paint order): a filled+stroked
    // rect shows its fill inside and its stroke on the border
    {
        SvgCanvas v;
        v.set_size(100, 50);
        SvgCanvas::Shape r;
        r.cx = 30;
        r.cy = 25;
        r.rx = 20;
        r.ry = 10;
        r.has_fill = true;
        r.fill = core::colors::Black;
        r.has_stroke = true;
        r.stroke = core::colors::Red;
        r.stroke_width = 3.0;
        v.add_shape(r);
        core::Graphics g(100, 50, nullptr);
        g.fill(core::colors::White);
        v.draw(g);
        // interior = the fill; the border column rides the stroke band
        EXPECT(test::pixel_at(g, 20, 25) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 10, 25) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 30, 15) == core::colors::Red.pixel);
    }

    // builder: svg_shape nodes materialize into Shape items; a filled
    // svg_path (the polyline/polygon node form) carries the optional fill
    {
        ui_node doc = column({svg()});
        ui_node &s = doc.children[0];
        s.named("dial");
        s.prop("vb_w", 100LL).prop("vb_h", 50LL);
        s.children.push_back(svg_shape("rect", 30, 25, 20, 10));
        s.children.push_back(svg_shape("ellipse", 70, 25, 5, 5, "#ff0000",
                                       "#00ff00"));
        ui_node pn = svg_path("10,10 40,10 10,40", "", true);
        pn.prop("fill", std::string("#0000ff"));
        s.children.push_back(pn);
        FlexPanel host;
        host.set_size(200, 100);
        build(host, doc);
        host.layout();
        auto *v = static_cast<SvgCanvas *>(host.find_by_id("dial"));
        EXPECT(v != nullptr);
        EXPECT(v->shapes().size() == 2);
        EXPECT(v->shapes()[0].kind == SvgCanvas::Shape::Kind::rect);
        EXPECT(v->shapes()[0].cx == 30 && v->shapes()[0].ry == 10);
        EXPECT(v->shapes()[0].has_fill && !v->shapes()[0].has_stroke);
        EXPECT(v->shapes()[1].kind == SvgCanvas::Shape::Kind::ellipse);
        EXPECT(v->shapes()[1].has_fill && v->shapes()[1].has_stroke);
        EXPECT(v->shapes()[1].stroke ==
               core::Color::from(0, 255, 0, 255));
        EXPECT(v->paths().size() == 1);
        EXPECT(v->paths()[0].has_fill);
        EXPECT(v->paths()[0].fill == core::Color::from(0, 0, 255, 255));
        EXPECT(v->paths()[0].closed);
    }

    // DrawPath (H-6): structured commands flatten at add time into
    // per-subpath Path items; close marks closed; fill-only clears
    // the stroke
    {
        SvgCanvas v;
        v.set_size(100, 50);
        SvgCanvas::DrawPath dp;
        SvgCanvas::PathCmd m;
        m.op = SvgCanvas::PathCmd::Op::move;
        m.x = 10;
        m.y = 10;
        dp.cmds.push_back(m);
        SvgCanvas::PathCmd l1 = m;
        l1.op = SvgCanvas::PathCmd::Op::line;
        l1.x = 40;
        l1.y = 10;
        dp.cmds.push_back(l1);
        SvgCanvas::PathCmd q = m;
        q.op = SvgCanvas::PathCmd::Op::quad;
        q.x1 = 60;
        q.y1 = 30;
        q.x = 80;
        q.y = 40;
        dp.cmds.push_back(q);
        SvgCanvas::PathCmd cl = m;
        cl.op = SvgCanvas::PathCmd::Op::close;
        dp.cmds.push_back(cl);
        // a second subpath via move
        SvgCanvas::PathCmd m2 = m;
        m2.x = 5;
        m2.y = 45;
        dp.cmds.push_back(m2);
        SvgCanvas::PathCmd l2 = l1;
        l2.x = 15;
        l2.y = 45;
        dp.cmds.push_back(l2);
        dp.has_fill = true;
        dp.fill = core::colors::Black;
        dp.has_stroke = true;
        dp.stroke = core::colors::Red;
        v.add_draw_path(dp);
        EXPECT(v.paths().size() == 2);
        const SvgCanvas::Path &first = v.paths()[0];
        EXPECT(first.pts.size() >= 4);  // M + L + the quad flattening
        EXPECT(first.closed);
        EXPECT(first.pts.front().first == 10 && first.pts.front().second == 10);
        EXPECT(first.pts.back().first == 80 && first.pts.back().second == 40);
        EXPECT(first.has_fill && first.has_stroke);
        const SvgCanvas::Path &second = v.paths()[1];
        EXPECT(second.pts.size() == 2);
        EXPECT(!second.closed);
        // fill-only clears the stroke
        v.clear_vectors();
        dp.cmds.clear();
        dp.cmds.push_back(m);
        dp.cmds.push_back(l1);
        dp.has_stroke = false;
        v.add_draw_path(dp);
        EXPECT(v.paths().size() == 1);
        EXPECT(v.paths()[0].has_fill && !v.paths()[0].has_stroke);
        // a bare move with no segments drops
        v.clear_vectors();
        dp.cmds.clear();
        dp.cmds.push_back(m);
        v.add_draw_path(dp);
        EXPECT(v.paths().empty());
    }

    // canvas transform (H-6): identity renders byte-identically,
    // translate shifts, scale widens, rotation routes through the
    // even-odd fallback
    {
        const auto render = [](SvgCanvas &v) {
            core::Graphics g(100, 50, nullptr);
            g.fill(core::colors::White);
            v.draw(g);
            std::vector<uint32_t> out(static_cast<size_t>(100) * 50);
            for (int y = 0; y < 50; ++y)
            {
                for (int x = 0; x < 100; ++x)
                {
                    out[static_cast<size_t>(y) * 100 + x] =
                        test::pixel_at(g, x, y);
                }
            }
            return out;
        };
        const auto at = [](const std::vector<uint32_t> &px, const int x,
                           const int y) {
            return px[static_cast<size_t>(y) * 100 + x];
        };
        const auto make = [] {
            auto c = std::make_unique<SvgCanvas>();
            c->set_size(100, 50);
            SvgCanvas::Shape box;
            box.cx = 20;
            box.cy = 20;
            box.rx = 10;
            box.ry = 10;
            box.has_fill = true;
            box.fill = core::colors::Black;
            c->add_shape(box);
            return c;
        };
        const auto base = render(*make());

        // identity: byte-identical to no transform
        {
            auto vid = make();
            vid->set_transform(0, 0, 0, 1, 1);
            EXPECT(!vid->transform_active());
            EXPECT(render(*vid) == base);
        }

        // translate shifts the fill
        {
            auto vt = make();
            vt->set_transform(10, 0);
            EXPECT(vt->transform_active());
            const auto tr = render(*vt);
            // the box moved to [20,40]: far interior ink, the old
            // center and the far edge blank
            EXPECT(at(tr, 38, 20) == core::colors::Black.pixel);
            EXPECT(at(tr, 15, 20) == core::colors::White.pixel);
            EXPECT(at(tr, 45, 20) == core::colors::White.pixel);
        }

        // scale doubles the half-extent (10 -> 20: interior to 39)
        {
            auto vs = make();
            vs->set_transform(0, 0, 0, 2, 2);
            const auto sc = render(*vs);
            // the center doubles too: the box spans [20,60]
            EXPECT(at(sc, 39, 20) == core::colors::Black.pixel);
            EXPECT(at(sc, 59, 20) == core::colors::Black.pixel);
            EXPECT(at(sc, 75, 20) == core::colors::White.pixel);
        }

        // rotate 90° (visual clockwise on y-down) about the origin,
        // then translate back on-canvas: the box spans x 0..20,
        // y 10..30
        {
            auto vr = make();
            vr->set_transform(30, 0, 90);
            const auto ro = render(*vr);
            EXPECT(at(ro, 5, 20) == core::colors::Black.pixel);
            EXPECT(at(ro, 25, 20) == core::colors::White.pixel);
            EXPECT(at(ro, 10, 40) == core::colors::White.pixel);
        }
    }

    return test::report("svg");
}
