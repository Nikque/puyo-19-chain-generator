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
constexpr int TARGET_CHAIN = 19;
constexpr int DEFAULT_COLORS = 5;

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
};

struct Solution {
    Field field;
    std::array<Point, 4> trigger;
    std::vector<Domino> placementSequence;
};

static bool containsPoint(const std::array<Point, 4>& points, Point p) {
    return std::find(points.begin(), points.end(), p) != points.end();
}

struct WaveInfo {
    // この波で消える「連結成分」ごとの座標。4個以外も記録して検査する。
    std::vector<std::vector<Point>> groups;
};

static int clearAndDrop(Field& field, WaveInfo* waveOut = nullptr) {
    std::array<std::array<bool, H>, W> seen{};
    std::array<std::array<bool, H>, W> remove{};
    WaveInfo wave;

    const std::array<Point, 4> dirs{{
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}
    }};

    for (int x = 0; x < W; ++x) {
        const int limitY = std::min(CLEAR_H, static_cast<int>(field.col[x].size()));

        for (int y = 0; y < limitY; ++y) {
            const Cell color = field.col[x][y];
            if (color == 0 || seen[x][y]) continue;

            std::vector<Point> component;
            std::vector<Point> stack{{x, y}};
            seen[x][y] = true;

            while (!stack.empty()) {
                const Point p = stack.back();
                stack.pop_back();
                component.push_back(p);

                for (const Point d : dirs) {
                    const int nx = p.first + d.first;
                    const int ny = p.second + d.second;
                    if (nx < 0 || nx >= W || ny < 0 || ny >= CLEAR_H) continue;
                    if (ny >= static_cast<int>(field.col[nx].size())) continue;
                    if (seen[nx][ny] || field.col[nx][ny] != color) continue;
                    seen[nx][ny] = true;
                    stack.emplace_back(nx, ny);
                }
            }

            if (component.size() >= 4) {
                std::sort(component.begin(), component.end());
                wave.groups.push_back(component);
                for (const Point p : component) remove[p.first][p.second] = true;
            }
        }
    }

    int removedCount = 0;
    for (const auto& group : wave.groups) {
        removedCount += static_cast<int>(group.size());
    }
    if (waveOut) *waveOut = wave;
    if (removedCount == 0) return 0;

    // すべての消去を同時に適用し、13段すべてを列ごとに落下させる。
    for (int x = 0; x < W; ++x) {
        std::vector<Cell> compact;
        compact.reserve(field.col[x].size());
        for (int y = 0; y < static_cast<int>(field.col[x].size()); ++y) {
            if (!remove[x][y]) compact.push_back(field.col[x][y]);
        }
        field.col[x] = std::move(compact);
    }

    return removedCount;
}

static int chainCount(Field field) {
    int chains = 0;
    while (clearAndDrop(field) > 0) ++chains;
    return chains;
}

