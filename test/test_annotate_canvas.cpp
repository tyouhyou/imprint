/*
 * AnnotateCanvas suite (TJ-NEW, code-contract §3.6): the logical-space
 * coordinate maps (round trip through the shared ImageView letterbox),
 * capture state machine (press/move/release/on_cancel, the Slider
 * pattern), the stroke data model (inject/remove/clear, one id space),
 * and the deterministic zero-allocation draw.
 */

#include "test.hpp"

#include "test_alloc_count.hpp"

#include "imapp.hpp"
#include "imui.hpp"
#include "logging.hpp"

#include <climits>
#include <utility>
#include <vector>

using namespace zb::ui;

namespace
{
    zb::input::input_event press_at(const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = x;
        ev.y = y;
        return ev;
    }

    zb::input::input_event release_at(const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_up;
        ev.x = x;
        ev.y = y;
        return ev;
    }

    zb::input::input_event move_to(const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_move;
        ev.x = x;
        ev.y = y;
        return ev;
    }

    // a canvas fed a solid white image; ink reads against it
    void feed_white_image(AnnotateCanvas &c, const int w, const int h)
    {
        std::vector<core::Color> px(
            static_cast<size_t>(w) * static_cast<size_t>(h),
            core::colors::White);
        c.set_image(core::image_t{px.data(), w, h, 0});
    }

    // a canvas at (0,0) inside a panel root, ready for dispatched input
    struct CaptureTree
    {
        Panel root;
        AnnotateCanvas *canvas = nullptr;
        InputDispatcher dispatcher;

        CaptureTree(const int root_w, const int root_h, const int cw,
                    const int ch)
        {
            root.set_size(root_w, root_h);
            auto c = std::make_unique<AnnotateCanvas>();
            c->set_size(cw, ch);
            c->set_position(0, 0);
            canvas = c.get();
            root.add_child(std::move(c));
        }
    };

    // the committed capture payloads of one canvas
    struct CaptureLog
    {
        int fires = 0;
        std::vector<AnnotateCanvas::Stroke> strokes;

        void attach(AnnotateCanvas &c)
        {
            c.stroke_ended += [this](const AnnotateCanvas::Stroke &s)
            {
                ++fires;
                strokes.push_back(s);
            };
        }
    };
}  // namespace

