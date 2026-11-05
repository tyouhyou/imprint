#include "mock_server.hpp"

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <thread>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "logging.hpp"

namespace zb::tianji
{
    namespace
    {
        // mirrors the feed client's response-line cap
        constexpr size_t kMaxLineBytes = 1 << 20;  // 1 MiB

        Json envelope(const char *type, const std::string &room_id)
        {
            Json m = Json::object();
            m.set("v", Json::number(1));
            m.set("type", Json::str(type));
            m.set("room_id", Json::str(room_id));
            return m;
        }

        Json make_welcome(const std::string &room_id)
        {
            Json m = envelope("welcome", room_id);
            m.set("protocol", Json::number(1));
            m.set("role", Json::str("actor"));
            return m;
        }

        // the seats in transcript order of first appearance
        std::vector<std::pair<std::string, std::string>> seat_roster(
            const Json &record)
        {
            std::vector<std::pair<std::string, std::string>> seats;
            const Json *tr = record.find("transcript");
            if (tr == nullptr)
            {
                return seats;
            }
            for (const Json &line : tr->items())
            {
                const std::string seat = line.get_str("seat");
                const std::string name = line.get_str("name");
                bool seen = false;
                for (const auto &s : seats)
                {
                    if (s.first == seat)
                    {
                        seen = true;
                        break;
                    }
                }
                if (!seen)
                {
                    seats.emplace_back(seat, name);
                }
            }
            return seats;
        }

        Json make_snapshot(const std::string &room_id, const Json &record)
        {
            Json m = envelope("room_snapshot", room_id);
            m.set("phase", Json::str("playing"));
            m.set("last_seq", Json::number(
                                  record.find("transcript") != nullptr
                                      ? static_cast<double>(record.find("transcript")->items().size())
                                      : 0.0));
            Json seats = Json::array();
            for (const auto &s : seat_roster(record))
            {
                Json seat = Json::object();
                seat.set("seat_id", Json::str(s.first));
                seat.set("name", Json::str(s.second));
                seat.set("controller_kind", Json::str("ai"));
                seats.push_back(std::move(seat));
            }
            m.set("seats", std::move(seats));
            return m;
        }

        Json make_view(const std::string &room_id, const Json &record)
        {
            Json m = envelope("view", room_id);
            Json view = Json::object();
            const Json *tr = record.find("transcript");
            const auto roster = seat_roster(record);
            const std::string my_seat =
                roster.empty() ? std::string("seat.none") : roster.front().first;
            view.set("seat_id", Json::str(my_seat));
            view.set("tick_unit", Json::str("站时"));
            // recent: the transcript so far, in order — the client's
            // left stream is exactly this list plus the event deltas
            Json recent = Json::array();
            if (tr != nullptr)
            {
                for (const Json &line : tr->items())
                {
                    Json r = Json::object();
                    r.set("seat_id", Json::str(line.get_str("seat")));
                    r.set("name", Json::str(line.get_str("name")));
                    r.set("text", Json::str(line.get_str("text")));
                    r.set("channel", Json::str("public"));
                    recent.push_back(std::move(r));
                }
            }
            view.set("recent", std::move(recent));
            // legal_actions: the documented server-generated proposals —
            // the client never invents actions, the mock proposes the
            // same shape SeatView carries
            Json legal = Json::array();
            Json speak = Json::object();
            speak.set("action", Json::str("speak"));
            speak.set("targets", Json::array());
            legal.push_back(std::move(speak));
            Json search = Json::object();
            search.set("action", Json::str("search"));
            Json targets = Json::array();
            targets.push_back(Json::str("loc.equip"));
            search.set("targets", std::move(targets));
            legal.push_back(std::move(search));
            view.set("legal_actions", std::move(legal));
            Json window = Json::object();
            window.set("window_id", Json::str("w-0001"));
            window.set("submitted", Json::boolean(false));
            view.set("window", std::move(window));
            m.set("seat_view", std::move(view));
            return m;
        }

