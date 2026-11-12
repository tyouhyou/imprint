#include "test.hpp"

#include "imui.hpp"

using namespace zb::ui;

namespace
{
    // measure/text metrics are protected on Widget; a probe label lifts
    // them so the H-1 suites can assert the wrapped block geometry
    struct ProbeLabel : Label
    {
        using Label::text_advance;
        using Label::text_height;
    };

    // T-1: a uniform 4px-advance provider covering every code point. The
    // 5x7 fallback leaves CJK units uncovered (zero advance), so the CJK
    // wrap suites drive the engine through this stub; the wrap rules are
    // provider-agnostic, only the widths matter here.
    struct WideStubProvider : GlyphProvider
    {
        bool covers(const char32_t) const override { return true; }
        // A-25: the stub models a font WITH a non-BMP glyph — the emoji
        // is one atomic 8px code point (measure(len 1, cp >= 0x10000) =
        // 8), like the old surrogate pair was one 8px unit
        text_metrics measure(const char32_t *s, const int len) const override
        {
            return {4 * len + (len == 1 && s[0] >= 0x10000 ? 4 : 0), 7, 7};
        }
        text_metrics line_metrics() const override { return {0, 7, 7}; }
        void write(core::Graphics &, const char32_t *, const int, const int,
                   const int, const core::Color &) const override
        {
        }
    };
}  // namespace

