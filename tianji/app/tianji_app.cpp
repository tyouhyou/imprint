#include "tianji_app.hpp"

#include "snapshot.hpp"
#include "tianji_stage.gen.hpp"  // bytes_embed blob: the 800x600 stage placeholder

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imui.hpp"

#include "annotate_canvas.hpp"
#include "flex_panel.hpp"
#include "image_view.hpp"
#include "list_box.hpp"
#include "logging.hpp"
#include "scroll_panel.hpp"
#include "text_input.hpp"

#include "codec/image.hpp"
#include "text/runtime_ttf_provider.hpp"

#include "feed/feed_client.hpp"
#include "feed/mock_server.hpp"

namespace zb::app::tianji
{
    namespace
    {
        // offline replay pacing (R6): one message per this many steps —
        // a capture is a pure function of (feed source, step count)
        constexpr std::uint64_t kReplayStepInterval = 30;

        // the stream's row geometry: the height provider's advance
        // estimate (wide CJK units ~14px, narrow units ~7px) over the
        // stream column's inner width; the real advance depends on the
        // provider (5x7 vs the runtime CJK font), this is the shared
        // approximation the variable-height rows size by
        constexpr int kStreamInnerWidth = 344;
        constexpr int kLinePitch = 20;

        int row_lines(const std::string &utf8)
        {
            // wide-unit estimate over the UTF-8 bytes: lead-byte
            // classification is enough for the estimate (CJK leads
            // E4-E9..., everything else narrow); a surrogate pair is
            // one wide unit
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
            int lines = (advance + kStreamInnerWidth - 1) / kStreamInnerWidth;
            if (lines < 1)
            {
                lines = 1;
            }
            return lines;
        }

        // the built-in offline transcript: a synthetic four-line scene
        // (no cross-repo content dependency; TIANJI_REPLAY replaces it
        // with a run_*.json-shaped file)
        const char *kBuiltinReplay =
            "{"
            "\"session_id\":\"sess_offline\","
            "\"end_mode\":\"fail\","
            "\"transcript\":["
            "{\"tick\":0,\"seat\":\"seat.a\",\"name\":\"A\","
            "\"text\":\"The east-wing lamp went out at the third bell.\"},"
            "{\"tick\":0,\"seat\":\"seat.b\",\"name\":\"B\","
            "\"text\":\"I heard the supply door, not the lamp.\"},"
            "{\"tick\":1,\"seat\":\"seat.a\",\"name\":\"A\","
            "\"text\":\"Then the timer was moved before the bell.\"}"
            "],"
            "\"beats\":[],"
            "\"claims\":[]"
            "}";

        zb::tianji::Json load_replay_record()
        {
            const char *path = std::getenv("TIANJI_REPLAY");
            if (path != nullptr)
            {
                // the FILE* guard: bytes.append can throw (OOM) between
                // fopen and fclose (the acquire/release RAII rule)
                struct file_guard
                {
                    std::FILE *f;
                    explicit file_guard(std::FILE *file)
                        : f(file) {}
                    ~file_guard()
                    {
                        if (f != nullptr)
                            std::fclose(f);
                    }
                };
                if (std::FILE *f = std::fopen(path, "rb"))
                {
                    file_guard guard{f};
                    std::string bytes;
                    char chunk[4096];
                    size_t n;
                    while ((n = std::fread(chunk, 1, sizeof(chunk), guard.f)) > 0)
                    {
                        bytes.append(chunk, n);
                    }
                    const char *p = bytes.data();
                    std::string err;
                    const zb::tianji::Json record = zb::tianji::Json::parse(p, p + bytes.size(), &err);
                    if (err.empty())
                    {
                        return record;
                    }
                    LW << "tianji: TIANJI_REPLAY parse failed (" << err
                       << "); falling back to the built-in transcript";
                }
                else
                {
                    LW << "tianji: TIANJI_REPLAY unreadable; falling "
                          "back to the built-in transcript";
                }
            }
            const char *p = kBuiltinReplay;
            return zb::tianji::Json::parse(p, p + std::strlen(kBuiltinReplay));
        }

        std::string row_text(const zb::tianji::StreamRow &r)
        {
            // ASCII category tags: the 5x7 fallback covers them in every
            // build; the runtime CJK font carries the row body
            switch (r.kind)
            {
            case zb::tianji::RowKind::narration:
                return "NARR " + r.text;
            case zb::tianji::RowKind::speech:
                return r.name + ": " + r.text;
            case zb::tianji::RowKind::action:
                return "ACT " + (r.name.empty() ? "" : r.name + " ") + r.text;
            case zb::tianji::RowKind::clue:
                return "CLUE " + r.text;
            case zb::tianji::RowKind::system:
                return "SYS " + r.text;
            case zb::tianji::RowKind::ending:
                return "END " + r.text;
            }
            return r.text;
        }
    }  // namespace

