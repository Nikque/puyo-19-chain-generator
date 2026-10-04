#pragma once
// Data shared by the GUI, its tests and the result files: settings text,
// playback frames and the versioned .puyo archive. All game logic comes from
// the engine (core/ and generator/); nothing is re-implemented for display.
#include "../generator/generator.h"
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace puyo_gui {
using namespace puyo;
using Point = std::pair<int, int>; // x from the left 0..5, y from the bottom 0..12

// A plain grid for drawing, editing and files. Unlike the engine's bit planes
// it can also hold the editor-only piece kinds (see gui_editor.h).
struct Board {
    std::array<std::array<Cell, 13>, 6> cells{}; // [x][bottom-up y]
    bool operator==(const Board&) const = default;
};
inline Board boardFromBits(const BitField& f) {
    Board b;
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y) b.cells[x][y] = f.get(x, y);
    return b;
}
// Only the kinds the engine knows (colours and garbage); the rest stay empty.
inline BitField bitsFromBoard(const Board& b) {
    BitField f;
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y)
            if (b.cells[x][y] >= 1 && b.cells[x][y] <= GARBAGE) f.set(x, y, b.cells[x][y]);
    return f;
}
inline std::vector<Point> pointsOf(FieldBits bits) {
    std::vector<Point> points;
    forEachCell(bits, [&](int x, int y) { points.emplace_back(x, y); });
    return points;
}

inline GeneratorConfig defaults() {
    GeneratorConfig c;
    c.initialSeed = 20261004;
    c.colorCount = 4;
    c.beamWidth = 48;
    c.restarts = 100000;
    c.candidatesPerParent = 2;
    c.targetSuccessCount = 100;
    c.targetChain = 19;
    return c;
}
inline std::string configText(const GeneratorConfig& c) {
    std::ostringstream o;
    o << "[Generator]\n"
      << "target_chain = " << c.targetChain << '\n'
      << "target_success_count = " << c.targetSuccessCount << '\n'
      << "initial_seed = " << c.initialSeed << '\n'
      << "color_count = " << c.colorCount << '\n'
      << "beam_width = " << c.beamWidth << '\n'
      << "candidates_per_parent = " << c.candidatesPerParent << '\n'
      << "restarts = " << c.restarts << '\n'
      << "min_extra_puyos = " << c.minExtraPuyos << '\n'
      << "max_extra_puyos = " << c.maxExtraPuyos << '\n'
      << "threads = " << c.threads << '\n';
    return o.str();
}
inline void writeConfig(const std::filesystem::path& path, const GeneratorConfig& c) {
    std::ofstream f(path, std::ios::binary);
    f << configText(c);
    f.flush();
    if (!f) throw std::runtime_error("設定を保存できません: " + pathToUtf8(path));
}
constexpr int INPUT_COUNT = 10;
// No floating point conversion, including values above 2^53.
inline GeneratorConfig parseInputs(const std::array<std::string, INPUT_COUNT>& t) {
    for (const auto& s : t)
        if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("設定は半角の10進整数で入力してください。");
    const auto orZero = [](const std::string& s, const char* key) { return s == "0" ? 0 : parsePositiveInt(s, key); };
    GeneratorConfig c;
    c.targetChain = parsePositiveInt(t[0], "target_chain");
    c.targetSuccessCount = parsePositiveInt(t[1], "target_success_count");
    c.initialSeed = parseSeed(t[2]);
    c.colorCount = parsePositiveInt(t[3], "color_count");
    c.minExtraPuyos = orZero(t[4], "min_extra_puyos");
    c.maxExtraPuyos = orZero(t[5], "max_extra_puyos");
    c.beamWidth = parsePositiveInt(t[6], "beam_width");
    c.candidatesPerParent = parsePositiveInt(t[7], "candidates_per_parent");
    c.restarts = parsePositiveInt(t[8], "restarts");
    c.threads = orZero(t[9], "threads");
    if (c.targetChain < 1 || c.targetChain > 19) throw std::runtime_error("連鎖数は1〜19です。");
    if (c.colorCount != 4 && c.colorCount != 5) throw std::runtime_error("色数は4または5です。");
    if (c.minExtraPuyos > c.maxExtraPuyos || c.maxExtraPuyos > 78 - 4 * c.targetChain)
        throw std::runtime_error("余剰は 0 ≤ 下限 ≤ 上限 ≤ 78−4×連鎖数 です。");
    if (c.threads > 256) throw std::runtime_error("スレッド数は0（自動）〜256です。");
    return c;
}

