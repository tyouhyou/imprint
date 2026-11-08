#include "studio_app.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imui.hpp"

#include "flex_panel.hpp"
#include "list_box.hpp"
#include "logging.hpp"
#include "scroll_panel.hpp"
#include "text_area.hpp"
#include "text_input.hpp"

#include "text/runtime_ttf_provider.hpp"

#include "snapshot.hpp"  // TIANJI_STUDIO_PNG 存证（USE_PNG build）

namespace zb::app::studio
{
    namespace
    {
        // the stream geometry (the tianji game client's estimator):
        // wide CJK units ~14px, narrow units ~7px — the shared
        // approximation the variable-height rows size by
        constexpr int kLinePitch = 20;
        constexpr int kTitlePitch = 18;
        constexpr int kHistRows = 6;
        constexpr int kOutPaneHeight = 150;

        // the fp-poll cadence: ~1s at the shell's 30ms tick — the same
        // refresh rhythm as the TUI's set_interval(1.0) projection poll
        constexpr std::uint64_t kProjectPollInterval = 33;

        // wire timeouts (ms): hello covers the interpreter + import
        // startup; commands are file-scoped; project.get is a stat+read
        constexpr int kHelloTimeoutMs = 20000;
        constexpr int kCommandTimeoutMs = 10000;
        constexpr int kProjectTimeoutMs = 2000;

        int row_lines(const std::string &utf8, const int inner_width)
        {
            int wide = 0;
            int narrow = 0;
            for (size_t i = 0; i < utf8.size();)
            {
                const unsigned char c = static_cast<unsigned char>(utf8[i]);
                if (c < 0x80)
                {
                    ++narrow;
                    ++i;
                }
                else if ((c >= 0xE0 && c <= 0xEF) || c >= 0xF0)
                {
                    ++wide;
                    i += (c >= 0xF0) ? 4 : 3;
                }
                else
                {
                    ++narrow;  // continuation bytes never lead here
                    ++i;
                }
            }
            const int advance = wide * 14 + narrow * 7;
            int lines = (advance + inner_width - 1) / inner_width;
            if (lines < 1)
            {
                lines = 1;
            }
            return lines;
        }
    }  // namespace

    std::string StudioApp::dial_text(const void *arg, const size_t index)
    {
        const auto *ctx = static_cast<const RowContext *>(arg);
        return ctx->rows->at(index);
    }

    int StudioApp::dial_height(const void *arg, const size_t index)
    {
        const auto *ctx = static_cast<const RowContext *>(arg);
        return row_lines(ctx->rows->at(index), ctx->inner_width) * kLinePitch + 6;
    }

    std::string StudioApp::out_text(const void *arg, const size_t index)
    {
        const auto *ctx = static_cast<const RowContext *>(arg);
        return ctx->rows->at(index);
    }

    int StudioApp::out_height(const void *arg, const size_t index)
    {
        const auto *ctx = static_cast<const RowContext *>(arg);
        return row_lines(ctx->rows->at(index), ctx->inner_width) * kLinePitch + 4;
    }

    std::string StudioApp::hist_text(const void *arg, const size_t index)
    {
        const auto *ctx = static_cast<const RowContext *>(arg);
        return ctx->rows->at(index);
    }

    void StudioApp::create_window(const uint32_t max_client_width,
                                  const uint32_t max_client_height,
                                  void *buffer)
    {
        window_ = zb::make_shared<CanvasWindow>();
        window_->create(max_client_width > 0 ? max_client_width : 1200,
                        max_client_height > 0 ? max_client_height : 800,
                        buffer);
        // explicit geometry still needs the layout pass to assign
        // positions (Panel stacks by size; FlexPanel flexes) — without
        // auto_layout every child sits at (0,0)
        window_->set_auto_layout(true);
        build_panes(max_client_width > 0 ? max_client_width : 1200,
                    max_client_height > 0 ? max_client_height : 800);
        load_font();
#if defined(IMCORE_HAS_TTF_RUNTIME)
        if (zb::ui::has_font_family())
        {
            apply_font(window_->root());
        }
#endif
        start_bridge();
    }

