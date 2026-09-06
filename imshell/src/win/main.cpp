#include <windows.h>
#include <windowsx.h>
#include <memory>
#include "imcore.hpp"
#include "input.hpp"
#include "app_maker.hpp"
#include "shell/input_source.hpp"
#include "shell/platform_font.hpp"
#include "shell/presentation.hpp"
#include "shell/presenter.hpp"
#include "win_input.hpp"

using namespace zb::app;
using namespace zb::input;

namespace
{
    const TCHAR AppClassName[] = TEXT("MyApp");

    zb::SharedPtr<IApp> g_app;
    HWND g_hwnd = nullptr;
    const void *g_framebuffer = nullptr; // framebuffer blitted on WM_PAINT
    LONG g_buffer_width = 0;
    LONG g_buffer_height = 0;
    // painted events coalesce before WM_PAINT runs (it is the
    // lowest-priority message): the union of every invalidated region
    // survives here until the present (A-2 presentation seam)
    zb::shell::dirty_coalescer g_pending;
    // I-2a: the presented mapping of the current client area, kept
    // current by WM_SIZE for the input inverse map (paint recomputes it
    // from the live client rect)
    zb::shell::presentation g_presentation;

    // the only place the window size enters the shell: the fixed-size
    // buffer fitted into the current client area (I-2a)
    zb::shell::presentation current_presentation(const HWND hwnd)
    {
        RECT rc;
        if (!GetClientRect(hwnd, &rc))
        {
            return {};
        }
        return zb::shell::presentation_fit(rc.right - rc.left, rc.bottom - rc.top,
                                           g_buffer_width, g_buffer_height);
    }

    // pointer events whose window-client point lands on the letterbox are
    // not app input; keyboard and other events carry no pointer position
    // and always pass. to_buffer is the exact inverse of the stretch, so
    // a hit lands on the pixel shown under the cursor
    bool pointer_in_buffer(const input_event &ev, int &bx, int &by)
    {
        if (!zb::shell::maps_pointer(ev.type))
        {
            bx = ev.x;
            by = ev.y;
            return true;
        }
        return g_presentation.to_buffer(ev.x, ev.y, bx, by);
    }

    // called on every "painted" event, requests a repaint by invalidating
    // only the region that was actually drawn
    void handle_painted(const void *data)
    {
        if (nullptr == data)
        {
            return;
        }
        g_framebuffer = data;
        if (nullptr == g_hwnd)
        {
            return;
        }
        int x = 0, y = 0, w = 0, h = 0;
        const bool dirty = g_app->dirty_region(x, y, w, h);
        if (!dirty)
        {
            // no dirty tracking: present the whole buffer
            g_pending.add(0, 0, g_buffer_width, g_buffer_height);
            InvalidateRect(g_hwnd, nullptr, FALSE);
            return;
        }
        // an empty frame adds nothing and must not drop pending regions
        g_pending.add(x, y, w, h);
        // the buffer region invalidates the dest rect it stretches into
        // (I-2a); a resize invalidates the whole client anyway, so a
        // stale mapping between the two cannot lose pixels
        const zb::shell::presentation pres = current_presentation(g_hwnd);
        const zb::shell::present_rect d =
            zb::shell::presentation_region(pres, zb::shell::present_rect{x, y, w, h});
        if (d.w <= 0)
        {
            return;
        }
        RECT rc{d.x, d.y, d.x + d.w, d.y + d.h};
        InvalidateRect(g_hwnd, &rc, FALSE);
    }

    void paint_app(const HWND hwnd, HDC hDC, const RECT &rc_paint)
    {
        if (nullptr == g_framebuffer || 0 == g_buffer_width || 0 == g_buffer_height)
        {
            return;
        }
        const zb::shell::presentation pres = current_presentation(hwnd);
        if (pres.w <= 0 || pres.h <= 0)
        {
            return;
        }

        // 1:1 presents only the region the painted callbacks accumulated
        // (the long-verified path); a scaled presentation always presents
        // the whole fitted rect from the live buffer (the mac drawRect
        // strategy): the fill-then-partial-stretch alternative relies on
        // the update region never exceeding dest(pending) -- an invariant
        // shell-side bookkeeping cannot guarantee across the system's own
        // invalidations -- and any rect it misses is erased by the
        // letterbox fill but not redrawn (content black-holes). A full
        // self-covering present cannot lose pixels by construction.
        zb::shell::present_rect r = g_pending.get();
        const bool one_to_one = pres.x == 0 && pres.y == 0 &&
                                pres.w == g_buffer_width &&
                                pres.h == g_buffer_height;
        if (!one_to_one ||
            (rc_paint.left <= 0 && rc_paint.top <= 0 &&
             rc_paint.right >= pres.x + pres.w && rc_paint.bottom >= pres.y + pres.h))
        {
            r = zb::shell::present_rect{0, 0, g_buffer_width, g_buffer_height};
        }
        g_pending.clear();  // presented; the next painted callback accumulates afresh

        // the letterbox: the client area outside the presented rect is
        // not app content; the blit below is clipped to the paint region
        // by BeginPaint, so the fill stays flicker-free
        FillRect(hDC, &rc_paint, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        // NOTE: this blit assumes 32bpp (biBitCount = 32, BI_RGB). A
        // COLOR_DEPTH=16 build would produce an abgr1555 buffer while the
        // 16bpp DIB expects 555, swapping red and blue -- 16bpp is meant
        // for embedded displays only (see imcore/CMakeLists.txt).
        BITMAPINFO bmi = {
            sizeof(BITMAPINFOHEADER),
            g_buffer_width,
            -g_buffer_height, // top-down
            1,
            zb::ui::core::ImColor_Depth,
            BI_RGB,
            (DWORD)((((g_buffer_width * zb::ui::core::ImColor_Depth) + 31) & ~31) >> 3) * (DWORD)g_buffer_height,
            0, 0, 0, 0};

        // I-2a: the buffer region stretches into its dest rect with
        // nearest-neighbor resampling (COLORONCOLOR) -- the dest rect is
        // ceiled conservatively by presentation_region, so a partial
        // present repaints every dest pixel the region can touch. At
        // 1:1 (the default window size) dest and source coincide.
        const zb::shell::present_rect d = zb::shell::presentation_region(pres, r);
        if (d.w <= 0 || d.h <= 0)
        {
            return;
        }
        SetStretchBltMode(hDC, COLORONCOLOR);
        StretchDIBits(
            hDC,
            d.x, d.y, d.w, d.h,
            r.x, r.y, r.w, r.h,
            g_framebuffer, &bmi,
            DIB_RGB_COLORS, SRCCOPY);
    }
} // namespace

extern "C" LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

extern "C" int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    WNDCLASSEX wcx{};
    wcx.cbSize = sizeof(wcx);
    wcx.style = CS_HREDRAW | CS_VREDRAW;
    wcx.lpfnWndProc = WndProc;
    wcx.cbClsExtra = 0;
    wcx.cbWndExtra = 0;
    wcx.hInstance = hInstance;
    wcx.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcx.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcx.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wcx.lpszMenuName = NULL;
    wcx.lpszClassName = AppClassName;
    wcx.hIconSm = wcx.hIcon;