    std::string TianjiApp::stream_text(const void *arg, const size_t index)
    {
        const auto *ctx = static_cast<const StreamContext *>(arg);
        return row_text(ctx->rows->at(index));
    }

    int TianjiApp::stream_height(const void *arg, const size_t index)
    {
        const auto *ctx = static_cast<const StreamContext *>(arg);
        const std::string text = row_text(ctx->rows->at(index));
        return row_lines(text) * kLinePitch + 6;
    }

    void TianjiApp::create_window(const uint32_t max_client_width,
                                  const uint32_t max_client_height,
                                  void *buffer)
    {
        window_ = zb::make_shared<CanvasWindow>();
        window_->create(max_client_width, max_client_height, buffer);
        auto &root = window_->root();

        // the main row: left stream + right stage
        auto main_row = std::make_unique<zb::ui::FlexPanel>();
        main_row->set_direction(zb::ui::FlexPanel::flex_direction::row);
        main_row->set_size(max_client_width > 0 ? static_cast<int>(max_client_width) : 1200,
                           max_client_height > 48
                               ? static_cast<int>(max_client_height) - 48
                               : 600);

        auto stream_scroll = std::make_unique<zb::ui::ScrollPanel>();
        stream_scroll->set_size(380, main_row->get_size().height);
        stream_scroll->set_follow_bottom(true);  // FI-05: the stream pins
        auto stream_list = std::make_unique<zb::ui::ListBox>();
        stream_list->set_size(360, 1);
        stream_list->set_visible_rows(20);
        stream_list->set_item_text(&TianjiApp::stream_text, &stream_ctx_);
        stream_list->set_item_height(&TianjiApp::stream_height, &stream_ctx_);
        stream_ = stream_list.get();
        stream_scroll->add_child(std::move(stream_list), 1);
        stream_scroll_ = stream_scroll.get();
        main_row->add_child(std::move(stream_scroll), 0,
                            zb::ui::FlexPanel::self_align::stretch);

        auto stage = std::make_unique<zb::ui::AnnotateCanvas>();
        stage->set_canvas_size(800, 600);  // the fixed logical stage
                                           // the placeholder scene (FI-09/V-4): the packed PNG decodes
                                           // once into the content-id cache -- without an image the
                                           // canvas has no mapped rect and cannot capture strokes; the
                                           // real scene art is the FI-04 ask to tianji
                                           // (tianji/for_tianji.md 4). USE_PNG build: the scene art is
                                           // compressed shipping data, the codecs are its path.
#if defined(USE_PNG)
        // a decode failure keeps the empty canvas (strokes cannot
        // capture) — the LW already said why
        (void)stage->set_image_png("stage.placeholder", tianji_stage,
                                   tianji_stage_len);
#endif
        stage->set_annotate_enabled(true);  // case analysis: draw on it
        stage_ = stage.get();
        main_row->add_child(std::move(stage), 1);

        const int main_width = main_row->get_size().width;
        root.add_child(std::move(main_row));

        // the command row: proposals hint + the command line
        auto cmd_row = std::make_unique<zb::ui::FlexPanel>();
        cmd_row->set_direction(zb::ui::FlexPanel::flex_direction::row);
        cmd_row->set_size(main_width, 48);
        auto proposals = std::make_unique<zb::ui::Label>();
        proposals->set_text("");
        proposals_label_ = proposals.get();
        cmd_row->add_child(std::move(proposals), 1);
        auto command = std::make_unique<zb::ui::TextInput>();
        command->set_size(380, 24);
        command->submitted += [this](const std::string &text)
        {
            command_text_ = text;
            submit_command();
        };
        command_ = command.get();
        cmd_row->add_child(std::move(command), 0);
        root.add_child(std::move(cmd_row));

        // the CJK dialogue font (the FI-04 recipe's application-side
        // landing): TIANJI_FONT names a full TTF, loaded as the process
        // family so the stream rows and the command line render hanzi.
        // Init path: a failure degrades to the 5x7 fallback (CJK units
        // skip) with one log line, never a crash. The static family is
        // the app-scoped anchor the L-5 ownership rule requires.
#if defined(IMCORE_HAS_TTF_RUNTIME)
        if (const char *font = std::getenv("TIANJI_FONT");
            font != nullptr && font[0] != '\0')
        {
            try
            {
                static zb::ui::TtfFamily family =
                    zb::ui::TtfFamily::from_file(font);
                set_font_family(family);
            }
            catch (const std::exception &e)
            {
                LW << "tianji: TIANJI_FONT load failed (" << e.what()
                   << "); CJK rows degrade to the 5x7 fallback";
            }
        }
#endif

        // the feed source: the RoomServer socket when named, the
        // offline replay otherwise
        const char *sock = std::getenv("TIANJI_SOCKET");
        if (sock != nullptr && sock[0] != '\0')
        {
            socket_path_ = sock;
            feed_ = std::make_unique<zb::tianji::FeedClient>();
            std::string wire_err;
            if (feed_->connect(socket_path_.c_str()))
            {
                zb::tianji::Json hello = zb::tianji::Json::object();
                hello.set("client_id", zb::tianji::Json::str("imprint-tianji"));
                hello.set("user_id", zb::tianji::Json::str("player"));
                auto msgs = feed_->request("hello", std::move(hello), &wire_err);
                if (msgs.empty() && !wire_err.empty())
                {
                    LW << "tianji: TIANJI_SOCKET hello failed (" << wire_err
                       << "); falling back to the offline replay";
                    feed_.reset();
                }
                else
                {
                    zb::tianji::Json join = zb::tianji::Json::object();
                    join.set("room_id", zb::tianji::Json::str("room_default"));
                    join.set("role", zb::tianji::Json::str("actor"));
                    msgs = feed_->request("join_room", std::move(join), &wire_err);
                    if (msgs.empty() && !wire_err.empty())
                    {
                        LW << "tianji: TIANJI_SOCKET join failed (" << wire_err
                           << "); falling back to the offline replay";
                        feed_.reset();
                    }
                    else
                    {
                        for (const zb::tianji::Json &m : msgs)
                        {
                            zb::tianji::apply_message(view_, m);
                        }
                        socket_ready_ = true;
                    }
                }
            }
            else
            {
                LW << "tianji: TIANJI_SOCKET connect failed; falling "
                      "back to the offline replay";
                feed_.reset();
            }
        }
        if (!socket_ready_)
        {
            const zb::tianji::Json record = load_replay_record();
            replay_ = zb::tianji::MockTianjiServer::synthesize(record, "room_offline");
        }

        refresh_stream();
        refresh_proposals();
    }

