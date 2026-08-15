#include <windows.h>
#include <windowsx.h>
#include <memory>
#include "imcore.hpp"
#include "input.hpp"
#include "app_maker.hpp"

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

    // called on every "painted" event, requests a repaint by invalidating the window
    void handle_painted(const void *data)
    {
        if (nullptr == data)
        {
            return;
        }
        g_framebuffer = data;
        if (nullptr != g_hwnd)
        {
            InvalidateRect(g_hwnd, nullptr, FALSE);
        }
    }

    void paint_app(HWND hwnd, HDC hDC)
    {
        if (nullptr == g_framebuffer || 0 == g_buffer_width || 0 == g_buffer_height)
        {
            return;
        }

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

        SetDIBitsToDevice(
            hDC,
            0, 0,
            g_buffer_width, g_buffer_height,
            0, 0,
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
    ShowWindow(hwnd, SW_RESTORE);
    UpdateWindow(hwnd);

    g_app->paint(); // trigger the first paint

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
            paint_app(hwnd, hDC);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            break;
        }
        case WM_KEYDOWN:
        {
            input_event ev;
            ev.type = input_type::key_down;
            switch (wParam)
            {
                case VK_RETURN: ev.key = static_cast<int>(key_code::enter); break;
                case VK_TAB: ev.key = static_cast<int>(key_code::tab); break;
                case VK_ESCAPE: ev.key = static_cast<int>(key_code::escape); break;
                case VK_SPACE: ev.key = static_cast<int>(key_code::space); break;
                case VK_BACK: ev.key = static_cast<int>(key_code::backspace); break;
                case VK_DELETE: ev.key = static_cast<int>(key_code::del); break;
                case VK_UP: ev.key = static_cast<int>(key_code::up); break;
                case VK_DOWN: ev.key = static_cast<int>(key_code::down); break;
                case VK_LEFT: ev.key = static_cast<int>(key_code::left); break;
                case VK_RIGHT: ev.key = static_cast<int>(key_code::right); break;
                default: return 0;
            }
            if (g_app)
            {
                g_app->input(ev);
            }
            return 0;
        }
        case WM_CHAR:
        {
            // printable characters arrive as WM_CHAR (after
            // TranslateMessage); the space key keeps its key_code::space
            // routing for navigation activation, other printable ASCII
            // becomes a character event (ch field, see dispatcher B1)
            const int c = static_cast<int>(wParam);
            if (c >= 0x20 && c <= 0x7e && c != ' ')
            {
                input_event ev;
                ev.type = input_type::key_down;
                ev.ch = c;
                if (g_app)
                {
                    g_app->input(ev);
                }
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        {
            input_event ev;
            ev.type = input_type::mouse_left_down;
            ev.touch_id = 0;
            ev.button = mouse_button_t::left;
            ev.x = GET_X_LPARAM(lParam);
            ev.y = GET_Y_LPARAM(lParam);
            if (g_app)
            {
                g_app->input(ev);
            }
            break;
        }
        case WM_LBUTTONUP:
        {
            input_event ev;
            ev.type = input_type::mouse_left_up;
            ev.touch_id = 0;
            ev.button = mouse_button_t::left;
            ev.x = GET_X_LPARAM(lParam);
            ev.y = GET_Y_LPARAM(lParam);
            if (g_app)
            {
                g_app->input(ev);
            }
            break;
        }
        case WM_RBUTTONDOWN:
        {
            input_event ev;
            ev.type = input_type::mouse_right_down;
            ev.touch_id = 0;
            ev.button = mouse_button_t::right;
            ev.x = GET_X_LPARAM(lParam);
            ev.y = GET_Y_LPARAM(lParam);
            if (g_app)
            {
                g_app->input(ev);
            }
            break;
        }
        case WM_RBUTTONUP:
        {
            input_event ev;
            ev.type = input_type::mouse_right_up;
            ev.touch_id = 0;
            ev.button = mouse_button_t::right;
            ev.x = GET_X_LPARAM(lParam);
            ev.y = GET_Y_LPARAM(lParam);
            if (g_app)
            {
                g_app->input(ev);
            }
            break;
        }
        case WM_MOUSEWHEEL:
        {
            // wParam HIWORD: wheel rotation delta. (> 0 roll up, < 0 roll down)
            input_event ev;
            ev.type = input_type::mouse_wheel;
            ev.delta = GET_WHEEL_DELTA_WPARAM(wParam);
            ev.touch_id = 0;
            if (g_app)
            {
                g_app->input(ev);
            }
            break;
        }
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            break;
        }
        default:
        {
            return (int)DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }

    return 0;
}