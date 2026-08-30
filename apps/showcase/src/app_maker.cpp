#include "app_maker.hpp"
#include "showcase.hpp"

zb::SharedPtr<zb::app::IApp> zb::app::make_app()
{
    return zb::make_shared<zb::app::showcase::Showcase>();
}
