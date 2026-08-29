#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include "imcore.hpp"
#include "input.hpp"
#include "theme.hpp"

#if defined(USE_FONT)
#include "text/font.hpp"
#endif
#include "text/bitmap_provider.hpp"

namespace zb::ui
{
    /*
     * Base class of a widget.
     *
     * Geometry:
     *   - position and size are relative to the parent widget (or to the
     *     window for the root widget)
     *   - draw() clips to the widget's own area and calls draw_at() with a
     *     Graphics whose origin is the widget's top-left corner; drawing
     *     outside the area is clipped away
     *   - hit() tests a point given in the widget's local coordinates
     *
     * Background: optional color and/or image (the image wins over the
     * color). Drawn by the base class before draw_at().
     *
     * Text: always rendered (UTF-8 input, UTF-16 storage). The primary
     * glyph provider is the font assigned via set_font() (USE_FONT
     * builds), a custom one set via set_glyph_provider(), or the
     * process-wide default (set_default_glyph_provider); code units the
     * primary provider does not cover fall back to the built-in 5x7
     * bitmap glyphs, and code units nothing covers are skipped (see
     * docs/code-contract.md section 2.4). draw_at() defaults to drawing
     * the text, so a widget that just shows background and text needs no
     * override (Label).
     *
     * A widget is a leaf primitive. Widgets that hold children (containers)
     * keep the child list themselves and render children inside draw_at()
     * by calling child->draw().
     */
    class Widget
    {
        friend class InputDispatcher;
        friend class Panel;
        friend class FlexPanel;
        friend class Dialog;

    public:
        enum class h_align
        {
            left,
            center,
            right
        };

        enum class v_align
        {
            top,
            center,
            bottom
        };

        Widget();
        virtual ~Widget() = default;

        Widget(const Widget &) = delete;
        Widget &operator=(const Widget &) = delete;

        void set_position(const core::impoint_t &p)
        {
            mark_dirty();     // old area
            position = p;
            mark_dirty();     // new area
        }
        void set_position(const int x, const int y)
        {
            mark_dirty();
            position = {x, y};
            mark_dirty();
        }
        [[nodiscard]] core::impoint_t get_position() const { return position; }

        void set_size(const core::imsize_t &s)
        {
            mark_dirty();
            size = s;
            size_explicit_w_ = true;
            size_explicit_h_ = true;
            mark_dirty();
            mark_layout_dirty();
        }
        void set_size(const int w, const int h)
        {
            mark_dirty();
            size = {w, h};
            size_explicit_w_ = true;
            size_explicit_h_ = true;
            mark_dirty();
            mark_layout_dirty();
        }
        [[nodiscard]] core::imsize_t get_size() const { return size; }

        /*
         * Natural (content-derived) size of the widget, used by flex
         * layouts for children that were never explicitly sized
         * (set_size). The default returns the current size; widgets with
         * intrinsic geometry (Label, Checkbox, Slider, ListBox, ...)
         * override it.
         */
        [[nodiscard]] virtual core::imsize_t measure() const { return size; }

        // layout-driven resize: does not mark the size as explicit, so a
        // flex-assigned size never overrides the widget's measure()
        void set_size_auto(const int w, const int h)
        {
            mark_dirty();
            size = {w, h};
            size_explicit_w_ = false;
            size_explicit_h_ = false;
            mark_dirty();
            // the layout that writes this size re-lays all children in
            // this pass, so the widget itself needs no invalidation; its
            // ancestors' layout can depend on the size, so they do
            mark_layout_dirty(false);
        }
        // per-axis variants for flex layouts: growing a child along the
        // main axis must not discard an explicit cross-axis size (the
        // two-axis set_size_auto clears both flags)
        void set_width_auto(const int w)
        {
            mark_dirty();
            size.width = w;
            size_explicit_w_ = false;
            mark_dirty();
            mark_layout_dirty(false);
        }
        void set_height_auto(const int h)
        {
            mark_dirty();
            size.height = h;
            size_explicit_h_ = false;
            mark_dirty();
            mark_layout_dirty(false);
        }
        [[nodiscard]] virtual bool is_size_explicit() const { return size_explicit_w_ || size_explicit_h_; }
        [[nodiscard]] bool is_width_explicit() const { return size_explicit_w_; }
        [[nodiscard]] bool is_height_explicit() const { return size_explicit_h_; }

