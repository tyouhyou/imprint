#ifndef TICTACTOE_HPP
#define TICTACTOE_HPP

#include "iapp.hpp"
#include "tictactoe_controller.hpp"
#include "tictactoe_view.hpp"

namespace zb::app::tictactoe
{
    /*
     * IApp adapter for the tic-tac-toe demo: owns the view (widget tree +
     * styling) and the controller (round flow) and forwards the IApp
     * protocol to the view's window. The app contains no platform-specific
     * code; text is rendered with the built-in bitmap font, so no
     * Font/FreeType is needed.
     */
    class Tictactoe : public IApp
    {
    public:
        Tictactoe() = default;

        void create_window() override;
        void create_window(const uint32_t &max_client_width, const uint32_t &max_client_height, void *buffer) override
        {
            make_window(max_client_width, max_client_height, buffer);
        }
        void create_window(const uint32_t &max_client_width, const uint32_t &max_client_height) override
        {
            make_window(max_client_width, max_client_height);
        }

        zb::SharedPtr<IWindow> window() noexcept override;

        void input(const zb::input::input_event &ev) noexcept override;
        void paint() noexcept override;
        bool is_dirty() const noexcept override;
        void on_painting(event::PAINT_EVENT::EventHandler) noexcept override;
        void on_painted(event::PAINT_EVENT::EventHandler) noexcept override;
        void on_closing(event::CLOSE_EVENT::EventHandler) noexcept override;
        void on_closed(event::CLOSE_EVENT::EventHandler) noexcept override;

    private:
        void make_window(const uint32_t &max_client_width, const uint32_t &max_client_height, void *buffer = nullptr);

        int32_t _width{800};
        int32_t _height{600};

        TictactoeView view_;
        TictactoeController controller_;
    };
}

#endif // TICTACTOE_HPP
