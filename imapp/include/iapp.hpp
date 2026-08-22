#ifndef IAPP_HPP
#define IAPP_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include "imcore.hpp"
#include "iwindow.hpp"
#include "core/ptr.hpp"
#include "event_def.hpp"

namespace zb::app
{
    /*
     * Application interface.
     *
     * Rendering loop protocol (the shell owns the main loop, see imshell/):
     *   - input events are forwarded via input(); the app repaints as
     *     needed (CanvasWindow repaints after every input event)
     *   - the shell calls paint() to request a frame; the app renders and
     *     emits the "painted" event so the shell can present the buffer
     *   - on always-on displays (linux-fb, NDS) the shell simply calls
     *     paint() every frame of its own main loop
     */
    class IApp
    {
    private:

    public:
        virtual ~IApp() = default;

        /*
         * @brief create the application window
         *        The IApp implementation will determine the size of the window, and create a buffer for initial data.
         * @return shared pointer to the GUI instance
         */
        virtual void create_window() = 0;

        /*
         * @brief Create the application window
         *        When the display device or buffer is limited on the specified platform
         *        (say, a embed system which exposes a solid framebuffer),
         *        the creation of window is not determined by the implement of IApp, but by the platform control.
         *        Use this method to create the window with a specific size and an optional buffer,
         *        otherwise, use the default create_window() method.
         * @param max_client_width maximum width of the client area
         * @param max_client_height maximum height of the client area
         * @param buffer for initial data (must be writable, see Graphics)
         * @return shared pointer to the GUI instance
         */
        virtual void create_window(const uint32_t &max_client_width, const uint32_t &max_client_height, void *buffer) = 0;

        virtual void create_window(const uint32_t &max_client_width, const uint32_t &max_client_height) = 0;

        virtual zb::SharedPtr<IWindow> window() noexcept = 0;

        /*
         * @brief fire the input event
         */
        virtual void input(const input::input_event &ev) noexcept = 0;
        virtual void paint() = 0;

        /*
         * @brief whether a repaint is owed: until the first paint, after
         *        input changed something, or after any widget setter
         *        reported damage (the tree propagates it to the window,
         *        so changes from timers/callbacks count too; see also
         *        CanvasWindow::invalidate). Idle-polling shells
         *        (linux-fb, NDS) can skip paint() entirely while this
         *        is false.
         */
        virtual bool is_dirty() const noexcept = 0;

        /*
         * @brief the region the last paint() drew, in surface pixels
         *        (see IGui::dirty_region). The default reports false and
         *        shells then present the whole buffer.
         */
        [[nodiscard]] virtual bool dirty_region(int &, int &, int &, int &) const noexcept
        {
            return false;
        }

        virtual void on_painting(event::PAINT_EVENT::EventHandler) noexcept = 0;
        virtual void on_painted(event::PAINT_EVENT::EventHandler) noexcept = 0;

        virtual void on_closing(event::CLOSE_EVENT::EventHandler) noexcept = 0;
        virtual void on_closed(event::CLOSE_EVENT::EventHandler) noexcept = 0;
    };
}

#endif // IAPP_HPP