#include "showcase.hpp"

#include <cstdio>
#include <string>

#include "gauge_dial.hpp"
#include "html.hpp"
#include "knob.hpp"
#include "logging.hpp"
#include "progress_bar.hpp"
#include "snapshot.hpp"
#include "theme.hpp"
#include "text/utf8.hpp"
#include "ui_builder.hpp"

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "text/runtime_ttf_provider.hpp"
#endif

#include "showcase_signal.gen.hpp"   // packed by html_embed at build time
#include "showcase_font.gen.hpp" // packed by bytes_embed at build time

namespace zb::app::showcase
{
    namespace
    {
        constexpr int kModeCount = 3;
        constexpr const char *kModeNames[kModeCount] = {"CYAN", "AMBER", "GREEN"};

        // deterministic telemetry: a triangle wave, pure in the frame
        // counter (no timers, no RNG -- the F-2 app-side pattern)
        int sample_at(int frame)
        {
            const int phase = frame % 64;
            return 30 + ((phase < 32) ? (phase * 28) / 32
                                      : ((64 - phase) * 28) / 32);
        }

        void set_text(zb::ui::Widget *w, const std::string &s)
        {
            if (w != nullptr)
            {
                w->set_text(zb::ui::utf8_to_utf16(s.c_str()));
            }
        }
    }

    void Showcase::install_font()
    {
#if defined(IMCORE_HAS_TTF_RUNTIME)
        if (zb::ui::has_font_family())
        {
            return;
        }
        // from_memory borrows: the packed blob has static storage
        // (bytes_embed output) and outlives the family
        static const zb::ui::TtfFamily family =
            zb::ui::TtfFamily::from_file("assets/fonts/Inter-Regular.ttf");
        zb::ui::set_font_family(family);
#endif
    }

    void Showcase::apply_mode(const int index)
    {
        mode_ = index % kModeCount;
        // the design's panel colors are CSS-fixed; the theme switch
        // moves the widget palette: accent, focus, selection
        zb::ui::Theme t = zb::ui::dark_theme();
        const zb::ui::core::Color accents[kModeCount] = {
            zb::ui::core::Color::from(0x4f, 0xd6, 0xe0, 255),
            zb::ui::core::Color::from(0xf0, 0xb2, 0x43, 255),
            zb::ui::core::Color::from(0x79, 0xe0, 0x8a, 255),
        };
        t.accent = accents[mode_];
        t.focus_mark = accents[mode_];
        zb::ui::set_theme(t);
        if (trend_ != nullptr)
        {
            trend_->set_line_color(accents[mode_]);
        }
        set_text(status_, kModeNames[mode_]);
    }