struct Frame {
    Board board;
    std::vector<Point> marked; // white rings: the puyos that clear first
    std::vector<Point> pair;   // yellow rings: a last pair that fires the chain
    std::string label;
    int chain = 0;
    int score = 0;
};
// Frame 0 is the board right before it fires; then, for every wave, the board
// with the cleared puyos removed (survivors keep their height) and the board
// after gravity.
inline std::vector<Frame> timeline(const Solution& s) {
    const std::string problem = verifySolution(s);
    if (!problem.empty()) throw std::runtime_error("盤面の検証に失敗しました: " + problem);
    std::vector<Frame> frames;
    Frame first{boardFromBits(s.field), pointsOf(s.trigger), {}, "発火直前: " + std::to_string(s.targetChain) + "連鎖"};
    for (int id = 0; id < PAIR_POSITIONS; ++id)
        if (s.firePairs >> id & 1) {
            first.pair = pointsOf(pairCells(s.field, id));
            break;
        }
    frames.push_back(std::move(first));
    BitField f = s.field;
    int score = 0;
    Wave w;
    for (int n = 1; findWave(f, w); ++n) {
        score += waveScore(w, n);
        f.erase(w.cleared | w.garbage);
        frames.push_back({boardFromBits(f), {}, {}, std::to_string(n) + "連鎖目: 消去後・落下前", n, score});
        f.drop();
        frames.push_back({boardFromBits(f), {}, {}, std::to_string(n) + "連鎖目: 落下後", n, score});
    }
    frames.back().label = f.empty() ? "連鎖終了: 全消し" : "連鎖終了: 余剰が残る";
    return frames;
}
// The frame shown when a result is selected ("発火へ").
inline size_t ignitionFrame(const Solution&) { return 0; }

struct Record {
    Solution solution;
    uint64_t seed = 0;
    std::filesystem::path path;
};
// Versioned, bounded archive. Version 2: board, trigger and last pairs.
// Version 1 (v0.3.0) also held a build sequence, which is read and ignored.
inline void saveRecord(const std::filesystem::path& path, const Solution& s, uint64_t seed) {
    std::ofstream o(path, std::ios::binary);
    o << "PUYO_GUI 2\n" << seed << ' ' << s.targetChain << '\n';
    int h[W];
    s.field.heights(h);
    for (int x = 0; x < W; ++x) {
        o << h[x];
        for (int y = 0; y < h[x]; ++y) o << ' ' << int(s.field.get(x, y));
        o << '\n';
    }
    for (Point p : pointsOf(s.trigger)) o << p.first << ' ' << p.second << '\n';
    o << s.firePairs << '\n';
    o.flush();
    if (!o) throw std::runtime_error("結果を保存できません: " + pathToUtf8(path));
}
inline Record loadRecord(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 65536) throw std::runtime_error("結果ファイルが大きすぎます。");
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int version;
    Record r;
    r.path = path;
    if (!(in >> magic >> version) || magic != "PUYO_GUI" || (version != 1 && version != 2))
        throw std::runtime_error("対応する.puyo形式ではありません。");
    std::string seed;
    in >> seed >> r.solution.targetChain;
    r.seed = parseSeed(seed);
    if (r.solution.targetChain < 1 || r.solution.targetChain > 19) throw std::runtime_error("不正な連鎖数です。");
    for (int x = 0; x < W; ++x) {
        int n;
        if (!(in >> n) || n < 0 || n > 13) throw std::runtime_error("不正な盤面です。");
        for (int y = 0; y < n; ++y) {
            int v;
            if (!(in >> v) || v < 1 || v > 5) throw std::runtime_error("不正な色です。");
            r.solution.field.set(x, y, Cell(v));
        }
    }
    const auto point = [&](Point& p) {
        if (!(in >> p.first >> p.second) || p.first < 0 || p.first >= 6 || p.second < 0 || p.second >= 13)
            throw std::runtime_error("不正な座標です。");
    };
    for (int i = 0; i < 4; ++i) {
        Point p;
        point(p);
        r.solution.trigger |= FieldBits::cell(p.first, p.second);
    }
    if (version == 2) {
        if (!(in >> r.solution.firePairs) || !r.solution.firePairs || r.solution.firePairs >> PAIR_POSITIONS)
            throw std::runtime_error("不正な最後の1組です。");
    } else {
        int count;
        if (!(in >> count) || count < 2 || count > 41) throw std::runtime_error("不正な組数です。");
        for (int i = 0; i < count; ++i) {
            Point a, b;
            int colorA, colorB, setupClear;
            char orientation;
            std::string controls;
            point(a);
            point(b);
            if (!(in >> colorA >> colorB >> orientation >> setupClear >> std::quoted(controls)) || controls.size() > 200)
                throw std::runtime_error("不正な構築手順です。");
        }
        r.solution.firePairs = firePairsByDefinition(r.solution.field);
        if (!r.solution.firePairs) throw std::runtime_error("この盤面には、発火できる最後の1組がありません（現在の判定規則）。");
    }
    std::string tail;
    if (in >> tail) throw std::runtime_error("結果ファイルに余分なデータがあります。");
    const std::string problem = verifySolution(r.solution);
    if (!problem.empty()) throw std::runtime_error("結果ファイルのエンジン検証に失敗しました: " + problem);
    return r;
}
} // namespace puyo_gui
