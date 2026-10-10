#include "runtime_ttf_provider.hpp"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/error.hpp"
#include "stb_truetype.h"

namespace zb::ui
{
    namespace
    {
        // the provider_for range; the same envelope the build-time
        // rasterizer validates (tools/ttf_subset.cpp)
        constexpr int kMinPixelSize = 1;
        constexpr int kMaxPixelSize = 128;

        // smallest plausible sfnt input: the 12-byte offset table plus
        // one 16-byte table-directory record. stb_truetype takes no
        // length, so a caller buffer shorter than its table directory
        // makes it read past the end -- we reject before it does (W-5).
        constexpr size_t kMinFontBytes = 12 + 16;
    }  // namespace

    struct TtfFamilyState
    {
        // from_file keeps its copy here; from_memory points `data` at the
        // caller's buffer (validated by sfnt_envelope_ok before the parse)
        std::vector<unsigned char> owned;
        const unsigned char *data = nullptr;
        stbtt_fontinfo info = {};
        size_t budget = TtfFamily::kDefaultCacheBudget;

        // one rasterized glyph; the key is (pixel size, code unit) --
        // everything that changes the rendered pixels. Every field is
        // default-initialized: an empty bitmap (e.g. the space glyph)
        // leaves w/h at 0, never indeterminate
        struct GlyphEntry
        {
            int px = 0;
            char16_t ch = 0;
            int advance = 0;
            int xoff = 0;
            int yoff = 0;
            int w = 0;
            int h = 0;
            std::vector<unsigned char> cov;
        };

        std::vector<GlyphEntry> cache;
        size_t cache_bytes = 0;
        long long rasterizations = 0;

        // memoized per-size providers (TtfFamily::provider_for); each
        // provider borrows *this (non-owning), so this vector is the
        // only owner -- no shared_ptr cycle
        std::vector<std::pair<int, zb::SharedPtr<GlyphProvider>>> per_size;
    };

    namespace
    {
        // sfnt integers are big-endian
        unsigned be16(const unsigned char *d, const size_t at)
        {
            return (static_cast<unsigned>(d[at]) << 8) | static_cast<unsigned>(d[at + 1]);
        }

        unsigned long long be32(const unsigned char *d, const size_t at)
        {
            return (static_cast<unsigned long long>(be16(d, at)) << 16) | be16(d, at + 2);
        }

        /*
         * Envelope check for a font buffer (W-5). stb_truetype takes no
         * length: it reads the offset table, then seeks to whatever
         * offset each table record declares. A caller buffer shorter than
         * that — or one whose records point outside it — would be read
         * out of bounds before any validity answer comes back. Validate
         * the whole sfnt envelope ourselves first: the header, the table
         * directory, and every table's [offset, offset+length) span.
         */
        bool sfnt_envelope_ok(const unsigned char *d, const size_t n)
        {
            if (d == nullptr || n < kMinFontBytes)
            {
                return false;
            }
            const int offset = stbtt_GetFontOffsetForIndex(d, 0);
            if (offset < 0)
            {
                return false;
            }
            const size_t base = static_cast<size_t>(offset);
            if (base + 12 > n)
            {
                return false;
            }
            const size_t num_tables = be16(d, base + 4);
            if (base + 12 + num_tables * 16 > n)
            {
                return false;  // the directory itself runs past the end
            }
            for (size_t i = 0; i < num_tables; ++i)
            {
                const size_t rec = base + 12 + i * 16;
                const auto table_off = be32(d, rec + 8);
                const auto table_len = be32(d, rec + 12);
                // 64-bit sums: a record can declare offsets near 2^32
                if (table_off + table_len > static_cast<unsigned long long>(n))
                {
                    return false;
                }
            }
            return true;
        }

        int glyph_index(const TtfFamilyState &s, const char16_t ch)
        {
            return stbtt_FindGlyphIndex(&s.info, ch);
        }

        // the pre-rounded advance of one code unit; never rasterizes
        // (measure only needs metrics). A font may declare a negative
        // advance (italic kerning, some CJK fonts): clamp at the provider
        // boundary so the widget advance cache never receives a negative
        // total -- that would collide with its -1 "not computed" sentinel
        // (permanent cache miss) and push the pen backwards (W-6).
        int advance_px(const TtfFamilyState &s, const float scale, const char16_t ch)
        {
            const int g = glyph_index(s, ch);
            if (g == 0)
            {
                return 0;
            }
            int adv = 0;
            int lsb = 0;
            stbtt_GetGlyphHMetrics(&s.info, g, &adv, &lsb);
            const long long px = std::llround(static_cast<float>(adv) * scale);
            return px > 0 ? static_cast<int>(px) : 0;
        }

