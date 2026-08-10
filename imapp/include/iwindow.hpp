#ifndef IWINDOW_HPP
#define IWINDOW_HPP

#include <string>
#include <cstdint>
#include "input.hpp"

namespace zb::app
{
    class IWindow
    {
    public:
        virtual ~IWindow() noexcept = default;

        [[nodiscard]] virtual void* data() const noexcept = 0;

        [[nodiscard]] virtual int32_t width() const noexcept = 0;

        [[nodiscard]] virtual int32_t height() const noexcept = 0;

        [[nodiscard]] virtual std::string title() const noexcept { return "untitled"; }
    };

}

#endif // IWINDOW_HPP