        void set_visible(const bool v)
        {
            mark_dirty();
            visible = v;
        }
        [[nodiscard]] bool is_visible() const { return visible; }

        // visible and every ancestor visible: the state keyboard focus
        // and active presses must respect (a widget inside a hidden
        // dialog is not effectively visible even though its own flag
        // is still set)
        [[nodiscard]] bool is_effectively_visible() const
        {
            for (const Widget *w = this; w != nullptr; w = w->parent)
            {
                if (!w->is_visible())
                {
                    return false;
                }
            }
            return true;
        }

        /*
         * Identity for host/designer-side references and event wiring.
         * Optional: ids must be unique within the widget tree that uses
         * them; the framework itself never depends on them.
         */
        void set_id(const char *id) { id_ = id; }
        void set_id(const std::string &id) { id_ = id; }
        [[nodiscard]] const std::string &get_id() const { return id_; }

        /*
         * Damage reporting (region invalidation, see docs/code-contract.md
         * section 3.2). Every state setter marks the widget automatically,
         * and the layout containers mark themselves, so application code
         * never calls these. Widget authors call mark_dirty() from their
         * own state setters (the same obligation as implementing draw_at).
         *
         * The no-argument form reports the widget's own bounds; the rect
         * form reports an absolute-screen rectangle (the widget still has
         * to be within the tree -- damage is collected at paint time by
         * walking the tree, never through the pointer).
         *
         * A widget with a zero-size area reports nothing (no visible
         * damage); a repaint requested while not a single widget reported
         * damage falls back to a full-frame repaint.
         */
        void mark_dirty() { mark_dirty_rect(get_absolute_position().x, get_absolute_position().y, size.width, size.height); }
        void mark_dirty(const int x, const int y, const int w, const int h) { mark_dirty_rect(x, y, w, h); }
        [[nodiscard]] bool is_dirty() const { return dirty_; }
        // this widget or any descendant reported damage since the last
        // paint (the window's repaint-owed source, see CanvasWindow)
        [[nodiscard]] bool is_subtree_dirty() const { return subtree_dirty_; }
        // aggregate reads the damage rect and clears it in one pass
        void take_dirty(int *out_l, int *out_t, int *out_r, int *out_b)
        {
            if (dirty_)
            {
                *out_l = std::min(*out_l, dirty_l_);
                *out_t = std::min(*out_t, dirty_t_);
                *out_r = std::max(*out_r, dirty_r_);
                *out_b = std::max(*out_b, dirty_b_);
            }
        }
        void clear_dirty() { dirty_ = false; }

        /*
         * Damage walk: aggregates this widget's and every descendant's
         * reported rect into the union given in (l, t, r, b), consuming
         * each rect as it is read, then recomputes the subtree-pending
         * flag bottom-up. Damage reported after this walk (e.g. by a
         * setter firing during the draw) survives into the next frame.
         * Called once by the window at paint time.
         */
        void walk_damage(int *out_l, int *out_t, int *out_r, int *out_b);
        // resets every reported rect and pending flag in the subtree
        void walk_clear_damage();

        // background
        void set_background_color(const core::Color &c)
        {
            background = c;
            mark_dirty();
        }
        void set_background_image(const core::image_t &img)
        {
            background_image = img;
            mark_dirty();
        }
        [[nodiscard]] bool has_background() const { return background.has_value() || background_image.has_value(); }