        const TtfFamilyState::GlyphEntry *find_entry(const TtfFamilyState &s,
                                                     const int px, const char16_t ch)
        {
            for (const auto &e : s.cache)
            {
                if (e.px == px && e.ch == ch)
                {
                    return &e;
                }
            }
            return nullptr;
        }

        // get-or-rasterize: a miss counts one rasterization (the 8 gate
        // counts misses through rasterization_count()) and may allocate;
        // an over-budget insert drops the whole cache (the ListBox rule).
        // Reference-stability invariant: the returned reference points
        // into s.cache — consume it fully BEFORE the next entry_for /
        // measure / write call, any of which can push_back or clear the
        // vector and invalidate it (write() observes this today)
        const TtfFamilyState::GlyphEntry &entry_for(TtfFamilyState &s, const int px,
                                                    const float scale, const char16_t ch)
        {
            if (const auto *hit = find_entry(s, px, ch))
            {
                return *hit;
            }
            ++s.rasterizations;
            TtfFamilyState::GlyphEntry e;
            e.px = px;
            e.ch = ch;
            e.advance = advance_px(s, scale, ch);
            const int g = glyph_index(s, ch);
            if (g != 0)
            {
                int w = 0;
                int h = 0;
                int xoff = 0;
                int yoff = 0;
                unsigned char *bmp = stbtt_GetGlyphBitmap(&s.info, scale, scale, g,
                                                          &w, &h, &xoff, &yoff);
                if (bmp != nullptr)
                {
                    e.cov.assign(bmp, bmp + static_cast<size_t>(w) * static_cast<size_t>(h));
                    e.w = w;
                    e.h = h;
                    e.xoff = xoff;
                    e.yoff = yoff;
                    // stb_truetype allocated with the default STBTT_malloc
                    // (malloc); the wrapper TU overrides nothing
                    std::free(bmp);
                }
            }
            const size_t bytes = e.cov.size();
            if (s.cache_bytes + bytes > s.budget)
            {
                s.cache.clear();
                s.cache_bytes = 0;
            }
            s.cache_bytes += bytes;
            s.cache.push_back(std::move(e));
            return s.cache.back();
        }
    }  // namespace

    TtfFamily TtfFamily::from_file(const char *path, const size_t cache_budget)
    {
        if (path == nullptr || path[0] == '\0')
        {
            throw error("TtfFamily: empty font path");
        }
        std::FILE *f = std::fopen(path, "rb");
        if (f == nullptr)
        {
            throw error(std::string("TtfFamily: cannot open font file: ") + path);
        }
        std::vector<unsigned char> bytes;
        if (std::fseek(f, 0, SEEK_END) == 0)
        {
            const long n = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (n > 0)
            {
                bytes.resize(static_cast<size_t>(n));
                if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size())
                {
                    bytes.clear();
                }
            }
        }
        std::fclose(f);
        if (bytes.empty())
        {
            throw error(std::string("TtfFamily: cannot read font file: ") + path);
        }
        return make(std::move(bytes), nullptr, 0, cache_budget);
    }

    TtfFamily TtfFamily::make(std::vector<unsigned char> owned,
                              const unsigned char *borrowed, const size_t borrowed_n,
                              const size_t cache_budget)
    {
        TtfFamily out;
        out.state_ = zb::make_shared<TtfFamilyState>();
        out.state_->owned = std::move(owned);
        out.state_->data = borrowed != nullptr ? borrowed : out.state_->owned.data();
        out.state_->budget = cache_budget;
        // the buffer length is a precondition, not a stored field: the sfnt
        // envelope check runs on every init path (W-5), so a buffer whose
        // declared tables run past it never reaches the parser
        const size_t n = borrowed != nullptr ? borrowed_n : out.state_->owned.size();
        if (!sfnt_envelope_ok(out.state_->data, n))
        {
            throw error("TtfFamily: not a parseable TrueType font (buffer does not contain the sfnt tables it declares)");
        }
        const int offset = stbtt_GetFontOffsetForIndex(out.state_->data, 0);
        if (offset < 0 || !stbtt_InitFont(&out.state_->info, out.state_->data, offset))
        {
            throw error("TtfFamily: not a parseable TrueType font");
        }
        return out;
    }

    TtfFamily TtfFamily::from_memory(const unsigned char *bytes, const size_t n,
                                     const size_t cache_budget)
    {
        if (bytes == nullptr || n == 0)
        {
            throw error("TtfFamily: empty font buffer");
        }
        // the length is the caller's claim about how far `bytes` is
        // readable; sfnt_envelope_ok() inside make() checks it against what
        // the buffer itself declares before any parser reads it (W-5)
        return make({}, bytes, n, cache_budget);
    }

    zb::SharedPtr<GlyphProvider> TtfFamily::provider_for(const int px) const
    {
        if (state_ == nullptr)
        {
            throw error("TtfFamily: provider_for on an empty handle (use from_file/from_memory)");
        }
        if (px < kMinPixelSize || px > kMaxPixelSize)
        {
            throw error("TtfFamily: pixel size out of range 1..128");
        }
        for (const auto &[size, provider] : state_->per_size)
        {
            if (size == px)
            {
                return provider;
            }
        }
        zb::SharedPtr<TtfRuntimeProvider> p(new TtfRuntimeProvider(state_.get(), px));
        state_->per_size.emplace_back(px, p);
        return p;
    }

    long long TtfFamily::rasterization_count() const
    {
        return state_ != nullptr ? state_->rasterizations : 0;
    }

    size_t TtfFamily::cache_bytes() const
    {
        return state_ != nullptr ? state_->cache_bytes : 0;
    }