    void TianjiApp::refresh_stream()
    {
        stream_ctx_.rows = &view_.rows;
        // the provider owns the height (FI-06): the count setter
        // re-derives the widget height from the fresh model
        stream_->set_item_count(view_.rows.size());
        stream_scroll_->set_follow_bottom(true);  // stay pinned on append
    }

    void TianjiApp::refresh_proposals()
    {
        if (view_.proposals.empty())
        {
            proposals_label_->set_text("");
            return;
        }
        std::string text = "proposals:";
        for (const auto &p : view_.proposals)
        {
            text += " " + p.action;
            if (!p.target.empty())
            {
                text += "(" + p.target + ")";
            }
        }
        proposals_label_->set_text(text.c_str());
    }

    void TianjiApp::append_row(const zb::tianji::StreamRow &row)
    {
        view_.rows.push_back(row);
        refresh_stream();
    }

    void TianjiApp::submit_command()
    {
        // resolve the command against the server-enumerated proposals —
        // exact action match first, then prefix; the client never
        // invents an action the server did not list
        const std::string &cmd = command_text_;
        const zb::tianji::Proposal *match = nullptr;
        for (const auto &p : view_.proposals)
        {
            if (p.action == cmd)
            {
                match = &p;
                break;
            }
        }
        if (match == nullptr)
        {
            for (const auto &p : view_.proposals)
            {
                if (cmd.rfind(p.action, 0) == 0)
                {
                    match = &p;
                    break;
                }
            }
        }
        if (match == nullptr)
        {
            zb::tianji::StreamRow r;
            r.kind = zb::tianji::RowKind::system;
            r.text = "not a legal action: " + cmd;
            append_row(r);
            return;
        }
        if (socket_ready_ && feed_ != nullptr)
        {
            zb::tianji::Json fields = zb::tianji::Json::object();
            fields.set("window_id", zb::tianji::Json::str(view_.window_id));
            fields.set("seat_id", zb::tianji::Json::str(view_.seat_id));
            zb::tianji::Json proposal = zb::tianji::Json::object();
            proposal.set("action", zb::tianji::Json::str(match->action));
            fields.set("proposal", std::move(proposal));
            std::string err;
            auto msgs = feed_->request("submit_proposal", std::move(fields), &err);
            if (msgs.empty() && !err.empty())
            {
                wire_down(err);
                return;
            }
            for (const zb::tianji::Json &m : msgs)
            {
                zb::tianji::apply_message(view_, m);
            }
            refresh_stream();
            refresh_proposals();
            return;
        }
        // offline replay: nothing to submit to — say so in the stream
        zb::tianji::StreamRow r;
        r.kind = zb::tianji::RowKind::system;
        r.text = "offline replay: " + match->action + " (no server)";
        append_row(r);
    }

