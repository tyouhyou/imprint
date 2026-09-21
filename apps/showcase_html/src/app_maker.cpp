#include "app_maker.hpp"
#include "showcase_html.hpp"

zb::SharedPtr<zb::app::IApp> zb::app::make_app()
{
    return zb::make_shared<zb::app::showcase_html::ShowcaseHtml>();
}
