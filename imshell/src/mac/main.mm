// macOS AppKit shell (A-20). One shell, one codebase: no macOS 13/14
// splits -- every AppKit/CG API used here predates both releases; the
// deployment target is the toolchain layer's decision
// (CMAKE_OSX_DEPLOYMENT_TARGET, see imshell/CMakeLists.txt).
//
// Structure mirrors the other shells through the A-2 seams:
//   - InputSource: NSEvent -> input_event lives in nsevent helpers with
//     the same shape as win_input / x11_input; events feed the app
//     through zb::shell::feed_input
//   - presentation: painted callbacks decide the region through
//     region_to_present and invalidate the view; AppKit unions the
//     invalidated rects itself, so no coalescer is needed; drawRect
//     presents through one CGImage wrap (zero copy) of the app buffer
//
// 32bpp only, like the win shell's blit NOTE: the CGImage wraps the
// buffer as 32-bit little-endian ARGB (B,G,R,A bytes, straight alpha),
// which is exactly the internal layout at COLOR_DEPTH=32.

#import <Cocoa/Cocoa.h>

#include <cstdio>
#include <iostream>

#include "imcore.hpp"
#include "input.hpp"
#include "app_maker.hpp"
#include "shell/input_source.hpp"
#include "shell/platform_font.hpp"
#include "shell/presenter.hpp"
#include "logging.hpp"

using namespace zb::app;

// the full @interface must precede the helpers: nsevent_to_pointer sends
// messages to g_view, and under ARC a forward-declared receiver is a
// hard error (the send needs the method return types for ownership),
// not a warning like in manual reference counting
@interface ImprintView : NSView
@end

namespace
{
    zb::SharedPtr<IApp> g_app;
    ImprintView *g_view = nil;
    CGImageRef g_image = nullptr;
    int g_buffer_width = 0;
    int g_buffer_height = 0;

    void feed(const zb::input::input_event &ev)
    {
        if (g_app != nullptr)
        {
            zb::shell::feed_input(*g_app, ev);
        }
    }

    /*
     * A-2 InputSource, keyboard half: navigation keys map from the
     * hardware key codes (stable across layouts); printable text comes
     * from the characters string (already layout aware). Returns false
     * when the event produces nothing the framework understands.
     */
    bool nsevent_to_key(NSEvent *nse, zb::input::input_event &out)
    {
        out = zb::input::input_event{};
        out.type = zb::input::input_type::key_down;
        switch (nse.keyCode)
        {
            case 36: out.key = static_cast<int>(zb::input::key_code::enter); break;  // return
            case 48: out.key = static_cast<int>(zb::input::key_code::tab); break;
            case 53: out.key = static_cast<int>(zb::input::key_code::escape); break;
            case 49: out.key = static_cast<int>(zb::input::key_code::space); break;
            case 51: out.key = static_cast<int>(zb::input::key_code::backspace); break;
            case 117: out.key = static_cast<int>(zb::input::key_code::del); break;   // forward delete
            case 126: out.key = static_cast<int>(zb::input::key_code::up); break;
            case 125: out.key = static_cast<int>(zb::input::key_code::down); break;
            case 123: out.key = static_cast<int>(zb::input::key_code::left); break;
            case 124: out.key = static_cast<int>(zb::input::key_code::right); break;
            default: break;
        }
        if (out.key == 0)
        {
            NSString *chars = nse.characters;
            if (chars.length > 0)
            {
                const unichar c = [chars characterAtIndex:0];
                if (c >= 0x20 && c <= 0x7e && c != ' ')
                {
                    // printable ASCII only, like the other shells' character
                    // paths; space keeps its navigation-key routing (B1)
                    out.ch = c;
                }
            }
        }
        return out.key != 0 || out.ch != 0;
    }

    /*
     * A-2 InputSource, pointer half: window points (bottom-left origin)
     * become top-left surface pixels. The buffer pixel is one point (the
     * fixed-size buffer presents at 1x scale).
     */
    zb::input::input_event nsevent_to_pointer(const zb::input::input_type type, NSEvent *nse)
    {
        zb::input::input_event ev;
        ev.type = type;
        ev.touch_id = 0;
        const NSPoint p = [g_view convertPoint:nse.locationInWindow fromView:nil];
        ev.x = static_cast<int>(p.x);
        const int y = static_cast<int>(g_view.bounds.size.height - p.y);
        ev.y = y < 0 ? 0 : y;  // the bottom edge lands exactly on the bound
        return ev;
    }
}

