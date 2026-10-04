// Compares the reach rule used by the generator (core/reach.h, after puyoai)
// with the movement model of v0.3.0 (kicks and quick turns searched by BFS,
// without gravity) for every combination of column heights.
//
// Prints how many placements each one allows, and groups the differences by
// the heights of the columns on the way from the spawn column to the target:
//   L = 10 or lower, 11, 12, 13.
// Build together with generator/*.cpp; takes about a minute on 16 threads.
#include "../generator/generator.h"
#include "../tests/reference/v030_engine.h"
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>

using namespace puyo;

static std::string pattern(const int h[W], int id) {
    const PairPosition p = pairPosition(id);
    const int target = p.x1 == p.x2 ? p.x1 : (p.x1 >= SPAWN_X ? p.x2 : p.x1);
    std::string s = id < W ? "V " : "H ";
    const int step = target < SPAWN_X ? -1 : 1;
    for (int x = SPAWN_X;; x += step) {
        s += h[x] <= 10 ? "L" : h[x] == 11 ? "11" : h[x] == 12 ? "12" : "13";
        if (x == target) break;
        s += target < SPAWN_X ? " < " : " > ";
    }
    // The neighbour of the spawn column on the other side matters for the quick turn.
    const int other = SPAWN_X - step;
    s += std::string("   (other side ") + (h[other] >= 12 ? "12+" : "low") + ")";
    return s;
}

int main() {
    std::mutex mutex;
    long long both = 0, onlyRule = 0, onlyModel = 0, profiles = 0;
    std::map<std::string, long long> patterns;
    std::vector<std::thread> pool;
    const int threads = int(std::max(1u, std::thread::hardware_concurrency() / 2));
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            long long b = 0, r = 0, m = 0, n = 0;
            std::map<std::string, long long> local;
            for (int code = t; code < 14 * 14 * 14 * 14 * 14 * 12; code += threads) {
                int h[W], c = code;
                for (int x = 0; x < W; ++x) {
                    const int base = x == SPAWN_X ? 12 : 14;
                    h[x] = c % base;
                    c /= base;
                }
                v030::Field field;
                for (int x = 0; x < W; ++x) field.col[x].assign(size_t(h[x]), Cell(1));
                const unsigned rule = reachablePairs(h);
                ++n;
                for (int id = 0; id < PAIR_POSITIONS; ++id) {
                    const PairPosition p = pairPosition(id);
                    v030::Domino d{{p.x1, h[p.x1]}, {p.x2, h[p.x2] + (p.x1 == p.x2)}, 1, 2, p.x1 == p.x2 ? 'V' : 'H', {}};
                    if (d.a.second >= H || d.b.second >= H) continue; // would rest in the 14th row
                    const bool model = v030::findPlacementRoute(field, d);
                    const bool mine = rule >> id & 1;
                    if (mine && model) ++b;
                    else if (mine) ++r, ++local["RULE ONLY " + pattern(h, id)];
                    else if (model) ++m, ++local[pattern(h, id)];
                }
            }
            std::lock_guard lock(mutex);
            both += b; onlyRule += r; onlyModel += m; profiles += n;
            for (auto& [k, v] : local) patterns[k] += v;
        });
    for (auto& t : pool) t.join();
    std::printf("height profiles: %lld\nallowed by both: %lld\nonly by the rule: %lld\nonly by the v0.3.0 model: %lld\n\n",
                profiles, both, onlyRule, onlyModel);
    for (auto& [k, v] : patterns) std::printf("%10lld  %s\n", v, k.c_str());
}
