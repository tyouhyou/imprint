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

#include <iostream>

#include "imcore.hpp"
#include "input.hpp"
#include "app_maker.hpp"
#include "shell/input_source.hpp"
#include "shell/presenter.hpp"
#include "logging.hpp"

using namespace zb::app;

@class ImprintView;

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

@interface ImprintView : NSView
@end

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
