// Tests of the bitboard core and the generator against cell-by-cell references.
//
// Two references are used: a tiny grid simulator written here (handles holes,
// garbage and the 13th row), and the unchanged v0.3.0 engine.
#include "../generator/generator.h"
#include "reference/v030_engine.h"
#include <iostream>
#include <map>
#include <random>
#include <set>

using namespace puyo;

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

// ---- cell-by-cell reference ---------------------------------------------------
struct Grid {
    Cell c[W][H]{};
};
static BitField toBits(const Grid& g) {
    BitField f;
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y)
            if (g.c[x][y]) f.set(x, y, g.c[x][y]);
    return f;
}
static bool sameCells(const BitField& f, const Grid& g) {
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y)
            if (f.get(x, y) != g.c[x][y]) return false;
    return true;
}
struct GridWave {
    std::set<std::pair<int, int>> cleared, garbage;
    int colors = 0, groups = 0, bonus = 0;
};
static bool gridWave(Grid& g, GridWave& w) {
    bool seen[W][H]{};
    bool colorUsed[8]{};
    const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < CLEAR_H; ++y) {
            if (seen[x][y] || g.c[x][y] < 1 || g.c[x][y] > 5) continue;
            std::vector<std::pair<int, int>> group{{x, y}};
            seen[x][y] = true;
            for (size_t i = 0; i < group.size(); ++i)
                for (int d = 0; d < 4; ++d) {
                    const int nx = group[i].first + dx[d], ny = group[i].second + dy[d];
                    if (nx < 0 || nx >= W || ny < 0 || ny >= CLEAR_H || seen[nx][ny] || g.c[nx][ny] != g.c[x][y]) continue;
                    seen[nx][ny] = true;
                    group.push_back({nx, ny});
                }
            if (group.size() < 4) continue;
            ++w.groups;
            w.bonus += groupBonus(int(group.size()));
            colorUsed[g.c[x][y]] = true;
            w.cleared.insert(group.begin(), group.end());
        }
    if (w.cleared.empty()) return false;
    for (bool used : colorUsed) w.colors += used;
    for (auto [x, y] : w.cleared)
        for (int d = 0; d < 4; ++d) {
            const int nx = x + dx[d], ny = y + dy[d];
            if (nx >= 0 && nx < W && ny >= 0 && ny < CLEAR_H && g.c[nx][ny] == GARBAGE) w.garbage.insert({nx, ny});
        }
    for (auto [x, y] : w.cleared) g.c[x][y] = 0;
    for (auto [x, y] : w.garbage) g.c[x][y] = 0;
    for (int x = 0; x < W; ++x) {
        int dst = 0;
        for (int y = 0; y < H; ++y)
            if (g.c[x][y]) {
                const Cell v = g.c[x][y];
                g.c[x][y] = 0;
                g.c[x][dst++] = v;
            }
    }
    return true;
}
static std::set<std::pair<int, int>> cells(FieldBits b) {
    std::set<std::pair<int, int>> s;
    forEachCell(b, [&](int x, int y) { s.insert({x, y}); });
    return s;
}

// ---- conversions to the v0.3.0 engine -----------------------------------------
static v030::Field toOld(const BitField& f) {
    v030::Field o;
    int h[W];
    f.heights(h);
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < h[x]; ++y) o.col[x].push_back(f.get(x, y));
    return o;
}
static BitField fromOld(const v030::Field& o) {
    BitField f;
    for (int x = 0; x < W; ++x)
        for (size_t y = 0; y < o.col[x].size(); ++y) f.set(x, int(y), o.col[x][y]);
    return f;
}
static std::string keyOf(const BitField& f) { return toOld(f).key(); }

