#include "app_maker.hpp"
#include "drift.hpp"

zb::SharedPtr<zb::app::IApp> zb::app::make_app()
{
    return zb::make_shared<zb::app::drift::Drift>();
}
