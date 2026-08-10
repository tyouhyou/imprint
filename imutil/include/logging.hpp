#pragma once

#include <functional>
#include <mutex>
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

#define LD (zb::Logging::Stream(zb::Logging_Level::debug) << zb::Logging::Stream::get_cur_datetime() << "[" << __FILENAME_4_LOGGING__ << "(" << __LINE__ << ")" << "::" << __func__ << "]")
#define LI (zb::Logging::Stream(zb::Logging_Level::info) << zb::Logging::Stream::get_cur_datetime() << "[" << __FILENAME_4_LOGGING__ << "(" << __LINE__ << ")" << "::" << __func__ << "]")
#define LW (zb::Logging::Stream(zb::Logging_Level::warn) << zb::Logging::Stream::get_cur_datetime() << "[" << __FILENAME_4_LOGGING__ << "(" << __LINE__ << ")" << "::" << __func__ << "]")
#define LE (zb::Logging::Stream(zb::Logging_Level::error) << zb::Logging::Stream::get_cur_datetime() << "[" << __FILENAME_4_LOGGING__ << "(" << __LINE__ << ")" << "::" << __func__ << "]")
#define LF (zb::Logging::Stream(zb::Logging_Level::fatal) << zb::Logging::Stream::get_cur_datetime() << "[" << __FILENAME_4_LOGGING__ << "(" << __LINE__ << ")" << "::" << __func__ << "]")

    class Logging
    {
    public:
        class Stream
        {
        public:
            explicit Stream(const Logging_Level level) : level(level)
            {
            }
            ~Stream()
            {
                ss << std::endl;
                Logging::log(level, ss.str());
            }

            Stream(const Stream &) = delete;
            Stream &operator=(const Stream &) = delete;
            Stream(Stream &&) = delete;
            Stream &operator=(Stream &&) = delete;

            template <typename T>
            Stream &operator<<(const T &value)
            {
                ss << value;
                return *this;
            }

            Stream &operator<<(std::ostream &(*manip)(std::ostream &))
            {
                ss << manip;
                return *this;
            }

        private:
            std::stringstream ss;
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
    };
}
