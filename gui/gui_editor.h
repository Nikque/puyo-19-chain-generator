#pragma once
#include "gui_model.h"
#include <algorithm>

namespace puyo_gui {
// The editor allows empty cells, floating cells and kinds the generator never
// produces. Kinds 1..6 are the engine's codes; 7..10 exist only here.
enum Piece : Cell { Empty = 0, Red = 1, Green = 2, Blue = 3, Yellow = 4, Purple = 5,
                    Garbage = 6, PointPuyo = 7, Hard = 8, Iron = 9, Wall = 10 };
inline constexpr std::array<const char*, 11> pieceNames{
    "消す（空）", "赤 R", "緑 G", "青 B", "黄 Y", "紫 P", "おじゃま ○", "得点 $", "かた H", "鉄 I", "壁 #"};
inline constexpr wchar_t pieceLetters[] = L" RGBYP○$HI#";

// Gravity with fixed walls: each stretch between walls is compacted on its own.
inline bool settle(Board& b) {
    const Board before = b;
    for (int x = 0; x < 6; ++x) {
        int dst = 0;
        for (int y = 0; y < 13; ++y) {
            const Cell value = b.cells[x][y];
            if (value == Wall) {
                dst = y + 1;
                continue;
            }
            if (value) {
                b.cells[x][y] = Empty;
                b.cells[x][dst++] = value;
            }
        }
    }
    return b != before;
}

struct EditorWave {
    std::vector<Point> erased, cracked;
    int score = 0, colored = 0;
};
// One clearing wave without gravity. Which coloured puyos clear (and the
// 13th-row rule, groups and colours for the score) comes from the engine; the
// effect on the special kinds next to them is applied here.
inline EditorWave editorClear(Board& b, int chain) {
    BitField colors;
    for (int x = 0; x < 6; ++x)
        for (int y = 0; y < 13; ++y)
            if (b.cells[x][y] >= Red && b.cells[x][y] <= Purple) colors.set(x, y, b.cells[x][y]);
    EditorWave wave;
    Wave w;
    if (!findWave(colors, w)) return wave;
    wave.colored = w.puyos;
    // Cells touched by cleared puyos from one direction or more, and from two or more.
    const FieldBits a = w.cleared.up(), c = w.cleared.down(), d = w.cleared.left(), e = w.cleared.right();
    uint16_t once[6], twice[6], cleared[6];
    ((a | c | d | e) & MASK_12).columns(once);
    (((a & c) | (a & d) | (a & e) | (c & d) | (c & e) | (d & e)) & MASK_12).columns(twice);
    w.cleared.columns(cleared);
    int base = 10 * wave.colored, points = 0;
    for (int x = 0; x < 6; ++x)
        for (int y = 0; y < 12; ++y) {
            Cell& cell = b.cells[x][y];
            bool erase = cleared[x] >> y & 1;
            if (once[x] >> y & 1) {
                if (cell == Garbage || cell == PointPuyo) {
                    erase = true;
                    if (cell == PointPuyo) points += 50;
                } else if (cell == Hard) {
                    if (twice[x] >> y & 1) {
                        erase = true;
                        base += 60;
                    } else {
                        cell = Garbage;
                        wave.cracked.push_back({x, y});
                        base += 10;
                    }
                }
            }
            if (erase) {
                wave.erased.push_back({x, y});
                cell = Empty;
            }
        }
    wave.score = base * std::max(1, CHAIN_BONUS[std::clamp(chain, 1, 19)] + COLOR_BONUS[w.colors] + w.groupBonus) + points;
    return wave;
}
inline std::vector<Frame> editorTimeline(Board b) {
    std::vector<Frame> frames{{b, {}, {}, "編集盤面（開始前）"}};
    if (settle(b)) frames.push_back({b, {}, {}, "初期落下後"});
    int score = 0;
    for (int n = 1; n <= 19; ++n) {
        const EditorWave wave = editorClear(b, n);
        if (!wave.colored) break;
        score += wave.score;
        frames.push_back({b, {}, {}, std::to_string(n) + "連鎖: 消去・変化後", n, score});
        settle(b);
        frames.push_back({b, {}, {}, std::to_string(n) + "連鎖: 落下後", n, score});
    }
    frames.back().label += " / シミュレーション終了";
    return frames;
}

struct Editor {
    Board board{};
    std::vector<Board> undo, redo;
    void replace(const Board& value) {
        if (board == value) return;
        undo.push_back(board);
        if (undo.size() > 256) undo.erase(undo.begin());
        redo.clear();
        board = value;
    }
    void back() {
        if (undo.empty()) return;
        redo.push_back(board);
        board = undo.back();
        undo.pop_back();
    }
    void forward() {
        if (redo.empty()) return;
        undo.push_back(board);
        board = redo.back();
        redo.pop_back();
    }
};
inline void saveBoard(const std::filesystem::path& path, const Board& b) {
    std::ofstream out(path, std::ios::binary);
    out << "PUYO_BOARD 1\n";
    for (int y = 12; y >= 0; --y)
        for (int x = 0; x < 6; ++x) out << int(b.cells[x][y]) << (x == 5 ? '\n' : ' ');
    out.flush();
    if (!out) throw std::runtime_error("編集盤面を保存できません。");
}
inline Board loadBoard(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 4096) throw std::runtime_error("盤面ファイルが大きすぎます。");
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int version;
    if (!(in >> magic >> version) || magic != "PUYO_BOARD" || version != 1)
        throw std::runtime_error("対応する編集盤面形式ではありません。");
    Board b;
    for (int y = 12; y >= 0; --y)
        for (int x = 0; x < 6; ++x) {
            int v;
            if (!(in >> v) || v < 0 || v > 10) throw std::runtime_error("不正なぷよ種別です。");
            b.cells[x][y] = Cell(v);
        }
    std::string extra;
    if (in >> extra) throw std::runtime_error("盤面ファイルに余分なデータがあります。");
    return b;
}
} // namespace puyo_gui