        Json make_event(const std::string &room_id, const Json &line,
                        const long long seq)
        {
            Json m = envelope("event", room_id);
            m.set("seq", Json::number(static_cast<double>(seq)));
            Json ev = Json::object();
            ev.set("seq", Json::number(static_cast<double>(seq)));
            ev.set("tick", Json::number(static_cast<double>(line.get_int("tick"))));
            ev.set("type", Json::str("speech.uttered"));
            ev.set("actor", Json::str("seat:" + line.get_str("seat")));
            Json audience = Json::object();
            audience.set("mode", Json::str("all"));
            ev.set("audience", std::move(audience));
            Json payload = Json::object();
            payload.set("seat_id", Json::str(line.get_str("seat")));
            payload.set("name", Json::str(line.get_str("name")));
            payload.set("text", Json::str(line.get_str("text")));
            ev.set("payload", std::move(payload));
            m.set("event", std::move(ev));
            return m;
        }
    }  // namespace

    MockTianjiServer::~MockTianjiServer()
    {
        stop();  // wake a blocked accept, if any
        if (listen_fd_ >= 0)
        {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
    }

    std::vector<Json> MockTianjiServer::synthesize(const Json &record,
                                                   const std::string &room_id)
    {
        // the full stream: welcome, snapshot, view, then one event per
        // transcript line with seq = its 1-based order
        std::vector<Json> out;
        out.push_back(make_welcome(room_id));
        out.push_back(make_snapshot(room_id, record));
        out.push_back(make_view(room_id, record));
        const Json *tr = record.find("transcript");
        long long seq = 0;
        if (tr != nullptr)
        {
            for (const Json &line : tr->items())
            {
                out.push_back(make_event(room_id, line, ++seq));
            }
        }
        // the replay beats (TJ-C5): a prop walking a fixed triangle --
        // beat b places it on (120 + (200*b) % 560, 200 + 80*(b % 3)),
        // revision = b + 1. The real beats arrive as the server's stage
        // messages; this synthetic walk makes the replay visible
        // without scene art.
        for (int b = 0; b < 3; ++b)
        {
            Json m = envelope("stage", room_id);
            Json sv = Json::object();
            sv.set("scene_id", Json::str("scene.equip"));
            sv.set("revision", Json::number(static_cast<double>(b + 1)));
            Json canvas = Json::object();
            canvas.set("w", Json::number(800));
            canvas.set("h", Json::number(600));
            sv.set("canvas", std::move(canvas));
            Json props = Json::array();
            Json prop = Json::object();
            prop.set("id", Json::str("p.marker"));
            prop.set("x",
                     Json::number(static_cast<double>(120 + (200 * b) % 560)));
            prop.set("y",
                     Json::number(static_cast<double>(200 + 80 * (b % 3))));
            prop.set("w", Json::number(60));
            prop.set("h", Json::number(40));
            prop.set("color", Json::str("#ffcc00"));
            props.push_back(std::move(prop));
            sv.set("props", std::move(props));
            Json overlays = Json::array();
            Json ring = Json::object();
            ring.set("id", Json::str("o.1"));
            ring.set("x",
                     Json::number(static_cast<double>(150 + (200 * b) % 560)));
            ring.set("y",
                     Json::number(static_cast<double>(220 + 80 * (b % 3))));
            ring.set("r", Json::number(30));
            ring.set("color", Json::str("#ffcc00"));
            overlays.push_back(std::move(ring));
            sv.set("overlays", std::move(overlays));
            m.set("stage_view", std::move(sv));
            out.push_back(std::move(m));
        }
        return out;
    }

    bool MockTianjiServer::listen(std::string *socket_path_out,
                                  std::string *error)
    {
        stop();
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
        {
            if (error != nullptr)
            {
                *error = std::string("socket: ") + std::strerror(errno);
            }
            return false;
        }
        // Linux abstract namespace: no filesystem cleanup, unique path
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        addr.sun_path[0] = '\0';
        static std::atomic<unsigned> counter{0};
        const std::string name = "tianji-mock-" +
                                 std::to_string(::getpid()) + "-" +
                                 std::to_string(counter++);
        if (name.size() >= sizeof(addr.sun_path) - 1)
        {
            ::close(fd);
            if (error != nullptr)
            {
                *error = "socket name too long";
            }
            return false;
        }
        std::strncpy(addr.sun_path + 1, name.c_str(), name.size());
        const socklen_t addrlen = static_cast<socklen_t>(
            offsetof(sockaddr_un, sun_path) + 1 + name.size());
        if (::bind(fd, reinterpret_cast<const sockaddr *>(&addr), addrlen) != 0)
        {
            const int e = errno;
            ::close(fd);
            if (error != nullptr)
            {
                *error = std::string("bind: ") + std::strerror(e);
            }
            return false;
        }
        if (::listen(fd, 1) != 0)
        {
            const int e = errno;
            ::close(fd);
            if (error != nullptr)
            {
                *error = std::string("listen: ") + std::strerror(e);
            }
            return false;
        }
        listen_fd_ = fd;
        socket_path_ = "@" + name;  // the client-side spelling: @ = abstract
        if (socket_path_out != nullptr)
        {
            *socket_path_out = socket_path_;
        }
        return true;
    }

    void MockTianjiServer::stop()
    {
        if (listen_fd_ >= 0)
        {
            // shutdown (not close): close() does not wake a blocked
            // accept on Linux, shutdown() does — serve() returns with
            // a failed accept instead of hanging; the fd itself is
            // closed by the destructor
            ::shutdown(listen_fd_, SHUT_RDWR);
        }
    }

    void MockTianjiServer::serve()
    {
        // one read of the fd: after stop() shuts it down the accept
        // fails and serve returns; the serve thread never re-reads the
        // member afterwards (the fd outlives until the destructor)
        const int listen_fd = listen_fd_;
        if (listen_fd < 0)
        {
            return;
        }
        const std::vector<Json> stream = synthesize(record_, room_id_);
        const int conn = ::accept(listen_fd, nullptr, nullptr);
        if (conn < 0)
        {
            return;  // stop() shut the listener down
        }
        // one line in, one line out — the same shape the Python server
        // speaks; the connection closes when the peer does
        std::string buf;
        char chunk[4096];
        for (;;)
        {
            const size_t nl = buf.find('\n');
            if (nl == std::string::npos)
            {
                // the client side's own cap, mirrored: a peer that
                // floods without a newline is cut, not grown
                if (buf.size() > kMaxLineBytes)
                {
                    LW << "tianji/mock: request line exceeds the size "
                          "cap; disconnecting";
                    break;
                }
                const ssize_t n = ::read(conn, chunk, sizeof(chunk));
                if (n <= 0)
                {
                    if (n < 0 && errno == EINTR)
                    {
                        continue;
                    }
                    break;  // peer gone
                }
                buf.append(chunk, static_cast<size_t>(n));
                continue;
            }
            const std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            const char *p = line.data();
            const char *end = p + line.size();
            std::string err;
            const Json req = Json::parse(p, end, &err);
            Json reply = Json::array();
            if (!err.empty() || req.type() != Json::Type::Object)
            {
                Json e = envelope("error", room_id_);
                e.set("code", Json::str("BAD_REQUEST"));
                e.set("message", Json::str(err.empty() ? "not an object" : err));
                reply.push_back(std::move(e));
            }
            else
            {
                const std::string type = req.get_str("type");
                if (type == "hello")
                {
                    reply.push_back(make_welcome(room_id_));
                }
                else if (type == "join_room")
                {
                    reply.push_back(make_snapshot(room_id_, record_));
                    reply.push_back(make_view(room_id_, record_));
                }
                else if (type == "subscribe")
                {
                    const long long after = req.get_int("after_seq");
                    long long seq = 0;
                    for (const Json &msg : stream)
                    {
                        if (msg.get_str("type") == "event")
                        {
                            ++seq;
                            if (seq > after)
                            {
                                reply.push_back(msg);
                            }
                        }
                    }
                }
                else if (type == "ping")
                {
                    reply.push_back(envelope("pong", room_id_));
                }
                else
                {
                    Json e = envelope("error", room_id_);
                    e.set("code", Json::str("UNKNOWN_TYPE"));
                    e.set("message", Json::str(type));
                    reply.push_back(std::move(e));
                }
            }
            std::string out = reply.dump();
            out.push_back('\n');
            size_t off = 0;
            while (off < out.size())
            {
                const ssize_t n = ::write(conn, out.data() + off, out.size() - off);
                if (n <= 0)
                {
                    if (n < 0 && errno == EINTR)
                    {
                        continue;
                    }
                    break;
                }
                off += static_cast<size_t>(n);
            }
        }
        ::close(conn);
    }
}  // namespace zb::tianji
