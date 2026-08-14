/*
 * Single implementation TU for the vendored stb codecs
 * (third_party/stb, see third_party/README.md).
 *
 * Compiled only when USE_PNG or USE_JPEG is on; the default build stays
 * zero-dependency. Never include this header from anywhere else.
 */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"