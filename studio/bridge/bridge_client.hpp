#pragma once

/*
 * The studio bridge client (P1-10, tianji repo docs/09 §9.1): the UI
 * side of the studio-bridge wire. One JSON line out per request, one
 * line in per response (id-correlated), unsolicited events (no id)
 * queue FIFO for the app's step() loop.
 *
 * Transport: a spawned child process (`python -m studio.pipeline
 * studio-bridge`) over two pipes — Studio is single-user with no
 * server to share, spawning *is* "executing the prototype", and pipes
 * carry none of the AF_UNIX platform gates (macOS included). The read
 * fd is non-blocking; poll() drains it once per shell step (the same
 * shape as the tianji game client's poll_feed). request() is a bounded
 * poll(2) wait — the bridge answers commands in well under a second;
 * the slow part of a turn (the LLM call) lives in the bridge's own
 * worker thread and comes back as a turn.finished EVENT, never as a
 * response.
 *
 * Lifecycle contract: closing the write fd sends the bridge EOF, which
 * is its graceful exit (docs/09 §9.1 "EOF（客户端关 stdin）= 桥退出").
 * A /quit console.input response (data.quit) arrives BEFORE the EOF;
 * both paths end the child.
 */

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "feed/json.hpp"  // the tj_json wire DOM (shared with tianji/feed)

namespace zb::studio
{
    class BridgeClient
    {
    public:
        BridgeClient() = default;
        ~BridgeClient();
        BridgeClient(const BridgeClient &) = delete;
        BridgeClient &operator=(const BridgeClient &) = delete;

        /*
         * Spawns `command` (a shell line, exec'd via sh -c) with its
         * working directory set to `cwd` and its stdio wired to this
         * client. On failure returns false and fills `error`; nothing
         * is left running (the child execs or _exits).
         */
        bool spawn(const std::string &cwd, const std::string &command,
                   std::string *error = nullptr);

        // test seam: attach already-open fds instead of spawning. The
        // client takes ownership (close() closes both).
        void attach(int read_fd, int write_fd);

        void close();
        bool alive() const { return read_fd_ >= 0; }

        /*
         * Non-blocking drain: readable bytes -> complete lines ->
         * dispatch. A line with an `id` matching the pending request
         * resolves it; everything else (and any line while no request
         * is pending) queues as an event. Call once per step().
         */
        void poll();

        /*
         * Sends {v:1, id, type, ...fields} and waits up to timeout_ms
         * for the id-correlated response. Returns true on an ok:true
         * response (data_out receives it); false on error/timeout with
         * `error` filled ("timeout" or the wire error). One request at
         * a time: a second call while one is pending fails fast.
         */
        bool request(const std::string &type, const zb::tianji::Json &fields,
                     int timeout_ms, zb::tianji::Json *data_out,
                     std::string *error = nullptr);

        // pops the oldest queued event; false when none pending
        bool next_event(zb::tianji::Json *event_out);

        // events seen but not yet popped (app-side liveness signal)
        [[nodiscard]] size_t pending_events() const { return events_.size(); }

    private:
        void dispatch_line(const std::string &line);
        bool write_line(const std::string &line, std::string *error);

        int read_fd_ = -1;
        int write_fd_ = -1;
        long long next_id_ = 1;
        bool pending_ = false;        // a request is in flight
        std::string pending_id_;
        zb::tianji::Json pending_data_;
        std::string pending_error_;   // non-empty = error response / wire fail
        bool pending_resolved_ = false;
        std::string read_buffer_;
        std::deque<zb::tianji::Json> events_;
    };
}  // namespace zb::studio
