#pragma once

#include <exception>
#include <string>

namespace zb::ui
{
    /*
     * One-shot initialization-path failure with a human-readable message
     * (contract §1): resource/surface construction throws this; the hot
     * paths (paint, input dispatch) never throw.
     */
    class error : public std::exception
    {
    public:
        explicit error(const std::string &msg) noexcept : msg(msg) {}

        const char *what() const noexcept override { return msg.c_str(); }

    private:
        std::string msg;
    };
}  // namespace zb::ui
