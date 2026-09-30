#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <fstream>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// 6列×13段。最上段(13段目)は連結・消去対象外だが、落下対象には含める。
constexpr int W = 6;
constexpr int H = 13;
constexpr int CLEAR_H = 12;
constexpr int MIN_TARGET_CHAIN = 1;
constexpr int MAX_TARGET_CHAIN = 19;
constexpr int DEFAULT_TARGET_CHAIN = MAX_TARGET_CHAIN;

// 色: 0=空, 1..4/5=通常色。おじゃまぷよは使わない。
using Cell = uint8_t;
using Point = std::pair<int, int>; // x=左から0..5, y=下から0..12
using Shape = std::vector<Point>;

struct Field {
    // 各列を下から上へ格納。生成盤面は列内に穴のない状態に保つ。
    std::array<std::vector<Cell>, W> col;

    std::string key() const {
        std::string s;
        for (const auto& c : col) {
            s.push_back(static_cast<char>(c.size()));
            for (Cell v : c) s.push_back(static_cast<char>(v));
        }
        return s;
    }

    bool operator==(const Field& other) const {
        return col == other.col;
    }

    bool empty() const {
        for (const auto& c : col) if (!c.empty()) return false;
        return true;
    }
};

struct Candidate {
    Field field;
    std::array<Point, 4> trigger;
};

struct Domino {
    // 座標と色は完成盤面上の位置。sequenceは実際に置く順。
    Point a;
    Point b;
    Cell colorA;
    Cell colorB;
    char orientation; // 'V' または 'H'
    std::string controls; // L/R/D: move, A/B: rotate; then lock and split/drop
    int setupClear = 0; // explicitly planned 5-clear to create odd cell parity
};

struct Solution {
    Field field;
    std::array<Point, 4> trigger;
    std::vector<Domino> placementSequence;
    int targetChain = DEFAULT_TARGET_CHAIN;
};

static bool containsPoint(const std::array<Point, 4>& points, Point p) {
    return std::find(points.begin(), points.end(), p) != points.end();
}

struct WaveInfo {
    // この波で消える「連結成分」ごとの座標。4個以外も記録して検査する。
    std::vector<std::vector<Point>> groups;
};

// Fixed-size scratch storage avoids heap allocations for every connected component.
static int clearAndDrop(Field &field, WaveInfo *waveOut = nullptr) {
    bool seen[W][H]{};
    bool remove[W][H]{};
    Point stack[W * CLEAR_H], component[W * CLEAR_H];
    int removed = 0;
    if (waveOut)
        waveOut->groups.clear();
    constexpr Point dirs[] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int x = 0; x < W; ++x) {
        const int limit = std::min(CLEAR_H, int(field.col[x].size()));
        for (int y = 0; y < limit; ++y) {
            if (seen[x][y] || !field.col[x][y])
                continue;
            const Cell color = field.col[x][y];
            int top = 0, count = 0;
            stack[top++] = {x, y};
            seen[x][y] = true;
            while (top) {
                Point q = stack[--top];
                component[count++] = q;
                for (Point d : dirs) {
                    int nx = q.first + d.first, ny = q.second + d.second;
                    if (nx < 0 || nx >= W || ny < 0 || ny >= CLEAR_H ||
                        ny >= int(field.col[nx].size()) || seen[nx][ny] ||
                        field.col[nx][ny] != color)
                        continue;
                    seen[nx][ny] = true;
                    stack[top++] = {nx, ny};
                }
            }
            if (count < 4)
                continue;
            removed += count;
            for (int i = 0; i < count; ++i)
                remove[component[i].first][component[i].second] = true;
            if (waveOut) {
                std::vector<Point> group(component, component + count);
                std::sort(group.begin(), group.end());
                waveOut->groups.push_back(std::move(group));
            }
        }
    }
    if (!removed)
        return 0;
    for (int x = 0; x < W; ++x) {
        int dst = 0;
        for (int y = 0; y < int(field.col[x].size()); ++y)
            if (!remove[x][y])
                field.col[x][dst++] = field.col[x][y];
        field.col[x].resize(dst);
    }
    return removed;
}

static int chainCount(Field field) {
    int chains = 0;
    while (clearAndDrop(field) > 0) ++chains;
    return chains;
}

static bool cleanChain(
    Field field,
    const std::array<Point, 4>& expectedTrigger,
    int targetChain = DEFAULT_TARGET_CHAIN,
    std::vector<WaveInfo>* traceOut = nullptr)
{
    int waves = 0;
    while (true) {
        WaveInfo wave;
        const int removed = clearAndDrop(field, &wave);
        if (removed == 0) break;

        // 各波は、同時消しなし・4個ちょうどの単独グループ。
        if (wave.groups.size() != 1 || wave.groups.front().size() != 4) {
            return false;
        }

        if (waves == 0) {
            std::vector<Point> actual = wave.groups.front();
            std::vector<Point> expected(
                expectedTrigger.begin(), expectedTrigger.end());
            std::sort(actual.begin(), actual.end());
            std::sort(expected.begin(), expected.end());
            if (actual != expected) return false;
        }

        ++waves;
        if (traceOut) traceOut->push_back(wave);
        if (waves > targetChain) return false;
    }

    return waves == targetChain;
}
static Shape normalizeShape(Shape shape) {
    int minX = shape.front().first;
    int minY = shape.front().second;
    for (const Point p : shape) {
        minX = std::min(minX, p.first);
        minY = std::min(minY, p.second);
    }
    for (Point& p : shape) {
        p.first -= minX;
        p.second -= minY;
    }
    std::sort(shape.begin(), shape.end());
    return shape;
}