        // text (input is UTF-8, see docs/code-contract.md section 2)
        void set_text(const char *text);
        void set_text(const std::u16string &text)
        {
            text_ = text;
            advance_cache_ = -1;
            mark_dirty();
            mark_layout_dirty();
        }
        [[nodiscard]] const std::u16string &get_text() const { return text_; }
        // per-widget override of the theme's `text` token (contract 10.3)
        void set_text_color(const core::Color &c)
        {
            text_color_ = c;
            mark_dirty();
        }

        // moves the text baseline start (e.g. a checkbox labelling to the
        // right of its box); applied on top of the alignment. Set when
        // the geometry that determines it changes (ctor/setters), never
        // from the const draw path
        void set_text_offset(const core::impoint_t &off) { text_offset_ = off; }
        void set_h_align(const h_align a)
        {
            halign = a;
            mark_dirty();
        }
        void set_v_align(const v_align a)
        {
            valign = a;
            mark_dirty();
        }

        /*
         * Sets the primary glyph provider (e.g. a FreeTypeProvider).
         * Uncovered code units fall back to the built-in bitmap glyphs.
         */
        void set_glyph_provider(const zb::SharedPtr<GlyphProvider> &provider)
        {
            primary_provider_ = provider;
            advance_cache_ = -1;
            mark_dirty();
            mark_layout_dirty();
        }
#if defined(USE_FONT)
        /* convenience: wraps the font as the primary provider */
        void set_font(const Font *f)
        {
            mark_dirty();
            font_ = f;
            primary_provider_ = zb::make_shared<FreeTypeProvider>(f);
            advance_cache_ = -1;
            mark_layout_dirty();
        }
#endif

        /*
         * Renders the widget: clips to the widget's own area, draws the
         * background (color then image), then the foreground (draw_at).
         */
        void draw(core::Graphics &g) const;

        /*
         * Recomputes the position of the widget's children (if any).
         * Leaf widgets do nothing. Called after the widget tree is set up
         * or when child geometry changes.
         */
        virtual void layout() { clear_layout_dirty(); }

        /*
         * Layout invalidation (batch J5): the geometry/content setters
         * flag this widget and its ancestors; a host that enables
         * automatic layout (CanvasWindow::set_auto_layout) runs the root
         * layout from paint(). The flag is cleared at the end of every
         * layout pass, so a pending layout runs at most once per paint.
         */
        void mark_layout_dirty(const bool include_self = true)
        {
            Widget *w = include_self ? this : parent;
            for (; w != nullptr; w = w->parent)
            {
                w->layout_dirty_ = true;
            }
        }
        [[nodiscard]] bool is_layout_dirty() const { return layout_dirty_; }
        void clear_layout_dirty() { layout_dirty_ = false; }

        /*
         * Tests a point given in the widget's local coordinates.
         * Virtual so widgets can restrict hit-testing (e.g. a closed
         * Dialog is not hittable).
         */
        [[nodiscard]] virtual bool hit(const int x, const int y) const;

        // widget tree
        [[nodiscard]] bool is_descendant_of(const Widget *ancestor) const;

        /*
         * First descendant (or this widget) with the given id; nullptr
         * when missing. Linear search: id lookup is for wiring and
         * debugging, not for hot paths.
         */
        Widget *find_by_id(const std::string &id);

        // containers report true so host code can adapt add_child() (see
        // FlexPanel); leaves are false
        [[nodiscard]] virtual bool is_flex_container() const { return false; }

        /*
         * Absolute position in the widget tree (the root's offset is the
         * origin of the coordinate system used by input events).
         */
        [[nodiscard]] core::impoint_t get_absolute_position() const
        {
            core::impoint_t p{0, 0};
            for (const Widget *w = this; w != nullptr; w = w->parent)
            {
                p.x += w->position.x;
                p.y += w->position.y;
            }
            return p;
        }

        // focus
        [[nodiscard]] bool is_focused() const { return focused; }
        [[nodiscard]] virtual bool is_focusable() const { return false; }

    protected:
        /*
         * Draws the widget's foreground; the origin of `area` is the
         * widget's top-left corner and drawing is clipped to the widget's
         * own area. The default foreground is the text.
         */
        virtual void draw_at(core::Graphics &area) const { draw_text(area); }

