#include "test.hpp"

#include <chrono>
#include <cstdio>
#include <memory>

#include "imcore.hpp"
#include "imui.hpp"

using namespace zb::ui;

// Traversal cost micro-benchmark (batch J9): synthetic trees at three
// sizes, timed for the three hot traversals (damage walk / focusable
// collection / full-tree draw). No assertions -- the printout is the
// evidence baseline for the conditional optimization backlog (the >5%
// frame-CPU threshold). Run on a desktop build only.
namespace
{
    struct rig
    {
        std::unique_ptr<Panel> root;
        std::vector<Widget *> leaves;
        core::Graphics::ptr surface;

        explicit rig(const int leaf_count_per_branch)
        {
            root = std::make_unique<Panel>();
            root->set_size(600, 400);
            surface = core::Graphics::make_ptr(600, 400);
            for (int b = 0; b < 10; ++b)
            {
                auto branch = std::make_unique<Panel>();
                branch->set_size(600, 20);
                branch->set_position(0, b * 40);
                for (int i = 0; i < leaf_count_per_branch; ++i)
                {
                    auto leaf = std::make_unique<Label>();
                    leaf->set_text("AB");
                    leaf->set_size(60, 18);
                    leaf->set_position(i * 60, 0);
                    leaves.push_back(leaf.get());
                    branch->add_child(std::move(leaf));
                }
                root->add_child(std::move(branch));
            }
        }

        [[nodiscard]] int total() const { return 1 + 10 + static_cast<int>(leaves.size()); }
    };

    template <typename F>
    double us_per_call(F &&f, const int rounds)
    {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < rounds; ++i)
        {
            f();
        }
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::micro>(t1 - t0).count() / rounds;
    }
}  // namespace

int test_perf_walk()
{
    const int rounds_walk = 200;
    const int rounds_draw = 10;
    std::printf("perf: nodes | walk_damage(us) | collect_focusable(us) | full draw(us)\n");
    for (const int leaves : {10, 50, 100})  // 111 / 511 / 1011 nodes
    {
        rig r(leaves);

        const double walk = us_per_call(
            [&]
            {
                int l = 0, t = 0, rr = -1, b = -1;
                r.root->walk_damage(&l, &t, &rr, &b);
            },
            rounds_walk);

        // the keyboard hot path: collect focusable widgets + advance
        // (InputDispatcher::focus_next, the Tab/arrow entry)
            const double focus = us_per_call(
            [&]
            {
                InputDispatcher d;
                d.focus_next(*r.root, true);
            },
            rounds_walk);

        const double draw = us_per_call(
            [&] { r.root->draw(*r.surface); },
            rounds_draw);

        std::printf("perf: %4d | %12.1f | %18.1f | %13.1f\n",
                    r.total(), walk, focus, draw);
    }
    return test::report("perf_walk");
}