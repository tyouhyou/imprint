#include "tictactoe.hpp"

#include <stdexcept>

namespace zb::app::tictactoe
{
    zb::SharedPtr<IWindow> Tictactoe::window() noexcept
    {
        return view_.window();
    }

    void Tictactoe::create_window()
    {
        make_window(static_cast<uint32_t>(_width), static_cast<uint32_t>(_height));
    }

    void Tictactoe::make_window(uint32_t max_client_width, uint32_t max_client_height, void *buffer)
    {
        if (view_.window())
        {
            throw std::runtime_error("gui already created");
        }

        view_.build(max_client_width, max_client_height, buffer);
        controller_.attach(view_);
        controller_.start();
    }

    void Tictactoe::input(const zb::input::input_event &ev) noexcept
    {
        if (auto w = view_.window())
        {
            w->input(ev);
        }
    }

    void Tictactoe::paint() noexcept
    {
        if (auto w = view_.window())
        {
            w->paint();
        }
    }

    bool Tictactoe::is_dirty() const noexcept
    {
        auto w = view_.window();
        return w ? w->is_dirty() : false;
    }

    bool Tictactoe::dirty_region(int &x, int &y, int &rw, int &rh) const noexcept
    {
        auto w = view_.window();
        return w ? w->dirty_region(x, y, rw, rh) : false;
    }

    void Tictactoe::on_painting(const event::PAINT_EVENT::EventHandler handler) noexcept
    {
        if (auto w = view_.window())
        {
            w->painting += handler;
        }
    }

    void Tictactoe::on_painted(const event::PAINT_EVENT::EventHandler handler) noexcept
    {
        if (auto w = view_.window())
        {
            w->painted += handler;
        }
    }

    void Tictactoe::on_closing(const event::CLOSE_EVENT::EventHandler handler) noexcept
    {
        if (auto w = view_.window())
        {
            w->closing += handler;
        }
    }

    void Tictactoe::on_closed(const event::CLOSE_EVENT::EventHandler handler) noexcept
    {
        if (auto w = view_.window())
        {
            w->closed += handler;
        }
    }
}  // namespace zb::app::tictactoe
