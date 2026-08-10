#ifndef IGUI_HPP
#define IGUI_HPP

#include <string>
#include <cstdint>
#include "event_def.hpp"

namespace zb::app
{
    /*
     * IGui - signal interface (Qt signals style).
     *
     * The event members are intentionally public: they are the signals a
     * concrete gui object may emit, and any holder of an IGui may subscribe
     * to them. Only the concrete implementation emits events; external code
     * only subscribes.
     */
    class IGui
    {
    public:
        virtual ~IGui() noexcept = default;

        event::PAINT_EVENT painting;
        event::PAINT_EVENT painted;

        event::CLOSE_EVENT closing;
        event::CLOSE_EVENT closed;
        
        virtual void close() noexcept = 0;

        /*
         * @brief whether the gui owes a repaint since the last paint().
         *        Idle-polling shells (linux-fb, NDS) can skip paint()
         *        entirely while this is false.
         */
        [[nodiscard]] virtual bool is_dirty() const noexcept = 0;
    };

}

#endif // IGUI_HPP