static std::vector<Shape> makeTetrominoShapes() {
    // 5種の自由テトロミノを、回転・反転して重複を除く。
    const std::vector<Shape> bases{
        {{0,0}, {1,0}, {2,0}, {3,0}}, // I
        {{0,0}, {1,0}, {0,1}, {1,1}}, // O
        {{0,0}, {1,0}, {2,0}, {1,1}}, // T
        {{0,0}, {0,1}, {0,2}, {1,0}}, // L
        {{1,0}, {2,0}, {0,1}, {1,1}}  // S
    };

    std::set<Shape> unique;
    for (const Shape& base : bases) {
        for (int reflected = 0; reflected < 2; ++reflected) {
            for (int rotation = 0; rotation < 4; ++rotation) {
                Shape transformed;
                for (Point p : base) {
                    int x = reflected ? -p.first : p.first;
                    int y = p.second;
                    for (int r = 0; r < rotation; ++r) {
                        const int oldX = x;
                        x = -y;
                        y = oldX;
                    }
                    transformed.emplace_back(x, y);
                }
                unique.insert(normalizeShape(std::move(transformed)));
            }
        }
    }

    return std::vector<Shape>(unique.begin(), unique.end());
}

static std::vector<Candidate> predecessors(const Field &post, const std::vector<Shape> &shapes,
                                           int colorCount, std::mt19937_64* rng = nullptr, size_t sampleLimit = 0) {
    // Keep small insertion descriptions until sampling. Most candidates at
    // intermediate depths are discarded, so allocating six columns for each
    // of them wastes time. Shuffle preserves the original sampling order.
    struct Insertion { const Shape* shape; int x, y; Cell color; };
    std::vector<Insertion> insertions;

    for (const Shape &shape : shapes) {
        int maxX = 0;
        int maxY = 0;
        for (const Point p : shape) {
            maxX = std::max(maxX, p.first);
            maxY = std::max(maxY, p.second);
        }
        const int shapeW = maxX + 1;

        // 各相対列に入るテトロミノぷよの相対y座標。
        std::array<std::vector<int>, W> groupYs{};
        for (const Point p : shape)
            groupYs[p.first].push_back(p.second);

        bool columnsAreContiguous = true;
        for (int dx = 0; dx < shapeW; ++dx) {
            auto &ys = groupYs[dx];
            if (ys.empty())
                continue;
            std::sort(ys.begin(), ys.end());
            for (size_t i = 1; i < ys.size(); ++i) {
                if (ys[i] != ys[i - 1] + 1)
                    columnsAreContiguous = false;
            }
        }
        if (!columnsAreContiguous)
            continue;

        for (int x0 = 0; x0 + shapeW <= W; ++x0) {
            for (int y0 = 0; y0 + maxY < CLEAR_H; ++y0) {
                std::array<int, W> slots{};
                std::array<int, W> counts{};
                bool valid = true;

                for (int dx = 0; dx < shapeW; ++dx) {
                    const auto &ys = groupYs[dx];
                    if (ys.empty())
                        continue;

                    const int x = x0 + dx;
                    const int slot = y0 + ys.front();
                    const int count = static_cast<int>(ys.size());

                    if (slot > static_cast<int>(post.col[x].size()) ||
                        static_cast<int>(post.col[x].size()) + count > H) {
                        valid = false;
                        break;
                    }
                    slots[x] = slot;
                    counts[x] = count;
                }
                if (!valid)
                    continue;

                // Insert empty slots first. All pre-existing components must be
                // smaller than four, regardless of the color later assigned.
                Cell cells[W][H]{};
                int heights[W];
                for (int x = 0; x < W; ++x) {
                    heights[x] = int(post.col[x].size()) + counts[x];
                    for (int y = 0; y < int(post.col[x].size()); ++y)
                        cells[x][y + (counts[x] && y >= slots[x] ? counts[x] : 0)] = post.col[x][y];
                }
                bool visited[W][CLEAR_H]{};
                bool otherClear = false;
                constexpr Point neighbors[] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                Point stack[W * CLEAR_H];
                for (int x = 0; x < W && !otherClear; ++x)
                    for (int y = 0; y < std::min(heights[x], CLEAR_H) && !otherClear; ++y) {
                        if (!cells[x][y] || visited[x][y])
                            continue;
                        int top = 0, count = 0;
                        stack[top++] = {x, y};
                        visited[x][y] = true;
                        const Cell c = cells[x][y];
                        while (top && !otherClear) {
                            Point q = stack[--top];
                            if (++count >= 4) {
                                otherClear = true;
                                break;
                            }
                            for (Point d : neighbors) {
                                int nx = q.first + d.first, ny = q.second + d.second;
                                if (nx < 0 || nx >= W || ny < 0 || ny >= CLEAR_H ||
                                    visited[nx][ny] || cells[nx][ny] != c)
                                    continue;
                                visited[nx][ny] = true;
                                stack[top++] = {nx, ny};
                            }
                        }
                    }
                if (otherClear)
                    continue;
                unsigned forbidden = 0;
                for (size_t i = 0; i < shape.size(); ++i) {
                    Point q{x0 + shape[i].first, y0 + shape[i].second};
                    for (Point d : neighbors) {
                        int nx = q.first + d.first, ny = q.second + d.second;
                        if (nx >= 0 && nx < W && ny >= 0 && ny < CLEAR_H)
                            forbidden |= 1u << cells[nx][ny];
                    }
                }
                for (int color = 1; color <= colorCount; ++color) {
                    if (forbidden & (1u << color))
                        continue;
                    insertions.push_back({&shape, x0, y0, Cell(color)});
                }
            }
        }
    }

    if (rng) std::shuffle(insertions.begin(), insertions.end(), *rng);
    if (sampleLimit && insertions.size() > sampleLimit)
        insertions.resize(sampleLimit);
    std::vector<Candidate> result;
    result.reserve(insertions.size());
    for (const auto& insertion : insertions) {
        std::array<int, W> slots;
        slots.fill(H);
        std::array<int, W> counts{};
        std::array<Point, 4> trigger{};
        for (size_t i = 0; i < insertion.shape->size(); ++i) {
            Point q{insertion.x + (*insertion.shape)[i].first,
                    insertion.y + (*insertion.shape)[i].second};
            trigger[i] = q;
            slots[q.first] = std::min(slots[q.first], q.second);
            ++counts[q.first];
        }
        Field pre;
        for (int x = 0; x < W; ++x) {
            auto& column = pre.col[x];
            const auto& original = post.col[x];
            const size_t slot = counts[x] ? size_t(slots[x]) : original.size();
            column.reserve(original.size() + counts[x]);
            column.insert(column.end(), original.begin(), original.begin() + slot);
            column.insert(column.end(), counts[x], insertion.color);
            column.insert(column.end(), original.begin() + slot, original.end());
        }
        result.push_back({std::move(pre), trigger});
    }
    return result;
}

