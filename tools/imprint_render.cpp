/*
 * imprint-render — design-file renderer CLI (Batch G P2).
 *
 * Renders a .ui or HTML design file into a pixel buffer offscreen and
 * writes it as PNG (self-contained vendored stb writer, no USE_PNG
 * needed) or GIF (the imcore writer), and prints the frame hash on
 * stdout — the deterministic-render story as a one-liner:
 *
 *   imprint-render tools/examples/menu.ui --out menu.png
 *   imprint-render assets/designs/imprint_console.html --out hero.png
 *   # stdout: <out>: WxH hash=<16 hex>  (byte-stable per build config)
 *
 * Exit codes: 0 ok, 1 usage/IO error, 2 the design file parses to
 * nothing (the ui_embed validation semantics). Screen size: --size,
 * else the HTML page box, else the 800x600 app default (warned).
 */
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "canvas_window.hpp"
#include "flex_panel.hpp"
#include "html.hpp"
#include "logging.hpp"
#include "snapshot.hpp"
#include "theme.hpp"
#include "ui_builder.hpp"
#include "ui_file.hpp"

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "text/runtime_ttf_provider.hpp"
#endif

namespace
{
    bool has_extension(const std::string &path, const std::string &ext)
    {
        return path.size() >= ext.size() &&
               path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
    }

    bool is_html_path(const std::string &path)
    {
        return has_extension(path, ".html") || has_extension(path, ".htm");
    }

    std::string default_out_path(const std::string &in)
    {
        const std::size_t dot = in.rfind('.');
        const std::size_t slash = in.find_last_of("/\\");
        if (dot != std::string::npos &&
            (slash == std::string::npos || dot > slash))
        {
            return in.substr(0, dot) + ".png";
        }
        return in + ".png";
    }

    bool read_file(const std::string &path, std::string &out)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            return false;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }

#if defined(IMCORE_HAS_TTF_RUNTIME)
    // proportional-text path (the ui_preview install_font_family
    // precedent): without it text renders in the 5x7 bitmap, which is
    // the honest zero-option default but not a design preview
    void install_font_family(const std::string &path)
    {
        if (path.empty() || zb::ui::has_font_family())
        {
            return;
        }
        try
        {
            // function-static anchor: the family state holds a copy,
            // the handle stays alive for static-lifetime widgets
            static zb::ui::TtfFamily family = zb::ui::TtfFamily::from_file(path.c_str());
            zb::ui::set_font_family(family);
            LD << "imprint-render: font family installed from '" << path << "'";
        }
        catch (const zb::ui::error &e)
        {
            LW << "imprint-render: cannot load font '" << path << "': "
               << e.what();
        }
    }
#endif

    // writes the RGB888 rows stb expects from the buffer's channel
    // accessors — layout- and depth-independent (A-19)
    bool write_png_rgb(const zb::app::IWindow &win, const std::string &path)
    {
        const int w = win.width();
        const int h = win.height();
        auto *pixels = static_cast<const zb::ui::core::Color *>(win.data());
        std::vector<unsigned char> rgb(static_cast<std::size_t>(w) * h * 3u);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const zb::ui::core::Color &c = pixels[y * w + x];
                auto *px = &rgb[(static_cast<std::size_t>(y) * w + x) * 3u];
                px[0] = c.r();
                px[1] = c.g();
                px[2] = c.b();
            }
        }
        return stbi_write_png(path.c_str(), w, h, 3, rgb.data(),
                              static_cast<int>(w) * 3) != 0;
    }
}

