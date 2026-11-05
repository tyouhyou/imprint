#pragma once

/*
 * The view builder (TJ-C2): v1 feed messages -> the client's view
 * model. This is the message-level I/F's output face: the left
 * message stream's rows, the server-enumerated legal-action
 * proposals, and the stream cursor — exactly the shapes
 * docs/backlog.md Batch FI "The client, as specified" names
 * (旁白/发言/动作成败/线索发现/系统提示/终局卡 categories, seat-stable
 * names, follow-bottom, proposals never invented client-side).
 *
 * TJ-C3's app turns the model into widgets (ListBox rows + styled
 * runs); this module stays widget-free so the protocol semantics are
 * testable without a shell.
 */

#include <string>
#include <vector>

#include "json.hpp"

namespace zb::tianji
{
    enum class RowKind
    {
        narration,   // 旁白 / world broadcast
        speech,      // 发言 (speech.uttered)
        action,      // 动作成败 (action.*)
        clue,        // 线索发现 (clue.*)
        system,      // 系统提示 (presence / window / errors)
        ending       // 终局卡 (session.ended)
    };

    struct StreamRow
    {
        RowKind kind = RowKind::system;
        int tick = 0;
        std::string name;  // seat-stable display name ("" for narration)
        std::string text;
        long long seq = 0;  // the event cursor this row came from
    };

    struct Proposal
    {
        std::string action;
        std::string target;  // the first legal target ("" when open)
        std::string intent;  // the command line's hint text
    };

    struct TianjiView
    {
        std::vector<StreamRow> rows;
        std::vector<Proposal> proposals;
        std::string seat_name;   // "me" display name from the SeatView
        std::string seat_id;
        std::string window_id;
        bool window_submitted = false;
        long long last_seq = 0;  // the subscribe cursor (resume with it)
    };

    /*
     * Applies one v1 message to the view. Returns false for messages
     * that carry no stream-visible change (pong, stage-only, unknown
     * types — logged once per type by the caller if it wants noise).
     * Known messages:
     *   room_snapshot — seats/phase set the roster baseline
     *   view          — seat_view.recent seeds the stream, the "me"
     *                   name, legal_actions -> proposals
     *   event         — one row per known event type; unknown types
     *                   land as narration with the raw text (the
     *                   server filter is trusted; nothing is dropped
     *                   silently)
     *   error         — a system row with code + message
     */
    bool apply_message(TianjiView &view, const Json &msg);
}  // namespace zb::tianji
