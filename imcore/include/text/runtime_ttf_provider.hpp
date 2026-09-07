#pragma once

#include <cstddef>
#include <vector>

#include "glyph_provider.hpp"

namespace zb::ui
{
    struct TtfFamilyState;  // opaque: stb_truetype state + the shared glyph cache

    /*
     * Runtime TTF family (batch L-5): one loaded TrueType font plus the
     * bounded glyph cache shared by every per-size provider it hands out
     * (docs/code-contract.md 2.4, 8). Compiled only under
     * IMCORE_HAS_TTF_RUNTIME (USE_TTF_RUNTIME); with the option off no
     * part of this exists and the 5x7 / build-time subset paths stand.
     *
     * Init path: from_file/from_memory parse the font and throw
     * zb::ui::error on failure (contract 1.1). from_memory borrows the
     * caller's buffer -- it must outlive the family and every provider
     * handed out from it. Handles copy freely; copies share the state.
     */
    class TtfFamily
    {
    public:
        static constexpr size_t kDefaultCacheBudget = 64 * 1024;

        // reads and owns a copy of the font bytes
        static TtfFamily from_file(const char *path,
                                   size_t cache_budget = kDefaultCacheBudget);
        // the zero-copy form for ROM-packed blobs: the caller's buffer
        // must outlive the family
        static TtfFamily from_memory(const unsigned char *bytes, size_t n,
                                     size_t cache_budget = kDefaultCacheBudget);

        /*
         * One provider per pixel size (1..128; outside the range throws
         * -- init path). Equal px returns the same instance; every size
         * shares the family's glyph cache. Widgets consume these through
         * set_glyph_provider unchanged -- the seam never grows.
         */
        [[nodiscard]] zb::SharedPtr<GlyphProvider> provider_for(int px) const;

        // observability seams (contract 8): monotonic cache-miss count
        // across all sizes, and the cache's current byte load. The
        // counter is the portable proof across the imcore DLL boundary
        // (the ListBox rasterization_count precedent).
        [[nodiscard]] long long rasterization_count() const;
        [[nodiscard]] size_t cache_bytes() const;

        TtfFamily() = default;  // an empty handle; provider_for throws
        TtfFamily(const TtfFamily &) = default;
        TtfFamily &operator=(const TtfFamily &) = default;

    private:
        // parses and validates the font (throws zb::ui::error); a
        // non-null `borrowed` wins over the owned copy
        static TtfFamily make(std::vector<unsigned char> owned,
                              const unsigned char *borrowed, size_t borrowed_n,
                              size_t cache_budget);

        zb::SharedPtr<TtfFamilyState> state_;
    };

    /*
     * One pixel size of a TtfFamily: a plain GlyphProvider the widget
     * seam consumes unchanged. Created only through
     * TtfFamily::provider_for -- the stb state and the cache live in the
     * shared family state, so the provider itself stays stateless apart
     * from its size bookkeeping.
     *
     * The holder must not outlive its family: the state pointer is
     * non-owning (broken ownership -- if it kept a refcount the state's
     * per-size memo would never be freed, a reference cycle). In every
     * real use the family anchor outlives the widgets that hold a
     * provider (a static font / app-scoped family).
     */
    class TtfRuntimeProvider final : public GlyphProvider
    {
    public:
        bool covers(const char16_t ch) const override;
        text_metrics measure(const char16_t *str, const int len) const override;
        text_metrics line_metrics() const override;
        void write(core::Graphics &g, const char16_t *str, const int len,
                   const int x, const int y, const core::Color &color) const override;

    private:
        friend class TtfFamily;
        TtfRuntimeProvider(TtfFamilyState *state, int px);

        TtfFamilyState *state_;
        int px_ = 0;
        float scale_ = 0.0f;
        text_metrics line_;
    };
}
