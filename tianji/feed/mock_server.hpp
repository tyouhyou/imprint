#pragma once

/*
 * The mock feeder (TJ-C2): a stand-in tianji RoomServer that speaks
 * the same wire (one JSON line per request, one line per response) so
 * the feed client is exercised end to end without tianji in the room.
 *
 * It replays a session record shaped like tianji's
 * `samples/run_子夜客栈.json` (session_id / transcript[] / beats[] /
 * claims[]): hello returns [welcome], join_room returns
 * [room_snapshot, view], subscribe(after_seq) returns the synthesized
 * [event...] stream past the cursor (seq is the 1-based transcript
 * order, so resume is exact), ping returns [pong]. Deterministic:
 * same record + same request order -> same bytes.
 */

#include <string>
#include <vector>

#include "json.hpp"

namespace zb::tianji
{
    class MockTianjiServer
    {
    public:
        MockTianjiServer() = default;
        ~MockTianjiServer();
        MockTianjiServer(const MockTianjiServer &) = delete;
        MockTianjiServer &operator=(const MockTianjiServer &) = delete;

        /*
         * Builds the synthesized message set from a session record
         * (the run_*.json shape). Exposed for tests and for an
         * offline mode: room_id is the mock's, seats are derived from
         * the transcript's distinct (seat, name) pairs.
         */
        static std::vector<Json> synthesize(const Json &record,
                                            const std::string &room_id);

        /*
         * Binds a fresh abstract-namespace or filesystem socket and
         * serves ONE connection (the feed client's): reads request
         * lines, answers with the synthesized responses, until the
         * peer disconnects. Runs on a caller-provided thread — serve()
         * blocks; stop() from another thread breaks it out. Returns
         * false and fills `error` when the socket cannot be created.
         */
        bool listen(std::string *socket_path_out, std::string *error);
        void serve();          // blocks: one client, then returns
        void stop();           // thread-safe: closes the listener
        const std::string &socket_path() const { return socket_path_; }

        // installs the record BEFORE listen()
        void set_record(Json record, std::string room_id)
        {
            record_ = std::move(record);
            room_id_ = std::move(room_id);
        }

    private:
        int listen_fd_ = -1;
        std::string socket_path_;
        Json record_;
        std::string room_id_ = "room_mock";
    };
}  // namespace zb::tianji
