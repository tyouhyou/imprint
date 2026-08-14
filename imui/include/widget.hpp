#pragma once

#include <memory>
#include <optional>
#include <string>

#include "imcore.hpp"
#include "input.hpp"

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
     * glyph provider is either the font assigned via set_font() (USE_FONT
     * builds) or a custom one set via set_glyph_provider(); code units the
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

        Widget() = default;
        virtual ~Widget() = default;

        Widget(const Widget &) = delete;
        Widget &operator=(const Widget &) = delete;

        void set_position(const core::impoint_t &p) { position = p; }
        void set_position(const int x, const int y) { position = {x, y}; }
        [[nodiscard]] core::impoint_t get_position() const { return position; }

        void set_size(const core::imsize_t &s) { size = s; }
        void set_size(const int w, const int h) { size = {w, h}; }
        [[nodiscard]] core::imsize_t get_size() const { return size; }

        void set_visible(const bool v) { visible = v; }
        [[nodiscard]] bool is_visible() const { return visible; }

        // background
        void set_background_color(const core::Color &c) { background = c; }
        void set_background_image(const core::image_t &img) { background_image = img; }
        [[nodiscard]] bool has_background() const { return background.has_value() || background_image.has_value(); }

        // text (input is UTF-8, see docs/code-contract.md section 2)
        void set_text(const char *text);
        void set_text(const std::u16string &text) { text_ = text; }
        [[nodiscard]] const std::u16string &get_text() const { return text_; }
        void set_text_color(const core::Color &c) { text_color = c; }

        // moves the text baseline start (e.g. a checkbox labelling to the
        // right of its box); applied on top of the alignment
        void set_text_offset(const core::impoint_t &off) const { text_offset_ = off; }
        void set_h_align(const h_align a) { halign = a; }
        void set_v_align(const v_align a) { valign = a; }

        /*
         * Sets the primary glyph provider (e.g. a FreeTypeProvider).
         * Uncovered code units fall back to the built-in bitmap glyphs.
         */
        void set_glyph_provider(const zb::SharedPtr<GlyphProvider> &provider) { primary_provider_ = provider; }
#if defined(USE_FONT)
        /* convenience: wraps the font as the primary provider */
        void set_font(const Font *f)
        {
            font_ = f;
            primary_provider_ = zb::make_shared<FreeTypeProvider>(f);
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
        virtual void layout() {}

        /*
         * Tests a point given in the widget's local coordinates.
         * Virtual so widgets can restrict hit-testing (e.g. a closed
         * Dialog is not hittable).
         */
        [[nodiscard]] virtual bool hit(const int x, const int y) const;

        // widget tree
        [[nodiscard]] bool is_descendant_of(const Widget *ancestor) const;

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

        // draws the text via the primary provider with bitmap fallback
        void draw_text(core::Graphics &area) const;

        /*
         * Returns the primary glyph provider: the custom provider set via
         * set_glyph_provider(), else the FreeType wrapper of the assigned
         * font (USE_FONT), else nullptr (bitmap-only rendering).
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
        void set_focused(const bool f) { focused = f; }

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
        bool visible = true;
        bool focused = false;

        // background
        std::optional<core::Color> background;
        std::optional<core::image_t> background_image;

        // text
        std::u16string text_;
        core::Color text_color = core::colors::Black;
        // layout hint applied at draw time; widgets may update it from
        // their (const) draw path (e.g. a checkbox labelling next to its
        // box)
        mutable core::impoint_t text_offset_{0, 0};
        h_align halign = h_align::left;
        v_align valign = v_align::top;
        zb::SharedPtr<GlyphProvider> primary_provider_;
        zb::SharedPtr<BitmapProvider> bitmap_fallback_ = zb::make_shared<BitmapProvider>();
#if defined(USE_FONT)
        const Font *font_ = nullptr;
#endif
    };
}
