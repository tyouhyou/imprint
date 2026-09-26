// Library-mode host loop (code-contract §11): a native C++ user owns
// main and delegates the platform loop to the shell; the framework-mode
// ${STORY} mains are thin wrappers over the same function. The shell
// still owns the loop it runs (ARCHITECTURE §4.1) — run() only moves
// the ownership boundary from "the shell's main" to "a function the
// host's main calls".
#ifndef IM_SHELL_RUN_HPP
#define IM_SHELL_RUN_HPP

#include <cstdint>
#include <string>

#include "iapp.hpp"

namespace zb::shell
{
    struct run_options
    {
        // Overrides the OS window title only; the app-visible
        // IWindow::title() is untouched. Empty = the window's own title.
        std::string title;

        // Nonzero pair = the constrained create_window(width, height);
        // zero (the default) = the plain create_window(), except the FB
        // shell substitutes its own panel size (screen geometry is
        // shell-owned, ARCHITECTURE §3.1). Ignored when the app already
        // created its window.
        uint32_t width = 0;
        uint32_t height = 0;
    };

    /*
     * Drives the platform event loop until the app closes (the window is
     * closed, or the app fires the closed signal). Returns 0 on a normal
     * close. Init path: platform/window/surface creation failures throw
     * zb::ui::error (code-contract §1/§11). Hosts: Windows, Linux (X11 +
     * FB), macOS — the NDS firmware entry stays the framework-mode main
     * (§11.3). Single thread: run owns the thread it is called on.
     */
    int run(zb::SharedPtr<zb::app::IApp> app, const run_options &options = {});
}

#endif