static void testWaves(std::mt19937_64& rng) {
    // Random boards with holes, garbage, the reserved code and the 13th row.
    for (int i = 0; i < 20000; ++i) {
        Grid g;
        const int colors = 2 + int(rng() % 4), fill = 30 + int(rng() % 70);
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < H; ++y)
                if (int(rng() % 100) < fill) g.c[x][y] = rng() % 7 == 0 ? GARBAGE : Cell(1 + rng() % colors);
        BitField f = toBits(g);
        check(sameCells(f, g), "set/get");
        check(f.count() == [&] { int n = 0; for (auto& col : g.c) for (Cell v : col) n += v != 0; return n; }(), "count");
        for (int step = 0; step < 30; ++step) {
            GridWave expected;
            Wave w;
            const bool more = gridWave(g, expected);
            check(f.hasClear() == more, "hasClear");
            check(stepWave(f, w) == more, "stepWave result");
            if (!more) break;
            check(cells(w.cleared) == expected.cleared, "cleared cells");
            check(cells(w.garbage) == expected.garbage, "garbage cells");
            check(w.puyos == int(expected.cleared.size()) && w.colors == expected.colors &&
                  w.groups == expected.groups && w.groupBonus == expected.bonus, "wave details");
            check(sameCells(f, g), "board after gravity");
        }
    }
    // The 13th row never connects, but falls.
    BitField ghost;
    const Cell column[] = {2, 2, 2, 2, 3, 4, 3, 4, 3, 1, 1, 1, 1};
    for (int y = 0; y < H; ++y) ghost.set(0, y, column[y]);
    check(runChain(ghost).chains == 2, "row 13 must drop but not connect before dropping");
    BitField none;
    const Cell stable[] = {2, 3, 2, 3, 2, 3, 2, 3, 2, 1, 1, 1, 1};
    for (int y = 0; y < H; ++y) none.set(0, y, stable[y]);
    check(!none.hasClear(), "row 13 must not count toward a clear");
    // Scores: one group of four in the first wave is 40 points, a second wave of four is 320.
    BitField two;
    const Cell left[] = {2, 1, 1, 1}, right[] = {2, 1, 2, 2};
    for (int y = 0; y < 4; ++y) two.set(0, y, left[y]), two.set(1, y, right[y]);
    const ChainResult r = runChain(two);
    check(r.chains == 2 && r.score == 40 + 40 * 8, "chain score");
}

static void testInsertAndKeys(std::mt19937_64& rng) {
    for (int i = 0; i < 2000; ++i) {
        v030::Field o;
        for (auto& c : o.col) {
            const int h = int(rng() % 12);
            for (int y = 0; y < h; ++y) c.push_back(Cell(1 + rng() % 5));
        }
        BitField f = fromOld(o);
        const int x = int(rng() % W), n = 1 + int(rng() % 2), slot = int(rng() % (o.col[x].size() + 1));
        const Cell color = Cell(1 + rng() % 5);
        f.insert(x, slot, n, color);
        o.col[x].insert(o.col[x].begin() + slot, size_t(n), color);
        check(toOld(f) == o, "insert");
        BitField g = fromOld(o);
        check(g == f && g.key() == f.key() && KeyHash{}(g.key()) == KeyHash{}(f.key()), "key equality");
        g.set(x, 0, Cell(g.get(x, 0) % 5 + 1));
        check(!(g.key() == f.key()), "key difference");
    }
}

static std::set<std::string> oldKeys(const std::vector<v030::Candidate>& values) {
    std::set<std::string> result;
    for (const auto& v : values) result.insert(v.field.key());
    return result;
}

static void testPredecessors(std::mt19937_64& rng) {
    const auto shapes = v030::makeTetrominoShapes();
    int withTrigger = 0;
    for (int i = 0; i < 400; ++i) {
        const int colors = i % 2 ? 4 : 5;
        BitField f;
        FieldBits trigger;
        if (i < 150) { // arbitrary compact boards, including full hidden rows
            v030::Field o;
            for (auto& c : o.col) {
                const int h = int(rng() % 14);
                for (int y = 0; y < h; ++y) c.push_back(Cell(1 + rng() % colors));
            }
            f = fromOld(o);
        } else { // boards made by the search itself, with their trigger
            if (i % 3 == 0) f.set(0, 0, 1), f.set(5, 0, 2);
            for (int depth = 0; depth < i % 20; ++depth) {
                const auto options = predecessors(f, trigger, colors);
                if (options.empty()) break;
                const auto& pick = options[rng() % options.size()];
                f = pick.field;
                trigger = pick.trigger;
            }
        }
        withTrigger += trigger.any();
        const auto found = predecessors(f, trigger, colors);
        std::set<std::string> keys;
        std::map<std::string, std::set<std::pair<int, int>>> triggers;
        for (const auto& p : found) {
            keys.insert(keyOf(p.field));
            triggers[keyOf(p.field)] = cells(p.trigger);
        }
        check(keys.size() == found.size(), "duplicate predecessors");
        const auto reference = v030::referencePredecessors(toOld(f), shapes, colors);
        check(keys == oldKeys(reference), "predecessors differ from the reference");
        for (const auto& candidate : reference) {
            std::set<std::pair<int, int>> t(candidate.trigger.begin(), candidate.trigger.end());
            check(triggers[candidate.field.key()] == t, "trigger differs from the reference");
        }
        // Each predecessor clears exactly its group and falls back to the board.
        for (const auto& p : found) {
            BitField g = p.field;
            Wave w;
            check(stepWave(g, w) && w.groups == 1 && w.puyos == 4 && w.cleared == p.trigger && g == f, "one step back");
        }
    }
    check(withTrigger > 100, "predecessor test must cover boards with a trigger");
}

