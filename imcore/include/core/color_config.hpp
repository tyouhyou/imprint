#pragma once

/*
 * Token-paste helpers shared by the color model headers. The concrete
 * pixel types are selected at build time by the RGB_MODEL / ENDIAN /
 * COLOR_DEPTH macros (see imcore/CMakeLists.txt), so the paste result
 * is only valid in the build that defined those macros.
 */

#define _MAKERGBNAME(r, e) r##_##e##_t
#define MAKERGBNAME(r, e) _MAKERGBNAME(r, e)

#define _MAKECOLORTYPENAME(c) color##c##_t
#define MAKECOLORTYPENAME(c) _MAKECOLORTYPENAME(c)