    void StudioApp::build_panes(const uint32_t w, const uint32_t h)
    {
        auto &root = window_->root();
        const int width = static_cast<int>(w);
        const int height = static_cast<int>(h);

        // vertical budget: header / main / out(title+pane) / status /
        // input / hints — everything explicit (the SIXEL buffer never
        // resizes, contract §11.1)
        const int header_h = 28;
        const int hints_h = 16;
        const int status_h = 22;
        const int input_h = 26;
        const int out_h = kOutPaneHeight;
        const int main_h = height - header_h - kTitlePitch - out_h -
                           kTitlePitch - status_h - input_h - hints_h;

        // -- header -------------------------------------------------------
        auto header = std::make_unique<zb::ui::FlexPanel>();
        header->set_direction(zb::ui::FlexPanel::flex_direction::row);
        header->set_size(width, header_h);
        auto title = std::make_unique<zb::ui::Label>();
        title->set_text("天机剧场 · Studio · 创作会话");
        auto subtitle = std::make_unique<zb::ui::Label>();
        subtitle->set_text("未绑定作品（/work <id>）");
        subtitle_ = subtitle.get();
        header->add_child(std::move(title), 1);
        header->add_child(std::move(subtitle), 0);
        root.add_child(std::move(header));

        // -- main row: left 40% (history / dialogue), right 60% (text) ----
        auto main_row = std::make_unique<zb::ui::FlexPanel>();
        main_row->set_direction(zb::ui::FlexPanel::flex_direction::row);
        main_row->set_size(width, main_h);
        const int left_w = width * 2 / 5;
        const int right_w = width - left_w;

        auto left_col = std::make_unique<zb::ui::FlexPanel>();
        left_col->set_direction(zb::ui::FlexPanel::flex_direction::column);
        left_col->set_size(left_w, main_h);

        auto hist_title = std::make_unique<zb::ui::Label>();
        hist_title->set_text("会话历史（最近）");
        left_col->add_child(std::move(hist_title), 0);

        auto hist_scroll = std::make_unique<zb::ui::ScrollPanel>();
        // the history pane keeps the TUI's fixed six-row budget: an
        // explicit box (a ScrollPanel measures its content, and a short
        // model would collapse the pane)
        hist_scroll->set_size(left_w - 8, kHistRows * kLinePitch);
        auto hist_list = std::make_unique<zb::ui::ListBox>();
        hist_list->set_size(left_w - 24, kHistRows * kLinePitch);
        hist_list->set_row_height(kLinePitch);
        hist_list->set_visible_rows(kHistRows);
        hist_ctx_.rows = &hist_rows_;
        hist_list->set_item_text(&StudioApp::hist_text, &hist_ctx_);
        hist_list->changed += [this](const size_t index)
        {
            if (index < hist_ids_.size() && !hist_ids_[index].empty())
            {
                command_text_ = "/open " + hist_ids_[index];
                submit_command();
            }
        };
        history_ = hist_list.get();
        hist_scroll->add_child(std::move(hist_list), 1);
        left_col->add_child(std::move(hist_scroll), 0);

        auto dial_title = std::make_unique<zb::ui::Label>();
        dial_title->set_text("对话窗口 · Ctrl+E/Y 滚动（human × ai.writer）");
        left_col->add_child(std::move(dial_title), 0);

        auto dial_scroll = std::make_unique<zb::ui::ScrollPanel>();
        dial_scroll->set_follow_bottom(true);  // FI-05: the stream pins
        auto dial_list = std::make_unique<zb::ui::ListBox>();
        dial_list->set_size(left_w - 8, 1);
        dial_list->set_visible_rows(8);
        dial_ctx_.rows = &dial_rows_;
        dial_ctx_.inner_width = left_w - 20;
        dial_list->set_item_text(&StudioApp::dial_text, &dial_ctx_);
        dial_list->set_item_height(&StudioApp::dial_height, &dial_ctx_);
        dialogue_ = dial_list.get();
        dial_scroll->add_child(std::move(dial_list), 1);
        dial_scroll_ = dial_scroll.get();
        left_col->add_child(std::move(dial_scroll), 1);

        main_row->add_child(std::move(left_col), 0);

        auto right_col = std::make_unique<zb::ui::FlexPanel>();
        right_col->set_direction(zb::ui::FlexPanel::flex_direction::column);
        right_col->set_size(right_w, main_h);
        auto manu_title = std::make_unique<zb::ui::Label>();
        manu_title->set_text("文本窗口 · manuscript.md + AI 待签收草稿（点击后方向键浏览）");
        right_col->add_child(std::move(manu_title), 0);
        auto manu = std::make_unique<zb::ui::TextArea>();
        manu->set_size(right_w - 8, main_h - kTitlePitch);
        manu->set_read_only(true);
        manuscript_ = manu.get();
        right_col->add_child(std::move(manu), 1);
        main_row->add_child(std::move(right_col), 0);
        root.add_child(std::move(main_row));

        // -- output pane ---------------------------------------------------
        auto out_title = std::make_unique<zb::ui::Label>();
        out_title->set_text("输出（提示 · 回执 · 错误 · help）· Ctrl+W/S 滚动");
        out_title->set_size(width, kTitlePitch);
        root.add_child(std::move(out_title));
        auto out_scroll = std::make_unique<zb::ui::ScrollPanel>();
        out_scroll->set_follow_bottom(true);
        out_scroll->set_size(width, out_h);
        auto out_list = std::make_unique<zb::ui::ListBox>();
        out_list->set_size(width - 8, 1);
        out_list->set_visible_rows(4);
        out_ctx_.rows = &out_rows_;
        out_ctx_.inner_width = width - 20;
        out_list->set_item_text(&StudioApp::out_text, &out_ctx_);
        out_list->set_item_height(&StudioApp::out_height, &out_ctx_);
        output_ = out_list.get();
        out_scroll->add_child(std::move(out_list), 1);
        out_scroll_ = out_scroll.get();
        root.add_child(std::move(out_scroll));

        // -- status line ----------------------------------------------------
        auto status_row = std::make_unique<zb::ui::FlexPanel>();
        status_row->set_direction(zb::ui::FlexPanel::flex_direction::row);
        status_row->set_size(width, status_h);
        auto status = std::make_unique<zb::ui::Label>();
        status->set_text("会话 —（/new [标题] 或 /open <id>）");
        status_ = status.get();
        status_row->add_child(std::move(status), 1);
        root.add_child(std::move(status_row));

        // -- input line ------------------------------------------------------
        auto input_row = std::make_unique<zb::ui::FlexPanel>();
        input_row->set_direction(zb::ui::FlexPanel::flex_direction::row);
        input_row->set_size(width, input_h);
        auto hint = std::make_unique<zb::ui::Label>();
        hint->set_text("非 / 文本=发言");
        input_row->add_child(std::move(hint), 0);
        auto command = std::make_unique<zb::ui::TextInput>();
        command->set_size(width - 110, input_h);
        command->submitted += [this](const std::string &text)
        {
            command_text_ = text;
            submit_command();
        };
        command_ = command.get();
        input_row->add_child(std::move(command), 1);
        root.add_child(std::move(input_row));

        // -- key hints (FI-11 adaptation note) --------------------------------
        auto hints = std::make_unique<zb::ui::Label>();
        hints->set_text("Ctrl+Q 退出 ｜ Ctrl+E/Y 对话↑↓ ｜ Ctrl+W/S 输出↑↓ ｜ 滚轮滚动各窗格");
        hints->set_size(width, hints_h);
        root.add_child(std::move(hints));

        // the input line owns the keyboard from the first frame (the
        // TUI keeps focus on the input row — no click needed). The
        // dispatcher starts with no focus target and Tab is its
        // documented focus-forward key, so walk it until the command
        // line holds the focus (deterministic tree order, bounded).
        for (int i = 0; i < 24 && command_ != nullptr &&
                         !command_->is_focused();
             ++i)
        {
            zb::input::input_event ev{};
            ev.type = zb::input::input_type::key_down;
            ev.key = static_cast<int>(zb::input::key_code::tab);
            window_->input(ev);
        }

        write_output("（未连接桥）正在 spawn Python 原型机……");
        write_output("滚动：对话 Ctrl+E/Y / 输出 Ctrl+W/S；滚轮可用。Ctrl+Q 退出。");
    }

