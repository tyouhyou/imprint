#include "app_maker.hpp"
#include "g2048.hpp"

zb::SharedPtr<zb::app::IApp> zb::app::make_app()
{
    return zb::make_shared<zb::app::g2048::G2048>();
}
