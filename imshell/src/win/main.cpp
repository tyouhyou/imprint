#include <windows.h>
#include <windowsx.h>
#include <memory>
#include "imcore.hpp"
#include "input.hpp"
#include "app_maker.hpp"
#include "shell/input_source.hpp"
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
        const zb::shell::present_rect p = g_pending.get();
        if (p.w <= 0)
        {
            return;
        }
        RECT rc{p.x, p.y, p.x + p.w, p.y + p.h};
        InvalidateRect(g_hwnd, &rc, FALSE);
    }

    void paint_app(HWND hwnd, HDC hDC, const RECT &rc_paint)
    {
        if (nullptr == g_framebuffer || 0 == g_buffer_width || 0 == g_buffer_height)
        {
            return;
        }

        // a system-triggered repaint (first show, resize) invalidates the
        // whole client area: blit the entire buffer; otherwise only the
        // region the painted callbacks accumulated
        zb::shell::present_rect p = g_pending.get();
        if (rc_paint.left <= 0 && rc_paint.top <= 0 &&
            rc_paint.right >= g_buffer_width && rc_paint.bottom >= g_buffer_height)
        {
            p = zb::shell::present_rect{0, 0, g_buffer_width, g_buffer_height};
        }
        if (p.w <= 0 || p.h <= 0)
        {
            return;
        }
        g_pending.clear();  // presented; the next painted callback accumulates afresh

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

        // the whole DIB is submitted (StartScan = 0, every row) and the
        // source rectangle selects the region. Passing the base pointer
        // together with StartScan = y would tell GDI the buffer holds
        // rows [y, y+h): it would blit the top of the frame for any
        // region below the first row
        SetDIBitsToDevice(
            hDC,
            p.x, p.y,
            p.w, p.h,
            p.x, p.y,
            0, g_buffer_height,
            g_framebuffer, &bmi,
            DIB_RGB_COLORS);
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
    // the framebuffer is fixed-size: no thick frame (a resize could only
    // crop the buffer with no redraw)
    const DWORD dwStyle = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
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