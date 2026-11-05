// tianji feed suites (TJ-C2): the v1 JSON subset parser, the feed
// client against the mock server over a real Unix socket, and the
// view builder's message -> stream-row semantics. POSIX-only (the
// socket path); the CMake gate keeps it out of embedded/win builds.
#include "test.hpp"

#include <cstring>
#include <thread>

#include "feed/feed_client.hpp"
#include "feed/json.hpp"
#include "feed/mock_server.hpp"
#include "feed/view_builder.hpp"

#include "scene_replay.hpp"

using namespace zb::tianji;

namespace
{
    // a session record shaped exactly like tianji's
    // samples/run_子夜客栈.json (the feeder's replay input)
    Json sample_record()
    {
        const char *kRecord =
            "{"
            "\"session_id\":\"sess_test\","
            "\"end_mode\":\"fail\","
            "\"transcript\":["
            "{\"tick\":0,\"seat\":\"seat.shen\",\"name\":\"沈三娘\","
            "\"text\":\"关于行踪，我知道的是：林九的客房在东厢\",\"source\":\"ai\"},"
            "{\"tick\":0,\"seat\":\"seat.luo\",\"name\":\"阿萝\","
            "\"text\":\"林九，你说昨夜见过人？\",\"source\":\"ai\"},"
            "{\"tick\":1,\"seat\":\"seat.shen\",\"name\":\"沈三娘\","
            "\"text\":\"我有不在场证明\",\"source\":\"ai\"}"
            "],"
            "\"beats\":[{\"tick\":0,\"summary\":\"speak×2\"}],"
            "\"claims\":[]"
            "}";
        const char *p = kRecord;
        return Json::parse(p, p + std::strlen(kRecord));
    }
}  // namespace

int test_tianji_json()
{
    // scalars, containers, round-trip
    {
        const char *doc = "{\"a\":1,\"b\":[true,null,\"x\\n\\u4F60\"],"
                          "\"c\":-2.5}";
        const char *p = doc;
        const Json v = Json::parse(p, p + std::strlen(doc));
        EXPECT(v.type() == Json::Type::Object);
        EXPECT(v.get_int("a") == 1);
        EXPECT(v.get_int("c") == -3);  // half-away-from-zero rounding
        const Json *b = v.find("b");
        EXPECT(b != nullptr && b->items().size() == 3);
        EXPECT(b->items()[0].as_bool(true) == true);
        EXPECT(b->items()[1].is_null());
        EXPECT(b->items()[2].as_str() == "x\n\xE4\xBD\xA0");
    }
    // \uXXXX surrogate pairs combine; lone halves become U+FFFD
    {
        const char *doc = "\"\\uD83D\\uDE00 \\uD800 \\uDE00 \\u4F60\"";
        const char *p = doc;
        const Json v = Json::parse(p, p + std::strlen(doc));
        EXPECT(v.as_str() ==
               "\xF0\x9F\x98\x80 \xEF\xBF\xBD \xEF\xBF\xBD \xE4\xBD\xA0");
    }
    // hardening: depth cap, trailing garbage, raw control byte,
    // unterminated string — all fail with an error, never throw
    {
        std::string deep;
        for (int i = 0; i < Json::kMaxDepth + 2; ++i)
        {
            deep += '[';
        }
        deep += "1";
        for (int i = 0; i < Json::kMaxDepth + 2; ++i)
        {
            deep += ']';
        }
        const char *p = deep.data();
        std::string err;
        Json::parse(p, p + deep.size(), &err);
        EXPECT(!err.empty());

        const char *trailing = "{\"a\":1} junk";
        p = trailing;
        const Json v = Json::parse(p, p + std::strlen(trailing));
        EXPECT(v.type() == Json::Type::Object);
        EXPECT(p - trailing == 7);  // the parser stops at the value's end

        const char *rawctl = "\"a\x01"
                             "b\"";
        p = rawctl;
        Json::parse(p, p + 6, &err);
        EXPECT(!err.empty());

        const char *unterm = "\"abc";
        p = unterm;
        Json::parse(p, p + 4, &err);
        EXPECT(!err.empty());
    }
    // dump: compact, strings re-escaped, integral numbers without noise
    {
        Json o = Json::object();
        o.set("t", Json::str("a\"b\nc"));
        o.set("n", Json::number(42));
        o.set("neg", Json::number(-2.5));
        Json arr = Json::array();
        arr.push_back(Json::boolean(false));
        o.set("l", std::move(arr));
        EXPECT(o.dump() ==
               "{\"t\":\"a\\\"b\\nc\",\"n\":42,\"neg\":-2.500000,"
               "\"l\":[false]}");
    }
    return test::report("tianji_json");
}