    void TianjiApp::wire_down(const std::string &why)
    {
        if (!socket_ready_)
        {
            return;  // already down
        }
        socket_ready_ = false;
        LW << "tianji: feed dropped (" << why << "); falling back to "
                                                 "the offline replay";
        zb::tianji::StreamRow r;
        r.kind = zb::tianji::RowKind::system;
        r.text = "feed dropped: " + why;
        append_row(r);
        if (replay_.empty())
        {
            // the socket mode never built the replay queue: build it so
            // the app keeps a live stream (the documented reconnect =
            // connect + subscribe(after_seq) is the host's restart path)
            replay_ = zb::tianji::MockTianjiServer::synthesize(
                load_replay_record(), "room_offline");
            replay_at_ = 0;
        }
    }

    void TianjiApp::poll_feed()
    {
        if (!socket_ready_ || feed_ == nullptr)
        {
            return;
        }
        zb::tianji::Json sub = zb::tianji::Json::object();
        sub.set("after_seq", zb::tianji::Json::number(static_cast<double>(view_.last_seq)));
        std::string err;
        auto msgs = feed_->request("subscribe", std::move(sub), &err);
        if (msgs.empty() && !err.empty())
        {
            wire_down(err);  // the socket closed under us
            return;
        }
        bool changed = false;
        for (const zb::tianji::Json &m : msgs)
        {
            StageState s;
            if (zb::app::tianji::parse_stage(m, s))
            {
                stages_.push_back(std::move(s));  // a replay beat
                changed = true;
                continue;
            }
            if (zb::tianji::apply_message(view_, m))
            {
                changed = true;
            }
        }
        if (changed)
        {
            refresh_stream();
            refresh_proposals();
        }
    }

    void TianjiApp::step(const std::uint32_t step_micros,
                         const std::uint64_t index) noexcept
    {
        (void)step_micros;
        try
        {
            if (socket_ready_)
            {
                poll_feed();
            }
            else if (replay_at_ < replay_.size() &&
                     index % kReplayStepInterval == 0)
            {
                const zb::tianji::Json &m = replay_[replay_at_];
                StageState s;
                if (zb::app::tianji::parse_stage(m, s))
                {
                    stages_.push_back(std::move(s));
                }
                else if (zb::tianji::apply_message(view_, m))
                {
                    refresh_stream();
                    refresh_proposals();
                }
                ++replay_at_;
            }
            // the stage beats present half an interval after the
            // message that carried them: each tick does one thing
            if (index % kReplayStepInterval == kReplayStepInterval / 2)
            {
                present_stage_beat();
            }
        }
        catch (const std::exception &e)
        {
            // the step never throws into the shell loop (R6)
            LE << "tianji: step failed: " << e.what();
        }
    }

    void TianjiApp::present_stage_beat()
    {
        if (stage_presented_ >= stages_.size())
        {
            return;
        }
        compose_stage(stages_[stage_presented_], stage_buf_);
        const core::image_t view{stage_buf_.data(), kStageWidth,
                                 kStageHeight, 0};
        stage_->set_image(view);
        ++stage_presented_;
        if (stage_presented_ == stages_.size())
        {
            // the replay finished: the optional GIF 存证 (the
            // GIF89a writer from the snapshot helper)
            export_replay_gif();
        }
    }

    void TianjiApp::export_replay_gif()
    {
        const char *gif = std::getenv("TIANJI_GIF");
        if (gif == nullptr || gif[0] == '\0')
        {
            return;
        }
        // the snapshot helper's init path throws; the step never does
        try
        {
            zb::snap::save_gif(*window_, gif);
        }
        catch (const std::exception &e)
        {
            LW << "tianji: replay GIF export failed: " << e.what();
        }
    }

    void TianjiApp::input(const zb::input::input_event &ev) noexcept
    {
        window_->input(ev);
    }
}  // namespace zb::app::tianji
