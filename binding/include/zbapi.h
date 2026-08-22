#ifndef ZBAPI_H
#define ZBAPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * C encoding of the framework, for cross-language hosts (python, wasm).
 *
 * The host plays the shell role (see IApp rendering loop protocol):
 * it owns the frame loop, samples input and presents the framebuffer.
 *
 * The framebuffer format is the one the framework was compiled with
 * (COLOR_DEPTH / ENDIAN / RGB_MODEL, see imcore/CMakeLists.txt):
 *   - COLOR_DEPTH=32: 4 bytes per pixel, bgra on Windows/x86
 *   - COLOR_DEPTH=16: 2 bytes per pixel, abgr1555
 * Hosts should be written against a fixed build configuration.
 */

/* opaque app handle */
typedef struct zb_app zb_app_t;

/* called after every painted frame; see zb_set_painted_callback */
typedef void (*zb_painted_cb)(void *userdata);
/* level: zb::Logging_Level (0=debug .. 4=fatal) */
typedef void (*zb_log_cb)(int level, const char *message);

/* ---- input types (must match zb::input::input_type) ---- */
enum
{
    ZB_INPUT_NONE = 0,
    ZB_INPUT_MOUSE_LEFT_DOWN = 1,
    ZB_INPUT_MOUSE_LEFT_UP,
    ZB_INPUT_MOUSE_LEFT_CLICK,
    ZB_INPUT_MOUSE_RIGHT_DOWN,
    ZB_INPUT_MOUSE_RIGHT_UP,
    ZB_INPUT_MOUSE_RIGHT_CLICK,
    ZB_INPUT_MOUSE_WHEEL,
    ZB_INPUT_MOUSE_MOVE,
    ZB_INPUT_TOUCH_DOWN = 9,
    ZB_INPUT_TOUCH_UP,
    ZB_INPUT_TOUCH_MOVE,
    ZB_INPUT_KEY_DOWN = 12,
    ZB_INPUT_KEY_UP
};

/* ---- keyboard codes (must match zb::input::key_code) ---- */
enum
{
    ZB_KEY_BACKSPACE = 8,
    ZB_KEY_TAB = 9,
    ZB_KEY_ENTER = 13,
    ZB_KEY_ESCAPE = 27,
    ZB_KEY_SPACE = 32,
    ZB_KEY_DEL = 127,
    ZB_KEY_UP = 256,
    ZB_KEY_DOWN,
    ZB_KEY_LEFT,
    ZB_KEY_RIGHT
};

/* creates the default app (see make_app) and its window */
zb_app_t *zb_app_create(uint32_t width, uint32_t height);

/* destroys the app */
void zb_app_destroy(zb_app_t *app);

/*
 * Host contract notes:
 *   - the host IS the shell: it drives input/paint/buffer and nothing
 *     else; the buffer format is fixed at build time by COLOR_DEPTH
 *   - all entry points must be called from ONE thread (the embedded
 *     glibc shim declares the process single-threaded; a host that
 *     calls zb_* from worker threads must marshal them itself)
 */

/* feeds an input event; fields without meaning for the type are ignored:
 *   - touch_* / mouse_* (click, move): x, y
 *   - touch_*:                         touch_id identifies the finger
 *                                      (0 for single-touch shells / mouse)
 *   - mouse_wheel:                     key holds the wheel delta
 *   - key_*:                           key holds the navigation/editing key
 *                                      code (see zb_key_code); ch holds the
 *                                      printable character (ASCII today,
 *                                      Unicode later) -- navigation keys
 *                                      always set key and leave ch 0, and
 *                                      a text-producing key may set both
 */
void zb_input(zb_app_t *app, int type, int x, int y, int key, int ch, int touch_id);

/* renders one frame; the host should drive this from its own loop */
void zb_paint(zb_app_t *app);

/* framebuffer: returns a pointer to the pixel data (valid until the next
 * zb_paint call) and stores the size in pixels. May return NULL when the
 * app has no window yet. */
const uint8_t *zb_buffer(zb_app_t *app, uint32_t *out_width, uint32_t *out_height);

/* registers a callback called after every painted frame; the host can then
 * present the framebuffer (via zb_buffer). userdata is passed through. */
void zb_set_painted_callback(zb_app_t *app, zb_painted_cb cb, void *userdata);

/* optional log hook receiving framework log messages */
void zb_set_log_callback(zb_log_cb cb);

#ifdef __cplusplus
}
#endif

#endif /* ZBAPI_H */