static bool legalTrigger(
    const Field& field,
    const std::array<Point, 4>& trigger)
{
    // 少なくとも1個の発火ぷよについて、上にあるぷよが1個以下であること。
    bool hasReachableTriggerPuyo = false;
    for (const Point p : trigger) {
        const int above = static_cast<int>(field.col[p.first].size()) - p.second - 1;
        if (above <= 1) hasReachableTriggerPuyo = true;
    }
    if (!hasReachableTriggerPuyo) return false;


    return true;
}

static bool dominoTouchesTrigger(
    const Domino& domino,
    const std::array<Point, 4>& trigger)
{
    return containsPoint(trigger, domino.a) || containsPoint(trigger, domino.b);
}

// A deliberately conservative, discrete Tsu movement model. Axis starts at
// (column 3,row 12), child above; the axis never enters row 14. Normal turns,
// side/floor kicks and quick turns are searched, without teleporting over walls.
// We never store a row-14 puyo: solutions need no persistent row-14 obstruction.
struct FallingPair {
    int x, y, r;
};
static constexpr Point PAIR_OFFSETS[] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
static bool pairFits(const Field &f, FallingPair p) {
    Point d = PAIR_OFFSETS[p.r];
    int cx = p.x + d.first, cy = p.y + d.second;
    if (p.x < 0 || p.x >= W || p.y < 0 || p.y >= H || cx < 0 || cx >= W || cy < 0 || cy > H)
        return false;
    return p.y >= int(f.col[p.x].size()) && cy >= int(f.col[cx].size());
}
static std::optional<FallingPair> turnPair(const Field &f, FallingPair p, int dir) {
    FallingPair q{p.x, p.y, (p.r + dir + 4) % 4};
    if (pairFits(f, q))
        return q;
    Point d = PAIR_OFFSETS[q.r];
    if (d.first) { // blocked child: kick away from it
        q.x -= d.first;
        if (pairFits(f, q))
            return q;
    } else if (d.second < 0) { // floor kick (one cell)
        ++q.y;
        if (pairFits(f, q))
            return q;
    }
    // Quick turn swaps axis/child when BOTH horizontal sides are blocked.
    if (p.r == 0 || p.r == 2) {
        FallingPair left{p.x, p.y, 3}, right{p.x, p.y, 1};
        if (!pairFits(f, left) && !pairFits(f, right)) {
            q = {p.x, p.y + PAIR_OFFSETS[p.r].second, (p.r + 2) % 4};
            if (pairFits(f, q))
                return q;
        }
    }
    return std::nullopt;
}
static bool landingMatches(const Field &lower, FallingPair p, const Domino &d) {
    if (pairFits(lower, {p.x, p.y - 1, p.r}))
        return false;
    Point off = PAIR_OFFSETS[p.r];
    Point a{p.x, int(lower.col[p.x].size())};
    Point b{p.x + off.first, int(lower.col[p.x + off.first].size())};
    if (off.first == 0) {
        if (off.second > 0)
            ++b.second;
        else
            ++a.second;
    }
    // Select the incoming pair colors to match this axis/child orientation.
    return (a == d.a && b == d.b) || (a == d.b && b == d.a);
}
static bool findPlacementRoute(const Field &lower, Domino &d) {
    constexpr int N = W * H * 4;
    auto id = [](FallingPair p) { return (p.y * W + p.x) * 4 + p.r; };
    FallingPair queue[N];
    int previous[N];
    char command[N];
    std::fill(previous, previous + N, -2);
    FallingPair spawn{2, 11, 0};
    if (!pairFits(lower, spawn))
        return false; // includes the death cell
    int head = 0, tail = 0;
    queue[tail++] = spawn;
    previous[id(spawn)] = -1;
    while (head < tail) {
        FallingPair p = queue[head++];
        if (landingMatches(lower, p, d)) {
            d.controls.clear();
            for (int k = id(p); previous[k] != -1; k = previous[k]) {
                char c = command[k];
                if (c == 'Q' || c == 'T') {
                    d.controls.push_back(c == 'Q' ? 'A' : 'B');
                    d.controls.push_back(c == 'Q' ? 'A' : 'B');
                } else
                    d.controls.push_back(c);
            }
            std::reverse(d.controls.begin(), d.controls.end());
            // Report colors in actual axis/child order, not left/bottom order.
            Point axis{p.x, int(lower.col[p.x].size())};
            if (p.r == 2)
                ++axis.second;
            if (axis == d.b) {
                std::swap(d.a, d.b);
                std::swap(d.colorA, d.colorB);
            }
            return true;
        }
        auto push = [&](FallingPair q, char c) {
            if (!pairFits(lower, q) || previous[id(q)] != -2)
                return;
            previous[id(q)] = id(p);
            command[id(q)] = c;
            queue[tail++] = q;
        };
        push({p.x - 1, p.y, p.r}, 'L');
        push({p.x + 1, p.y, p.r}, 'R');
        push({p.x, p.y - 1, p.r}, 'D');
        if (auto q = turnPair(lower, p, 1))
            push(*q, q->r == (p.r + 1) % 4 ? 'A' : 'Q');
        if (auto q = turnPair(lower, p, -1))
            push(*q, q->r == (p.r + 3) % 4 ? 'B' : 'T');
    }
    return false;
}

