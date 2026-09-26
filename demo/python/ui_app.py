#!/usr/bin/env python3
"""Python host for a DECLARATIVE app (zb_app_create_from_ui, P3).

The UI is a design file (the same .ui grammar the C++ demo apps pack),
the behavior is plain Python callbacks bound by widget id. No C++ on
the user side at all: the C ABI builds the widget tree from the design
text, Python gets called with the id of whatever was activated.

Usage:
    # interactive
    python3 demo/python/ui_app.py --lib build/build_linux/lib/libzbapi.so

    # headless scripted run (the CI-style verification):
    SDL_VIDEODRIVER=dummy python3 demo/python/ui_app.py \
        --lib build/build_linux/lib/libzbapi.so --keys tab,enter --frames 5

Options:
    --lib <path>   path to the shared zbapi library; the default is the
                   first existing build output for the platform
    --size WxH     window size, default 320x240
    --keys LIST    comma-separated key script (tab/enter/space) fed
                   before the frame loop, for scripted runs
    --frames N     run N frames then exit (default: infinite loop)
"""

import argparse
import ctypes
import os
import sys

import pygame

from myapp import bgra_to_rgba, default_lib, feed_input, load_zbapi

# a design file: static structure + ids only -- behavior lives in the
# Python callbacks below (the declarative boundary, docs/design-file.md)
UI_TEXT = """column id="root" spacing=8 padding=12
  label id="title" text="Counter demo (declarative)"
  label id="count" text="Clicks: 0"
  button id="inc" text="Count up"
  button id="reset" text="Reset"
"""

KEY_SCRIPT = {"tab": (12, 9), "enter": (12, 13), "space": (12, 32)}  # (type, key)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lib", default=default_lib(),
                        help="path to the zbapi shared library")
    parser.add_argument("--size", default="320x240",
                        help="window size WxH (default 320x240)")
    parser.add_argument("--keys", default="",
                        help="comma-separated key script fed before the loop")
    parser.add_argument("--frames", type=int, default=0,
                        help="paint N frames then exit (default: infinite)")
    args = parser.parse_args()
    width, height = (int(v) for v in args.size.split("x"))

    lib = load_zbapi(args.lib)

    lib.zb_app_create_from_ui.argtypes = [
        ctypes.c_char_p, ctypes.c_int, ctypes.c_uint32, ctypes.c_uint32]
    lib.zb_app_create_from_ui.restype = ctypes.c_void_p
    action_cb_type = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_void_p)
    lib.zb_set_event_callback.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, action_cb_type, ctypes.c_void_p]
    lib.zb_widget_text.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    lib.zb_widget_text.restype = ctypes.c_int
    lib.zb_widget_set_text.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]

    clicks = [0]

    def read_label():
        buf = ctypes.create_string_buffer(64)
        lib.zb_widget_text(app, b"count", buf, 64)
        return buf.value.decode("utf-8")

    def on_action(widget_id, userdata):
        # fires inside the driving zb_* call (single thread); the label
        # write marks damage -- the next zb_paint presents it
        _ = userdata
        widget = widget_id.decode("utf-8")
        print("action: %s" % widget)
        if widget == "inc":
            clicks[0] += 1
            lib.zb_widget_set_text(app, b"count",
                                   ("Clicks: %d" % clicks[0]).encode())
        elif widget == "reset":
            clicks[0] = 0
            lib.zb_widget_set_text(app, b"count", b"Clicks: 0")

    # keep the ctypes callback object alive for the whole session
    action_cb = action_cb_type(on_action)

    app = lib.zb_app_create_from_ui(UI_TEXT.encode(), 0, width, height)
    if not app:
        print("zb_app_create_from_ui failed", file=sys.stderr)
        sys.exit(1)
    lib.zb_set_event_callback(app, b"inc", action_cb, None)
    lib.zb_set_event_callback(app, b"reset", action_cb, None)
    print("label reads: %r" % read_label())

    pygame.init()
    screen = pygame.display.set_mode((width, height))
    clock = pygame.time.Clock()

    class FakeEvent:  # feeds the key script through myapp.feed_input
        def __init__(self, key):
            self.type = pygame.KEYDOWN
            self.key = {9: pygame.K_TAB, 13: pygame.K_RETURN,
                        32: pygame.K_SPACE}[key]
            self.unicode = ""

    for token in filter(None, args.keys.split(",")):
        zb_type, zb_key = KEY_SCRIPT[token]
        _ = zb_type  # feed_input maps the pygame key; type rides KEYDOWN
        feed_input(lib, app, FakeEvent(zb_key))
        print("scripted key:", token)

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
    final_label = read_label()
    lib.zb_app_destroy(app)
    print("ran %d frames, label reads: %r" % (frames, final_label))
    return 0


if __name__ == "__main__":
    sys.exit(main())
