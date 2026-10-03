#include "app_maker.hpp"
#include "seedmap.hpp"

zb::SharedPtr<zb::app::IApp> zb::app::make_app()
{
    return zb::make_shared<zb::app::seedmap::Seedmap>();
}