// An odd target board cannot be built by pairs without a prior odd clear.
// Six incoming puyos (AA, AA, AB) bridge two separated vertical AA groups,
// clearing exactly five A's and leaving B at the required bottom cell.
// Every placement and the resulting residual field are verified.
static bool makeOddPreamble(const Field &residual, std::vector<Domino> &sequence) {
    int x = -1;
    Cell b = 0;
    for (int c = 0; c < W; ++c)
        if (!residual.col[c].empty()) {
            if (x != -1 || residual.col[c].size() != 1)
                return false;
            x = c;
            b = residual.col[c][0];
        }
    if (x < 0)
        return false;
    const Cell a = b == 1 ? 2 : 1;
    const int other = x <= 3 ? x + 2 : x - 2, middle = (x + other) / 2;
    sequence = {{{other, 0}, {other, 1}, a, a, 'V', {}, 0},
                {{x, 0}, {x, 1}, a, a, 'V', {}, 0},
                {{middle, 0}, {x, 2}, a, b, 'H', {}, 5}};
    Field built;
    for (size_t i = 0; i < sequence.size(); ++i) {
        Domino &d = sequence[i];
        if (!findPlacementRoute(built, d))
            return false;
        // These low placements can only land with the vertical axis below.
        built.col[d.a.first].push_back(d.colorA);
        built.col[d.b.first].push_back(d.colorB);
        Field check = built;
        WaveInfo wave;
        int removed = clearAndDrop(check, &wave);
        if (i < 2 && removed)
            return false;
        if (i == 2) {
            if (removed != 5 || wave.groups.size() != 1 || !(check == residual))
                return false;
            built = std::move(check);
        }
    }
    return built == residual;
}

// Peel top pairs backwards. Every edge must have a route from the spawn.
// Removing top cells cannot introduce a clear; check the first lower field,
// then all of its subsets are also stable until the final firing pair.
static bool peelToBuildSequence(const Field &field, const std::array<Point, 4> &trigger,
                                bool firstPeel, std::mt19937_64 &rng, int &nodeBudget,
                                std::unordered_set<std::string> &dead,
                                std::vector<Domino> &sequence) {
    if (field.empty()) {
        sequence.clear();
        return true;
    }
    int puyos = 0;
    for (const auto &c : field.col)
        puyos += int(c.size());
    if (puyos == 1)
        return makeOddPreamble(field, sequence);
    if (--nodeBudget < 0)
        return false;
    const std::string key = field.key();
    if (!firstPeel && dead.contains(key))
        return false;
    std::vector<Domino> options;
    for (int x = 0; x < W; ++x) {
        int h = int(field.col[x].size());
        if (h >= 2)
            options.push_back(
                {{x, h - 2}, {x, h - 1}, field.col[x][h - 2], field.col[x][h - 1], 'V', {}});
    }
    // Horizontal pairs split (chigiri) after contact, so heights may differ.
    for (int x = 0; x + 1 < W; ++x) {
        int h = int(field.col[x].size()), k = int(field.col[x + 1].size());
        if (h && k)
            options.push_back({{x, h - 1},
                               {x + 1, k - 1},
                               field.col[x][h - 1],
                               field.col[x + 1][k - 1],
                               'H',
                               {}});
    }
    std::shuffle(options.begin(), options.end(), rng);
    for (Domino d : options) {
        if (firstPeel && !dominoTouchesTrigger(d, trigger))
            continue;
        Field lower = field;
        lower.col[d.a.first].pop_back();
        lower.col[d.b.first].pop_back();
        if (lower.col[2].size() >= CLEAR_H)
            continue;
        if (firstPeel) {
            Field check = lower;
            if (clearAndDrop(check))
                continue;
        }
        if (!findPlacementRoute(lower, d))
            continue;
        std::vector<Domino> lowerSequence;
        if (peelToBuildSequence(lower, trigger, false, rng, nodeBudget, dead, lowerSequence)) {
            if (lowerSequence.size() < 3) {
                unsigned colors = (1u << d.colorA) | (1u << d.colorB);
                for (const Domino &prev : lowerSequence)
                    colors |= (1u << prev.colorA) | (1u << prev.colorB);
                int count = 0;
                for (unsigned v = colors; v; v >>= 1)
                    count += int(v & 1);
                if (count > 3)
                    continue;
            }
            sequence = std::move(lowerSequence);
            sequence.push_back(std::move(d));
            return true;
        }
        if (nodeBudget < 0)
            return false; // budget exhaustion is not a proof of failure
    }
    if (!firstPeel)
        dead.insert(key);
    return false;
}