int test_tianji_feed()
{
    // end to end over a real (abstract) Unix socket: the mock server
    // replays the sample record, the client speaks the v1 envelope
    MockTianjiServer server;
    server.set_record(sample_record(), "room_mock");
    std::string path;
    std::string err;
    EXPECT(server.listen(&path, &err));
    std::thread serve_thread([&server]
                             { server.serve(); });

    FeedClient client;
    EXPECT(client.connect(path.c_str(), &err));

    // hello -> [welcome]
    auto msgs = client.request("hello", Json::object(), &err);
    EXPECT(msgs.size() == 1);
    EXPECT(!msgs.empty() && msgs[0].get_str("type") == "welcome");

    // join_room -> [room_snapshot, view]
    Json join = Json::object();
    join.set("room_id", Json::str("room_mock"));
    join.set("role", Json::str("actor"));
    msgs = client.request("join_room", std::move(join), &err);
    EXPECT(msgs.size() == 2);
    EXPECT(msgs[0].get_str("type") == "room_snapshot");
    EXPECT(msgs[0].find("seats") != nullptr &&
           msgs[0].find("seats")->items().size() == 2);  // 沈三娘 + 阿萝
    EXPECT(msgs[1].get_str("type") == "view");

    // subscribe -> the whole event stream (3 lines), seq 1..3
    Json sub = Json::object();
    sub.set("after_seq", Json::number(0));
    msgs = client.request("subscribe", std::move(sub), &err);
    EXPECT(msgs.size() == 3);
    EXPECT(msgs[0].get_str("type") == "event");
    EXPECT(msgs[0].find("event")->get_int("seq") == 1);
    EXPECT(client.last_seq() == 3);

    // resume after_seq=2: exactly the remaining event, no duplicates
    Json resume = Json::object();
    resume.set("after_seq", Json::number(2));
    msgs = client.request("subscribe", std::move(resume), &err);
    EXPECT(msgs.size() == 1);
    EXPECT(msgs[0].find("event")->get_int("seq") == 3);

    // unknown type -> an error message object, not a crash
    msgs = client.request("frobnicate", Json::object(), &err);
    EXPECT(msgs.size() == 1);
    EXPECT(msgs[0].get_str("type") == "error");

    client.close();
    serve_thread.join();
    server.stop();
    return test::report("tianji_feed");
}

