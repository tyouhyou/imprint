#pragma once

#include <stdint.h>
#include "color_config.hpp"
#include "im_defines.hpp"

namespace zb::ui::core
{
    // NOTE (A-11, 2026-08-28): the struct field order is the in-memory byte
    // order. On x86 little-endian, `argb32_be_t {b,g,r,a}` and
    // `bgra32_le_t {b,g,r,a}` describe the *same* BGRA bytes; the names are
    // endian-relative and documented in ARCHITECTURE.md §4.4. Prefer reasoning
    // about `COLOR_DEPTH`/`ENDIAN`/`RGB_MODEL` rather than the typedef name.
    typedef struct _argb32_be_t
    {
        uint8_t b;
        uint8_t g;
        uint8_t r;
        uint8_t a;
    } argb32_be_t,   // big-endian, ARGB
        bgra32_le_t; // little-endian, BGRA

    typedef struct _bgra32_t
    {
        uint8_t a;
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } bgra32_be_t,   // big-endian, BGRA
        argb32_le_t; // little-endian, ARGB

    typedef struct _rgba32_t
    {
        uint8_t a;
        uint8_t b;
        uint8_t g;
        uint8_t r;
    } rgba32_be_t,     // big-endian, RGBA
        abgr32_le_t,   // little-endian, ABGR
        rgba8888_be_t; // big-endian, RGBA8888

    typedef struct _abgr32_t
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    } abgr32_be_t,     // big-endian, ABGR
        rgba32_le_t,   // little-endian, RGBA
        rgba8888_le_t; // little-endian, RGBA8888

    typedef MAKERGBNAME(RGB_MODEL, ENDIAN) rgb32_t;

    typedef union _color32_t
    {
        uint32_t pixel = 0;
        rgb32_t rgb;

        operator uint32_t() const { return pixel; }
        void operator=(const _color32_t &c) { pixel = c.pixel; }
        void operator=(const uint32_t &c) { pixel = c; }
        _color32_t operator|(const _color32_t &c) { return {c.pixel | pixel}; }

        static _color32_t from(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF)
        {
            _color32_t c{};
            c.rgb.a = a;
            c.rgb.b = b;
            c.rgb.g = g;
            c.rgb.r = r;
            return c;
        }
    } color32_t;

} // namespace zb::ui