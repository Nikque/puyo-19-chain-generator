#pragma once
// Where can the falling pair be put, given only the column heights?
//
// A pair position is a set of two cells, independent of which puyo is the
// axis: six vertical positions V(x) and five horizontal positions H(x, x+1).
//
//   id 0..5  : vertical in column id
//   id 6..10 : horizontal in columns (id-6, id-5)
//
// The rule follows puyoai's PuyoController::isReachable, which has been used
// against the real game (Puyo Puyo Tsu):
//  - the pair appears in the third column; the game is over if that column
//    already holds twelve puyos;
//  - a column of height 11 or less can always be passed;
//  - a column of height 13 can never be passed (the axis puyo cannot enter the
//    14th row);
//  - a column of height 12 can be passed only while the axis puyo is lifted to
//    the 13th row: right after the spawn when both neighbours of the third
//    column are 12 or higher (quick turn), when the previous column on the way
//    is exactly 11 high (floor kick), or over a one-column gap from another
//    12-high column.
// In addition this program never rests a puyo in the 14th row, so a vertical
// pair needs a column of height 11 or less.
#include "field_bits.h"

namespace puyo {

constexpr int PAIR_POSITIONS = 11;
constexpr int SPAWN_X = 2;

struct PairPosition {
    int x1, x2; // columns of the two puyos (equal for a vertical pair)
};
constexpr PairPosition pairPosition(int id) {
    return id < W ? PairPosition{id, id} : PairPosition{id - W, id - W + 1};
}

// Can the pair travel from the spawn column to column `target`?
inline bool columnReachable(const int h[W], int target) {
    if (h[SPAWN_X] >= 12) return false;
    bool lifted = h[SPAWN_X - 1] >= 12 && h[SPAWN_X + 1] >= 12;
    const int step = target < SPAWN_X ? -1 : 1;
    for (int x = SPAWN_X; x != target;) {
        const int previous = x;
        x += step;
        if (h[x] <= 11) {
            lifted = false;
            continue;
        }
        if (h[x] == 12) {
            if (lifted) continue;
            if (h[previous] == 11 || (previous != SPAWN_X && h[previous - step] == 12)) {
                lifted = true;
                continue;
            }
        }
        return false;
    }
    return true;
}

// Bit id is set when pair position id can be reached on a field with these heights.
inline unsigned reachablePairs(const int h[W]) {
    unsigned column = 0;
    for (int x = 0; x < W; ++x)
        if (columnReachable(h, x)) column |= 1u << x;
    unsigned result = 0;
    for (int x = 0; x < W; ++x)
        if ((column >> x & 1) && h[x] <= 11) result |= 1u << x;
    for (int x = 0; x + 1 < W; ++x) {
        const int outer = x >= SPAWN_X ? x + 1 : x; // the column farther from the spawn
        if ((column >> outer & 1) && h[x] <= 12 && h[x + 1] <= 12) result |= 1u << (W + x);
    }
    return result;
}

} // namespace puyo