static bool replayPlacement(const Field &field, const Domino &d) {
    FallingPair p{2, 11, 0};
    if (!pairFits(field, p))
        return false;
    for (size_t i = 0; i < d.controls.size(); ++i) {
        char c = d.controls[i];
        if (c == 'L' || c == 'R' || c == 'D') {
            if (c == 'L')
                --p.x;
            else if (c == 'R')
                ++p.x;
            else
                --p.y;
            if (!pairFits(field, p))
                return false;
        } else if (c == 'A' || c == 'B') {
            auto q = turnPair(field, p, c == 'A' ? 1 : -1);
            if (!q)
                return false;
            if (q->r != (p.r + (c == 'A' ? 1 : 3)) % 4) {
                // A blocked first input arms the quick turn, the second swaps.
                if (i + 1 >= d.controls.size() || d.controls[i + 1] != c)
                    return false;
                ++i;
            }
            p = *q;
        } else
            return false;
    }
    Point axis{p.x, int(field.col[p.x].size())};
    if (p.r == 2)
        ++axis.second;
    return landingMatches(field, p, d) && axis == d.a;
}
static bool verifyBuildSequence(const Solution &s) {
    Field built;
    unsigned openingColors = 0;
    for (size_t i = 0; i < s.placementSequence.size(); ++i) {
        const Domino &d = s.placementSequence[i];
        if (!replayPlacement(built, d))
            return false;
        if (i < 3)
            openingColors |= (1u << d.colorA) | (1u << d.colorB);
        Point points[] = {d.a, d.b};
        Cell colors[] = {d.colorA, d.colorB};
        if (points[0].first == points[1].first && points[0].second > points[1].second) {
            std::swap(points[0], points[1]);
            std::swap(colors[0], colors[1]);
        }
        for (int k = 0; k < 2; ++k) {
            if (points[k].second != int(built.col[points[k].first].size()) || points[k].second >= H)
                return false;
            built.col[points[k].first].push_back(colors[k]);
        }
        if (d.setupClear) {
            if (i != 2 || d.setupClear != 5)
                return false;
            WaveInfo wave;
            if (clearAndDrop(built, &wave) != 5 || wave.groups.size() != 1)
                return false;
        } else if (i + 1 < s.placementSequence.size()) {
            Field check = built;
            if (built.col[2].size() >= CLEAR_H || clearAndDrop(check))
                return false;
        }
    }
    int colorCount = 0;
    for (unsigned v = openingColors; v; v >>= 1)
        colorCount += int(v & 1);
    return colorCount <= 3 && built == s.field && cleanChain(built, s.trigger, s.targetChain);
}

static void reportProgress(
    int targetChain,
    int attempt,
    int totalAttempts,
    int depth,
    size_t parent,
    size_t parentTotal,
    size_t beamSize,
    size_t work,
    size_t workTotal,
    const char* phase,
    bool force = false)
{
    using Clock = std::chrono::steady_clock;
    static Clock::time_point lastUpdate{};
    const Clock::time_point now = Clock::now();
    if (!force && lastUpdate != Clock::time_point{} &&
        now - lastUpdate < std::chrono::milliseconds(300)) {
        return;
    }

    std::ostringstream status;
    status << "再試行 " << attempt << '/' << totalAttempts
           << " | 段階 " << depth << '/' << targetChain
           << " | " << phase
           << " | 親盤面 " << parent << '/' << parentTotal
           << " | ビーム " << beamSize;
    if (workTotal > 0) {
        status << " | 候補 " << work << '/' << workTotal;
    } else {
        status << " | 次候補 " << work;
    }

    std::string line = status.str();
    constexpr size_t STATUS_WIDTH = 140;
    if (line.size() < STATUS_WIDTH) line.resize(STATUS_WIDTH, ' ');
    std::cout << '\r' << line << std::flush;
    lastUpdate = now;
}