int test_tianji_view()
{
    // the docs/07 §5.2 first-connect flow through the view builder:
    // snapshot + view seed the stream AND the cursor; the subscribe
    // rides that cursor, so the history never lands twice
    TianjiView view;
    auto msgs = MockTianjiServer::synthesize(sample_record(), "room_mock");
    // the first-connect shape: welcome, snapshot, view (the subscribe
    // tail with after_seq = snapshot.last_seq is empty — verified over
    // the wire in test_tianji_feed)
    int applied = 0;
    for (int i = 0; i < 3; ++i)
    {
        if (apply_message(view, msgs[static_cast<size_t>(i)]))
        {
            ++applied;
        }
    }
    EXPECT(applied == 2);           // snapshot (cursor) + view (seed); welcome no
    EXPECT(view.rows.size() == 3);  // the recent seed, in order
    EXPECT(view.rows[0].kind == RowKind::speech);
    EXPECT(view.rows[0].name == "沈三娘");
    EXPECT(view.last_seq == 3);  // from the snapshot, not from events

    // the wire-level resume check: subscribe(after_seq=3) replays
    // nothing (the mock's event seqs are 1..3, all <= the cursor).
    // The stream is welcome, snapshot, view, 3 events, then the 3
    // TJ-C5 stage beats -- only the event slice carries the cursor
    EXPECT(msgs.size() == 9);
    for (size_t i = 3; i < 6; ++i)
    {
        const zb::tianji::Json *ev = msgs[i].find("event");
        EXPECT(ev != nullptr);
        if (ev != nullptr)
        {
            EXPECT(ev->get_int("seq") <= view.last_seq);
        }
    }

    // a NEW event (seq 4) appends one row and advances the cursor
    const char *kNew = "{\"type\":\"event\",\"room_id\":\"room_mock\","
                       "\"seq\":4,\"event\":{\"seq\":4,\"tick\":2,"
                       "\"type\":\"speech.uttered\","
                       "\"actor\":\"seat:seat.luo\",\"payload\":"
                       "{\"seat_id\":\"seat.luo\",\"name\":\"阿萝\","
                       "\"text\":\"那你说说，是谁？\"}}}";
    const char *p = kNew;
    EXPECT(apply_message(view, Json::parse(p, p + std::strlen(kNew))));
    EXPECT(view.rows.size() == 4);
    EXPECT(view.rows[3].seq == 4);
    EXPECT(view.rows[3].name == "阿萝");
    EXPECT(view.last_seq == 4);

    // the proposals come from the server's legal_actions (never
    // invented client-side): speak (open) + search (loc.equip)
    EXPECT(view.proposals.size() == 2);
    EXPECT(view.proposals[0].action == "speak");
    EXPECT(view.proposals[0].target.empty());
    EXPECT(view.proposals[1].action == "search");
    EXPECT(view.proposals[1].target == "loc.equip");

    // the event-kind table (the stream's categories): action, clue,
    // ending, narration (world.broadcast / narrative.*), and the
    // unknown-type degradation to system
    {
        const char *kinds[] = {
            "{\"type\":\"event\",\"seq\":5,\"event\":{\"seq\":5,"
            "\"type\":\"action.search\",\"payload\":{\"seat_id\":\"s\","
            "\"name\":\"阿萝\",\"text\":\"搜了设备舱，一无所获\"}}}",
            "{\"type\":\"event\",\"seq\":6,\"event\":{\"seq\":6,"
            "\"type\":\"clue.found\",\"payload\":{\"text\":\"定时器\"}}}",
            "{\"type\":\"event\",\"seq\":7,\"event\":{\"seq\":7,"
            "\"type\":\"session.ended\",\"payload\":{\"text\":\"失败\"}}}",
            "{\"type\":\"event\",\"seq\":8,\"event\":{\"seq\":8,"
            "\"type\":\"world.broadcast\",\"payload\":{\"text\":\"灯光闪烁\"}}}",
            "{\"type\":\"event\",\"seq\":9,\"event\":{\"seq\":9,"
            "\"type\":\"narrative.cue\",\"payload\":{\"text\":\"钟声\"}}}",
            "{\"type\":\"event\",\"seq\":10,\"event\":{\"seq\":10,"
            "\"type\":\"future.unknown\",\"payload\":{\"text\":\"?\"}}}",
        };
        const RowKind want[] = {
            RowKind::action,
            RowKind::clue,
            RowKind::ending,
            RowKind::narration,
            RowKind::narration,
            RowKind::system,
        };
        for (int i = 0; i < 6; ++i)
        {
            p = kinds[i];
            EXPECT(apply_message(view, Json::parse(p, p + std::strlen(kinds[i]))));
            EXPECT(view.rows.back().kind == want[i]);
        }
    }

    // error messages land as system rows; pong/stage are ignored
    const char *kErr = "{\"type\":\"error\",\"code\":\"SEAT_LEASE_LOST\","
                       "\"message\":\"窗口已过\"}";
    p = kErr;
    EXPECT(apply_message(view, Json::parse(p, p + std::strlen(kErr))));
    EXPECT(view.rows.back().kind == RowKind::system);
    const char *kPong = "{\"type\":\"pong\"}";
    p = kPong;
    EXPECT(!apply_message(view, Json::parse(p, p + std::strlen(kPong))));
    const char *kStage = "{\"type\":\"stage\",\"room_id\":\"r\"}";
    p = kStage;
    EXPECT(!apply_message(view, Json::parse(p, p + std::strlen(kStage))));
    return test::report("tianji_view");
}

using zb::app::tianji::compose_stage;
using zb::app::tianji::kStageWidth;
using zb::app::tianji::parse_stage;
using zb::app::tianji::StageState;

