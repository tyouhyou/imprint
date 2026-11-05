#include "app_maker.hpp"
#include "tianji_app.hpp"

zb::SharedPtr<zb::app::IApp> zb::app::make_app()
{
    return zb::make_shared<zb::app::tianji::TianjiApp>();
}
