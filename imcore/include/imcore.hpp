#pragma once

#include "core/color.hpp"
#include "core/graphics.hpp"
#include "core/im_defines.hpp"
#if defined(USE_FONT)
#include "text/font.hpp"
#endif
#if defined(USE_JPEG) || defined(USE_PNG)
#include "codec/image.hpp"
#endif
