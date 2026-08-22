#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <chrono>
#include <iomanip>
#include <cstring>
#include <sstream>

namespace zb
{
    enum class Logging_Level
    {
        debug,
        info,
        warn,
        error,
        fatal
    };

#if defined(_MSC_VER)
#define __PATH_SEP_STR_4_LOGGING__ "\\"
#define __PATH_SEP_CHR_4_LOGGING__ '\\'
#else
#define __PATH_SEP_STR_4_LOGGING__ "/"
#define __PATH_SEP_CHR_4_LOGGING__ '/'
#endif
#define __FILENAME_4_LOGGING__ strrchr(__PATH_SEP_STR_4_LOGGING__ __FILE__, __PATH_SEP_CHR_4_LOGGING__) + 1

/*
 * The macro is a stream expression: LD << "msg". The level check lives
 * in the Stream constructor -- a suppressed level never builds the
 * stringstream, so a hot path that logs LD allocates nothing once the
 * minimum level is raised (see Logging::set_min_level).
 */
#define LD (zb::Logging::Stream(zb::Logging_Level::debug, __FILENAME_4_LOGGING__, __LINE__, __func__))
#define LI (zb::Logging::Stream(zb::Logging_Level::info, __FILENAME_4_LOGGING__, __LINE__, __func__))
#define LW (zb::Logging::Stream(zb::Logging_Level::warn, __FILENAME_4_LOGGING__, __LINE__, __func__))
#define LE (zb::Logging::Stream(zb::Logging_Level::error, __FILENAME_4_LOGGING__, __LINE__, __func__))
#define LF (zb::Logging::Stream(zb::Logging_Level::fatal, __FILENAME_4_LOGGING__, __LINE__, __func__))

    class Logging
    {
    public:
        class Stream
        {
        public:
            Stream(const Logging_Level level, const char *file, const int line, const char *func)
                : level(level)
            {
                if (!suppressed(level))
                {
                    ss.emplace() << get_cur_datetime() << "[" << file << "(" << line << ")" << "::" << func << "]";
                }
            }
            ~Stream()
            {
                if (ss)
                {
                    ss->put('\n');
                    Logging::log(level, ss->str());
                }
            }

            Stream(const Stream &) = delete;
            Stream &operator=(const Stream &) = delete;
            Stream(Stream &&) = delete;
            Stream &operator=(Stream &&) = delete;

            template <typename T>
            Stream &operator<<(const T &value)
            {
                if (ss)
                {
                    *ss << value;
                }
                return *this;
            }

            Stream &operator<<(std::ostream &(*manip)(std::ostream &))
            {
                if (ss)
                {
                    *ss << manip;
                }
                return *this;
            }

        private:
            // empty when the level is suppressed: no allocation happens
            std::optional<std::stringstream> ss;
            Logging_Level level;
            Stream() = default;

        public:
            static std::string get_cur_datetime()
            {
                const auto now = std::chrono::system_clock::now();
                auto dat = std::chrono::system_clock::to_time_t(now);
                std::tm buf{};
#if defined(_MSC_VER)
                localtime_s(&buf, &dat);
#else
                localtime_r(&dat, &buf);
#endif
                const auto ms = (std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()) % 1000;
                std::stringstream ss;
                ss << std::put_time(&buf, "[%Y-%m-%d, %H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms << "]";
                return ss.str();
            }
        };

    public:
        Logging() = delete;
        Logging(const Logging &) = delete;
        Logging &operator=(const Logging &) = delete;
        Logging(Logging &&) = delete;
        Logging &operator=(Logging &&) = delete;

        using log_handler_t = std::function<void(const Logging_Level &level, const std::string &message)>;

        /*
         * Runtime verbosity: messages below the minimum level are dropped
         * without constructing anything (a hot path that logs LD becomes
         * allocation-free once the level is at info or above). The
         * default is debug -- everything logs -- matching the historical
         * behavior; the NDS debug workflow depends on it. The flag is a
         * plain read (the UI contract is single-threaded; see the mutex
         * note below).
         */
        static void set_min_level(const Logging_Level level) { s_min_level = level; }
        static bool suppressed(const Logging_Level level)
        {
            return static_cast<int>(level) < static_cast<int>(s_min_level);
        }

        /*
         * Installs the message handler. log() is safe to call from any
         * thread: it copies the handler under the mutex and invokes it
         * outside the lock (a handler that logs again cannot deadlock).
         */
        static void set_log_handle(const log_handler_t &func = log_handler_t())
        {
            std::lock_guard<std::mutex> lock(s_log_mutex);
            s_log_handler = func;
        }

        static void log(const Logging_Level &level, const std::string &message)
        {
            log_handler_t handler;
            {
                std::lock_guard<std::mutex> lock(s_log_mutex);
                handler = s_log_handler;
            }
            if (handler)
            {
                handler(level, message);
            }
        }

        static void debug(const std::string &message)
        {
            log(Logging_Level::debug, message);
        }

        static void info(const std::string &message)
        {
            log(Logging_Level::info, message);
        }

        static void warn(const std::string &message)
        {
            log(Logging_Level::warn, message);
        }

        static void error(const std::string &message)
        {
            log(Logging_Level::error, message);
        }

        static void fatal(const std::string &message)
        {
            log(Logging_Level::fatal, message);
        }

    private:
        // plain storage behind a mutex: std::atomic would pull in libatomic,
        // which the embedded (NDS) toolchain does not ship. On NDS the mutex
        // is a single-core no-op, which is all that platform needs.
        inline static std::mutex s_log_mutex;
        inline static log_handler_t s_log_handler;
        inline static Logging_Level s_min_level = Logging_Level::debug;
    };
}
