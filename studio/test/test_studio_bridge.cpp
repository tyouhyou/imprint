// studio bridge suites (P1-10): the bridge client's wire protocol —
// id-correlated request/response, unsolicited event queueing, spawn
// round-trip against the stdlib-only mock bridge. POSIX-only (pipes);
// the CMake gate keeps it out of embedded/win builds.
#include "test.hpp"

#include <cstring>
#include <time.h>
#include <unistd.h>

#include "bridge/bridge_client.hpp"

using namespace zb::studio;
using zb::tianji::Json;

namespace
{
    // a pipe pair with a non-blocking read end (the client re-marks it,
    // but start clean)
    struct PipePair
    {
        int r = -1;
        int w = -1;

        bool open()
        {
            int fds[2];
            if (::pipe(fds) != 0)
            {
                return false;
            }
            r = fds[0];
            w = fds[1];
            return true;
        }
    };

    // the test seam: client reads response_pipe.r, writes
    // request_pipe.w; the "fake bridge" is the test's own hands on
    // response_pipe.w / request_pipe.r
    struct FakeBridge
    {
        PipePair to_client;  // bridge writes responses/events here
        PipePair to_bridge;  // the client's request line lands here

        bool open()
        {
            return to_client.open() && to_bridge.open();
        }

        // the canned line a real bridge would emit (one write = one
        // line; PIPE_BUF keeps it atomic and the client reads eagerly)
        bool feed(const char *line)
        {
            const size_t n = std::strlen(line);
            return ::write(to_client.w, line, n) == static_cast<ssize_t>(n);
        }

        std::string drain_request()
        {
            char buf[4096] = {};
            const ssize_t n = ::read(to_bridge.r, buf, sizeof(buf) - 1);
            if (n <= 0)
            {
                return {};
            }
            std::string s(buf, static_cast<size_t>(n));
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
            {
                s.pop_back();
            }
            return s;
        }

        void close_all()
        {
            for (const int fd : {to_client.r, to_client.w, to_bridge.r, to_bridge.w})
            {
                if (fd >= 0)
                {
                    ::close(fd);
                }
            }
        }
    };
}  // namespace

int test_studio_json()
{
    // the tj_json reuse: parse + typed access + dump round-trip
    const char *doc = "{\"v\":1,\"id\":\"r-1\",\"ok\":true,"
                      "\"data\":{\"fp\":\"a|1|\",\"history\":[],"
                      "\"text\":\"中文\"}}";
    const char *p = doc;
    const Json v = Json::parse(p, p + std::strlen(doc));
    EXPECT(v.type() == Json::Type::Object);
    EXPECT(v.get_bool("ok", false) == true);
    const Json *data = v.find("data");
    EXPECT(data != nullptr && data->get_str("text") == "中文");
    EXPECT(data->find("history") != nullptr &&
           data->find("history")->items().empty());
    const std::string dumped = v.dump();
    const char *q = dumped.c_str();
    const Json again = Json::parse(q, q + dumped.size());
    EXPECT(again.get_str("id") == "r-1");
    return test::report("studio_json");
}

int test_studio_bridge_protocol()
{
    // request/response correlation: the response is pre-fed with the
    // deterministic id (r-1), the request line lands on the fake bridge
    {
        FakeBridge fb;
        EXPECT(fb.open());
        EXPECT(fb.feed("{\"v\":1,\"id\":\"r-1\",\"ok\":true,"
                       "\"data\":{\"pong\":true}}\n"));
        BridgeClient c;
        c.attach(fb.to_client.r, fb.to_bridge.w);
        Json data;
        std::string err;
        EXPECT(c.request("bridge.ping", Json::object(), 500, &data, &err));
        EXPECT(err.empty() && data.get_bool("pong", false));
        const std::string req = fb.drain_request();
        EXPECT(req.find("\"type\":\"bridge.ping\"") != std::string::npos);
        EXPECT(req.find("\"id\":\"r-1\"") != std::string::npos);
        EXPECT(req.find("\"v\":1") != std::string::npos);
        c.close();
        fb.close_all();
    }
    // the request carries the fields (fp for project.get)
    {
        FakeBridge fb;
        EXPECT(fb.open());
        EXPECT(fb.feed("{\"v\":1,\"id\":\"r-1\",\"ok\":true,"
                       "\"data\":{\"unchanged\":true}}\n"));
        BridgeClient c;
        c.attach(fb.to_client.r, fb.to_bridge.w);
        Json fields = Json::object();
        fields.set("fp", Json::str("mock-1"));
        Json data;
        EXPECT(c.request("project.get", fields, 500, &data));
        const std::string req = fb.drain_request();
        EXPECT(req.find("\"fp\":\"mock-1\"") != std::string::npos);
        EXPECT(data.get_bool("unchanged", false));
        c.close();
        fb.close_all();
    }
    // error responses: ok=false surfaces the error payload, data empty
    {
        FakeBridge fb;
        EXPECT(fb.open());
        EXPECT(fb.feed("{\"v\":1,\"id\":\"r-1\",\"ok\":false,"
                       "\"error\":{\"code\":\"unknown_op\","
                       "\"message\":\"未知操作：nope\"}}\n"));
        BridgeClient c;
        c.attach(fb.to_client.r, fb.to_bridge.w);
        Json data;
        std::string err;
        EXPECT(!c.request("nope", Json::object(), 500, &data, &err));
        EXPECT(err.find("unknown_op") != std::string::npos);
        EXPECT(err.find("未知操作") != std::string::npos);
        c.close();
        fb.close_all();
    }
    // unsolicited events queue (and do not resolve the pending
    // request); next_event pops FIFO
    {
        FakeBridge fb;
        EXPECT(fb.open());
        EXPECT(fb.feed("{\"v\":1,\"type\":\"turn.finished\","
                       "\"data\":{\"ok\":true,\"lines\":[\"a\"]}}\n"));
        EXPECT(fb.feed("{\"v\":1,\"id\":\"r-1\",\"ok\":true,\"data\":{}}\n"));
        BridgeClient c;
        c.attach(fb.to_client.r, fb.to_bridge.w);
        Json data;
        EXPECT(c.request("console.input", Json::object(), 500, &data));
        EXPECT(c.pending_events() == 1);
        Json event;
        EXPECT(c.next_event(&event));
        EXPECT(event.get_str("type") == "turn.finished");
        const Json *d = event.find("data");
        EXPECT(d != nullptr && d->get_bool("ok", false));
        EXPECT(!c.next_event(&event));  // drained
        c.close();
        fb.close_all();
    }
    // timeout: no response -> false + "timeout"; the client takes
    // another request afterwards (pending cleared)
    {
        FakeBridge fb;
        EXPECT(fb.open());
        BridgeClient c;
        c.attach(fb.to_client.r, fb.to_bridge.w);
        Json data;
        std::string err;
        EXPECT(!c.request("bridge.ping", Json::object(), 120, &data, &err));
        EXPECT(err == "timeout");
        EXPECT(fb.feed("{\"v\":1,\"id\":\"r-2\",\"ok\":true,\"data\":{}}\n"));
        EXPECT(c.request("bridge.ping", Json::object(), 500, &data, &err));
        c.close();
        fb.close_all();
    }
    // bridge death before answering: EOF on the read end
    {
        FakeBridge fb;
        EXPECT(fb.open());
        BridgeClient c;
        c.attach(fb.to_client.r, fb.to_bridge.w);
        ::close(fb.to_client.w);  // the bridge dies
        fb.to_client.w = -1;
        Json data;
        std::string err;
        EXPECT(!c.request("bridge.ping", Json::object(), 1000, &data, &err));
        EXPECT(err == "bridge died before answering");
        EXPECT(!c.alive());
        c.close();
        fb.close_all();
    }
    // one request at a time is enforced in request() (a pending flag);
    // the app is single-threaded so re-entry cannot happen from step().
    // What the protocol test can pin: a timed-out request leaves the
    // client usable (pending cleared) — covered in the timeout block.
    return test::report("studio_bridge_protocol");
}