// The definition of a last pair, checked directly.
static unsigned firePairsDirect(const BitField& field) {
    unsigned result = 0;
    for (int id = 0; id < PAIR_POSITIONS; ++id) {
        const FieldBits pair = pairCells(field, id);
        if (pair.count() != 2) continue;
        BitField before = field;
        before.erase(pair);
        if (before.hasClear() || before.get(SPAWN_X, CLEAR_H - 1)) continue;
        int h[W];
        before.heights(h);
        if (reachablePairs(h) >> id & 1) result |= 1u << id;
    }
    return result;
}

static void testFirePairs(std::mt19937_64& rng) {
    int nonZero = 0, total = 0;
    for (int i = 0; i < 300; ++i) {
        const int colors = i % 2 ? 4 : 5;
        BitField f;
        FieldBits trigger;
        // A residual board with some extra puyos, then random steps back.
        const int extra = int(rng() % 30);
        for (int n = 0; n < extra; ++n) {
            int h[W];
            f.heights(h);
            const int x = int(rng() % W);
            if (h[x] >= (x == SPAWN_X ? 11 : H)) continue;
            f.set(x, h[x], Cell(1 + rng() % colors));
            if (f.hasClear()) f.set(x, h[x], EMPTY);
        }
        for (int depth = 0; depth < 19; ++depth) {
            const auto options = predecessors(f, trigger, colors);
            if (options.empty()) break;
            for (const auto& p : options) {
                const unsigned fast = firePairs(p.field, p.trigger);
                check(fast == firePairsDirect(p.field), "firePairs shortcut differs from the definition");
                nonZero += fast != 0;
                ++total;
                if (fast) {
                    const Solution s{p.field, p.trigger, fast, depth + 1};
                    const std::string problem = verifySolution(s);
                    check(problem.empty(), problem.c_str());
                    Solution bad = s;
                    bad.targetChain = depth + 2;
                    check(!verifySolution(bad).empty(), "wrong chain count accepted");
                    bad = s;
                    bad.firePairs = (1u << PAIR_POSITIONS) - 1;
                    check(fast == bad.firePairs || !verifySolution(bad).empty(), "invalid last pair accepted");
                }
            }
            const auto& pick = options[rng() % options.size()];
            f = pick.field;
            trigger = pick.trigger;
        }
    }
    check(nonZero > 1000 && total > nonZero, "fire pair test coverage");
}