int main(int argc, char *argv[])
{
    std::string in_path;
    std::string out_path;
    std::string font_path;
    std::string theme_name;
    int size_w = 0;
    int size_h = 0;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc)
        {
            out_path = argv[++i];
        }
        else if (arg == "--size" && i + 1 < argc)
        {
            if (std::sscanf(argv[++i], "%dx%d", &size_w, &size_h) != 2 ||
                size_w <= 0 || size_h <= 0)
            {
                LE << "imprint-render: --size expects WxH (positive)";
                return 1;
            }
        }
        else if (arg == "--font" && i + 1 < argc)
        {
            font_path = argv[++i];
        }
        else if (arg == "--theme" && i + 1 < argc)
        {
            theme_name = argv[++i];
        }
        else if (!arg.empty() && arg[0] == '-')
        {
            LE << "imprint-render: unknown option '" << arg << "'";
            return 1;
        }
        else if (in_path.empty())
        {
            in_path = arg;
        }
        else
        {
            LE << "imprint-render: exactly one design file is expected";
            return 1;
        }
    }
    if (in_path.empty())
    {
        LE << "usage: imprint-render <design.ui|design.html> "
              "[--out path.png|gif] [--size WxH] [--font ttf-path] [--theme dark|light]";
        return 1;
    }
    if (out_path.empty())
    {
        out_path = default_out_path(in_path);
    }

#if defined(IMCORE_HAS_TTF_RUNTIME)
    if (font_path.empty())
    {
        if (const char *env = std::getenv("IM_RENDER_FONT"))
        {
            font_path = env;
        }
    }
    install_font_family(font_path);
#endif
    if (theme_name == "dark")
    {
        zb::ui::set_theme(zb::ui::dark_theme());
    }
    else if (theme_name == "light")
    {
        zb::ui::set_theme(zb::ui::light_theme());
    }

    std::string text;
    if (!read_file(in_path, text))
    {
        LE << "imprint-render: cannot open '" << in_path << "'";
        return 1;
    }

    bool ok = false;
    zb::ui::ui_node doc;
    zb::ui::html_page page;
    if (is_html_path(in_path))
    {
        doc = zb::ui::parse_html(text.c_str(), &ok, &page);
    }
    else
    {
        doc = zb::ui::parse_ui_text(text.c_str(), &ok);
    }
    if (!ok)
    {
        LE << "imprint-render: '" << in_path << "' yields no widget";
        return 2;
    }

    // screen size: --size, then the HTML page box, then the app default
    if (size_w == 0)
    {
        size_w = (page.has_width && page.width > 0) ? page.width : 800;
        if (!(page.has_width && page.width > 0))
        {
            LW << "imprint-render: no page width; using 800 (--size overrides)";
        }
    }
    if (size_h == 0)
    {
        size_h = (page.has_height && page.height > 0) ? page.height : 600;
        if (!(page.has_height && page.height > 0))
        {
            LW << "imprint-render: no page height; using 600 (--size overrides)";
        }
    }

    // the ui_preview materialization path, single document: parse →
    // build() into a screen panel → layout → one frame
    zb::app::CanvasWindow window;
    window.create(static_cast<uint32_t>(size_w), static_cast<uint32_t>(size_h));
    window.set_auto_layout(true);
    auto screen = std::make_unique<zb::ui::FlexPanel>();
    screen->set_size(size_w, size_h);
    if (page.has_background)
    {
        screen->set_background_color(page.background);
    }
    zb::ui::build(*screen, doc);
    window.root().add_child(std::move(screen));
    window.root().layout();
    window.paint();

    const bool png = has_extension(out_path, ".png");
    const bool gif = has_extension(out_path, ".gif");
    if (png)
    {
        if (!write_png_rgb(window, out_path))
        {
            LE << "imprint-render: cannot write '" << out_path << "'";
            return 1;
        }
    }
    else if (gif)
    {
        try
        {
            zb::snap::save_gif(window, out_path);
        }
        catch (const zb::ui::error &e)
        {
            LE << e.what();
            return 1;
        }
    }
    else
    {
        LE << "imprint-render: unsupported output format '" << out_path
           << "' (use .png or .gif)";
        return 1;
    }

    std::printf("%s: %dx%d hash=%016llx\n", out_path.c_str(), size_w, size_h,
                static_cast<unsigned long long>(zb::snap::framebuffer_hash(window)));
    return 0;
}