int test_label()
{
    // text handling (no font needed)
    {
        Label l;
        l.set_text("abc");
        EXPECT(l.get_text() == std::string("abc"));
        l.set_text(nullptr);
        EXPECT(l.get_text().empty());
        l.set_text(std::string("xyz"));
        EXPECT(l.get_text() == std::string("xyz"));
    }

    // invalid UTF-8 bytes become U+FFFD (input is UTF-8 per the contract)
    {
        Label l;
        l.set_text("\x80\xFF");
        const auto &t = l.get_text();
        // two U+FFFD, 3 UTF-8 bytes each
        EXPECT(t.size() == 6);
        EXPECT(t == "\xEF\xBF\xBD\xEF\xBF\xBD");
    }

    // multibyte UTF-8 stores as its UTF-8 bytes
    {
        Label l;
        l.set_text("A\xE4\xB8\xAD");
        const auto &t = l.get_text();
        EXPECT(t.size() == 4);  // 'A' + the 3-byte 中
        EXPECT(t == "A\xE4\xB8\xAD");
    }

    // background color: drawn once set
    {
        auto g = core::Graphics::make_ptr(8, 8);
        Label l;
        l.set_position(1, 1);
        l.set_size(4, 4);
        l.draw(*g);
        EXPECT(test::pixel_at(*g, 1, 1) == 0);  // no background

        l.set_background_color(core::colors::Red);
        l.draw(*g);
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 0, 0) == 0);
    }

    // background image: blitted at the widget origin, clipped to the area
    {
        const core::Color img_pixels[] = {
            core::Color::from(255, 0, 0),
            core::Color::from(255, 0, 0),
            core::Color::from(255, 0, 0),
            core::Color::from(255, 0, 0),
        };
        const core::image_t img{img_pixels, 2, 2, 0};

        auto g = core::Graphics::make_ptr(8, 8);
        Label l;
        l.set_position(2, 2);
        l.set_size(3, 3);
        l.set_background_image(img);
        l.draw(*g);

        EXPECT(test::pixel_at(*g, 2, 2) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 1, 1) == 0);  // outside the label
        EXPECT(test::pixel_at(*g, 4, 4) == 0);  // image is 2x2, label is 3x3
    }

    // image wins over the background color
    {
        const core::Color img_pixels[] = {
            core::Color::from(0, 0, 255),
            core::Color::from(0, 0, 255),
            core::Color::from(0, 0, 255),
            core::Color::from(0, 0, 255),
        };
        const core::image_t img{img_pixels, 2, 2, 0};

        auto g = core::Graphics::make_ptr(8, 8);
        Label l;
        l.set_position(0, 0);
        l.set_size(4, 4);
        l.set_background_color(core::colors::Red);
        l.set_background_image(img);
        l.draw(*g);

        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Blue.pixel);  // image area
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Red.pixel);   // color beyond image
    }

    // default alignment values
    {
        Label l;
        EXPECT(l.get_text().empty());
        EXPECT(l.is_visible());
    }

    // H-1: wrap is default-off; the switch lives in the sidecar and the
    // line pitch rides the same allocation (no bare-widget growth)
    {
        Label l;
        EXPECT(!l.text_wrap());
        EXPECT(l.line_height() == 0);
        l.set_text_wrap(true);
        EXPECT(l.text_wrap());
        l.set_line_height(20);
        EXPECT(l.line_height() == 20);
        l.set_line_height(-5);  // clamps to 0 (unset -> provider pitch)
        EXPECT(l.line_height() == 0);
        Label b;
        EXPECT(!b.text_wrap());
    }

    // H-1: greedy wrapping breaks at spaces (the break space drops) and
    // at '\n'; consecutive hard breaks keep their empty lines; leading
    // and trailing spaces trim off a line; a width <= 0 is the single
    // line
    {
        Label l;
        l.set_text("aa bb cc");
        l.set_text_wrap(true);
        const auto &wide = l.wrap_spans(10000);
        EXPECT(wide.size() == 1);
        if (wide.size() == 1)
        {
            EXPECT(wide[0].first == 0 && wide[0].second == 8);
        }
        const auto &narrow = l.wrap_spans(8);
        EXPECT(narrow.size() == 3);
        if (narrow.size() == 3)
        {
            EXPECT(narrow[0].first == 0 && narrow[0].second == 2);
            EXPECT(narrow[2].second == 2);  // "cc"
        }
        const auto &zero = l.wrap_spans(0);
        EXPECT(zero.size() == 1 && zero[0].second == 8);

        Label br;
        br.set_text(std::string("aa\n bb\n\ncc "));
        br.set_text_wrap(true);
        const auto &sp = br.wrap_spans(10000);
        EXPECT(sp.size() == 4);  // aa | bb | (empty) | cc
        if (sp.size() == 4)
        {
            const std::string &t = br.get_text();
            EXPECT(t.substr(static_cast<std::size_t>(sp[0].first), static_cast<std::size_t>(sp[0].second)) == "aa");
            EXPECT(t.substr(static_cast<std::size_t>(sp[1].first), static_cast<std::size_t>(sp[1].second)) == "bb");
            EXPECT(t.substr(static_cast<std::size_t>(sp[2].first), static_cast<std::size_t>(sp[2].second)).empty());
            EXPECT(t.substr(static_cast<std::size_t>(sp[3].first), static_cast<std::size_t>(sp[3].second)) == "cc");
        }
    }

    // H-1: the wrapped block height is (lines-1) x pitch + line height;
    // the pitch is the provider line metrics unless line_height wins, and
    // a single line equals the plain text height
    {
        ProbeLabel l;
        l.set_text("www www www");
        l.set_text_wrap(true);
        const int h = l.text_height();
        const int one = l.wrapped_block_height(10000);
        EXPECT(one == h);
        const auto &sp = l.wrap_spans(15);
        EXPECT(sp.size() > 1);
        EXPECT(l.wrapped_block_height(15) == static_cast<int>(sp.size()) * h);

        l.set_line_height(20);
        const auto &sp2 = l.wrap_spans(15);
        EXPECT(l.wrapped_block_height(15) ==
               (static_cast<int>(sp2.size()) - 1) * 20 + h);
    }

    // H-1: measure() reports the box width and wrapped height once a box
    // is assigned (the convergent read), else the single-line demand
    {
        ProbeLabel l;
        l.set_text("aa bb");
        l.set_text_wrap(true);
        EXPECT(l.measure().width == l.text_advance());  // no box: single-line
        l.set_size(7, 7);
        EXPECT(l.measure().width == 7);
        EXPECT(l.measure().height == 2 * l.text_height());
    }

    // T-1: a pure-CJK paragraph wraps unit-by-unit at the assigned width
    // instead of sitting on one clipped line
    {
        Label l;
        l.set_glyph_provider(zb::make_shared<WideStubProvider>());
        l.set_text(std::string("\xe4\xb8\xad\xe6\x96\x87\xe6\xae\xb5\xe8\x90\xbd\xe6\xb5\x8b\xe8\xaf\x95"));
        l.set_text_wrap(true);
        const auto &sp = l.wrap_spans(8);  // 2 units x 4px per line
        EXPECT(sp.size() == 3);
        if (sp.size() == 3)
        {
            const std::string &t = l.get_text();
            EXPECT(t.substr(static_cast<std::size_t>(sp[0].first),
                            static_cast<std::size_t>(sp[0].second)) == "\xe4\xb8\xad\xe6\x96\x87");
            EXPECT(t.substr(static_cast<std::size_t>(sp[1].first),
                            static_cast<std::size_t>(sp[1].second)) == "\xe6\xae\xb5\xe8\x90\xbd");
            EXPECT(t.substr(static_cast<std::size_t>(sp[2].first),
                            static_cast<std::size_t>(sp[2].second)) == "\xe6\xb5\x8b\xe8\xaf\x95");
        }
    }

    // T-1: mixed Latin/CJK text breaks only at the CJK boundary — the
    // Latin run stays attached to the CJK unit it precedes
    {
        Label l;
        l.set_glyph_provider(zb::make_shared<WideStubProvider>());
        l.set_text(std::string("ab\xe4\xb8\xad\xe6\x96\x87\xe5\xad\x97"));
        l.set_text_wrap(true);
        const auto &sp = l.wrap_spans(12);
        EXPECT(sp.size() == 2);
        if (sp.size() == 2)
        {
            const std::string &t = l.get_text();
            EXPECT(t.substr(static_cast<std::size_t>(sp[0].first),
                            static_cast<std::size_t>(sp[0].second)) == "ab\xe4\xb8\xad");
            EXPECT(t.substr(static_cast<std::size_t>(sp[1].first),
                            static_cast<std::size_t>(sp[1].second)) == "\xe6\x96\x87\xe5\xad\x97");
        }
    }

    // T-1 kinsoku-lite: the pinned head set never starts a line, the
    // pinned tail set never ends one. The literals below must equal the
    // contract strings (code-contract §2 "Wrapped text") character for
    // character. Members outside the CJK break set (”’…‥“‘) cannot sit on
    // a CJK candidate boundary at all, so only CJK-set members exercise
    // the retraction.
    {
        const char32_t head[] = U"、。，．：；？！）］｝〉》」』”’…‥";
        const char32_t tail[] = U"（［｛〈《「『“‘";
        for (int i = 0; head[i] != U'\0'; ++i)
        {
            const char32_t x = head[i];
            std::string s = "\xe4\xb8\xad\xe4\xb8\xad";
            append_utf8(s, x);
            s += "\xe4\xb8\xad\xe4\xb8\xad\xe4\xb8\xad";
            Label l;
            l.set_glyph_provider(zb::make_shared<WideStubProvider>());
            l.set_text(s);
            l.set_text_wrap(true);
            const auto &sp = l.wrap_spans(8);
            for (const auto &line : sp)
            {
                const char *lp =
                    l.get_text().data() + static_cast<std::size_t>(line.first);
                EXPECT(decode_utf8_next(
                           lp, l.get_text().data() + l.get_text().size()) != x);
            }
        }
        for (int i = 0; tail[i] != U'\0'; ++i)
        {
            const char32_t x = tail[i];
            std::string s = "\xe4\xb8\xad";
            append_utf8(s, x);
            s += "\xe4\xb8\xad\xe4\xb8\xad\xe4\xb8\xad";
            Label l;
            l.set_glyph_provider(zb::make_shared<WideStubProvider>());
            l.set_text(s);
            l.set_text_wrap(true);
            const auto &sp = l.wrap_spans(8);
            for (const auto &line : sp)
            {
                const char *lp = l.get_text().data() +
                                 static_cast<std::size_t>(line.first +
                                                          line.second);
                EXPECT(decode_utf8_prev(lp, l.get_text().data()) != x);
            }
        }

        // the named cases, spelled out: no line starts with 、 or 。, and
        // none ends with 「 or （
        Label h;
        h.set_glyph_provider(zb::make_shared<WideStubProvider>());
        h.set_text(std::string("\xe4\xb8\xad\xe4\xb8\xad\xe3\x80\x82\xe6\x96\x87\xe6\x96\x87"));
        h.set_text_wrap(true);
        const auto &hs = h.wrap_spans(8);
        EXPECT(hs.size() == 3);
        for (const auto &line : hs)
        {
            const char *lp =
                h.get_text().data() + static_cast<std::size_t>(line.first);
            const char32_t c = decode_utf8_next(
                lp, h.get_text().data() + h.get_text().size());
            EXPECT(c != U'。' && c != U'、');
        }
        Label t;
        t.set_glyph_provider(zb::make_shared<WideStubProvider>());
        t.set_text(std::string("\xe4\xb8\xad\xe3\x80\x8c\xe4\xb8\xad\xef\xbc\x88\xe4\xb8\xad"));
        t.set_text_wrap(true);
        const auto &ts = t.wrap_spans(8);
        for (const auto &line : ts)
        {
            const char *lp = t.get_text().data() +
                             static_cast<std::size_t>(line.first +
                                                      line.second);
            const char32_t c = decode_utf8_prev(lp, t.get_text().data());
            EXPECT(c != U'「' && c != U'（');
        }
    }

    // T-1 code-point atomicity: a non-BMP character is one break unit —
    // no line edge ever falls inside its 4 bytes, and it moves whole to
    // the next line when it does not fit (the old pair-edge rule is
    // gone: a non-BMP code point is an ordinary non-CJK unit for
    // breaking, so the break candidates here are the spaces)
    {
        std::string s = "\xe4\xb8\xad";  // 中
        s += " \xf0\x9f\x98\x80 ";       // space, U+1F600, space
        s += "\xe6\x96\x87";             // 文
        Label l;
        l.set_glyph_provider(zb::make_shared<WideStubProvider>());
        l.set_text(s);
        l.set_text_wrap(true);
        // 12px holds one 4px unit beside the 8px emoji: the emoji moves
        // whole to the next line
        const auto &sp = l.wrap_spans(12);
        EXPECT(sp.size() == 3);
        if (sp.size() == 3)
        {
            EXPECT(sp[0].first == 0 && sp[0].second == 3);
            EXPECT(sp[1].first == 4 && sp[1].second == 4);
            EXPECT(sp[2].first == 9 && sp[2].second == 3);
        }
        // 20px holds "中 <emoji>"; the 文 overflow breaks at the space
        const auto &sp2 = l.wrap_spans(20);
        EXPECT(sp2.size() == 2);
        if (sp2.size() == 2)
        {
            EXPECT(sp2[0].first == 0 && sp2[0].second == 8);
            EXPECT(sp2[1].first == 9 && sp2[1].second == 3);
        }
        for (const auto &line : sp)
        {
            // no line edge falls inside the emoji's 4 bytes (offsets 5-7)
            EXPECT(!(line.first <= 4 && line.first + line.second >= 5 &&
                     line.first + line.second <= 7));
        }
    }

    // T-1: U+3000 (ideographic space) is an ordinary CJK unit — it takes
    // width, breaks like one, and never collapses off a line start the
    // way U+0020 does
    {
        Label l;
        l.set_glyph_provider(zb::make_shared<WideStubProvider>());
        std::string s = "\xe4\xb8\xad";
        s += "\xe3\x80\x80";  // U+3000
        s += "\xe4\xb8\xad\xe4\xb8\xad";
        l.set_text(s);
        l.set_text_wrap(true);
        const auto &sp = l.wrap_spans(8);
        EXPECT(sp.size() == 2);
        if (sp.size() == 2)
        {
            const std::string &t = l.get_text();
            EXPECT(t.substr(static_cast<std::size_t>(sp[0].first),
                            static_cast<std::size_t>(sp[0].second)) ==
                   "\xe4\xb8\xad\xe3\x80\x80");
            EXPECT(t.substr(static_cast<std::size_t>(sp[1].first),
                            static_cast<std::size_t>(sp[1].second)) == "\xe4\xb8\xad\xe4\xb8\xad");
        }
    }

    return test::report("label");
}