static bool cleanChain19(
    Field field,
    const std::array<Point, 4>& expectedTrigger,
    std::vector<WaveInfo>* traceOut = nullptr)
{
    int waves = 0;
    while (true) {
        WaveInfo wave;
        const int removed = clearAndDrop(field, &wave);
        if (removed == 0) break;

        // 19連鎖の各波は、同時消しなし・4個ちょうどの単独グループ。
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
        if (waves > TARGET_CHAIN) return false;
    }

    return waves == TARGET_CHAIN;
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

static std::vector<Candidate> predecessors(
    const Field& post,
    const std::vector<Shape>& shapes,
    int colorCount)
{
    std::vector<Candidate> result;
    std::unordered_set<std::string> seenFields;

    for (const Shape& shape : shapes) {
        int maxX = 0;
        int maxY = 0;
        for (const Point p : shape) {
            maxX = std::max(maxX, p.first);
            maxY = std::max(maxY, p.second);
        }
        const int shapeW = maxX + 1;

        // 各相対列に入るテトロミノぷよの相対y座標。
        std::array<std::vector<int>, W> groupYs{};
        for (const Point p : shape) groupYs[p.first].push_back(p.second);

        bool columnsAreContiguous = true;
        for (int dx = 0; dx < shapeW; ++dx) {
            auto& ys = groupYs[dx];
            if (ys.empty()) continue;
            std::sort(ys.begin(), ys.end());
            for (size_t i = 1; i < ys.size(); ++i) {
                if (ys[i] != ys[i - 1] + 1) columnsAreContiguous = false;
            }
        }
        if (!columnsAreContiguous) continue;

        for (int x0 = 0; x0 + shapeW <= W; ++x0) {
            for (int y0 = 0; y0 + maxY < CLEAR_H; ++y0) {
                std::array<int, W> slots{};
                std::array<int, W> counts{};
                bool valid = true;

                for (int dx = 0; dx < shapeW; ++dx) {
                    const auto& ys = groupYs[dx];
                    if (ys.empty()) continue;

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
                if (!valid) continue;

                for (int color = 1; color <= colorCount; ++color) {
                    Field pre;

                    for (int x = 0; x < W; ++x) {
                        if (counts[x] == 0) {
                            pre.col[x] = post.col[x];
                            continue;
                        }

                        const int slot = slots[x];
                        pre.col[x].insert(pre.col[x].end(),
                            post.col[x].begin(), post.col[x].begin() + slot);
                        for (int n = 0; n < counts[x]; ++n) {
                            pre.col[x].push_back(static_cast<Cell>(color));
                        }
                        pre.col[x].insert(pre.col[x].end(),
                            post.col[x].begin() + slot, post.col[x].end());
                    }

                    std::array<Point, 4> trigger{};
                    for (size_t i = 0; i < shape.size(); ++i) {
                        trigger[i] = {x0 + shape[i].first, y0 + shape[i].second};
                    }

                    // 逆操作の確認: 1波でちょうどこの4個が消え、postに戻ること。
                    Field after = pre;
                    WaveInfo wave;
                    if (clearAndDrop(after, &wave) != 4 ||
                        wave.groups.size() != 1 ||
                        wave.groups.front().size() != 4) continue;

                    const std::vector<Point>& removed = wave.groups.front();
                    std::vector<Point> expected(trigger.begin(), trigger.end());
                    std::sort(expected.begin(), expected.end());
                    if (removed != expected || !(after == post)) continue;

                    const std::string key = pre.key();
                    if (seenFields.insert(key).second) {
                        result.push_back(Candidate{std::move(pre), trigger});
                    }
                }
            }
        }
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

    // 左から3列目、下から12・13段目は、発火グループ以外では空ける。
    const std::array<Point, 2> forbidden{{{2, 11}, {2, 12}}};
    for (const Point p : forbidden) {
        if (p.second < static_cast<int>(field.col[p.first].size()) &&
            !containsPoint(trigger, p)) {
            return false;
        }
    }

    return true;
}

static bool dominoTouchesTrigger(
    const Domino& domino,
    const std::array<Point, 4>& trigger)
{
    return containsPoint(trigger, domino.a) || containsPoint(trigger, domino.b);
}

static void shuffleDominoes(std::vector<Domino>& values, std::mt19937_64& rng) {
    std::shuffle(values.begin(), values.end(), rng);
}

// 完成盤面を上からペア単位で剥がし、空盤面からの合法なペア配置順を探す。
// 最初に剥がすペア=実際に最後に置くペアとし、発火グループに触れることを要求する。
static bool peelToBuildSequence(
    const Field& field,
    const std::array<Point, 4>& trigger,
    bool firstPeel,
    std::mt19937_64& rng,
    int& nodeBudget,
    std::unordered_set<std::string>& dead,
    std::vector<Domino>& sequence)
{
    if (field.empty()) {
        sequence.clear();
        return true;
    }
    if (--nodeBudget < 0) return false;

    const std::string key = field.key();
    if (!firstPeel && dead.contains(key)) return false;

    std::vector<Domino> options;

    // 縦置き: 同じ列の一番上の2個。
    for (int x = 0; x < W; ++x) {
        const int h = static_cast<int>(field.col[x].size());
        if (h >= 2) {
            options.push_back(Domino{
                {x, h - 2}, {x, h - 1},
                field.col[x][h - 2], field.col[x][h - 1], 'V'
            });
        }
    }

    // 横置き: 高さが同じ隣接2列の一番上を1個ずつ。
    for (int x = 0; x + 1 < W; ++x) {
        const int h1 = static_cast<int>(field.col[x].size());
        const int h2 = static_cast<int>(field.col[x + 1].size());
        if (h1 > 0 && h1 == h2) {
            options.push_back(Domino{
                {x, h1 - 1}, {x + 1, h2 - 1},
                field.col[x][h1 - 1], field.col[x + 1][h2 - 1], 'H'
            });
        }
    }

    shuffleDominoes(options, rng);

    for (const Domino& d : options) {
        if (firstPeel && !dominoTouchesTrigger(d, trigger)) continue;

        Field lower = field;
        if (d.orientation == 'V') {
            lower.col[d.a.first].pop_back();
            lower.col[d.a.first].pop_back();
        } else {
            lower.col[d.a.first].pop_back();
            lower.col[d.b.first].pop_back();
        }

        std::vector<Domino> lowerSequence;
        if (peelToBuildSequence(lower, trigger, false, rng,
                                nodeBudget, dead, lowerSequence)) {
            // 下のペアを先に置き、このペアを最後に置く。
            sequence = std::move(lowerSequence);
            sequence.push_back(d);
            return true;
        }
    }

    if (!firstPeel) dead.insert(key);
    return false;
}

static void reportProgress(
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
           << " | 段階 " << depth << '/' << TARGET_CHAIN
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
    int totalAttempts)
{
    std::vector<Field> beam(1); // 完全な空盤面から開始。

    for (int depth = 1; depth <= TARGET_CHAIN; ++depth) {
        std::vector<Field> nextBeam;
        std::unordered_set<std::string> seenNext;

        reportProgress(attempt, totalAttempts, depth, 0, beam.size(),
                       beam.size(), 0, 0,
                       depth < TARGET_CHAIN ? "盤面を展開中" : "19連鎖を検証中",
                       true);

        for (size_t parentIndex = 0; parentIndex < beam.size(); ++parentIndex) {
            const Field& post = beam[parentIndex];
            reportProgress(attempt, totalAttempts, depth,
                           parentIndex + 1, beam.size(), beam.size(),
                           nextBeam.size(), 0,
                           depth < TARGET_CHAIN ? "盤面を展開中" : "19連鎖を検証中",
                           parentIndex == 0 || parentIndex % 16 == 0);
            std::vector<Candidate> prev = predecessors(post, shapes, colorCount);
            std::shuffle(prev.begin(), prev.end(), rng);

            if (depth < TARGET_CHAIN) {
                const size_t take = std::min<size_t>(perParent, prev.size());
                for (size_t i = 0; i < take; ++i) {
                    const std::string key = prev[i].field.key();
                    if (seenNext.insert(key).second) {
                        nextBeam.push_back(std::move(prev[i].field));
                    }
                }
                reportProgress(attempt, totalAttempts, depth,
                               parentIndex + 1, beam.size(), beam.size(),
                               nextBeam.size(), 0, "盤面を展開中");
                continue;
            }

            // 19段目で初めて発火位置・発火可能性・実配置順を確認する。
            for (size_t candidateIndex = 0;
                 candidateIndex < prev.size(); ++candidateIndex) {
                reportProgress(attempt, totalAttempts, depth,
                               parentIndex + 1, beam.size(), beam.size(),
                               candidateIndex + 1, prev.size(), "候補を検査中");
                Candidate& candidate = prev[candidateIndex];
                if (!legalTrigger(candidate.field, candidate.trigger)) continue;
                if (!cleanChain19(candidate.field, candidate.trigger)) continue;

                std::vector<Domino> placementSequence;
                std::unordered_set<std::string> dead;
                int pairSearchBudget = 200000;

                if (!peelToBuildSequence(
                        candidate.field, candidate.trigger, true, rng,
                        pairSearchBudget, dead, placementSequence)) {
                    continue;
                }

                return Solution{
                    std::move(candidate.field),
                    candidate.trigger,
                    std::move(placementSequence)
                };
            }
            reportProgress(attempt, totalAttempts, depth,
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

    std::cout << "pair placements from empty board; last placement fires:\n";
    for (size_t i = 0; i < solution.placementSequence.size(); ++i) {
        const Domino& d = solution.placementSequence[i];
        std::cout << (i + 1) << ": " << d.orientation << ' '
                  << colorChar(d.colorA) << colorChar(d.colorB)
                  << " at (" << d.a.first + 1 << ',' << d.a.second + 1
                  << ") and (" << d.b.first + 1 << ',' << d.b.second + 1
                  << ")\n";
    }
}

struct GeneratorConfig {
    uint64_t initialSeed = 0;
    int colorCount = 0;
    int beamWidth = 0;
    int restarts = 0;
    int candidatesPerParent = 0;
    int targetSuccessCount = 0;
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
    return config;
}

int main(int argc, char* argv[]) {
    try {
        const std::string configPath = argc >= 2 ? argv[1] : "config.ini";
        const GeneratorConfig config = loadConfig(configPath);

        std::cout << "seed=" << config.initialSeed
                  << ", colors=" << config.colorCount
                  << ", beam_width=" << config.beamWidth
                  << ", restarts=" << config.restarts
                  << ", candidates_per_parent="
                  << config.candidatesPerParent
                  << ", target_success_count="
                  << config.targetSuccessCount << "\n";

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
            reportProgress(attemptNumber, config.restarts, 0, 0, 0,
                           1, 0, 0, "空盤面から開始", true);
            auto result = generateOne(
                config.colorCount,
                config.beamWidth,
                config.candidatesPerParent,
                rng,
                shapes,
                attemptNumber,
                config.restarts);

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
                      << " 件のユニークな19連鎖盤面を生成しました。\n"
                      << "試行数: " << attemptsUsed
                      << ", 重複スキップ数: " << duplicateCount << "\n";
            return 0;
        }

        std::cerr << "試行上限に到達しました。ユニークな生成数: "
                  << successCount << '/' << config.targetSuccessCount
                  << ", 試行数: " << attemptsUsed
                  << ", 重複スキップ数: " << duplicateCount << "\n"
                  << "config.iniのrestarts、beam_width、candidates_per_parentを"
                  << "増やすか、initial_seedを変更してください。\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Configuration or generation error: "
                  << e.what() << "\n";
        return 2;
    }
}


