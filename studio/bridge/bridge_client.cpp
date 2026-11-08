#include "bridge_client.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "logging.hpp"

namespace zb::studio
{
    namespace
    {
        // the v1 envelope version and the line-size cap (a runaway
        // bridge is cut here; the response to project.get is the
        // biggest legit line)
        constexpr int kVersion = 1;
        constexpr size_t kMaxLineBytes = 4 << 20;  // 4 MiB

        // poll(2) slice inside request(): short enough to keep the
        // wait responsive, long enough to not spin
        constexpr int kRequestPollSliceMs = 20;

        double monotonic_s()
        {
            timespec ts{};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return static_cast<double>(ts.tv_sec) +
                   static_cast<double>(ts.tv_nsec) / 1e9;
        }
    }  // namespace

    BridgeClient::~BridgeClient()
    {
        close();
    }

    bool BridgeClient::spawn(const std::string &cwd,
                             const std::string &command,
                             std::string *error)
    {
        close();
        int in_pipe[2];   // parent -> child stdin
        int out_pipe[2];  // child stdout -> parent
        if (::pipe(in_pipe) != 0 || ::pipe(out_pipe) != 0)
        {
            if (error != nullptr)
            {
                *error = std::string("pipe: ") + std::strerror(errno);
            }
            return false;
        }
        const pid_t pid = ::fork();
        if (pid < 0)
        {
            if (error != nullptr)
            {
                *error = std::string("fork: ") + std::strerror(errno);
            }
            ::close(in_pipe[0]);
            ::close(in_pipe[1]);
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
            return false;
        }
        if (pid == 0)
        {
            // the child: wire the pipes onto stdio, chdir, exec. On any
            // failure _exit(127) — never return into the parent's frame.
            ::dup2(in_pipe[0], STDIN_FILENO);
            ::dup2(out_pipe[1], STDOUT_FILENO);
            for (const int fd : {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1]})
            {
                ::close(fd);
            }
            if (!cwd.empty() && ::chdir(cwd.c_str()) != 0)
            {
                ::_exit(126);
            }
            ::execl("/bin/sh", "sh", "-c", command.c_str(),
                    static_cast<char *>(nullptr));
            ::_exit(127);
        }
        // the parent: keep the far ends only, non-blocking reads
        ::close(in_pipe[0]);
        ::close(out_pipe[1]);
        int flags = ::fcntl(out_pipe[0], F_GETFL, 0);
        if (flags >= 0)
        {
            (void)::fcntl(out_pipe[0], F_SETFL, flags | O_NONBLOCK);
        }
        write_fd_ = in_pipe[1];
        read_fd_ = out_pipe[0];
        read_buffer_.clear();
        LW << "studio: bridge spawned (pid " << static_cast<int>(pid)
           << ", cwd " << cwd << ")";
        (void)pid;  // reaped by close()
        return true;
    }

    void BridgeClient::attach(int read_fd, int write_fd)
    {
        close();
        int flags = ::fcntl(read_fd, F_GETFL, 0);
        if (flags >= 0)
        {
            (void)::fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);
        }
        read_fd_ = read_fd;
        write_fd_ = write_fd;
        read_buffer_.clear();
    }

    void BridgeClient::close()
    {
        if (write_fd_ >= 0)
        {
            ::close(write_fd_);  // EOF = the bridge's graceful exit
            write_fd_ = -1;
        }
        if (read_fd_ >= 0)
        {
            ::close(read_fd_);
            read_fd_ = -1;
        }
        read_buffer_.clear();
        pending_ = false;
        pending_id_.clear();
        pending_error_.clear();
        events_.clear();
        // reap the child without blocking: it exits on the EOF above;
        // if it has not made it out yet, it becomes the reaper's
        // business (init) — the app is closing anyway
        while (::waitpid(-1, nullptr, WNOHANG) > 0)
        {
        }
    }

    void BridgeClient::poll()
    {
        if (read_fd_ < 0)
        {
            return;
        }
        char chunk[4096];
        for (;;)
        {
            const ssize_t n = ::read(read_fd_, chunk, sizeof(chunk));
            if (n > 0)
            {
                read_buffer_.append(chunk, static_cast<size_t>(n));
                // split complete lines as the buffer grows
                size_t nl = read_buffer_.find('\n');
                while (nl != std::string::npos)
                {
                    dispatch_line(read_buffer_.substr(0, nl));
                    read_buffer_.erase(0, nl + 1);
                    nl = read_buffer_.find('\n');
                }
                if (read_buffer_.size() > kMaxLineBytes)
                {
                    LW << "studio: bridge line exceeds the size cap; "
                          "dropping the buffer";
                    read_buffer_.clear();
                }
                continue;
            }
            if (n == 0)
            {
                LW << "studio: bridge closed its stdout (EOF)";
                ::close(read_fd_);
                read_fd_ = -1;
                return;
            }
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return;  // drained
            }
            LW << "studio: bridge read failed: " << std::strerror(errno);
            ::close(read_fd_);
            read_fd_ = -1;
            return;
        }
    }

    void BridgeClient::dispatch_line(const std::string &line)
    {
        if (line.empty())
        {
            return;
        }
        const char *p = line.data();
        const char *end = p + line.size();
        std::string parse_error;
        const zb::tianji::Json msg = zb::tianji::Json::parse(p, end, &parse_error);
        if (!parse_error.empty() || msg.type() != zb::tianji::Json::Type::Object)
        {
            LW << "studio: undecodable bridge line (" << parse_error << ")";
            return;
        }
        const std::string id = msg.get_str("id");
        if (pending_ && !id.empty() && id == pending_id_)
        {
            // the id-correlated response this request() is waiting for
            pending_resolved_ = true;
            pending_error_ = msg.get_str("error");
            if (pending_error_.empty() && !msg.get_bool("ok", false))
            {
                const zb::tianji::Json *err = msg.find("error");
                pending_error_ = err != nullptr
                                     ? err->dump()
                                     : std::string("ok=false without an error");
            }
            pending_data_ = msg.find("data") != nullptr ? *msg.find("data")
                                                        : zb::tianji::Json();
            return;
        }
        // anything else is an unsolicited event (docs/09 §9.1: no id)
        events_.push_back(msg);
    }

    bool BridgeClient::request(const std::string &type,
                               const zb::tianji::Json &fields,
                               int timeout_ms, zb::tianji::Json *data_out,
                               std::string *error)
    {
        if (data_out != nullptr)
        {
            *data_out = zb::tianji::Json();
        }
        if (error != nullptr)
        {
            error->clear();
        }
        if (write_fd_ < 0)
        {
            if (error != nullptr)
            {
                *error = "bridge not running";
            }
            return false;
        }
        if (pending_)
        {
            if (error != nullptr)
            {
                *error = "a request is already in flight";
            }
            return false;
        }
        pending_ = true;
        pending_id_ = "r-" + std::to_string(next_id_++);
        pending_resolved_ = false;
        pending_error_.clear();
        pending_data_ = zb::tianji::Json();

        zb::tianji::Json req = zb::tianji::Json::object();
        req.set("v", zb::tianji::Json::number(kVersion));
        req.set("id", zb::tianji::Json::str(pending_id_));
        req.set("type", zb::tianji::Json::str(type));
        for (const auto &m : fields.members())
        {
            req.set(m.first, m.second);
        }
        std::string write_error;
        if (!write_line(req.dump(), &write_error))
        {
            pending_ = false;
            if (error != nullptr)
            {
                *error = write_error;
            }
            return false;
        }

        const double deadline = monotonic_s() +
                                static_cast<double>(timeout_ms) / 1e3;
        for (;;)
        {
            if (pending_resolved_)
            {
                pending_ = false;
                if (!pending_error_.empty())
                {
                    if (error != nullptr)
                    {
                        *error = pending_error_;
                    }
                    return false;
                }
                if (data_out != nullptr)
                {
                    *data_out = pending_data_;
                }
                return true;
            }
            if (read_fd_ < 0)
            {
                pending_ = false;
                if (error != nullptr)
                {
                    *error = "bridge died before answering";
                }
                return false;
            }
            if (monotonic_s() >= deadline)
            {
                pending_ = false;
                if (error != nullptr)
                {
                    *error = "timeout";
                }
                return false;
            }
            pollfd pfd{};
            pfd.fd = read_fd_;
            pfd.events = POLLIN;
            const int ready = ::poll(&pfd, 1, kRequestPollSliceMs);
            if (ready > 0)
            {
                poll();  // drains and may resolve the pending request
            }
        }
    }

    bool BridgeClient::next_event(zb::tianji::Json *event_out)
    {
        if (events_.empty())
        {
            return false;
        }
        if (event_out != nullptr)
        {
            *event_out = events_.front();
        }
        events_.pop_front();
        return true;
    }

    bool BridgeClient::write_line(const std::string &line, std::string *error)
    {
        std::string out = line;
        out.push_back('\n');
        size_t off = 0;
        while (off < out.size())
        {
            const ssize_t n = ::write(write_fd_, out.data() + off,
                                      out.size() - off);
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
                return false;
            }
            off += static_cast<size_t>(n);
        }
        return true;
    }
}  // namespace zb::studio
