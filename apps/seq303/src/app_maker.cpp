#include "app_maker.hpp"
#include "seq303.hpp"

zb::SharedPtr<zb::app::IApp> zb::app::make_app()
{
    return zb::make_shared<zb::app::seq303::Sequencer>();
}