    void StudioApp::apply_font(zb::ui::Widget &w)
    {
        // the process family (set_font_family) resolves only for
        // widgets that request a size — the FI-04 recipe's second half;
        // 16px CJK reads well at the 20px row pitch
        w.set_font_size(16);
        // Panel and FlexPanel own their children in different stores
        // (FlexPanel is not a Panel subclass)
        if (auto *panel = dynamic_cast<zb::ui::Panel *>(&w))
        {
            for (auto &c : panel->get_children())
            {
                apply_font(*c);
            }
        }
        else if (auto *flex = dynamic_cast<zb::ui::FlexPanel *>(&w))
        {
            for (const auto &item : flex->get_items())
            {
                apply_font(*item.child);
            }
        }
    }

    void StudioApp::load_font()
    {
#if defined(IMCORE_HAS_TTF_RUNTIME)
        // the CJK recipe (tianji/for_tianji.md §5 / FI-04): a full TTF
        // as the process family; the dialogue workset wants the 2MB
        // cache tier, not the 64KB default. Failure degrades to 5x7
        // (CJK units skip) with one log line, never a crash.
        if (const char *font = std::getenv("TIANJI_FONT");
            font != nullptr && font[0] != '\0')
        {
            try
            {
                static zb::ui::TtfFamily family =
                    zb::ui::TtfFamily::from_file(font, 2u << 20);
                set_font_family(family);
            }
            catch (const std::exception &e)
            {
                LW << "studio: TIANJI_FONT load failed (" << e.what()
                   << "); CJK rows degrade to the 5x7 fallback";
            }
        }
#endif
    }

