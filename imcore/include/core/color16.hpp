/*
 * Refer to colour32.hpp for more information about I/F and implementation style.
 *
 * Just some pheudo codes for showing concept to handle
 * 16 bit color such as 565 rgb little-endian color.
 * and make it have same I/F with 32bit color
 */

#pragma once

#include <stdint.h>
#include "color_config.hpp"
#include "im_defines.hpp"

namespace zb::ui::core
{
    typedef struct _rgb_1_t
    {
        uint16_t value;
        operator uint8_t() const { return (uint8_t)((this->value >> 15) & 0x01); }
        void operator=(const uint8_t &v) { this->value = (this->value & (uint16_t)~0x8000u) | ((v > 0 ? 1 : 0) << 15); }
    } abgr1555_a_t;

    typedef struct _rgb_2_t
    {
        uint16_t value;
        operator uint8_t() const { return (uint8_t)(((this->value >> 10) & 0x1F) << 3); }
        void operator=(const uint8_t &v) { this->value = (this->value & (uint16_t)~0x7C00u) | ((v >> 3) << 10); }
    } abgr1555_b_t;

    typedef struct _rgb_3_t
    {
        uint16_t value;
        operator uint8_t() const { return (uint8_t)(((this->value >> 5) & 0x1F) << 3); }
        void operator=(const uint8_t &v) { this->value = (this->value & (uint16_t)~0x03E0u) | ((v >> 3) << 5); }
    } abgr1555_g_t;

    typedef struct _rgb_4_t
    {
        uint16_t value;
        operator uint8_t() const { return (uint8_t)((this->value & 0x1F) << 3); }
        void operator=(const uint8_t &v) { this->value = (this->value & (uint16_t)~0x001Fu) | (v >> 3); }
    } abgr1555_r_t;

    typedef union _abgr1555_t
    {
        uint16_t value = 0;
        abgr1555_a_t a;
        abgr1555_r_t r;
        abgr1555_g_t g;
        abgr1555_b_t b;

        // do not use this function directly, use color16_t::from() instead.
        void from(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF)
        {
            this->value = static_cast<uint16_t>(((a == 0 ? 0 : 1) << 15) |
                                                ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
        }
    } abgr_1555_t;

    typedef MAKERGBNAME(RGB_MODEL, ENDIAN) rgb16_t;

    typedef union _color16_t
    {
        uint16_t pixel = 0;
        rgb16_t rgb;

        operator uint16_t() const { return pixel; }
        void operator=(const uint16_t &c) { pixel = c; }
        void operator=(const _color16_t &c) { pixel = c.pixel; }
        _color16_t operator|(const _color16_t &c) const { return {static_cast<uint16_t>(c.pixel | pixel)}; }

        static _color16_t from(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF)
        {
            _color16_t c{};
            c.rgb.from(r, g, b, a);
            return c;
        }
    } color16_t;
}