static std::optional<Solution> generateOne(
    int colorCount,
    int beamWidth,
    int perParent,
    std::mt19937_64& rng,
    const std::vector<Shape>& shapes,
    int attempt,
    int totalAttempts,
    int maxExtraPuyos = 0,
    int targetChain = DEFAULT_TARGET_CHAIN,
    int minExtraPuyos = 0)
{
    std::vector<Field> beam(1);
    const int extraPuyos=maxExtraPuyos ? minExtraPuyos+int(rng()%(maxExtraPuyos-minExtraPuyos+1)) : 0;
    // Seed the inverse search with a stable, non-dead residual board. This
    // preserves the exact target number of waves for any permitted extra count.
    for (int i=0;i<extraPuyos;++i) {
        int x;
        do {x=int(rng()%W);} while (int(beam[0].col[x].size()) >= (x==2?11:H));
        int startColor=int(rng()%colorCount);
        bool inserted=false;
        for (int n=0;n<colorCount;++n) {
            beam[0].col[x].push_back(Cell(1+(startColor+n)%colorCount));
            Field check=beam[0];
            if (!clearAndDrop(check)) {inserted=true;break;}
            beam[0].col[x].pop_back();
        }
        if (!inserted) return std::nullopt;
    }

    for (int depth = 1; depth <= targetChain; ++depth) {
        std::vector<Field> nextBeam;
        std::unordered_set<std::string> seenNext;

        reportProgress(targetChain, attempt, totalAttempts, depth, 0, beam.size(),
                       beam.size(), 0, 0,
                       depth < targetChain ? "盤面を展開中" : "連鎖を検証中",
                       false);

        for (size_t parentIndex = 0; parentIndex < beam.size(); ++parentIndex) {
            const Field& post = beam[parentIndex];
            reportProgress(targetChain, attempt, totalAttempts, depth,
                           parentIndex + 1, beam.size(), beam.size(),
                           nextBeam.size(), 0,
                           depth < targetChain ? "盤面を展開中" : "連鎖を検証中",
                           false);
            std::vector<Candidate> prev = predecessors(
                post, shapes, colorCount, &rng, depth < targetChain ? size_t(perParent) : 0);

            if (depth < targetChain) {
                const size_t take = std::min<size_t>(perParent, prev.size());
                for (size_t i = 0; i < take; ++i) {
                    const std::string key = prev[i].field.key();
                    if (seenNext.insert(key).second) {
                        nextBeam.push_back(std::move(prev[i].field));
                    }
                }
                reportProgress(targetChain, attempt, totalAttempts, depth,
                               parentIndex + 1, beam.size(), beam.size(),
                               nextBeam.size(), 0, "盤面を展開中");
                continue;
            }

            // 目標連鎖数で初めて発火位置・発火可能性・実配置順を確認する。
            for (size_t candidateIndex = 0;
                 candidateIndex < prev.size(); ++candidateIndex) {
                reportProgress(targetChain, attempt, totalAttempts, depth,
                               parentIndex + 1, beam.size(), beam.size(),
                               candidateIndex + 1, prev.size(), "候補を検査中");
                Candidate& candidate = prev[candidateIndex];
                if (!legalTrigger(candidate.field, candidate.trigger)) continue;
                if (!cleanChain(candidate.field, candidate.trigger,targetChain)) continue;

                std::vector<Domino> placementSequence;
                std::unordered_set<std::string> dead;
                int pairSearchBudget = 200000;

                if (!peelToBuildSequence(
                        candidate.field, candidate.trigger, true, rng,
                        pairSearchBudget, dead, placementSequence)) {
                    continue;
                }

                Solution solution{std::move(candidate.field),candidate.trigger,std::move(placementSequence),targetChain};
                if (!verifyBuildSequence(solution)) continue;
                return solution;
            }
            reportProgress(targetChain, attempt, totalAttempts, depth,
                           parentIndex + 1, beam.size(), beam.size(),
                           prev.size(), prev.size(), "候補を検査中");
        }

        if (nextBeam.empty()) return std::nullopt;

        std::shuffle(nextBeam.begin(), nextBeam.end(), rng);
        if (nextBeam.size() > static_cast<size_t>(beamWidth)) {
            nextBeam.resize(beamWidth);
        }
        beam = std::move(nextBeam);
    }

    return std::nullopt;
}

static char colorChar(Cell color) {
    const char chars[] = ".RGBYP";
    return chars[color];
}

static std::string fieldToUrl(const Field& field) {
    // ishikawapuyo.net の盤面コード。2列分の色を8進相当の組み合わせ
    // (left * 8 + right) として、元コードと同じ64文字表で符号化する。
    static constexpr char URL_DIGITS[] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-";

    std::string encoded;
    bool firstNonEmptyPair = false;

    // 元コードと同じく、13段目(上端)から下へ、各行を左から3組読む。
    for (int y = H - 1; y >= 0; --y) {
        for (int x = 0; x < W; x += 2) {
            const Cell left = y < static_cast<int>(field.col[x].size())
                ? field.col[x][y] : 0;
            const Cell right = y < static_cast<int>(field.col[x + 1].size())
                ? field.col[x + 1][y] : 0;
            const unsigned code = static_cast<unsigned>(left) * 8u +
                                  static_cast<unsigned>(right);
            const char digit = URL_DIGITS[code];

            // 先頭に続く空の列ペアは省き、内部の空ペアは保持する。
            if (!firstNonEmptyPair && digit == '0') continue;
            firstNonEmptyPair = true;
            encoded.push_back(digit);
        }
    }

    return "https://ishikawapuyo.net/simu/pe.html?" + encoded;
}

