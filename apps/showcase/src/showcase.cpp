#include "showcase.hpp"

#include <cstdio>
#include <string>

#include "button.hpp"
#include "html.hpp"
#include "logging.hpp"
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

        constexpr int kBootThrottle = 35;

        // deterministic wave: a triangle, pure in the frame counter
        // (no timers, no RNG -- the F-2 app-side pattern)
        int wave_at(int frame)
        {
            const int phase = frame % 64;
            return (phase < 32) ? (phase * 28) / 32
                                : ((64 - phase) * 28) / 32;
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
        // (bytes_embed output) and outlives the family. The file path
        // form could never work on NDS (no filesystem) — the blob is
        // the whole point of the bytes_embed build step
        static const zb::ui::TtfFamily family =
            zb::ui::TtfFamily::from_memory(showcase_font, showcase_font_len);
        zb::ui::set_font_family(family);
#endif
    }

    void Showcase::apply_theme(const zb::ui::core::Color &accent)
    {
        // the design's panel colors are CSS-fixed; the theme switch
        // moves the widget palette: accent, focus, selection
        zb::ui::Theme t = zb::ui::dark_theme();
        t.accent = accent;
        t.focus_mark = accent;
        zb::ui::set_theme(t);
        if (trend_ != nullptr)
        {
            trend_->set_line_color(accent);
        }
    }

    void Showcase::apply_mode(const int index)
    {
        mode_ = index % kModeCount;
        const zb::ui::core::Color accents[kModeCount] = {
            zb::ui::core::Color::from(0x4f, 0xd6, 0xe0, 255),
            zb::ui::core::Color::from(0xf0, 0xb2, 0x43, 255),
            zb::ui::core::Color::from(0x79, 0xe0, 0x8a, 255),
        };
        if (!alert_)
        {
            apply_theme(accents[mode_]);
            set_text(status_, std::string("CONDITION ") + kModeNames[mode_]);
        }
    }

    void Showcase::apply_alert(const bool on)
    {
        alert_ = on;
        if (alert_btn_ != nullptr)
        {
            alert_btn_->set_text(on ? "CANCEL" : "ALERT");
        }
        if (on)
        {
            // red overrides the mode accent until cancelled
            apply_theme(zb::ui::core::Color::from(0xe0, 0x4f, 0x5a, 255));
            set_text(status_, "RED ALERT · ALL HANDS");
        }
        else
        {
            apply_mode(mode_);
        }
    }

    int Showcase::reactor_load() const
    {
        // base draw from the throttle plus each online system's draw
        int draw = 0;
        if (mod_a_ != nullptr && mod_a_->is_checked())
        {
            draw += 8;
        }
        if (mod_b_ != nullptr && mod_b_->is_checked())
        {
            draw += 5;
        }
        if (mod_c_ != nullptr && mod_c_->is_checked())
        {
            draw += 6;
        }
        int load = 25 + (throttle_val_ * 55) / 100 + draw;
        return load > 100 ? 100 : load;
    }

    void Showcase::update_throttle(const int value)
    {
        throttle_val_ = value;
        // 0..100 -> warp 0.0 .. 5.0
        const int warp_x10 = (value * 50) / 100;
        char buf[24];
        std::snprintf(buf, sizeof(buf), "WARP %d.%d", warp_x10 / 10, warp_x10 % 10);
        set_text(warpval_, buf);
        update_systems();
    }

    void Showcase::update_systems()
    {
        const int load = reactor_load();
        if (load_ != nullptr)
        {
            load_->set_value(load);
        }
        set_text(loadval_, std::to_string(load) + "%");
        if (temp_ != nullptr)
        {
            // the thermal mass lags the load
            const int temp = 20 + (load * 55) / 100;
            temp_->set_value(temp);
            set_text(tempval_, std::to_string(temp) + "\xc2\xb0" "C \xc2\xb7 nominal");
        }
    }

    void Showcase::show_about()
    {
        if (about_overlay_ == nullptr)
        {
            LE << "showcase: about overlay missing";
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
        const auto on_module = [this](const std::string &id)
        { set_text(status_, id + " switched"); };
        // binding rides materialization (code-contract 4): each id
        // attaches to the widget its node created
        zb::ui::build(*screen, doc, on_module);
        window_->root().add_child(std::move(screen));

        zb::ui::Widget &root = window_->root();
        helm_ = static_cast<zb::ui::GaugeDial *>(root.find_by_id("helm"));
        load_ = static_cast<zb::ui::GaugeDial *>(root.find_by_id("load"));
        temp_ = static_cast<zb::ui::ProgressBar *>(root.find_by_id("temp"));
        trend_ = static_cast<zb::ui::TrendLine *>(root.find_by_id("trend"));
        throttle_ = static_cast<zb::ui::Slider *>(root.find_by_id("throttle"));
        mod_a_ = static_cast<zb::ui::ToggleSwitch *>(root.find_by_id("modA"));
        mod_b_ = static_cast<zb::ui::ToggleSwitch *>(root.find_by_id("modB"));
        mod_c_ = static_cast<zb::ui::ToggleSwitch *>(root.find_by_id("modC"));
        radar_ = root.find_by_id("radar");
        headval_ = root.find_by_id("headval");
        warpval_ = root.find_by_id("warpval");
        brgval_ = root.find_by_id("brgval");
        loadval_ = root.find_by_id("loadval");
        tempval_ = root.find_by_id("tempval");
        status_ = root.find_by_id("status");
        fps_ = root.find_by_id("fps");

        // the design file declares the tags; these casts run on widgets
        // materialized from the same document (the builder-table
        // discipline, code-contract 4)
        if (throttle_ != nullptr)
        {
            throttle_->changed += [this](const int v) { update_throttle(v); };
        }
        if (mod_a_ != nullptr)
        {
            mod_a_->changed += [this](const bool)
            { update_systems(); set_text(status_, "SHIELDS " + std::string(mod_a_->is_checked() ? "ONLINE" : "OFFLINE")); };
        }
        if (mod_b_ != nullptr)
        {
            mod_b_->changed += [this](const bool)
            { update_systems(); set_text(status_, "LIFE SUPPORT " + std::string(mod_b_->is_checked() ? "ONLINE" : "OFFLINE")); };
        }
        if (mod_c_ != nullptr)
        {
            mod_c_->changed += [this](const bool)
            { update_systems(); set_text(status_, "AUX SENSORS " + std::string(mod_c_->is_checked() ? "ONLINE" : "OFFLINE")); };
        }

        // the command buttons (build() already gave them the generic
        // status sink; these are the real behaviors)
        if (auto *b = root.find_by_id("alert"))
        {
            alert_btn_ = static_cast<zb::ui::Button *>(b);
            alert_btn_->clicked += [this]() { apply_alert(!alert_); };
        }
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
                if (throttle_ != nullptr)
                {
                    throttle_->set_value(kBootThrottle);
                }
                if (mod_a_ != nullptr)
                {
                    mod_a_->set_checked(true);
                }
                if (mod_b_ != nullptr)
                {
                    mod_b_->set_checked(true);
                }
                if (mod_c_ != nullptr)
                {
                    mod_c_->set_checked(false);
                }
                if (trend_ != nullptr)
                {
                    trend_->clear();
                }
                apply_alert(false);
                apply_mode(0);
                update_throttle(kBootThrottle);
                heading_ = 0;
                frame_ = 0;
                set_text(status_, "CONDITION GREEN");
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
            const int load = reactor_load();
            for (int i = 0; i < 48; ++i)
            {
                const int v = load + wave_at(i * 2) - 14;
                trend_->append(v < 0 ? 0 : (v > 100 ? 100 : v));
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
        update_throttle(throttle_ != nullptr ? throttle_->get_value() : kBootThrottle);
    }

    void Showcase::paint() noexcept
    {
        if (ui_ready_)
        {
            ++frame_;
            // the helm: heading advances with the throttle (a pure
            // function of the frame counter and the input stream)
            heading_ = (heading_ + 1 + throttle_val_ / 25) % 360;
            if (helm_ != nullptr)
            {
                helm_->set_value(heading_);
            }
            if ((frame_ % 4) == 0)
            {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "HDG %03d", heading_);
                set_text(headval_, buf);
                const int brg_x10 = (heading_ * 10) % 3600;
                std::snprintf(buf, sizeof(buf), "+%03d.%d MARK 0",
                              brg_x10 / 10, brg_x10 % 10);
                set_text(brgval_, buf);
            }
            // the radar sweep needle: the design's ::after pseudo,
            // re-specified per frame (paint-only box, no layout)
            if (radar_ != nullptr)
            {
                const zb::ui::Widget::pseudo_spec *sp = radar_->pseudo(1);
                if (sp != nullptr)
                {
                    zb::ui::Widget::pseudo_spec spec = *sp;
                    spec.rot_ang = static_cast<int16_t>((frame_ * 3) % 360);
                    radar_->set_pseudo(1, spec);
                }
            }
            // the live feed advances every other frame
            if ((frame_ % 2) == 0 && trend_ != nullptr)
            {
                const int v = reactor_load() + wave_at(frame_) - 14;
                trend_->append(v < 0 ? 0 : (v > 100 ? 100 : v));
            }
            if ((frame_ % 16) == 0 && window_ != nullptr && !alert_)
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
