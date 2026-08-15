#include "app_maker.hpp"
#include "ui_preview.hpp"

zb::SharedPtr<zb::app::IApp> zb::app::make_app()
{
    return zb::make_shared<zb::app::ui_preview::UiPreview>();
}