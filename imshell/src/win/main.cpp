// Framework-mode thin main (code-contract §11): the loop lives in
// run_win.cpp; a library consumer calls zb::shell::run from their own
// main or WinMain instead of this executable (the subsystem is the
// consumer executable's link-time choice).
#include <windows.h>
#include "app_maker.hpp"
#include "core/error.hpp"
#include "logging.hpp"
#include "shell/run.hpp"

extern "C" int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    try
    {
        return zb::shell::run(zb::app::make_app());
    }
    catch (const zb::ui::error &e)
    {
        LE << e.what();
        return 1;
    }
}