@implementation ImprintView

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)mouseDown:(NSEvent *)e
{
    feed(nsevent_to_pointer(zb::input::input_type::mouse_left_down, e));
}
- (void)mouseUp:(NSEvent *)e
{
    feed(nsevent_to_pointer(zb::input::input_type::mouse_left_up, e));
}
- (void)rightMouseDown:(NSEvent *)e
{
    feed(nsevent_to_pointer(zb::input::input_type::mouse_right_down, e));
}
- (void)rightMouseUp:(NSEvent *)e
{
    feed(nsevent_to_pointer(zb::input::input_type::mouse_right_up, e));
}
- (void)mouseMoved:(NSEvent *)e
{
    // hover/drag moves drive the dispatcher: slop cancel, captured moves
    // (slider/listbox drag) and hover repaints all read them
    feed(nsevent_to_pointer(zb::input::input_type::mouse_move, e));
}
- (void)mouseDragged:(NSEvent *)e
{
    feed(nsevent_to_pointer(zb::input::input_type::mouse_move, e));
}
- (void)scrollWheel:(NSEvent *)e
{
    // normalize to signed notches like the other shells (A-15): one line
    // of scroll is one notch
    const CGFloat dy = e.deltaY;
    if (dy == 0)
    {
        return;
    }
    zb::input::input_event ev = nsevent_to_pointer(zb::input::input_type::mouse_wheel, e);
    ev.delta = dy > 0 ? 1 : -1;
    feed(ev);
}
- (void)keyDown:(NSEvent *)e
{
    zb::input::input_event ev;
    if (nsevent_to_key(e, ev))
    {
        feed(ev);
    }
    // unmapped keys without a character are dropped, like win_input's
    // swallowed keydowns
}

// diagnostic (vertical-flip investigation): record the draw-state that
// dictates how CGContextDrawImage lands in the view (isFlipped, layer
// path, base CTM) so the macOS 10.13 vs 13 contexts can be compared.
static void log_draw_state(NSView *view)
{
    NSGraphicsContext *nsc = [NSGraphicsContext currentContext];
    CGContextRef ctx = nsc ? nsc.CGContext : nullptr;
    const CGAffineTransform base = ctx ? CGContextGetCTM(ctx) : CGAffineTransformIdentity;
    FILE *f = fopen("/tmp/imprint_ctm.log", "w");
    if (f == nullptr)
    {
        return;
    }
    fprintf(f, "isFlipped=%d wantsLayer=%d layer=%p layerBacked=%d "
               "isLayerBacked=%d windowLayerBacked=%d windowBacking=%lu\n"
               "baseCTM a=%g b=%g c=%g d=%g tx=%g ty=%g\n"
               "bounds=%gx%g windowContentScale=%g\n",
            (int)view.isFlipped, (int)view.wantsLayer, view.layer,
            (int)view.layerContentsRedrawPolicy, (int)view.isLayerBacked,
            (int)(view.window ? [view.window.contentView isLayerBacked] : 0),
            (unsigned long)(view.window ? view.window.backingType : 0),
            base.a, base.b, base.c, base.d, base.tx, base.ty,
            view.bounds.size.width, view.bounds.size.height,
            (double)(view.window ? view.window.backingScaleFactor : 1.0));
    fclose(f);
}

- (void)drawRect:(NSRect)dirtyRect
{
    if (g_image == nullptr)
    {
        return;
    }
    NSGraphicsContext *nsc = [NSGraphicsContext currentContext];
    if (nsc == nil)
    {
        return;
    }
    log_draw_state(self);
    CGContextRef ctx = nsc.CGContext;
    CGContextSaveGState(ctx);
    // the framework buffer is top-down; CG draws bottom-up
    CGContextTranslateCTM(ctx, 0.0, self.bounds.size.height);
    CGContextScaleCTM(ctx, 1.0, -1.0);
    CGContextDrawImage(ctx, self.bounds, g_image);
    CGContextRestoreGState(ctx);
}

@end

@interface ImprintAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation ImprintAppDelegate

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    return YES;
}

@end

// diagnostic: dump the raw pixel buffer as a 32bpp BMP file.
// Memory layout is [B,G,R,A] (bgra32_le); BMP 32bpp expects [B,G,R,X]
// so the bytes can be written directly. Negative height = top-down.
static void dump_bmp(const char *path, const void *pixels, int width, int height)
{
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        return;
    }
    const int row_bytes = width * 4;
    const int pixel_data_size = row_bytes * height;
    const int file_size = 14 + 40 + pixel_data_size;

    // BMP file header (14 bytes)
    uint8_t fh[14] = {};
    fh[0] = 'B';
    fh[1] = 'M';
    fh[2] = static_cast<uint8_t>(file_size);
    fh[3] = static_cast<uint8_t>(file_size >> 8);
    fh[4] = static_cast<uint8_t>(file_size >> 16);
    fh[5] = static_cast<uint8_t>(file_size >> 24);
    fh[10] = 54;  // offset to pixel data

    // BITMAPINFOHEADER (40 bytes)
    uint8_t ih[40] = {};
    ih[0] = 40;  // header size
    ih[4] = static_cast<uint8_t>(width);
    ih[5] = static_cast<uint8_t>(width >> 8);
    ih[6] = static_cast<uint8_t>(width >> 16);
    ih[7] = static_cast<uint8_t>(width >> 24);
    const int neg_h = -height;  // negative = top-down
    ih[8] = static_cast<uint8_t>(neg_h);
    ih[9] = static_cast<uint8_t>(neg_h >> 8);
    ih[10] = static_cast<uint8_t>(neg_h >> 16);
    ih[11] = static_cast<uint8_t>(neg_h >> 24);
    ih[12] = 1;  // planes
    ih[14] = 32;  // bpp
    ih[20] = static_cast<uint8_t>(pixel_data_size);
    ih[21] = static_cast<uint8_t>(pixel_data_size >> 8);
    ih[22] = static_cast<uint8_t>(pixel_data_size >> 16);
    ih[23] = static_cast<uint8_t>(pixel_data_size >> 24);

    fwrite(fh, 1, 14, f);
    fwrite(ih, 1, 40, f);
    fwrite(pixels, 1, pixel_data_size, f);
    fclose(f);
}

