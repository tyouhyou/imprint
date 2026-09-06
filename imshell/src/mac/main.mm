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
//     presents the fixed buffer fitted into the view (I-2a, shared
//     integer seam) through one CGImage wrap (zero copy) of the app
//     buffer
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
#include "shell/platform_font.hpp"
#include "shell/presentation.hpp"
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
     * become top-left surface pixels through the I-2a inverse map -- the
     * same integer floor formula the forward stretch samples with.
     * Returns false when the point is on the letterbox (not app input;
     * the caller swallows the event).
     */
    bool nsevent_to_pointer(const zb::input::input_type type, NSEvent *nse,
                            zb::input::input_event &out)
    {
        out = {};
        out.type = type;
        out.touch_id = 0;
        const NSRect b = g_view.bounds;
        const zb::shell::presentation pres = zb::shell::presentation_fit(
            static_cast<int>(b.size.width), static_cast<int>(b.size.height),
            g_buffer_width, g_buffer_height);
        const NSPoint p = [g_view convertPoint:nse.locationInWindow fromView:nil];
        int bx = 0;
        int by = 0;
        if (!pres.to_buffer(static_cast<int>(p.x),
                            static_cast<int>(b.size.height - p.y), bx, by))
        {
            return false;
        }
        out.x = bx;
        out.y = by;
        return true;
    }

    // feeds one pointer event when it lands on the presented buffer
    void feed_pointer(const zb::input::input_type type, NSEvent *nse)
    {
        zb::input::input_event ev;
        if (nsevent_to_pointer(type, nse, ev))
        {
            feed(ev);
        }
    }
}

@implementation ImprintView

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)mouseDown:(NSEvent *)e
{
    feed_pointer(zb::input::input_type::mouse_left_down, e);
}
- (void)mouseUp:(NSEvent *)e
{
    feed_pointer(zb::input::input_type::mouse_left_up, e);
}
- (void)rightMouseDown:(NSEvent *)e
{
    feed_pointer(zb::input::input_type::mouse_right_down, e);
}
- (void)rightMouseUp:(NSEvent *)e
{
    feed_pointer(zb::input::input_type::mouse_right_up, e);
}
- (void)mouseMoved:(NSEvent *)e
{
    // hover/drag moves drive the dispatcher: slop cancel, captured moves
    // (slider/listbox drag) and hover repaints all read them
    feed_pointer(zb::input::input_type::mouse_move, e);
}
- (void)mouseDragged:(NSEvent *)e
{
    feed_pointer(zb::input::input_type::mouse_move, e);
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
    zb::input::input_event ev;
    if (!nsevent_to_pointer(zb::input::input_type::mouse_wheel, e, ev))
    {
        return;  // the cursor rests on the letterbox: not app input
    }
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
    if (g_buffer_width <= 0 || g_buffer_height <= 0 || g_app == nullptr)
    {
        return;
    }
    auto window = g_app->window();
    if (window == nullptr || window->width() <= 0 || window->height() <= 0)
    {
        return;
    }
    NSGraphicsContext *nsc = [NSGraphicsContext currentContext];
    if (nsc == nil)
    {
        return;
    }

    // zero-copy wrap of the current app buffer (B,G,R,A bytes == 32-bit
    // little-endian ARGB, straight alpha). The image is built fresh on
    // EVERY drawRect: a CGImage created once over a mutable buffer is
    // only guaranteed to present the bytes it decoded first (older
    // macOS caches the decode), so any later frame read through the
    // same image object was stale -- the first view was all the user
    // ever saw. A per-frame image keeps every present current.
    const size_t bytes_per_row = static_cast<size_t>(g_buffer_width) * sizeof(zb::ui::core::Color);
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        nullptr, window->data(), bytes_per_row * g_buffer_height, nullptr);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGImageRef frame = CGImageCreate(
        static_cast<size_t>(g_buffer_width), static_cast<size_t>(g_buffer_height),
        8, 32, bytes_per_row, cs,
        kCGBitmapByteOrder32Little | kCGImageAlphaFirst,
        provider, nullptr, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(cs);
    CGDataProviderRelease(provider);

    CGContextRef ctx = nsc.CGContext;
    CGContextSaveGState(ctx);
    // I-2a: the fixed buffer is presented fitted into the view, aspect
    // preserved and centered; the letterbox is filled black first (the
    // blit below is clipped to the dirty rect by AppKit)
    const NSRect bounds = self.bounds;
    const zb::shell::presentation pres = zb::shell::presentation_fit(
        static_cast<int>(bounds.size.width), static_cast<int>(bounds.size.height),
        g_buffer_width, g_buffer_height);
    [[NSColor blackColor] setFill];
    NSRectFillUsingOperation(bounds, NSCompositingOperationCopy);
    if (pres.w > 0 && pres.h > 0)
    {
        // nearest-neighbor resampling keeps the pixels identical to the
        // other desktop shells at the same window size (I-2a)
        CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
        // buffer row 0 belongs at the view top. AppKit hands drawRect a base
        // context whose Y orientation differs across window backing paths:
        // buffered windows (macOS 10.13: base.d > 0, y-up, identity CTM) vs
        // layer-backed windows (macOS >= 10.14: base.d < 0, y-down).
        // CGContextDrawImage puts the first data row at the rect top when the
        // effective CTM is y-up, so flip Y exactly when the base is y-down.
        // (Measured on 10.13.6 + verified logic on macOS 13; a retina base.d
        // keeps its sign, so the check is scale-invariant.)
        const CGAffineTransform base = CGContextGetCTM(ctx);
        if (base.d < 0.0)
        {
            CGContextTranslateCTM(ctx, 0.0, bounds.size.height);
            CGContextScaleCTM(ctx, 1.0, -1.0);
        }
        // the dest rect in the normalized y-up context: the image's top
        // row lands p.y points below the view top
        const CGRect dest = CGRectMake(pres.x, bounds.size.height - pres.y - pres.h,
                                       pres.w, pres.h);
        CGContextDrawImage(ctx, dest, frame);
    }
    CGImageRelease(frame);
    CGContextRestoreGState(ctx);
}

// I-2a: a resize changes the presented rect (and the letterbox bands) --
// redraw everything, not just the changed band
- (void)setFrameSize:(NSSize)newSize
{
    [super setFrameSize:newSize];
    [self setNeedsDisplayInRect:self.bounds];
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

    LD << "run on mac (AppKit)";

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
        // I-2a: the window is user-resizable; the fixed-size buffer is
        // presented scaled-to-fit, so a resize never crops anything
        win.styleMask |= NSWindowStyleMaskResizable;

        ImprintView *view = [[ImprintView alloc] initWithFrame:content];
        win.contentView = view;
        [win setAcceptsMouseMovedEvents:YES];
        [win center];
        g_view = view;

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
            // I-2a: the buffer region invalidates the view rect it
            // stretches into (top-left dest points -> bottom-left view
            // points); a resize re-invalidates the whole view through
            // setFrameSize, so a stale mapping cannot lose pixels
            const NSRect b = g_view.bounds;
            const zb::shell::presentation pres = zb::shell::presentation_fit(
                static_cast<int>(b.size.width), static_cast<int>(b.size.height),
                g_buffer_width, g_buffer_height);
            const zb::shell::present_rect d = zb::shell::presentation_region(pres, r);
            if (d.w <= 0)
            {
                return;
            }
            [g_view setNeedsDisplayInRect:NSMakeRect(d.x, b.size.height - d.y - d.h, d.w, d.h)];
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
    }

    return 0;
}
