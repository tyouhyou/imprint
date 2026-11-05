#pragma once

/*
 * The tianji feed client (TJ-C2): one JSON line out, one JSON line in
 * over the tianji RoomServer's Unix socket — the exact shape tianji's
 * own Python `RoomClient` speaks (docs/07 "传输口径": response is a
 * JSON ARRAY of message objects). No new protocol: hello /
 * join_room / subscribe (+ after_seq resume) / ping, and the
 * server-to-client message set from §5.1.
 *
 * Synchronous and single-threaded, like the Python client: a request
 * blocks until the response line arrives. Reconnect = connect() again
 * + subscribe(after_seq=last_seq) — the server re-sends events after
 * the cursor and never duplicates.
 */

#include <string>
#include <vector>

#include "json.hpp"

namespace zb::tianji
{
    class FeedClient
    {
    public:
        FeedClient() = default;
        ~FeedClient();
        FeedClient(const FeedClient &) = delete;
        FeedClient &operator=(const FeedClient &) = delete;

        // connects to the RoomServer socket; on failure returns false
        // and fills `error` (hot path: return values, never throws)
        bool connect(const char *socket_path, std::string *error = nullptr);
        void close();
        bool connected() const { return fd_ >= 0; }

        /*
         * Sends {v:1, id, type, user_id, ...fields} as one line and
         * reads the response line (a JSON array of messages). On
         * success returns the message array and bumps last_seq() from
         * any event messages in it. On failure returns an empty vector
         * and fills `error`.
         */
        std::vector<Json> request(const std::string &type, Json fields,
                                  std::string *error = nullptr);

        // the highest event seq seen (the subscribe cursor)
        long long last_seq() const { return last_seq_; }

    private:
        bool write_line(const std::string &line, std::string *error);
        bool read_line(std::string &line, std::string *error);

        int fd_ = -1;
        long long last_seq_ = 0;
        long long next_id_ = 1;
        std::string read_buffer_;
    };
}  // namespace zb::tianji
