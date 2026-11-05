#include "view_builder.hpp"

#include "logging.hpp"

namespace zb::tianji
{
    namespace
    {
        // event-type -> row kind (the registry slice the v1 stream
        // carries today; unknown types degrade to narration below)
        RowKind kind_of_event(const std::string &type)
        {
            if (type == "speech.uttered")
            {
                return RowKind::speech;
            }
            if (type.rfind("action.", 0) == 0)
            {
                return RowKind::action;
            }
            if (type.rfind("clue.", 0) == 0)
            {
                return RowKind::clue;
            }
            if (type == "session.ended")
            {
                return RowKind::ending;
            }
            if (type == "world.broadcast" || type.rfind("narrative.", 0) == 0)
            {
                return RowKind::narration;
            }
            // presence / window / seat.* / director.* / asset.*:
            // system-prompt noise
            return RowKind::system;
        }

        void push_row(TianjiView &view, const RowKind kind, const int tick,
                      std::string name, std::string text, const long long seq)
        {
            StreamRow r;
            r.kind = kind;
            r.tick = tick;
            r.name = std::move(name);
            r.text = std::move(text);
            r.seq = seq;
            view.rows.push_back(std::move(r));
        }

        // the unchecked-narrowing guard (the TJ-NEW review precedent):
        // wire integers land in int fields saturated, never truncated
        int clamp_int(const long long v)
        {
            if (v > 2147483647LL)
            {
                return 2147483647;
            }
            if (v < -2147483648LL)
            {
                return -2147483648;
            }
            return static_cast<int>(v);
        }
    }  // namespace

    bool apply_message(TianjiView &view, const Json &msg)
    {
        if (msg.type() != Json::Type::Object)
        {
            return false;
        }
        const std::string type = msg.get_str("type");
        if (type == "view")
        {
            const Json *sv = msg.find("seat_view");
            if (sv == nullptr)
            {
                return false;
            }
            view.seat_id = sv->get_str("seat_id");
            // the "me" name: the view's own display name rides the
            // roster the snapshot sent; the SeatView's me.name is the
            // documented place when present
            const Json *me = sv->find("me");
            if (me != nullptr)
            {
                view.seat_name = me->get_str("name");
            }
            const Json *recent = sv->find("recent");
            if (recent != nullptr)
            {
                for (const Json &r : recent->items())
                {
                    push_row(view, RowKind::speech,
                             clamp_int(r.get_int("tick")),
                             r.get_str("name"), r.get_str("text"), 0);
                }
            }
            view.proposals.clear();
            const Json *legal = sv->find("legal_actions");
            if (legal != nullptr)
            {
                for (const Json &a : legal->items())
                {
                    Proposal p;
                    p.action = a.get_str("action");
                    const Json *targets = a.find("targets");
                    if (targets != nullptr && !targets->items().empty() &&
                        targets->items().front().type() == Json::Type::String)
                    {
                        p.target = targets->items().front().as_str();
                    }
                    p.intent = a.get_str("intent");
                    view.proposals.push_back(std::move(p));
                }
            }
            const Json *window = sv->find("window");
            if (window != nullptr)
            {
                view.window_id = window->get_str("window_id");
                view.window_submitted = window->get_bool("submitted");
            }
            return true;
        }
        if (type == "event")
        {
            const Json *ev = msg.find("event");
            if (ev == nullptr)
            {
                return false;
            }
            const std::string et = ev->get_str("type");
            const long long seq = ev->get_int("seq");
            const int tick = clamp_int(ev->get_int("tick"));
            if (seq > view.last_seq)
            {
                view.last_seq = seq;
            }
            const Json *payload = ev->find("payload");
            if (payload == nullptr)
            {
                return false;
            }
            const RowKind kind = kind_of_event(et);
            std::string name;
            if (kind == RowKind::speech || kind == RowKind::action)
            {
                name = payload->get_str("name");
                if (name.empty())
                {
                    // the actor form "seat:seat.su": the seat-id tail is
                    // the fallback display name
                    const std::string actor = ev->get_str("actor");
                    if (actor.rfind("seat:", 0) == 0)
                    {
                        name = actor.substr(5);
                    }
                }
            }
            push_row(view, kind, tick, std::move(name),
                     payload->get_str("text"), seq);
            return true;
        }
        if (type == "error")
        {
            push_row(view, RowKind::system, 0, "",
                     msg.get_str("code") + ": " + msg.get_str("message"), 0);
            return true;
        }
        if (type == "room_snapshot")
        {
            // docs/07 §5.2 first-connect flow: the snapshot's last_seq
            // IS the resume cursor — the recent seed and the subscribe
            // cursor must not overlap, or the history lands twice
            const long long snap_seq = msg.get_int("last_seq");
            if (snap_seq > view.last_seq)
            {
                view.last_seq = snap_seq;
            }
            return true;
        }
        // welcome / stage / window / presence / pong: no
        // stream-visible change in the v1 subset
        return false;
    }
}  // namespace zb::tianji
