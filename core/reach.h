#pragma once
// Where can the falling pair be put, given only the column heights?
//
// A pair position is a set of two cells, independent of which puyo is the
// axis: six vertical positions V(x) and five horizontal positions H(x, x+1).
//
//   id 0..5  : vertical in column id
//   id 6..10 : horizontal in columns (id-6, id-5)
//
// The rule (Puyo Puyo Tsu):
//  - the pair appears in the third column; the game is over if that column
//    already holds twelve puyos;
//  - a column of height 11 or less can always be passed;
//  - a column of height 13 can never be passed (the axis puyo cannot enter the
//    14th row);
//  - a column of height 12 can be passed only while the axis puyo is lifted to
//    the 13th row (wall crossing). It gets there
//      a. right after the spawn, when both neighbours of the third column are
//         12 or higher (quick turn);
//      b. on a column exactly 11 high (floor kick);
//      c. on top of a 12-high column it has already climbed;
//    and, once lifted, it may hop over up to MAX_HOP lower columns before it
//    lands on the 12-high column. The floor kick may also be made on the other
//    side of the spawn column; the columns crossed on the way back, including
//    the spawn column, count as hopped.
// Cases a, b, c and a hop over one column from c are puyoai's
// PuyoController::isReachable. Hops from a floor kick and over two columns
// were added because they are ordinary wall crossings in the real game (the
// pair falls slowly enough). Every placement this rule allows is also allowed
// by the v0.3.0 movement model (tools/compare_reach.cpp).
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

// How many lower columns the lifted axis puyo can cross before it must land.
constexpr int MAX_HOP = 2;

// Can the pair travel from the spawn column to column `target`?
inline bool columnReachable(const int h[W], int target) {
    if (h[SPAWN_X] >= 12) return false;
    const int step = target < SPAWN_X ? -1 : 1;
    // fly: lower columns the lifted axis puyo may still cross; -1 = not lifted.
    int fly = -1;
    if ((h[SPAWN_X - 1] >= 12 && h[SPAWN_X + 1] >= 12) || h[SPAWN_X] == 11) {
        fly = MAX_HOP;
    } else {
        // A floor kick on the other side, then back across the spawn column.
        for (int hopped = 1, x = SPAWN_X - step; hopped <= MAX_HOP && x >= 0 && x < W; ++hopped, x -= step) {
            if (h[x] == 11) fly = MAX_HOP - hopped;
            if (h[x] >= 11) break; // lifted here, or a wall that cannot be climbed from below
        }
    }
    for (int x = SPAWN_X; x != target;) {
        x += step;
        if (h[x] >= 13) return false;
        if (h[x] == 12) {
            if (fly < 0) return false;
            fly = MAX_HOP;
        } else if (h[x] == 11) {
            fly = MAX_HOP;
        } else if (fly >= 0) {
            --fly; // crossing a lower column in the air
        }
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
