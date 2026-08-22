#!/usr/bin/env python3
"""Python host for the zbapi C ABI -- drives the framework with pygame.

The host plays the shell role: it owns the frame loop, feeds input and
presents the framebuffer. The framebuffer is 32-bit bgra (as compiled for
the default COLOR_DEPTH=32 / RGB_MODEL=bgra32 config), 4 bytes per pixel.

Usage:
    # interactive (ESC or window close to quit)
    python3 demo/python/myapp.py --lib build_linux/libzbapi.so

    # automated run: paint N frames then exit with code 0
    SDL_VIDEODRIVER=dummy python3 demo/python/myapp.py \
        --lib build_linux/libzbapi.so --frames 20

Options:
    --lib <path>   path to the shared zbapi library; the default is the
                   first existing build output for the platform
                   (build/bin/Debug/zbapi.dll on Windows,
                    build_linux/lib/libzbapi.so on Linux)
    --width/--height  app window size, default 256x192
    --frames N     run N frames then exit (default: infinite loop)
"""

import argparse
import ctypes
import os
import sys

import pygame


def default_lib():
    """First existing candidate for this platform, repo-root relative.

    Resolved against the repository root (two levels up from this script)
    so the default works from any working directory.
    """
    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    candidates = (
        ["build/bin/Debug/zbapi.dll", "build/bin/Release/zbapi.dll"]
        if sys.platform == "win32"
        else ["build_linux/lib/libzbapi.so", "build_py/lib/libzbapi.so"]
    )
    for rel in candidates:
        path = os.path.join(root, rel)
        if os.path.exists(path):
            return path
    return os.path.join(root, candidates[0])

# ---- input constants, must match binding/include/zbapi.h ----
ZB_INPUT_TOUCH_DOWN = 9
ZB_INPUT_TOUCH_UP = 10
ZB_INPUT_TOUCH_MOVE = 11
ZB_INPUT_KEY_DOWN = 12

ZB_KEY_TAB = 9
ZB_KEY_ENTER = 13
ZB_KEY_SPACE = 32
ZB_KEY_UP = 256
ZB_KEY_DOWN = 257
ZB_KEY_LEFT = 258
ZB_KEY_RIGHT = 259

# pygame key code -> zb key code for the keys the framework needs
_KEY_MAP = {
    pygame.K_UP: ZB_KEY_UP,
    pygame.K_DOWN: ZB_KEY_DOWN,
    pygame.K_LEFT: ZB_KEY_LEFT,
    pygame.K_RIGHT: ZB_KEY_RIGHT,
    pygame.K_RETURN: ZB_KEY_ENTER,
    pygame.K_TAB: ZB_KEY_TAB,
    pygame.K_SPACE: ZB_KEY_SPACE,
}


def load_zbapi(path):
    path = os.path.abspath(path)
    if not os.path.exists(path):
        raise SystemExit("zbapi library not found: %s (cwd=%s)"
                         % (path, os.getcwd()))
    # dependent DLLs (imcore.dll) live next to the library; register the
    # directory so loading works regardless of the working directory
    if sys.platform == "win32":
        os.add_dll_directory(os.path.dirname(path))
    lib = ctypes.CDLL(path)
    lib.zb_app_create.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
    lib.zb_app_create.restype = ctypes.c_void_p
    lib.zb_app_destroy.argtypes = [ctypes.c_void_p]
    lib.zb_input.argtypes = [ctypes.c_void_p, ctypes.c_int,
                             ctypes.c_int, ctypes.c_int, ctypes.c_int,
                             ctypes.c_int, ctypes.c_int]  # type,x,y,key,ch,touch_id
    lib.zb_paint.argtypes = [ctypes.c_void_p]
    lib.zb_buffer.argtypes = [ctypes.c_void_p,
                              ctypes.POINTER(ctypes.c_uint32),
                              ctypes.POINTER(ctypes.c_uint32)]
    lib.zb_buffer.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.zb_set_painted_callback.argtypes = [
        ctypes.c_void_p, ctypes.CFUNCTYPE(None, ctypes.c_void_p), ctypes.c_void_p]
    return lib


def bgra_to_rgba(data, length):
    """Framebuffer is 32-bit bgra (B,G,R,A bytes); pygame wants rgba.
    The alpha byte is NOT part of the buffer contract (it is padding) --
    pinned to opaque so the per-pixel-alpha blit can never make pixels
    transparent (borders drawn with colors::Black used to vanish)."""
    buf = bytes(data[:length])
    out = bytearray(buf)
    out[0::4] = buf[2::4]
    out[2::4] = buf[0::4]
    out[3::4] = b"\xff" * (length // 4)
    return bytes(out)


def feed_input(lib, app, event):
    """Map pygame events to zb_input calls (touch protocol, mouse = touch).

    The mouse stands in for a single touch pointer, so touch_id is 0.
    """
    if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
        lib.zb_input(app, ZB_INPUT_TOUCH_DOWN, *event.pos, 0, 0, 0)
    elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
        lib.zb_input(app, ZB_INPUT_TOUCH_UP, *event.pos, 0, 0, 0)
    elif event.type == pygame.MOUSEMOTION:
        lib.zb_input(app, ZB_INPUT_TOUCH_MOVE, *event.pos, 0, 0, 0)
    elif event.type == pygame.KEYDOWN:
        key = _KEY_MAP.get(event.key)
        ch = 0
        if key is None and event.unicode and len(event.unicode) == 1:
            ch = ord(event.unicode)  # printable character (B1 channel)
        lib.zb_input(app, ZB_INPUT_KEY_DOWN, 0, 0, key or 0, ch, 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lib", default=default_lib(),
                        help="path to the zbapi shared library "
                             "(default: first existing build output)")
    parser.add_argument("--width", type=int, default=256)
    parser.add_argument("--height", type=int, default=192)
    parser.add_argument("--frames", type=int, default=0,
                        help="paint N frames then exit (default: infinite)")
    args = parser.parse_args()

    lib = load_zbapi(args.lib)

    # keep the ctypes callback alive for the whole session
    painted_calls = []
    painted_cb = ctypes.CFUNCTYPE(None, ctypes.c_void_p)(
        lambda userdata: painted_calls.append(1))

    app = lib.zb_app_create(args.width, args.height)
    if not app:
        print("zb_app_create failed", file=sys.stderr)
        sys.exit(1)

    # demonstration of the painted callback (fires after every frame)
    lib.zb_set_painted_callback(app, painted_cb, None)

    pygame.init()
    screen = pygame.display.set_mode((args.width, args.height))
    clock = pygame.time.Clock()

    w = ctypes.c_uint32()
    h = ctypes.c_uint32()
    running = True
    frames = 0
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                running = False
            else:
                feed_input(lib, app, event)

        # render one frame and present the framebuffer
        lib.zb_paint(app)
        pixels = lib.zb_buffer(app, ctypes.byref(w), ctypes.byref(h))
        if pixels:
            rgba = bgra_to_rgba(pixels, w.value * h.value * 4)
            surface = pygame.image.frombuffer(rgba, (w.value, h.value), "RGBA")
            screen.blit(surface, (0, 0))
        pygame.display.flip()
        clock.tick(60)

        frames += 1
        if args.frames and frames >= args.frames:
            running = False

    pygame.quit()
    lib.zb_app_destroy(app)
    print("ran %d frames, painted callback fired %d times"
          % (frames, len(painted_calls)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
