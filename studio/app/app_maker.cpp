#include "app_maker.hpp"
#include "studio_app.hpp"

zb::SharedPtr<zb::app::IApp> zb::app::make_app()
{
    return zb::make_shared<zb::app::studio::StudioApp>();
}