    void Showcase::update_gain(const int value)
    {
        // 0..100 -> -24..0 dB, and the readouts follow the knob
        const int dB_x10 = (value * 240) / 100;
        char buf[24];
        if (value == 100)
        {
            std::snprintf(buf, sizeof(buf), "0.0dB");
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "-%d.%ddB", dB_x10 / 10, dB_x10 % 10);
        }
        set_text(gainval_, buf);
        set_text(loadval_, std::to_string(value) + "%");
        if (load_ != nullptr)
        {
            load_->set_value(value);
        }
        if (temp_ != nullptr)
        {
            // the thermal mass lags the load
            const int temp = 20 + (value * 55) / 100;
            temp_->set_value(temp);
            set_text(tempval_, std::to_string(temp) + "\xc2\xb0" "C \xc2\xb7 nominal");
        }
    }

    void Showcase::show_about()
    {
        if (about_overlay_ == nullptr)
        {
            std::fprintf(stderr, "showcase: about overlay missing\n");
            return;
        }
        about_overlay_->set_visible(true);
        window_->invalidate();
    }

    void Showcase::build_ui(uint32_t w, uint32_t h)
    {
        bool ok = false;
        const auto &doc_file = kHtmlFiles[0];
        const std::string text(reinterpret_cast<const char *>(doc_file.data),
                               doc_file.size);
        zb::ui::ui_node doc = zb::ui::parse_html(text.c_str(), &ok);
        if (!ok)
        {
            // the packed document is validated at build time; a failure
            // here is an invariant break, not a runtime condition
            LE << "showcase: the packed design document does not parse";
            return;
        }
        window_->set_auto_layout(true);
        auto screen = std::make_unique<zb::ui::FlexPanel>();
        screen->set_size(static_cast<int>(w), static_cast<int>(h));
        zb::ui::build(*screen, doc);
        window_->root().add_child(std::move(screen));

        zb::ui::Widget &root = window_->root();
        load_ = static_cast<zb::ui::GaugeDial *>(root.find_by_id("load"));
        temp_ = static_cast<zb::ui::ProgressBar *>(root.find_by_id("temp"));
        trend_ = static_cast<zb::ui::TrendLine *>(root.find_by_id("trend"));
        gain_ = static_cast<zb::ui::Knob *>(root.find_by_id("gain"));
        loadval_ = root.find_by_id("loadval");
        tempval_ = root.find_by_id("tempval");
        gainval_ = root.find_by_id("gainval");
        status_ = root.find_by_id("status");
        fps_ = root.find_by_id("fps");

        // the design file declares the tags; these casts run on widgets
        // materialized from the same document (the builder-table
        // discipline, code-contract 4)
        if (gain_ != nullptr)
        {
            gain_->changed += [this](const int v) { update_gain(v); };
        }
        const auto on_module = [this](const std::string &id)
        { set_text(status_, id + " toggled"); };
        zb::ui::bind_actions(root, doc, on_module);

        // the three command buttons (bind_actions already gave them the
        // generic status sink; these are the real behaviors)
        if (auto *b = root.find_by_id("mode"))
        {
            static_cast<zb::ui::Button *>(b)->clicked += [this]()
            { apply_mode(mode_ + 1); };
        }
        if (auto *b = root.find_by_id("about"))
        {
            static_cast<zb::ui::Button *>(b)->clicked += [this]()
            { show_about(); };
        }
        if (auto *b = root.find_by_id("reset"))
        {
            static_cast<zb::ui::Button *>(b)->clicked += [this]()
            {
                if (gain_ != nullptr)
                {
                    gain_->set_value(35);
                }
                if (trend_ != nullptr)
                {
                    trend_->clear();
                    for (int i = 0; i < 48; ++i)
                    {
                        trend_->append(sample_at(i * 2));
                    }
                }
                apply_mode(0);
                update_gain(35);
                frame_ = 0;
                set_text(status_, "RESET OK");
            };
        }

        // the ABOUT overlay is part of the design file (position:absolute,
        // display:none at boot): behavior is visibility only
        about_overlay_ = root.find_by_id("about_overlay");
        if (auto *b = root.find_by_id("close_btn"))
        {
            static_cast<zb::ui::Button *>(b)->clicked += [this]()
            {
                if (about_overlay_ != nullptr)
                {
                    about_overlay_->set_visible(false);
                }
                window_->invalidate();
                set_text(status_, "FNV \xc2\xb7 MATCH");
            };
        }

        // seed the live feed so the panel is never empty
        if (trend_ != nullptr)
        {
            trend_->set_y_range(0, 100);
            trend_->set_max_points(48);
            for (int i = 0; i < 48; ++i)
            {
                trend_->append(sample_at(i * 2));
            }
        }
    }

    void Showcase::create_window(uint32_t w, uint32_t h, void *buffer)
    {
        zb::ui::set_theme(zb::ui::dark_theme());
        install_font();
        window_ = zb::make_shared<zb::app::CanvasWindow>();
        window_->create(w, h, buffer);
        build_ui(w, h);
        ui_ready_ = true;
        apply_mode(0);
        update_gain(gain_ != nullptr ? gain_->get_value() : 35);
    }

    void Showcase::paint() noexcept
    {
        if (ui_ready_)
        {
            ++frame_;
            // the live feed advances every other frame; the status line
            // carries the frame hash -- same input, same pixels, on screen
            if ((frame_ % 2) == 0 && trend_ != nullptr)
            {
                trend_->append(sample_at(frame_));
            }
            if ((frame_ % 16) == 0 && window_ != nullptr)
            {
                const uint64_t h = zb::snap::framebuffer_hash(*window_);
                char buf[32];
                std::snprintf(buf, sizeof(buf), "FNV %04llx  MATCH",
                              static_cast<unsigned long long>((h >> 48) & 0xffff));
                set_text(status_, buf);
            }
            char fps[24];
            std::snprintf(fps, sizeof(fps), "FRAME %06d", frame_);
            set_text(fps_, fps);
            window_->invalidate();
        }
        window_->paint();
    }
}
