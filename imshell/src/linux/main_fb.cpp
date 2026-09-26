// Framework-mode thin main (code-contract §11): the loop lives in
// run_fb.cpp; a library consumer calls zb::shell::run from their own
// main instead of this executable.
#include <cstdio>
#include "app_maker.hpp"
#include "core/error.hpp"
#include "logging.hpp"
#include "shell/run.hpp"

int main(int argc, char *argv[])
{
    zb::Logging::set_log_handle([](const zb::Logging_Level &level, const std::string &message)
    {
        std::printf("%s", message.c_str());
    });

    LD << "run on FB";
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