int test_studio_bridge_spawn()
{
    // the real thing: spawn the stdlib-only mock bridge (python3) and
    // run a hello / project.get / turn event round-trip over the pipes
    if (::system("command -v python3 > /dev/null 2>&1") != 0)
    {
        std::printf("      (python3 not present; spawn suite skipped)\n");
        return test::report("studio_bridge_spawn");
    }
    BridgeClient c;
    std::string err;
    // cwd is irrelevant to the mock; STUDIO_MOCK_BRIDGE is the source
    // path the CMake target hands in
    EXPECT(c.spawn("/tmp", std::string("python3 ") + STUDIO_MOCK_BRIDGE,
                   &err));
    EXPECT(c.alive());

    Json data;
    EXPECT(c.request("bridge.hello", Json::object(), 5000, &data, &err));
    EXPECT(data.get_str("bridge") == "mock-bridge");
    EXPECT(data.get_str("seat_ai") == "ai.writer");

    EXPECT(c.request("project.get", Json::object(), 5000, &data, &err));
    EXPECT(data.get_str("fp") == "mock-1");
    EXPECT(data.get_str("status").find("假桥") != std::string::npos);

    // a speak turn: turn_started -> the unsolicited turn.finished
    Json fields = Json::object();
    fields.set("text", Json::str("你好"));
    EXPECT(c.request("console.input", fields, 5000, &data, &err));
    EXPECT(data.get_bool("turn_started", false));
    bool finished = false;
    const double deadline = []()
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<double>(ts.tv_sec);
    }() + 5.0;
    while (!finished)
    {
        c.poll();
        Json event;
        while (c.next_event(&event))
        {
            finished = event.get_str("type") == "turn.finished";
        }
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const double now = static_cast<double>(ts.tv_sec);
        if (finished || now > deadline)
        {
            break;
        }
        usleep(20000);
    }
    EXPECT(finished);

    fields = Json::object();
    fields.set("text", Json::str("/quit"));
    EXPECT(c.request("console.input", fields, 5000, &data, &err));
    EXPECT(data.get_bool("quit", false));
    c.close();
    return test::report("studio_bridge_spawn");
}

// the studio binary's own dispatch (the same filter convention as
// test_tianji: substring match, no filter = all)
int main(const int argc, char **argv)
{
    struct Suite
    {
        const char *name;
        int (*fn)();
    };
    const Suite suites[] = {
        {"studio_json", test_studio_json},
        {"studio_bridge_protocol", test_studio_bridge_protocol},
        {"studio_bridge_spawn", test_studio_bridge_spawn},
    };
    const char *filters[16] = {};
    int filter_count = 0;
    bool list_only = false;
    for (int i = 1; i < argc && filter_count < 16; ++i)
    {
        if (std::strcmp(argv[i], "--list") == 0)
        {
            list_only = true;
        }
        else
        {
            filters[filter_count++] = argv[i];
        }
    }
    int failures = 0;
    for (const Suite &s : suites)
    {
        bool selected = filter_count == 0;
        for (int i = 0; i < filter_count && !selected; ++i)
        {
            if (std::strstr(s.name, filters[i]) != nullptr)
            {
                selected = true;
            }
        }
        if (!selected)
        {
            continue;
        }
        std::printf("[%s]\n", s.name);
        if (!list_only)
        {
            failures += s.fn();
        }
    }
    if (list_only)
    {
        return 0;
    }
    if (failures != 0)
    {
        std::printf("TOTAL: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