        // draws the background (color then image); called by draw()
        void draw_background(core::Graphics &area) const;

        /*
         * Effective text color (theme contract 10.3): the per-widget
         * override when set, else the active theme's `text` token,
         * resolved at draw time so theme switches recolor live trees.
         */
        [[nodiscard]] core::Color effective_text_color() const
        {
            return text_color_.value_or(theme().text);
        }

        // draws the text via the primary provider with bitmap fallback
        void draw_text(core::Graphics &area) const;

        // text metrics over the whole text_, split into covered runs
        [[nodiscard]] int text_advance() const;
        [[nodiscard]] int text_height() const;
        // line metrics helpers (do not scan the string)
        [[nodiscard]] int text_ascent() const;

        /*
         * Advance width of an arbitrary UTF-16 run (not just text_),
         * split into covered runs like text_advance(). Text-hosting
         * widgets (TextInput) use it for caret math.
         */
        [[nodiscard]] int advance_of(const char16_t *data, const int len) const;

        /*
         * Draws an arbitrary UTF-16 run (not just text_) with the
         * baseline of the first (only) line at (x, y). Text-hosting
         * widgets (TextInput) render their own buffer with it.
         */
        void draw_text_at(core::Graphics &area, const char16_t *data, const int len,
                          const int x, const int y) const;

        /*
         * Returns the primary glyph provider: the custom provider set via
         * set_glyph_provider(), else the FreeType wrapper of the assigned
         * font (USE_FONT), else the process-wide default
         * (set_default_glyph_provider), else nullptr (bitmap-only
         * rendering).
         */
        [[nodiscard]] const GlyphProvider *primary_provider() const;

        /*
         * Input handling, driven by the input dispatcher. The event
         * coordinates refer to the root of the widget tree, but a widget
         * only receives events that hit its own area (the dispatcher
         * guarantees this).
         *
         * Return true to claim the event (the dispatcher then tracks the
         * widget as the pressed target until the release).
         */
        virtual bool on_input(const input::input_event &ev) { (void)ev; return false; }

        // called when the pressed target leaves its area while held
        virtual void on_cancel() {}

        /*
         * Pointer capture (drag semantics): when true, the dispatcher
         * delivers every move to this widget while it is the pressed
         * target and never cancels the press, no matter how far the
         * pointer leaves its area (e.g. a slider being dragged). The
         * return value of on_input() then reports whether the widget
         * changed (repaint gate). Default: false, the press is cancelled
         * by the standard slop rule.
         */
        [[nodiscard]] virtual bool captures_pointer() const { return false; }

        /*
         * Returns the deepest interactive child hit by a point given in
         * this widget's local coordinates, or nullptr.
         */
        virtual Widget *pick(const int x, const int y) { (void)x; (void)y; return nullptr; }

        /*
         * Tree traversal, used by the input dispatcher for focus cycling.
         * Containers override these.
         */
        virtual size_t child_count() const { return 0; }
        virtual Widget *child_at(const size_t i) { (void)i; return nullptr; }

        // set by the input dispatcher when the widget gains/loses focus
        void set_focused(const bool f)
        {
            if (focused != f)
            {
                mark_dirty();
            }
            focused = f;
        }

        // called when the focused widget is activated (Enter/Space)
        virtual void on_activate() {}

        /*
         * Group selection notification: called by a widget on its
         * same-parent siblings when it joined a selection group (the
         * group id and the selected widget). RadioButton uses it to
         * unselect itself when another member of its group is chosen;
         * the base does nothing.
         */
        virtual void on_group_selected(const int group, const Widget *selected)
        {
            (void)group;
            (void)selected;
        }