int test_annotate_canvas()
{
    // canvas size: default follows the image extent, explicit wins,
    // non-positive is one LW and no change
    {
        AnnotateCanvas c;
        feed_white_image(c, 4, 2);
        EXPECT(c.canvas_size().width == 4 && c.canvas_size().height == 2);

        int warns = 0;
        zb::Logging::set_log_handle(
            [&](zb::Logging_Level, const std::string &)
            { ++warns; });
        c.set_canvas_size(0, 9);
        c.set_canvas_size(16, -1);
        c.set_canvas_size(-3, -3);
        zb::Logging::set_log_handle();
        EXPECT(warns == 3);
        EXPECT(c.canvas_size().width == 4 && c.canvas_size().height == 2);

        c.set_canvas_size(16, 9);
        EXPECT(c.canvas_size().width == 16 && c.canvas_size().height == 9);
        EXPECT(!c.annotate_enabled());
        EXPECT(c.strokes().empty());
    }

    // round-trip identity at 1:1 (image 8x8, box 8x8, canvas 8x8): the
    // injected logical point draws at its device pixel and the captured
    // device point reads back as the same logical point
    {
        AnnotateCanvas c;
        c.set_size(8, 8);
        feed_white_image(c, 8, 8);

        AnnotateCanvas::Stroke s;
        s.id = 7;
        s.color = core::colors::Red;
        s.width = 1;
        s.points = {{3, 5}, {6, 5}};
        EXPECT(c.add_stroke(std::move(s)));

        core::Graphics g(8, 8, nullptr);
        g.fill(core::colors::Black);
        c.draw(g);
        EXPECT(test::pixel_at(g, 3, 5) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 6, 5) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 2, 5) == core::colors::White.pixel);

        CaptureTree t(8, 8, 8, 8);
        feed_white_image(*t.canvas, 8, 8);
        t.canvas->set_annotate_enabled(true);
        CaptureLog log;
        log.attach(*t.canvas);
        t.dispatcher.dispatch(t.root, press_at(4, 5));
        t.dispatcher.dispatch(t.root, release_at(4, 5));

        EXPECT(log.fires == 1);
        EXPECT(log.strokes.size() == 1);
        EXPECT(log.strokes[0].points.size() == 1);
        EXPECT(log.strokes[0].points[0].first == 4);
        EXPECT(log.strokes[0].points[0].second == 5);
    }

    // round-trip identity through a 2x letterbox (image 4x2 in an 8x4
    // box): the injected logical stroke draws scaled, and the captured
    // device point maps back onto the injected logical point
    {
        AnnotateCanvas c;
        c.set_size(8, 4);
        feed_white_image(c, 4, 2);

        AnnotateCanvas::Stroke s;
        s.id = 1;
        s.color = core::colors::Red;
        s.width = 1;
        s.points = {{0, 0}, {3, 1}};
        EXPECT(c.add_stroke(std::move(s)));

        core::Graphics g(8, 4, nullptr);
        g.fill(core::colors::Black);
        c.draw(g);
        // logical (0,0) -> device (0,0), logical (3,1) -> device (6,2)
        EXPECT(test::pixel_at(g, 0, 0) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 6, 2) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 0, 3) == core::colors::White.pixel);

        CaptureTree t(8, 4, 8, 4);
        feed_white_image(*t.canvas, 4, 2);
        t.canvas->set_annotate_enabled(true);
        CaptureLog log;
        log.attach(*t.canvas);
        t.dispatcher.dispatch(t.root, press_at(6, 2));
        t.dispatcher.dispatch(t.root, release_at(6, 2));

        EXPECT(log.fires == 1);
        EXPECT(log.strokes[0].points.size() == 1);
        EXPECT(log.strokes[0].points[0].first == 3);
        EXPECT(log.strokes[0].points[0].second == 1);
    }

    // an explicit canvas size names its own logical space (16x16 over an
    // 8x8 image in an 8x8 box): the maps still round-trip
    {
        AnnotateCanvas c;
        c.set_size(8, 8);
        feed_white_image(c, 8, 8);
        c.set_canvas_size(16, 16);
        c.set_annotate_enabled(true);

        AnnotateCanvas::Stroke s;
        s.id = 3;
        s.color = core::colors::Red;
        s.width = 1;
        s.points = {{8, 8}, {15, 15}};
        EXPECT(c.add_stroke(std::move(s)));

        core::Graphics g(8, 8, nullptr);
        g.fill(core::colors::Black);
        c.draw(g);
        // logical (8,8) -> device (4,4), logical (15,15) -> device (7,7)
        EXPECT(test::pixel_at(g, 4, 4) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 7, 7) == core::colors::Red.pixel);

        CaptureTree t(8, 8, 8, 8);
        feed_white_image(*t.canvas, 8, 8);
        t.canvas->set_canvas_size(16, 16);
        t.canvas->set_annotate_enabled(true);
        CaptureLog log;
        log.attach(*t.canvas);
        t.dispatcher.dispatch(t.root, press_at(4, 4));
        t.dispatcher.dispatch(t.root, release_at(4, 4));

        EXPECT(log.fires == 1);
        EXPECT(log.strokes[0].points[0].first == 8);
        EXPECT(log.strokes[0].points[0].second == 8);
    }

    // capture sequence: press starts, moves extend (same-point samples
    // dropped), release commits and fires once; ids increase; the
    // committed stroke carries the current defaults
    {
        CaptureTree t(8, 4, 8, 4);
        feed_white_image(*t.canvas, 4, 2);
        t.canvas->set_annotate_enabled(true);
        CaptureLog log;
        log.attach(*t.canvas);

        t.dispatcher.dispatch(t.root, press_at(2, 1));  // logical (1,0)
        t.dispatcher.dispatch(t.root, move_to(3, 1));   // logical (1,0): dropped
        t.dispatcher.dispatch(t.root, move_to(5, 2));   // logical (2,1)
        t.dispatcher.dispatch(t.root, release_at(5, 2));
        t.dispatcher.dispatch(t.root, press_at(0, 0));  // logical (0,0)
        t.dispatcher.dispatch(t.root, release_at(0, 0));

        EXPECT(log.fires == 2);
        EXPECT(log.strokes.size() == 2);
        EXPECT(log.strokes[0].points.size() == 2);
        EXPECT(log.strokes[0].points[0].first == 1);
        EXPECT(log.strokes[0].points[0].second == 0);
        EXPECT(log.strokes[0].points[1].first == 2);
        EXPECT(log.strokes[0].points[1].second == 1);
        EXPECT(log.strokes[1].points.size() == 1);
        // monotonically increasing ids from one space
        EXPECT(log.strokes[0].id == 1);
        EXPECT(log.strokes[1].id == 2);
        // the defaults: theme text color, width 2
        EXPECT(log.strokes[0].width == 2);
        EXPECT(log.strokes[0].color.pixel == theme().text.pixel);
        // strokes() mirrors the committed captures
        EXPECT(t.canvas->strokes().size() == 2);
    }

    // the captured pointer follows moves far outside the widget (the
    // Slider pattern) and the samples clamp into the logical rect
    {
        CaptureTree t(8, 4, 8, 4);
        feed_white_image(*t.canvas, 4, 2);
        t.canvas->set_annotate_enabled(true);
        CaptureLog log;
        log.attach(*t.canvas);

        EXPECT(t.dispatcher.dispatch(t.root, press_at(6, 3)));  // logical (3,1)
        t.dispatcher.dispatch(t.root, move_to(100, -50));       // clamps to (3,0)
        t.dispatcher.dispatch(t.root, move_to(100, -50));       // same: dropped
        t.dispatcher.dispatch(t.root, release_at(100, -50));

        EXPECT(log.fires == 1);
        EXPECT(log.strokes[0].points.size() == 2);
        EXPECT(log.strokes[0].points[0].first == 3);
        EXPECT(log.strokes[0].points[0].second == 1);
        EXPECT(log.strokes[0].points[1].first == 3);
        EXPECT(log.strokes[0].points[1].second == 0);
    }

    // the ink sits inside the letterbox image rect: a width-1 capture
    // dragged across the whole widget draws only on the mapped rect
    // (2x2 image in a 6x4 box letterboxes to 4x4 at (1,0))
    {
        CaptureTree t(6, 4, 6, 4);
        feed_white_image(*t.canvas, 2, 2);
        t.canvas->set_background_color(core::colors::Black);
        t.canvas->set_stroke_color(core::colors::Red);
        t.canvas->set_stroke_width(1);
        t.canvas->set_annotate_enabled(true);
        CaptureLog log;
        log.attach(*t.canvas);

        t.dispatcher.dispatch(t.root, press_at(0, 2));  // clamps to (0,1)
        t.dispatcher.dispatch(t.root, move_to(5, 2));   // clamps to (1,1)
        t.dispatcher.dispatch(t.root, release_at(5, 2));

        EXPECT(log.fires == 1);
        EXPECT(log.strokes[0].points.size() == 2);
        EXPECT(log.strokes[0].points[0].first == 0);
        EXPECT(log.strokes[0].points[0].second == 1);
        EXPECT(log.strokes[0].points[1].first == 1);
        EXPECT(log.strokes[0].points[1].second == 1);

        core::Graphics g(6, 4, nullptr);
        g.fill(core::colors::Black);
        t.canvas->draw(g);
        // logical (0,1) -> device (1,2), logical (1,1) -> device (3,2)
        EXPECT(test::pixel_at(g, 1, 2) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 2, 2) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 3, 2) == core::colors::Red.pixel);
        // the margins stay the widget background (the ink cannot reach
        // them), the uninked rect stays the image
        EXPECT(test::pixel_at(g, 0, 2) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 5, 2) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 4, 2) == core::colors::White.pixel);
        EXPECT(test::pixel_at(g, 1, 1) == core::colors::White.pixel);
        EXPECT(test::pixel_at(g, 3, 3) == core::colors::White.pixel);
    }

    // the stroke data model: inject/remove/clear; a duplicate id is one
    // LW and false
    {
        AnnotateCanvas c;
        feed_white_image(c, 4, 2);

        AnnotateCanvas::Stroke a;
        a.id = 10;
        a.color = core::colors::Red;
        a.width = 1;
        a.points = {{0, 0}, {3, 0}};
        EXPECT(c.add_stroke(std::move(a)));
        EXPECT(c.strokes().size() == 1);

        int warns = 0;
        zb::Logging::set_log_handle(
            [&](zb::Logging_Level, const std::string &)
            { ++warns; });
        AnnotateCanvas::Stroke dup;
        dup.id = 10;
        dup.color = core::colors::Blue;
        dup.width = 1;
        dup.points = {{0, 1}, {3, 1}};
        EXPECT(!c.add_stroke(std::move(dup)));
        zb::Logging::set_log_handle();
        EXPECT(warns == 1);
        EXPECT(c.strokes().size() == 1);
        EXPECT(c.strokes()[0].color.pixel == core::colors::Red.pixel);

        EXPECT(c.remove_stroke(10));
        EXPECT(!c.remove_stroke(10));
        EXPECT(c.strokes().empty());

        AnnotateCanvas::Stroke b;
        b.id = 11;
        b.color = core::colors::Blue;
        b.width = 1;
        b.points = {{0, 0}, {1, 1}};
        EXPECT(c.add_stroke(std::move(b)));
        c.clear_strokes();
        EXPECT(c.strokes().empty());
        c.clear_strokes();  // silent on empty
    }

    // injected (host-forwarded) points clamp into the logical rect
    // before the device map: a remote stroke cannot escape the canvas
    // and the device product cannot narrow silently (4x2 logical over
    // an 8x4 box: INT_MAX maps to 4294967294, far past int)
    {
        AnnotateCanvas c;
        c.set_size(8, 4);
        feed_white_image(c, 4, 2);

        AnnotateCanvas::Stroke s;
        s.id = 9;
        s.color = core::colors::Red;
        s.width = 1;
        s.points = {{INT_MAX, 1}, {INT_MIN, 1}};
        EXPECT(c.add_stroke(std::move(s)));

        core::Graphics g(8, 4, nullptr);
        g.fill(core::colors::Black);
        c.draw(g);
        // both points clamp to logical x 0..3 -> device x 0..6 at y=2
        EXPECT(test::pixel_at(g, 0, 2) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 6, 2) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 7, 2) == core::colors::White.pixel);
        EXPECT(test::pixel_at(g, 3, 0) == core::colors::White.pixel);
    }

    // disabled (the default): the widget never captures; injected
    // strokes still draw (the read-only stage)
    {
        CaptureTree t(8, 4, 8, 4);
        feed_white_image(*t.canvas, 4, 2);
        CaptureLog log;
        log.attach(*t.canvas);

        AnnotateCanvas::Stroke s;
        s.id = 5;
        s.color = core::colors::Red;
        s.width = 1;
        s.points = {{0, 0}, {3, 1}};
        EXPECT(t.canvas->add_stroke(std::move(s)));

        EXPECT(!t.dispatcher.dispatch(t.root, press_at(2, 1)));
        EXPECT(!t.dispatcher.dispatch(t.root, move_to(5, 2)));
        EXPECT(!t.dispatcher.dispatch(t.root, release_at(5, 2)));
        EXPECT(log.fires == 0);
        EXPECT(t.canvas->strokes().size() == 1);

        core::Graphics g(8, 4, nullptr);
        g.fill(core::colors::Black);
        t.canvas->draw(g);
        EXPECT(test::pixel_at(g, 0, 0) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, 6, 2) == core::colors::Red.pixel);
    }

    // draw order: strokes render in insertion order, the in-progress
    // stroke last (the overlap pixel shows the later ink)
    {
        CaptureTree t(8, 8, 8, 8);
        feed_white_image(*t.canvas, 8, 8);
        t.canvas->set_stroke_color(core::colors::Red);
        t.canvas->set_annotate_enabled(true);

        AnnotateCanvas::Stroke a;
        a.id = 1;
        a.color = core::colors::Blue;
        a.width = 4;
        a.points = {{1, 2}, {6, 2}};
        EXPECT(t.canvas->add_stroke(std::move(a)));

        // capture a crossing stroke and look while it is in progress
        t.dispatcher.dispatch(t.root, press_at(3, 0));
        t.dispatcher.dispatch(t.root, move_to(3, 6));

        core::Graphics g(8, 8, nullptr);
        g.fill(core::colors::Black);
        t.canvas->draw(g);
        // the joint pixel: the in-progress red covers the injected blue
        EXPECT(test::pixel_at(g, 3, 2) == core::colors::Red.pixel);
        // blue-only and red-only pixels
        EXPECT(test::pixel_at(g, 0, 2) == core::colors::Blue.pixel);
        EXPECT(test::pixel_at(g, 3, 5) == core::colors::Red.pixel);

        t.dispatcher.dispatch(t.root, release_at(3, 6));
        // the committed capture joins the injected stroke; its id skipped
        // past the injected id 1 (one id space, no collisions)
        EXPECT(t.canvas->strokes().size() == 2);
        EXPECT(t.canvas->strokes()[1].id == 2);
        EXPECT(t.canvas->strokes()[1].points.size() == 2);

        g.fill(core::colors::Black);
        t.canvas->draw(g);
        EXPECT(test::pixel_at(g, 3, 2) == core::colors::Red.pixel);
    }

    // the same stroke set drawn twice is byte-identical
    {
        AnnotateCanvas c;
        c.set_size(8, 4);
        feed_white_image(c, 4, 2);

        AnnotateCanvas::Stroke a;
        a.id = 1;
        a.color = core::colors::Red;
        a.width = 2;
        a.points = {{0, 0}, {3, 1}};
        EXPECT(c.add_stroke(std::move(a)));
        AnnotateCanvas::Stroke b;
        b.id = 2;
        b.color = core::colors::Blue;
        b.width = 3;
        b.points = {{3, 0}, {0, 1}};
        EXPECT(c.add_stroke(std::move(b)));

        core::Graphics g1(8, 4, nullptr);
        g1.fill(core::colors::Black);
        c.draw(g1);
        std::vector<uint32_t> snapshot;
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 8; ++x)
            {
                snapshot.push_back(test::pixel_at(g1, x, y));
            }
        }

        core::Graphics g2(8, 4, nullptr);
        g2.fill(core::colors::Black);
        c.draw(g2);
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 8; ++x)
            {
                EXPECT(test::pixel_at(g2, x, y) ==
                       snapshot[static_cast<size_t>(y) * 8 +
                                static_cast<size_t>(x)]);
            }
        }
    }

    // on_cancel (the dispatcher reset path) discards the in-progress
    // stroke; the next capture continues the id sequence
    {
        CaptureTree t(8, 4, 8, 4);
        feed_white_image(*t.canvas, 4, 2);
        t.canvas->set_annotate_enabled(true);
        CaptureLog log;
        log.attach(*t.canvas);

        t.dispatcher.dispatch(t.root, press_at(2, 1));
        t.dispatcher.dispatch(t.root, move_to(5, 2));
        t.dispatcher.reset();  // delivers on_cancel to the pressed target

        EXPECT(log.fires == 0);
        EXPECT(t.canvas->strokes().empty());
        // the abandoned release delivers nothing
        EXPECT(!t.dispatcher.dispatch(t.root, release_at(5, 2)));
        EXPECT(log.fires == 0);

        // the next capture takes the next id
        t.dispatcher.dispatch(t.root, press_at(0, 0));
        t.dispatcher.dispatch(t.root, release_at(0, 0));
        EXPECT(log.fires == 1);
        EXPECT(log.strokes[0].id == 2);
        EXPECT(t.canvas->strokes().size() == 1);
    }

    // the frame path allocates nothing: the mutation paths keep the
    // device buffer reserved, so repaints of the same stroke set only
    // refill it in place
    {
        zb::app::CanvasWindow w;
        w.create(24, 24);
        auto holder = std::make_unique<AnnotateCanvas>();
        holder->set_size(8, 8);
        holder->set_position(2, 2);
        feed_white_image(*holder, 8, 8);
        AnnotateCanvas::Stroke a;
        a.id = 1;
        a.color = core::colors::Red;
        a.width = 2;
        a.points = {{1, 1}, {6, 1}, {6, 6}};
        EXPECT(holder->add_stroke(std::move(a)));
        AnnotateCanvas *canvas = holder.get();
        w.root().add_child(std::move(holder));
        w.paint();  // consume the initial frame

        test::scoped_alloc_count c;
        for (int i = 0; i < 10; ++i)
        {
            canvas->set_stroke_color(core::colors::Red);  // owed repaint
            w.paint();
        }
        EXPECT(c.delta() == 0);
    }

    return test::report("annotate");
}
