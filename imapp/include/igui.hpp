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

        /*
         * The region of the framebuffer that the last paint() actually
         * drew, in pixels of the surface ([0..w) x [0..h), empty when
         * nothing was drawn). Presented shells copy only this region
         * (batch C). The default reports false: the shell then presents
         * the whole buffer, which stays correct for implementations
         * without region tracking.
         */
        [[nodiscard]] virtual bool dirty_region(int &, int &, int &, int &) const noexcept
        {
            return false;
        }
    };

}

#endif // IGUI_HPP