static void testReach(std::mt19937_64& rng) {
    // Fixed cases of the rule.
    const auto pairs = [](std::initializer_list<int> heights) {
        int h[W];
        std::copy(heights.begin(), heights.end(), h);
        return reachablePairs(h);
    };
    check(pairs({0, 0, 0, 0, 0, 0}) == (1u << PAIR_POSITIONS) - 1, "empty field: everything reachable");
    check(pairs({0, 0, 12, 0, 0, 0}) == 0, "occupied spawn cell");
    check(!(pairs({0, 0, 0, 13, 0, 0}) >> 4 & 1), "a 13-high column cannot be crossed");
    check(!(pairs({0, 13, 0, 0, 0, 0}) >> 0 & 1), "a 13-high column cannot be crossed (left)");
    check(!(pairs({0, 0, 0, 12, 0, 0}) >> 4 & 1), "a 12-high column needs a lift");
    check(pairs({0, 0, 11, 12, 0, 0}) >> 4 & 1, "floor kick from a column 11 high");
    check(pairs({0, 12, 5, 12, 0, 0}) >> 5 & 1, "quick turn between two 12-high columns");
    check(pairs({0, 0, 11, 12, 12, 0}) >> 5 & 1, "walking along 12-high columns");
    check(pairs({0, 0, 11, 12, 3, 12}) >> 10 & 1, "one-column gap");
    check(!(pairs({0, 0, 0, 0, 0, 12}) >> 5 & 1), "no vertical pair on a 12-high column");
    check(pairs({0, 0, 0, 0, 11, 12}) >> 10 & 1, "horizontal pair onto a 12-high column");
    // Whatever this rule allows, the v0.3.0 movement model (kicks and quick
    // turns, no gravity) must also allow: the rule is the stricter one.
    int both = 0, onlyOld = 0;
    for (int i = 0; i < 4000; ++i) {
        v030::Field o;
        int h[W];
        for (int x = 0; x < W; ++x) {
            h[x] = i % 4 == 0 ? int(rng() % 14) : 9 + int(rng() % 5);
            if (x == SPAWN_X && h[x] > 11) h[x] = 11;
            o.col[x].assign(size_t(h[x]), Cell(1));
        }
        const unsigned mine = reachablePairs(h);
        for (int id = 0; id < PAIR_POSITIONS; ++id) {
            const PairPosition p = pairPosition(id);
            v030::Domino d{{p.x1, h[p.x1]}, {p.x2, h[p.x2] + (p.x1 == p.x2)}, 1, 2, p.x1 == p.x2 ? 'V' : 'H', {}};
            if (d.a.second >= H || d.b.second >= H) {
                check(!(mine >> id & 1), "placement in the 14th row");
                continue;
            }
            const bool old = v030::findPlacementRoute(o, d);
            if (mine >> id & 1) {
                check(old, "the reach rule allows a placement the movement model cannot make");
                ++both;
            } else if (old) ++onlyOld;
        }
    }
    check(both > 10000, "reach test coverage");
    std::cout << "  reach: " << both << " placements allowed by both, " << onlyOld
              << " only by the v0.3.0 movement model\n";
}

static void testRng() {
    // Fixed values: the generator must give the same boards on every platform.
    Rng a(20261004, 1);
    const uint64_t first = a.next(), second = a.next();
    Rng b(20261004, 1), c(20261004, 2);
    check(b.next() == first && b.next() == second && c.next() != first, "streams");
    check(first == 1187160665230963522ull && second == 14745419000101852816ull, "rng golden values");
    int counts[7]{};
    for (int i = 0; i < 70000; ++i) ++counts[a.below(7)];
    for (int v : counts) check(v > 9500 && v < 10500, "below() is not uniform");
    int order[5] = {0, 1, 2, 3, 4};
    a.shuffle(order, 5);
    std::sort(order, order + 5);
    for (int i = 0; i < 5; ++i) check(order[i] == i, "shuffle lost an element");
}

static void testGeneration() {
    // Whole searches for several settings; every result must pass the direct check.
    int found = 0;
    for (int chain : {1, 2, 5, 10, 19})
        for (int colors : {4, 5})
            for (int extra : {0, 1, 78 - 4 * chain}) {
                GeneratorConfig config;
                config.colorCount = colors;
                config.beamWidth = 48;
                config.candidatesPerParent = 2;
                config.targetChain = chain;
                config.minExtraPuyos = config.maxExtraPuyos = extra;
                for (int attempt = 1; attempt <= 300; ++attempt) {
                    Rng rng(7, uint64_t(attempt));
                    const auto s = generateOne(config, rng);
                    if (!s) continue;
                    const std::string problem = verifySolution(*s);
                    check(problem.empty(), problem.c_str());
                    check(s->field.count() == 4 * chain + extra, "puyo count");
                    Rng again(7, uint64_t(attempt));
                    const auto t = generateOne(config, again);
                    check(t && t->field == s->field && t->firePairs == s->firePairs, "same seed, same board");
                    ++found;
                    break;
                }
            }
    check(found == 30, "every setting must produce a board");
}

int main() {
    try {
        for (int pass = 0; pass < 2; ++pass) {
            // Second pass: the gravity code path without PEXT/PDEP.
            if (pass == 1) {
                if (!detail::fastPext) break;
                detail::fastPext = false;
            }
            std::mt19937_64 rng(42);
            testWaves(rng);
            testInsertAndKeys(rng);
            testPredecessors(rng);
            testFirePairs(rng);
            std::cout << (pass ? "  portable gravity: ok\n" : "  waves, insertion, predecessors, last pairs: ok\n");
        }
        std::mt19937_64 rng(43);
        testReach(rng);
        testRng();
        testGeneration();
        std::cout << "All core tests passed"
#if PUYO_SSE
                  << " (SSE field bits).\n";
#else
                  << " (portable field bits).\n";
#endif
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