    void StudioApp::start_bridge()
    {
        bridge_ = std::make_unique<zb::studio::BridgeClient>();
        const char *home = std::getenv("TIANJI_HOME");
        bridge_cwd_ = (home != nullptr && home[0] != '\0') ? home : "../tianji";
        const char *cmd = std::getenv("STUDIO_BRIDGE_CMD");
        bridge_command_ =
            (cmd != nullptr && cmd[0] != '\0')
                ? cmd
                : std::string(".venv/bin/python -m studio.pipeline "
                              "studio-bridge --root .");
        std::string err;
        if (!bridge_->spawn(bridge_cwd_, bridge_command_, &err))
        {
            write_output("❌ 桥启动失败：" + err);
            write_output("   检查 TIANJI_HOME（默认 ../tianji）与 STUDIO_BRIDGE_CMD。");
            status_->set_text(("桥未连接（" + bridge_cwd_ + "）").c_str());
            return;
        }
        if (!send_hello())
        {
            return;
        }
        bridge_ready_ = true;
        request_project();
    }

    bool StudioApp::send_hello()
    {
        zb::tianji::Json data;
        std::string err;
        if (!bridge_->request("bridge.hello", zb::tianji::Json::object(),
                              kHelloTimeoutMs, &data, &err))
        {
            write_output("❌ 桥握手失败：" + err);
            status_->set_text(("桥未握手（" + bridge_cwd_ + "）").c_str());
            return false;
        }
        seat_ai_ = data.get_str("seat_ai", "ai.writer");
        const std::string responder = data.get_str("responder", "mock");
        const std::string model = data.get_str("model", "");
        write_output("创作会话 · Studio（C++/imprint 客户端，P1-10）");
        write_output("桥已连接：" + data.get_str("root", "?") +
                     " ｜ 通道 " + responder +
                     (responder == "real" ? "（" + model + "）" : "（TIANJI_LLM_* 未配置）"));
        write_output("非 / 文本 = 对话发言（AI 席按配方应答）；/help 查看命令。");
        return true;
    }

    void StudioApp::request_project()
    {
        if (bridge_ == nullptr || !bridge_->alive() ||
            bridge_->pending_events() > 0)
        {
            return;  // events first: the next step() drains before polling
        }
        zb::tianji::Json fields = zb::tianji::Json::object();
        if (!fp_.empty())
        {
            fields.set("fp", zb::tianji::Json::str(fp_));
        }
        zb::tianji::Json data;
        std::string err;
        if (!bridge_->request("project.get", std::move(fields),
                              kProjectTimeoutMs, &data, &err))
        {
            // a busy bridge answers late sometimes; retry next tick, but
            // a dead bridge is worth one line
            if (err != "timeout" && !bridge_dead_)
            {
                write_output("⚠ project.get：" + err);
            }
            return;
        }
        if (data.get_bool("unchanged", false))
        {
            return;
        }
        apply_snapshot(data);
    }