static void appendSuccessfulUrl(const std::string& url) {
    constexpr const char* OUTPUT_PATH = "19chain_urls.txt";
    std::ofstream output(OUTPUT_PATH, std::ios::app);
    if (!output) {
        throw std::runtime_error(std::string("cannot open output file: ") + OUTPUT_PATH);
    }
    output << url << '\n';
    output.flush();
    if (!output) {
        throw std::runtime_error(std::string("failed writing output file: ") + OUTPUT_PATH);
    }
}

static void printSolution(const Solution& solution, uint64_t seed) {
    std::cout << "seed=" << seed << "\n";
    std::cout << "chains=" << chainCount(solution.field) << "\n";
    int puyos=0;for (const auto& c:solution.field.col) puyos+=int(c.size());
    std::cout << "puyos=" << puyos << ", extra_puyos=" << puyos-4*solution.targetChain << "\n";
    std::cout << "field (top row first; columns left to right):\n";

    for (int y = H - 1; y >= 0; --y) {
        for (int x = 0; x < W; ++x) {
            const Cell value = y < static_cast<int>(solution.field.col[x].size())
                ? solution.field.col[x][y] : 0;
            std::cout << colorChar(value) << ' ';
        }
        std::cout << '\n';
    }

    std::cout << "URL: " << fieldToUrl(solution.field) << "\n";

    std::cout << "trigger cells (x from left, y from bottom, 1-based): ";
    for (const Point p : solution.trigger) {
        std::cout << '(' << p.first + 1 << ',' << p.second + 1 << ") ";
    }
    std::cout << "\n";

    std::cout << "pair placements from empty board; setup_clear marks preparation; last placement fires:\n";
    for (size_t i = 0; i < solution.placementSequence.size(); ++i) {
        const Domino& d = solution.placementSequence[i];
        std::cout << (i + 1) << ": " << d.orientation << ' '
                  << colorChar(d.colorA) << colorChar(d.colorB)
                  << " at (" << d.a.first + 1 << ',' << d.a.second + 1
                  << ") and (" << d.b.first + 1 << ',' << d.b.second + 1
                  << ") axis/child; controls=" << d.controls;
        if (d.setupClear) std::cout << "; setup_clear=" << d.setupClear;
        std::cout << "\n";
    }
}

struct GeneratorConfig {
    uint64_t initialSeed = 0;
    int colorCount = 0;
    int beamWidth = 0;
    int restarts = 0;
    int candidatesPerParent = 0;
    int targetSuccessCount = 0;
    int maxExtraPuyos = 0;
    int minExtraPuyos = 0;
    int targetChain = DEFAULT_TARGET_CHAIN;
};

static std::string trim(std::string value) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    const auto first = std::find_if(value.begin(), value.end(), notSpace);
    if (first == value.end()) return {};
    const auto last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return std::string(first, last);
}

static std::unordered_set<std::string> loadExistingUrls(const std::string& path) {
    std::unordered_set<std::string> urls;
    std::ifstream input(path);
    if (!input) return urls; // 初回実行時は出力ファイルがまだない。

    const std::string urlPrefix = "https://ishikawapuyo.net/simu/pe.html?";
    std::string line;
    while (std::getline(input, line)) {
        line = trim(std::move(line));
        if (line.compare(0, urlPrefix.size(), urlPrefix) == 0) {
            urls.insert(std::move(line));
        }
    }
    return urls;
}

static uint64_t parseSeed(const std::string& text) {
    if (text.empty() || text.front() == '-') {
        throw std::runtime_error("initial_seed must be an unsigned 64-bit integer");
    }
    size_t used = 0;
    const unsigned long long value = std::stoull(text, &used, 10);
    if (used != text.size()) {
        throw std::runtime_error("invalid trailing characters in initial_seed");
    }
    return static_cast<uint64_t>(value);
}

static int parsePositiveInt(const std::string& text, const std::string& key) {
    size_t used = 0;
    const long long value = std::stoll(text, &used, 10);
    if (used != text.size() || value < 1 || value > std::numeric_limits<int>::max()) {
        throw std::runtime_error(key + " must be a positive integer");
    }
    return static_cast<int>(value);
}

