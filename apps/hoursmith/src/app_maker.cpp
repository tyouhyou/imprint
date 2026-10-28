#include "app_maker.hpp"
#include "hoursmith.hpp"

zb::SharedPtr<zb::app::IApp> zb::app::make_app()
{
    return zb::make_shared<zb::app::hoursmith::Hoursmith>();
}