int main(int argc, char *argv[])
{
    zb::Logging::set_log_handle([](const zb::Logging_Level &level, const std::string &message)
    {
        std::cerr << message;
    });

    LI << "run on mac (AppKit)";

    if (sizeof(zb::ui::core::Color) != 4)
    {
        LE << "the mac shell presents the 32bpp internal layout only";
        return 1;
    }

    @autoreleasepool
    {
        zb::shell::install_platform_font();
        g_app = make_app();
        g_app->create_window();
        const auto window = g_app->window();
        g_buffer_width = window->width();
        g_buffer_height = window->height();

        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        ImprintAppDelegate *delegate = [ImprintAppDelegate new];
        app.delegate = delegate;

        NSRect content = NSMakeRect(0, 0, g_buffer_width, g_buffer_height);
        NSWindow *win = [[NSWindow alloc]
            initWithContentRect:content
                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                  NSWindowStyleMaskMiniaturizable
                          backing:NSBackingStoreBuffered
                            defer:NO];
        win.title = [NSString stringWithUTF8String:window->title().c_str()];
        // the framebuffer is fixed-size: no resize (a resize could only
        // crop the buffer with no redraw), like every other shell
        win.styleMask &= ~NSWindowStyleMaskResizable;

        ImprintView *view = [[ImprintView alloc] initWithFrame:content];
        win.contentView = view;
        [win setAcceptsMouseMovedEvents:YES];
        [win center];
        g_view = view;

        // zero-copy wrap of the app buffer: B,G,R,A bytes == 32-bit
        // little-endian ARGB, straight alpha (kCGImageAlphaFirst). The
        // image reads live memory, so drawRect always presents the
        // current frame
        const size_t bytes_per_row = static_cast<size_t>(g_buffer_width) * sizeof(zb::ui::core::Color);
        CGDataProviderRef provider = CGDataProviderCreateWithData(
            nullptr, window->data(), bytes_per_row * g_buffer_height, nullptr);
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        g_image = CGImageCreate(
            static_cast<size_t>(g_buffer_width), static_cast<size_t>(g_buffer_height),
            8, 32, bytes_per_row, cs,
            kCGBitmapByteOrder32Little | kCGImageAlphaFirst,
            provider, nullptr, false, kCGRenderingIntentDefault);
        CGColorSpaceRelease(cs);
        CGDataProviderRelease(provider);

        // the shell calls paint() to request a frame; the "painted" event
        // asks the shell to present (A-2 presentation seam). AppKit unions
        // invalidated rects until the next drawRect, so the pending region
        // needs no explicit coalescer
        g_app->on_painted([](const void *data)
        {
            if (data == nullptr || g_view == nil)
            {
                return;
            }
            int x = 0, y = 0, w = 0, h = 0;
            const bool dirty = g_app->dirty_region(x, y, w, h);
            const zb::shell::present_rect r = zb::shell::region_to_present(
                dirty, x, y, w, h, g_buffer_width, g_buffer_height);
            if (r.w <= 0)
            {
                return;  // nothing was drawn, nothing to present
            }
            // top-left surface pixels -> bottom-left view points
            [g_view setNeedsDisplayInRect:NSMakeRect(r.x, g_buffer_height - r.y - r.h, r.w, r.h)];
        });

        // the app requests to quit by closing its window (e.g. a QUIT
        // button): stop the run loop. stop: takes effect one event later,
        // so wake the loop with a dummy application event
        g_app->on_closed([]()
        {
            dispatch_async(dispatch_get_main_queue(), ^
            {
                [NSApp stop:NSApp];
                [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                                    location:NSMakePoint(0, 0)
                                               modifierFlags:0
                                                   timestamp:0
                                                windowNumber:0
                                                     context:nil
                                                     subtype:0
                                                       data1:0
                                                       data2:0]
                          atStart:YES];
            });
        });

        g_app->paint();  // fill the buffer before the first show
        dump_bmp("/tmp/imprint_buffer.bmp", window->data(), g_buffer_width, g_buffer_height);
        [win makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES];
        [app run];

        if (g_image != nullptr)
        {
            CGImageRelease(g_image);
            g_image = nullptr;
        }
    }

    return 0;
}