TtfRuntimeProvider::TtfRuntimeProvider(TtfFamilyState *state, const int px)
        : state_(state), px_(px)
    {
        scale_ = stbtt_ScaleForPixelHeight(&state_->info, static_cast<float>(px_));
        int ascent = 0;
        int descent = 0;
        int linegap = 0;
        stbtt_GetFontVMetrics(&state_->info, &ascent, &descent, &linegap);
        // the same metrics formula as the build-time subset (tools/
        // ttf_subset.cpp): no linegap, height = ascent - descent
        const int a = static_cast<int>(std::llround(static_cast<float>(ascent) * scale_));
        const int d = static_cast<int>(std::llround(static_cast<float>(descent) * scale_));
        line_ = text_metrics{0, a - d, a};
    }

    bool TtfRuntimeProvider::covers(const char16_t ch) const
    {
        return glyph_index(*state_, ch) != 0;
    }

    text_metrics TtfRuntimeProvider::measure(const char16_t *str, const int len) const
    {
        text_metrics m = line_;
        if (str == nullptr || len <= 0)
        {
            return m;
        }
        for (int i = 0; i < len; ++i)
        {
            m.width += advance_px(*state_, scale_, str[i]);
        }
        return m;
    }

    text_metrics TtfRuntimeProvider::line_metrics() const
    {
        return line_;
    }

    void TtfRuntimeProvider::write(core::Graphics &g, const char16_t *str, const int len,
                                   const int x, const int y, const core::Color &color) const
    {
        if (str == nullptr || len <= 0)
        {
            return;
        }
        // coverage rides the foreground color's alpha channel; the
        // rasterizer hard-clips per pixel (damage/clip safe) -- the
        // identical pixel semantics as TtfSubsetProvider::write
        const auto bak = g.is_alpha_enabled();
        g.enable_alpha(true);
        int pen = x;
        for (int i = 0; i < len; ++i)
        {
            const auto &e = entry_for(*state_, px_, scale_, str[i]);
            if (e.w > 0 && e.h > 0)
            {
                const int left = pen + e.xoff;
                const int top = y + e.yoff;  // y is the baseline
                for (int r = 0; r < e.h; ++r)
                {
                    for (int c = 0; c < e.w; ++c)
                    {
                        const uint8_t cov = e.cov[static_cast<size_t>(r) * e.w + c];
                        if (cov != 0)
                        {
                            g.draw_pixel(left + c, top + r,
                                         core::Color::from(color.r(), color.g(), color.b(), cov));
                        }
                    }
                }
            }
            pen += e.advance;
        }
        g.enable_alpha(bak);
    }
}  // namespace zb::ui
