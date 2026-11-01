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

    // T-1: a uniform 4px-advance provider covering every code unit. The
    // 5x7 fallback leaves CJK units uncovered (zero advance), so the CJK
    // wrap suites drive the engine through this stub; the wrap rules are
    // provider-agnostic, only the widths matter here.
    struct WideStubProvider : GlyphProvider
    {
        bool covers(const char16_t) const override { return true; }
        text_metrics measure(const char16_t *, const int len) const override
        {
            return {4 * len, 7, 7};
        }
        text_metrics line_metrics() const override { return {0, 7, 7}; }
        void write(core::Graphics &, const char16_t *, const int, const int,
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
        EXPECT(l.get_text() == std::u16string(u"abc"));
        l.set_text(nullptr);
        EXPECT(l.get_text().empty());
        l.set_text(std::u16string(u"xyz"));
        EXPECT(l.get_text() == std::u16string(u"xyz"));
    }

    // invalid UTF-8 bytes become U+FFFD (input is UTF-8 per the contract)
    {
        Label l;
        l.set_text("\x80\xFF");
        const auto &t = l.get_text();
        EXPECT(t.size() == 2);
        if (t.size() == 2)
        {
            EXPECT(t[0] == 0xFFFD);
            EXPECT(t[1] == 0xFFFD);
        }
    }

    // multibyte UTF-8 decodes into the right code units
    {
        Label l;
        l.set_text("A\xE4\xB8\xAD");
        const auto &t = l.get_text();
        EXPECT(t.size() == 2);
        if (t.size() == 2)
        {
            EXPECT(t[0] == u'A');
            EXPECT(t[1] == 0x4E2D);
        }
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
        br.set_text(std::u16string(u"aa\n bb\n\ncc "));
        br.set_text_wrap(true);
        const auto &sp = br.wrap_spans(10000);
        EXPECT(sp.size() == 4);  // aa | bb | (empty) | cc
        if (sp.size() == 4)
        {
            const std::u16string &t = br.get_text();
            EXPECT(t.substr(static_cast<std::size_t>(sp[0].first), static_cast<std::size_t>(sp[0].second)) == u"aa");
            EXPECT(t.substr(static_cast<std::size_t>(sp[1].first), static_cast<std::size_t>(sp[1].second)) == u"bb");
            EXPECT(t.substr(static_cast<std::size_t>(sp[2].first), static_cast<std::size_t>(sp[2].second)).empty());
            EXPECT(t.substr(static_cast<std::size_t>(sp[3].first), static_cast<std::size_t>(sp[3].second)) == u"cc");
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
        l.set_text(std::u16string(u"中文段落测试"));
        l.set_text_wrap(true);
        const auto &sp = l.wrap_spans(8);  // 2 units x 4px per line
        EXPECT(sp.size() == 3);
        if (sp.size() == 3)
        {
            const std::u16string &t = l.get_text();
            EXPECT(t.substr(static_cast<std::size_t>(sp[0].first),
                            static_cast<std::size_t>(sp[0].second)) == u"中文");
            EXPECT(t.substr(static_cast<std::size_t>(sp[1].first),
                            static_cast<std::size_t>(sp[1].second)) == u"段落");
            EXPECT(t.substr(static_cast<std::size_t>(sp[2].first),
                            static_cast<std::size_t>(sp[2].second)) == u"测试");
        }
    }

    // T-1: mixed Latin/CJK text breaks only at the CJK boundary — the
    // Latin run stays attached to the CJK unit it precedes
    {
        Label l;
        l.set_glyph_provider(zb::make_shared<WideStubProvider>());
        l.set_text(std::u16string(u"ab中文字"));
        l.set_text_wrap(true);
        const auto &sp = l.wrap_spans(12);
        EXPECT(sp.size() == 2);
        if (sp.size() == 2)
        {
            const std::u16string &t = l.get_text();
            EXPECT(t.substr(static_cast<std::size_t>(sp[0].first),
                            static_cast<std::size_t>(sp[0].second)) == u"ab中");
            EXPECT(t.substr(static_cast<std::size_t>(sp[1].first),
                            static_cast<std::size_t>(sp[1].second)) == u"文字");
        }
    }

    // T-1 kinsoku-lite: the pinned head set never starts a line, the
    // pinned tail set never ends one. The literals below must equal the
    // contract strings (code-contract §2 "Wrapped text") character for
    // character. Members outside the CJK break set (”’…‥“‘) cannot sit on
    // a CJK candidate boundary at all, so only CJK-set members exercise
    // the retraction.
    {
        const char16_t head[] = u"、。，．：；？！）］｝〉》」』”’…‥";
        const char16_t tail[] = u"（［｛〈《「『“‘";
        for (int i = 0; head[i] != u'\0'; ++i)
        {
            const char16_t x = head[i];
            std::u16string s = u"中中";
            s += x;
            s += u"中中中";
            Label l;
            l.set_glyph_provider(zb::make_shared<WideStubProvider>());
            l.set_text(s);
            l.set_text_wrap(true);
            const auto &sp = l.wrap_spans(8);
            for (const auto &line : sp)
            {
                EXPECT(l.get_text()[static_cast<std::size_t>(line.first)] != x);
            }
        }
        for (int i = 0; tail[i] != u'\0'; ++i)
        {
            const char16_t x = tail[i];
            std::u16string s = u"中";
            s += x;
            s += u"中中中";
            Label l;
            l.set_glyph_provider(zb::make_shared<WideStubProvider>());
            l.set_text(s);
            l.set_text_wrap(true);
            const auto &sp = l.wrap_spans(8);
            for (const auto &line : sp)
            {
                EXPECT(l.get_text()[static_cast<std::size_t>(line.first +
                                                             line.second) -
                                    1] != x);
            }
        }

        // the named cases, spelled out: no line starts with 、 or 。, and
        // none ends with 「 or （
        Label h;
        h.set_glyph_provider(zb::make_shared<WideStubProvider>());
        h.set_text(std::u16string(u"中中。文文"));
        h.set_text_wrap(true);
        const auto &hs = h.wrap_spans(8);
        EXPECT(hs.size() == 3);
        for (const auto &line : hs)
        {
            const char16_t c =
                h.get_text()[static_cast<std::size_t>(line.first)];
            EXPECT(c != u'。' && c != u'、');
        }
        Label t;
        t.set_glyph_provider(zb::make_shared<WideStubProvider>());
        t.set_text(std::u16string(u"中「中（中"));
        t.set_text_wrap(true);
        const auto &ts = t.wrap_spans(8);
        for (const auto &line : ts)
        {
            const char16_t c = t.get_text()[static_cast<std::size_t>(
                line.first + line.second - 1)];
            EXPECT(c != u'「' && c != u'（');
        }
    }

    // T-1 surrogate atomicity: a non-BMP character is one break unit —
    // no line ever splits its two code units, while the pair edges break
    // against an adjacent CJK unit
    {
        std::u16string s = u"中";
        s += static_cast<char16_t>(0xD83D);
        s += static_cast<char16_t>(0xDE00);  // U+1F600 as a surrogate pair
        s += u"文";
        Label l;
        l.set_glyph_provider(zb::make_shared<WideStubProvider>());
        l.set_text(s);
        l.set_text_wrap(true);
        // 12px holds unit+pair; the 文 overflow breaks at pair end | CJK
        const auto &sp = l.wrap_spans(12);
        EXPECT(sp.size() == 2);
        if (sp.size() == 2)
        {
            EXPECT(sp[0].first == 0 && sp[0].second == 3);
            EXPECT(sp[1].first == 3 && sp[1].second == 1);
        }
        // 8px holds one unit: the pair moves whole to the next line
        const auto &sp2 = l.wrap_spans(8);
        EXPECT(sp2.size() == 3);
        if (sp2.size() == 3)
        {
            EXPECT(sp2[0].first == 0 && sp2[0].second == 1);
            EXPECT(sp2[1].first == 1 && sp2[1].second == 2);
            EXPECT(sp2[2].first == 3 && sp2[2].second == 1);
        }
    }

    // T-1: U+3000 (ideographic space) is an ordinary CJK unit — it takes
    // width, breaks like one, and never collapses off a line start the
    // way U+0020 does
    {
        Label l;
        l.set_glyph_provider(zb::make_shared<WideStubProvider>());
        std::u16string s = u"中";
        s += static_cast<char16_t>(0x3000);
        s += u"中中";
        l.set_text(s);
        l.set_text_wrap(true);
        const auto &sp = l.wrap_spans(8);
        EXPECT(sp.size() == 2);
        if (sp.size() == 2)
        {
            const std::u16string &t = l.get_text();
            EXPECT(t.substr(static_cast<std::size_t>(sp[0].first),
                            static_cast<std::size_t>(sp[0].second)) ==
                   (std::u16string(u"中") + static_cast<char16_t>(0x3000)));
            EXPECT(t.substr(static_cast<std::size_t>(sp[1].first),
                            static_cast<std::size_t>(sp[1].second)) == u"中中");
        }
    }

    return test::report("label");
}