// TJ-C5: the stage-message parse + the deterministic compose
int test_tianji_scene()
{
    // the mock's synthesized beats parse into states
    auto msgs = zb::tianji::MockTianjiServer::synthesize(sample_record(),
                                                         "room_mock");
    // 3 welcome/snapshot/view + 3 events + 3 stage beats
    EXPECT(msgs.size() == 9);
    std::vector<StageState> beats;
    for (const auto &m : msgs)
    {
        StageState s;
        if (parse_stage(m, s))
        {
            beats.push_back(std::move(s));
        }
    }
    EXPECT(beats.size() == 3);
    EXPECT(beats[0].scene_id == "scene.equip");
    EXPECT(beats[0].revision == 1 && beats[2].revision == 3);
    EXPECT(beats[0].props.size() == 1);
    EXPECT(beats[0].props[0].id == "p.marker");
    EXPECT(beats[0].props[0].w == 60 && beats[0].props[0].h == 40);
    EXPECT(beats[1].props[0].x == 120 + 200);
    EXPECT(beats[0].overlays.size() == 1 && beats[0].overlays[0].r == 30);
    // the schema color form parses; the walk is deterministic
    EXPECT(beats[0].props[0].color.r() == 0xFF);
    EXPECT(beats[0].props[0].color.g() == 0xCC);
    EXPECT(beats[0].props[0].color.b() == 0x00);

    // the compose is a pure function of the state: same state, same
    // bytes; a moved prop changes exactly the covered region
    std::vector<core::Color> buf_a;
    std::vector<core::Color> buf_b;
    compose_stage(beats[0], buf_a);
    compose_stage(beats[0], buf_b);
    EXPECT(buf_a == buf_b);
    compose_stage(beats[1], buf_b);
    EXPECT(buf_a != buf_b);
    // the prop rect is where the beat put it (inside the canvas)
    const int w = kStageWidth;
    const int px = beats[0].props[0].x;
    const int py = beats[0].props[0].y;
    const core::Color &c =
        buf_a[static_cast<size_t>(py) * static_cast<size_t>(w) +
              static_cast<size_t>(px)];
    EXPECT(c.r() == 0xFF && c.g() == 0xCC && c.b() == 0x00);

    // untrusted wire numbers clamp into the canvas; a bad color falls
    // back to the theme blue; a message without a stage_view is not a
    // beat
    {
        const char *kWild = "{\"type\":\"stage\",\"stage_view\":{"
                            "\"scene_id\":\"s\",\"revision\":9,\"props\":["
                            "{\"id\":\"p\",\"x\":999999,\"y\":-5,\"w\":80,"
                            "\"h\":80,\"color\":\"not-a-color\"}]}}";
        const char *p = kWild;
        StageState s;
        EXPECT(parse_stage(zb::tianji::Json::parse(
                               p, p + std::strlen(kWild)),
                           s));
        EXPECT(s.props[0].x == kStageWidth);
        EXPECT(s.props[0].y == 0);
        EXPECT(s.props[0].color.b() == 200);  // the theme-blue fallback
        const char *kNoStage = "{\"type\":\"event\",\"seq\":1}";
        p = kNoStage;
        EXPECT(!parse_stage(zb::tianji::Json::parse(
                                p, p + std::strlen(kNoStage)),
                            s));
    }
    return test::report("tianji_scene");
}

// the tianji binary's own dispatch (test_imui's main is framework
// suites; this one stays app-side): the same filter convention —
// substring match, no filter = all
int main(const int argc, char **argv)
{
    struct Suite
    {
        const char *name;
        int (*fn)();
    };
    const Suite suites[] = {
        {"tianji_json", test_tianji_json},
        {"tianji_feed", test_tianji_feed},
        {"tianji_view", test_tianji_view},
        {"tianji_scene", test_tianji_scene},
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
    int selected = 0;
    int failures = 0;
    for (const Suite &s : suites)
    {
        bool wanted = filter_count == 0;
        for (int i = 0; i < filter_count; ++i)
        {
            if (std::strstr(s.name, filters[i]) != nullptr)
            {
                wanted = true;
                break;
            }
        }
        if (!wanted)
        {
            continue;
        }
        ++selected;
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