    if (0 == RegisterClassEx(&wcx))
    {
        return -1;
    }

    zb::shell::install_platform_font();
    g_app = make_app();
    g_app->create_window();
    g_app->on_painted(handle_painted);

    // the app requests to quit by closing its window (e.g. a QUIT button)
    g_app->on_closed([]() { PostQuitMessage(0); });

    auto window = g_app->window();
    g_buffer_width = window->width();
    g_buffer_height = window->height();

    // rendering loop protocol (see IApp): the shell calls paint() to
    // request a frame; the app repaints after every input event, and the
    // "painted" event asks the shell to present (InvalidateRect -> WM_PAINT)
    RECT client_rect{0, 0, g_buffer_width, g_buffer_height};
    // I-2a: the window is user-resizable; the fixed-size buffer is
    // presented scaled-to-fit, so a resize never crops anything
    const DWORD dwStyle = WS_OVERLAPPEDWINDOW;
    const DWORD dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
    AdjustWindowRectEx(&client_rect, dwStyle, FALSE, dwExStyle);

    const HWND hwnd = CreateWindowEx(
        dwExStyle,
        AppClassName,
#ifdef UNICODE
        // the window title is a runtime string, so TEXT() cannot be used
        std::wstring(window->title().begin(), window->title().end()).c_str(),
#else
        window->title().c_str(),
#endif
        dwStyle | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        client_rect.right - client_rect.left,
        client_rect.bottom - client_rect.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (nullptr == hwnd)
    {
        return -1;
    }

    g_hwnd = hwnd;
    g_presentation = current_presentation(hwnd);
    // fill the framebuffer before the first show: the initial WM_PAINT
    // then blits real content instead of the uninitialized window
    g_app->paint();
    ShowWindow(hwnd, SW_RESTORE);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

extern "C" LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_ERASEBKGND:
        {
            // prevent flickering
            return 1;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            const HDC hDC = BeginPaint(hwnd, &ps);
            paint_app(hwnd, hDC, ps.rcPaint);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
        {
            // I-2a: the window is user-resizable, the buffer is not --
            // the new client area only changes the presented rect. The
            // whole client is invalidated: the next WM_PAINT re-fills
            // the letterbox and re-stretches the buffer
            g_presentation = current_presentation(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }
        default:
        {
            break;
        }
    }

    // A-2 InputSource: the message -> input_event mapping (key codes,
    // character rules, wheel normalization) lives in win_input::translate,
    // dummy-driven unit-tested; this loop only converts the wheel's screen
    // point (the one message that is not in client coordinates) and feeds
    // the app through the shared seam
    if (g_app != nullptr)
    {
        POINT wheel_client{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (msg == WM_MOUSEWHEEL)
        {
            ScreenToClient(hwnd, &wheel_client);
        }
        input_event ev;
        switch (zb::shell::win_input::translate(msg, wParam, lParam, wheel_client, ev))
        {
            case zb::shell::win_input::result::handled:
            {
                // I-2a: window-client pixels -> buffer pixels; a pointer
                // event on the letterbox is not app input and is dropped
                int bx = 0;
                int by = 0;
                if (!pointer_in_buffer(ev, bx, by))
                {
                    return 0;
                }
                ev.x = bx;
                ev.y = by;
                zb::shell::feed_input(*g_app, ev);
                return 0;
            }
            case zb::shell::win_input::result::swallowed:
            {
                return 0;
            }
            case zb::shell::win_input::result::not_handled:
            {
                break;
            }
        }
    }
    return (int)DefWindowProc(hwnd, msg, wParam, lParam);
}