    void StudioApp::submit_command()
    {
        const std::string text = command_text_;
        command_text_.clear();
        if (text.empty() || bridge_ == nullptr || !bridge_->alive())
        {
            return;
        }
        command_->set_text("");
        zb::tianji::Json fields = zb::tianji::Json::object();
        fields.set("text", zb::tianji::Json::str(text));
        zb::tianji::Json data;
        std::string err;
        if (!bridge_->request("console.input", std::move(fields),
                              kCommandTimeoutMs, &data, &err))
        {
            write_output("❌ " + err);
            return;
        }
        for (const auto &line : data.members())
        {
            if (line.first == "lines" &&
                line.second.type() == zb::tianji::Json::Type::Array)
            {
                for (const auto &l : line.second.items())
                {
                    write_output(l.as_str());
                }
            }
        }
        if (data.get_bool("quit", false))
        {
            quit();
            return;
        }
        if (data.get_bool("turn_started", false))
        {
            set_busy(true);
        }
        request_project();  // fast feedback: the落流 lands immediately
    }

    void StudioApp::apply_snapshot(const zb::tianji::Json &data)
    {
        fp_ = data.get_str("fp", "");
        set_busy(data.get_bool("busy", false));

        // -- history（镜像 ui.py::_refresh_history） ------------------------
        hist_rows_.clear();
        hist_ids_.clear();
        for (const auto &m : data.members())
        {
            if (m.first != "history" || m.second.type() != zb::tianji::Json::Type::Array)
            {
                continue;
            }
            for (const auto &row : m.second.items())
            {
                const std::string sid = row.get_str("id", "");
                const std::string lock = row.get_bool("locked", true) ? "锁" : "开";
                const std::string mark = row.get_bool("current", false) ? ">" : "·";
                std::string label = sid.size() > 2 ? sid.substr(2) : sid;
                label += " " + row.get_str("title", "");
                const std::string work = row.get_str("work", "");
                if (!work.empty())
                {
                    label += " · " + work;
                }
                hist_rows_.push_back(lock + " " + mark + " " + label);
                hist_ids_.push_back(sid);
            }
        }
        if (hist_rows_.empty())
        {
            hist_rows_.push_back("（无——/new [标题] 创建）");
            hist_ids_.push_back("");
        }

        // -- dialogue（镜像 ui.py::_refresh_dialogue） -----------------------
        dial_rows_.clear();
        std::string dialogue_hint;
        for (const auto &m : data.members())
        {
            if (m.first != "dialogue" || m.second.type() != zb::tianji::Json::Type::Array)
            {
                continue;
            }
            if (m.second.items().empty())
            {
                dialogue_hint = "（谈话流为空——人类先开口；/draft 可委托 AI 代编）";
            }
            for (const auto &e : m.second.items())
            {
                const std::string seat = e.get_str("seat", "");
                const std::string kind = e.get_str("kind", "");
                std::string label;
                if (seat == "human")
                {
                    label = "你";
                }
                else if (seat == seat_ai_)
                {
                    label = seat;
                }
                else
                {
                    label = "·系统·";
                }
                const std::string ph = e.get_str("prompt_hash", "");
                if (kind == "draft_ref")
                {
                    // 草稿全文住 drafts/（文本窗待签收区），对话窗只留指针
                    dial_rows_.push_back(label + " ▸〔稿〕" + e.get_str("text", ""));
                }
                else
                {
                    dial_rows_.push_back(label + " ▸ " + e.get_str("text", ""));
                }
                if (!ph.empty())
                {
                    dial_rows_.push_back("    └─ 配方 " + ph);
                }
            }
        }
        if (dial_rows_.empty())
        {
            dial_rows_.push_back(dialogue_hint.empty()
                                     ? "（未打开会话——/new [标题] 或 /open <id>，或在左上历史里选）"
                                     : dialogue_hint);
        }

        // -- text window / status / subtitle --------------------------------
        manuscript_->set_text(data.get_str("text", "").c_str());
        status_->set_text(((busy_ ? "〔AI 回合中〕 " : "") +
                           data.get_str("status", ""))
                              .c_str());
        subtitle_->set_text(data.get_str("subtitle", "").c_str());
        refresh_lists();

        // the one-shot 存证 (the tianji client's TIANJI_GIF precedent):
        // the first applied snapshot is the boot state, pixel-proven
#if defined(USE_PNG)
        if (!shot_done_)
        {
            shot_done_ = true;
            if (const char *png = std::getenv("TIANJI_STUDIO_PNG");
                png != nullptr && png[0] != '\0')
            {
                try
                {
                    zb::snap::save_png(*window_, png);
                }
                catch (const std::exception &e)
                {
                    LW << "studio: PNG 存证 failed: " << e.what();
                }
            }
        }
#endif
    }