static GeneratorConfig loadConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open config file: " + path);
    }

    GeneratorConfig config;
    std::unordered_set<std::string> seen;
    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line)) {
        ++lineNumber;

        // UTF-8 BOM付きのconfig.iniも読み込めるようにする。
        if (lineNumber == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }

        const size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos) line.erase(comment);
        line = trim(std::move(line));
        if (line.empty()) continue;

        // セクション見出しは読み飛ばす。キー名はファイル全体で一意にする。
        if (line.front() == '[' && line.back() == ']') continue;

        const size_t equal = line.find('=');
        if (equal == std::string::npos) {
            throw std::runtime_error(
                "expected key=value at config.ini line " +
                std::to_string(lineNumber));
        }

        const std::string key = trim(line.substr(0, equal));
        const std::string value = trim(line.substr(equal + 1));
        if (key.empty() || value.empty()) {
            throw std::runtime_error(
                "empty key or value at config.ini line " +
                std::to_string(lineNumber));
        }
        if (!seen.insert(key).second) {
            throw std::runtime_error("duplicate config key: " + key);
        }

        if (key == "initial_seed") {
            config.initialSeed = parseSeed(value);
        } else if (key == "color_count") {
            config.colorCount = parsePositiveInt(value, key);
        } else if (key == "beam_width") {
            config.beamWidth = parsePositiveInt(value, key);
        } else if (key == "restarts") {
            config.restarts = parsePositiveInt(value, key);
        } else if (key == "candidates_per_parent") {
            config.candidatesPerParent = parsePositiveInt(value, key);
        } else if (key == "max_extra_puyos") {
            if (value=="0") config.maxExtraPuyos=0;
            else config.maxExtraPuyos=parsePositiveInt(value,key);
        } else if (key == "target_chain") {
            config.targetChain=parsePositiveInt(value,key);
        } else if (key == "min_extra_puyos") {
            if (value=="0") config.minExtraPuyos=0;
            else config.minExtraPuyos=parsePositiveInt(value,key);
        } else if (key == "target_success_count") {
            config.targetSuccessCount = parsePositiveInt(value, key);
        } else {
            throw std::runtime_error("unknown config key: " + key);
        }
    }

    const std::array<std::string, 6> required{{
        "initial_seed", "color_count", "beam_width",
        "restarts", "candidates_per_parent", "target_success_count"
    }};
    for (const std::string& key : required) {
        if (!seen.contains(key)) {
            throw std::runtime_error("missing config key: " + key);
        }
    }

    if (config.colorCount != 4 && config.colorCount != 5) {
        throw std::runtime_error("color_count must be 4 or 5");
    }
    if (config.targetChain<MIN_TARGET_CHAIN || config.targetChain>MAX_TARGET_CHAIN)
        throw std::runtime_error("target_chain must be between 1 and 19");
    if (config.maxExtraPuyos>W*H-4*config.targetChain)
        throw std::runtime_error("max_extra_puyos exceeds 78 - 4 * target_chain");
    if (config.minExtraPuyos>config.maxExtraPuyos)
        throw std::runtime_error("min_extra_puyos must not exceed max_extra_puyos");
    return config;
}

int main(int argc, char* argv[]) {
    try {
        const std::string configPath = argc >= 2 ? argv[1] : "config.ini";
        const GeneratorConfig config = loadConfig(configPath);
        const int targetChain=config.targetChain;

        std::cout << "seed=" << config.initialSeed
                  << ", colors=" << config.colorCount
                  << ", beam_width=" << config.beamWidth
                  << ", restarts=" << config.restarts
                  << ", candidates_per_parent="
                  << config.candidatesPerParent
                  << ", target_success_count="
                  << config.targetSuccessCount
                  << ", max_extra_puyos=" << config.maxExtraPuyos
                  << ", min_extra_puyos=" << config.minExtraPuyos
                  << ", target_chain=" << targetChain << "\n";

        std::mt19937_64 rng(config.initialSeed);
        const std::vector<Shape> shapes = makeTetrominoShapes();
        constexpr const char* OUTPUT_PATH = "19chain_urls.txt";
        std::unordered_set<std::string> seenUrls = loadExistingUrls(OUTPUT_PATH);
        int successCount = 0;
        int duplicateCount = 0;
        int attemptsUsed = 0;

        for (; attemptsUsed < config.restarts &&
               successCount < config.targetSuccessCount;
             ++attemptsUsed) {
            const int attemptNumber = attemptsUsed + 1;
            reportProgress(targetChain, attemptNumber, config.restarts, 0, 0, 0,
                           1, 0, 0, "空盤面から開始", true);
            auto result = generateOne(
                config.colorCount,
                config.beamWidth,
                config.candidatesPerParent,
                rng,
                shapes,
                attemptNumber,
                config.restarts,
                config.maxExtraPuyos,
                targetChain,
                config.minExtraPuyos);

            std::cout << '\n';

            if (result) {
                const std::string url = fieldToUrl(result->field);
                if (!seenUrls.insert(url).second) {
                    ++duplicateCount;
                    std::cout << "既存または今回の生成結果と重複したため保存をスキップします。\n";
                    continue;
                }

                appendSuccessfulUrl(url);
                ++successCount;
                printSolution(*result, config.initialSeed);
                std::cout << "URLを19chain_urls.txtに追記しました。\n"
                          << "今回のユニークな生成数: " << successCount
                          << '/' << config.targetSuccessCount << "\n";
            } else {
                std::cout << "再試行 " << attemptNumber << '/' << config.restarts
                          << " は解を見つけられませんでした。\n";
            }
        }

        if (successCount == config.targetSuccessCount) {
            std::cout << "目標数 " << config.targetSuccessCount
                      << " 件のユニークな" << targetChain << "連鎖盤面を生成しました。\n"
                      << "試行数: " << attemptsUsed
                      << ", 重複スキップ数: " << duplicateCount << "\n";
            return 0;
        }

        std::cerr << "試行上限に到達しました。ユニークな生成数: "
                  << successCount << '/' << config.targetSuccessCount
                  << ", 試行数: " << attemptsUsed
                  << ", 重複スキップ数: " << duplicateCount << "\n"
                  << "config.iniのrestartsを増やすか、initial_seedを変更してください。\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Configuration or generation error: "
                  << e.what() << "\n";
        return 2;
    }
}
