#include "feed_client.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "logging.hpp"

namespace zb::tianji
{
    namespace
    {
        // the v1 envelope version and the message-size cap (a line is
        // one message batch; a runaway peer is cut here)
        constexpr int kVersion = 1;
        constexpr size_t kMaxLineBytes = 1 << 20;  // 1 MiB
    }  // namespace

    FeedClient::~FeedClient()
    {
        close();
    }

    bool FeedClient::connect(const char *socket_path, std::string *error)
    {
        close();
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
        {
            if (error != nullptr)
            {
                *error = std::string("socket: ") + std::strerror(errno);
            }
            return false;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        // "@" names the Linux abstract namespace (no filesystem path to
        // clean up — the mock server binds there; the RoomServer's real
        // sockets are plain filesystem paths)
        const bool abstract_ns = socket_path[0] == '@';
        const size_t name_len =
            abstract_ns ? std::strlen(socket_path + 1) : std::strlen(socket_path);
        if (name_len >= sizeof(addr.sun_path) - (abstract_ns ? 1u : 0u))
        {
            ::close(fd);
            if (error != nullptr)
            {
                *error = "socket path too long";
            }
            return false;
        }
        socklen_t addrlen = 0;
        if (abstract_ns)
        {
            std::strncpy(addr.sun_path + 1, socket_path + 1,
                         sizeof(addr.sun_path) - 2);
            addrlen = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                             1 + name_len);
        }
        else
        {
            std::strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
            addrlen = static_cast<socklen_t>(sizeof(addr));
        }
        if (::connect(fd, reinterpret_cast<const sockaddr *>(&addr),
                      addrlen) != 0)
        {
            const int e = errno;
            ::close(fd);
            if (error != nullptr)
            {
                *error = std::string("connect: ") + std::strerror(e);
            }
            return false;
        }
        fd_ = fd;
        read_buffer_.clear();
        return true;
    }

    void FeedClient::close()
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
        read_buffer_.clear();
    }

    bool FeedClient::write_line(const std::string &line, std::string *error)
    {
        std::string out = line;
        out.push_back('\n');
        size_t off = 0;
        while (off < out.size())
        {
            const ssize_t n = ::write(fd_, out.data() + off, out.size() - off);
            if (n <= 0)
            {
                if (n < 0 && errno == EINTR)
                {
                    continue;
                }
                if (error != nullptr)
                {
                    *error = std::string("write: ") + std::strerror(errno);
                }
                close();
                return false;
            }
            off += static_cast<size_t>(n);
        }
        return true;
    }

    bool FeedClient::read_line(std::string &line, std::string *error)
    {
        line.clear();
        for (;;)
        {
            const size_t nl = read_buffer_.find('\n');
            if (nl != std::string::npos)
            {
                line.assign(read_buffer_, 0, nl);
                read_buffer_.erase(0, nl + 1);
                return true;
            }
            if (read_buffer_.size() > kMaxLineBytes)
            {
                if (error != nullptr)
                {
                    *error = "response line exceeds the size cap";
                }
                close();
                return false;
            }
            char chunk[4096];
            const ssize_t n = ::read(fd_, chunk, sizeof(chunk));
            if (n < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                if (error != nullptr)
                {
                    *error = std::string("read: ") + std::strerror(errno);
                }
                close();
                return false;
            }
            if (n == 0)
            {
                if (error != nullptr)
                {
                    *error = "server closed the connection";
                }
                close();
                return false;
            }
            read_buffer_.append(chunk, static_cast<size_t>(n));
        }
    }

    std::vector<Json> FeedClient::request(const std::string &type, Json fields,
                                          std::string *error)
    {
        if (fd_ < 0)
        {
            if (error != nullptr)
            {
                *error = "not connected";
            }
            return {};
        }
        if (fields.type() != Json::Type::Object)
        {
            fields = Json::object();
        }
        // the v1 envelope (docs/07 §4): v, id, type, then the fields
        Json req = Json::object();
        req.set("v", Json::number(kVersion));
        req.set("id", Json::str("c-" + std::to_string(next_id_++)));
        req.set("type", Json::str(type));
        for (const auto &m : fields.members())
        {
            req.set(m.first, m.second);
        }
        if (!write_line(req.dump(), error))
        {
            return {};
        }
        std::string line;
        if (!read_line(line, error))
        {
            return {};
        }
        const char *p = line.data();
        const char *end = p + line.size();
        std::string parse_error;
        Json response = Json::parse(p, end, &parse_error);
        if (!parse_error.empty())
        {
            if (error != nullptr)
            {
                *error = "bad response line: " + parse_error;
            }
            return {};
        }
        if (response.type() != Json::Type::Array)
        {
            // a bare object response is tolerated as a one-element list
            // (tolerance beats a dropped message; the documented shape
            // is the array)
            if (response.type() == Json::Type::Object)
            {
                Json one = Json::array();
                one.push_back(response);
                response = std::move(one);
            }
            else
            {
                if (error != nullptr)
                {
                    *error = "response is not a message array";
                }
                return {};
            }
        }
        for (const Json &msg : response.items())
        {
            if (msg.type() == Json::Type::Object &&
                msg.get_str("type") == "event")
            {
                const Json *ev = msg.find("event");
                if (ev != nullptr)
                {
                    last_seq_ = std::max(last_seq_, ev->get_int("seq"));
                }
            }
        }
        return response.items();
    }
}  // namespace zb::tianji