    void StudioApp::handle_event(const zb::tianji::Json &event)
    {
        const std::string type = event.get_str("type", "");
        const zb::tianji::Json *data = event.find("data");
        if (type == "turn.finished" && data != nullptr)
        {
            for (const auto &line : data->members())
            {
                if (line.first == "lines" &&
                    line.second.type() == zb::tianji::Json::Type::Array)
                {
                    for (const auto &l : line.second.items())
                    {
                        write_output(l.as_str());
                    }
                }
            }
            set_busy(false);
            request_project();  // the turn's落账 must reach the panes now
            return;
        }
        LW << "studio: unhandled bridge event " << type;
    }

    void StudioApp::write_output(const std::string &line)
    {
        out_rows_.push_back(line);
        if (output_ != nullptr)
        {
            output_->set_item_count(out_rows_.size());
        }
        if (out_scroll_ != nullptr)
        {
            out_scroll_->set_follow_bottom(true);
        }
    }

    void StudioApp::refresh_lists()
    {
        if (history_ != nullptr)
        {
            history_->set_item_count(hist_rows_.size());
        }
        if (dialogue_ != nullptr)
        {
            dialogue_->set_item_count(dial_rows_.size());
        }
        if (dial_scroll_ != nullptr)
        {
            dial_scroll_->set_follow_bottom(true);  // stay pinned on append
        }
        if (out_scroll_ != nullptr)
        {
            out_scroll_->set_follow_bottom(true);
        }
    }

    void StudioApp::set_busy(const bool busy)
    {
        busy_ = busy;
        if (command_ != nullptr)
        {
            command_->set_read_only(busy);  // 回合期间输入行禁用（docs/09 §7.5）
        }
    }

    void StudioApp::scroll_pane(zb::ui::ScrollPanel *pane, const int delta)
    {
        if (pane == nullptr)
        {
            return;
        }
        const int target = pane->scroll_offset() + delta;
        pane->set_scroll_offset(target);
    }

    void StudioApp::quit()
    {
        if (window_ != nullptr)
        {
            window_->close();  // the shell exits; ~BridgeClient sends EOF
        }
    }

    void StudioApp::input(const zb::input::input_event &ev) noexcept
    {
        // App-level bindings (the TUI's App BINDINGS mirrored): scroll
        // chords work while the input line keeps focus. The SIXEL shell
        // delivers Ctrl letters as control-byte chords (FI-11: no
        // Shift/Ctrl+arrow yet).
        if (ev.type == zb::input::input_type::key_down &&
            (ev.mods & zb::input::mod_ctrl) != 0)
        {
            switch (static_cast<char>(ev.key))
            {
            case 'q':
                quit();
                return;
            case 'e':  // 对话窗 up（Ctrl+E）
                scroll_pane(dial_scroll_, -4 * kLinePitch);
                return;
            case 'y':  // 对话窗 down（Ctrl+Y）
                scroll_pane(dial_scroll_, 4 * kLinePitch);
                return;
            case 'w':  // 输出窗 up（Ctrl+W）
                scroll_pane(out_scroll_, -4 * kLinePitch);
                return;
            case 's':  // 输出窗 down（Ctrl+S）
                scroll_pane(out_scroll_, 4 * kLinePitch);
                return;
            default:
                break;  // Ctrl+A/C/V etc. fall through to the widgets
            }
        }
        window_->input(ev);
    }

    void StudioApp::step(const std::uint32_t step_micros,
                         const std::uint64_t index) noexcept
    {
        (void)step_micros;
        try
        {
            if (bridge_ == nullptr || !bridge_->alive())
            {
                if (bridge_ready_ && !bridge_dead_)
                {
                    bridge_dead_ = true;
                    write_output("❌ 桥已退出（stdin EOF / 崩溃）——Ctrl+Q 关闭客户端。");
                    status_->set_text("桥已退出");
                }
                return;
            }
            bridge_->poll();
            zb::tianji::Json event;
            while (bridge_->next_event(&event))
            {
                handle_event(event);
            }
            if (index - last_poll_ >= kProjectPollInterval)
            {
                last_poll_ = index;
                request_project();
            }
        }
        catch (const std::exception &e)
        {
            // the step never throws into the shell loop (R6)
            LE << "studio: step failed: " << e.what();
        }
    }
}  // namespace zb::app::studio