        /*
         * Notifies every same-parent sibling (except this) that this
         * widget joined a selection group. Sits here (rather than in
         * RadioButton) because only the class itself may reach the
         * protected child walk through a base pointer.
         */
        void notify_siblings_group_selection(const int group, const Widget *selected)
        {
            if (parent != nullptr)
            {
                for (size_t i = 0; i < parent->child_count(); ++i)
                {
                    if (auto *sibling = parent->child_at(i))
                    {
                        if (sibling != this)
                        {
                            sibling->on_group_selected(group, selected);
                        }
                    }
                }
            }
        }

        // set by the container that owns this widget
        Widget *parent = nullptr;

    private:
        core::impoint_t position{0, 0};
        core::imsize_t size{0, 0};
        std::string id_;  // host/designer reference handle, unused by the framework
        bool visible = true;
        bool focused = false;
        bool size_explicit_w_ = false;  // set_size marks the axes it sized
        bool size_explicit_h_ = false;
        bool layout_dirty_ = true;  // first paint lays out the tree

        // damage reporting: one unioned rect per widget, in absolute
        // coordinates; empty (dirty_ false) means "nothing reported".
        // subtree_dirty_: this widget or any descendant reported damage
        // since the last paint; the hosting window reads the root's flag
        // to know a frame is owed without walking the tree. It sits next
        // to dirty_ to fill its alignment padding (J1 size gate).
        bool dirty_ = false;
        bool subtree_dirty_ = false;
        int dirty_l_ = 0;
        int dirty_t_ = 0;
        int dirty_r_ = -1;
        int dirty_b_ = -1;

        void mark_dirty_rect(const int x, const int y, const int w, const int h)
        {
            if (w <= 0 || h <= 0)
            {
                return;
            }
            const int r = x + w;
            const int b = y + h;
            if (!dirty_)
            {
                dirty_l_ = x;
                dirty_t_ = y;
                dirty_r_ = r;
                dirty_b_ = b;
                dirty_ = true;
            }
            else
            {
                dirty_l_ = std::min(dirty_l_, x);
                dirty_t_ = std::min(dirty_t_, y);
                dirty_r_ = std::max(dirty_r_, r);
                dirty_b_ = std::max(dirty_b_, b);
            }
            // the pending flag bubbles to the root (the same path as
            // mark_layout_dirty). A set flag implies every ancestor is
            // set too -- walk_damage's recompute never clears a parent
            // while a child is pending -- so the loop stops early when
            // this widget is pending already
            if (!subtree_dirty_)
            {
                subtree_dirty_ = true;
                for (Widget *a = parent; a != nullptr; a = a->parent)
                {
                    a->subtree_dirty_ = true;
                }
            }
        }

        // background
        std::optional<core::Color> background;
        std::optional<core::image_t> background_image;

        // text
        std::u16string text_;
        // unset = follow the active theme's `text` token (contract 10.3)
        std::optional<core::Color> text_color_;
        // layout hint applied at draw time; widgets set it when the
        // geometry that determines it changes (e.g. a checkbox label
        // offset tracks box_size + text_gap)
        core::impoint_t text_offset_{0, 0};
        h_align halign = h_align::left;
        v_align valign = v_align::top;
        zb::SharedPtr<GlyphProvider> primary_provider_;
        /*
         * Fallback provider (batch J6): every widget shares one
         * process-level BitmapProvider instance instead of allocating
         * its own. BitmapProvider is stateless (zero data members), so
         * the sharing is safe; adding state to it requires unsharing
         * first. The shared instance is created before any widget and
         * never destroyed (leaked on purpose), so a widget that outlives
         * main can never touch a dead provider.
         */
        zb::SharedPtr<BitmapProvider> bitmap_fallback_;
#if defined(USE_FONT)
        const Font *font_ = nullptr;
#endif
        /*
         * text_advance() cache (batch J4): the measurement splits text_
         * into provider runs and asks each provider, which costs per
         * call; the result depends only on text_ and the glyph providers
         * (covers/measure), so every setter that changes either resets
         * the cache. advance_of() (arbitrary runs) stays uncached.
         */
        mutable int advance_cache_ = -1;
    };
}
