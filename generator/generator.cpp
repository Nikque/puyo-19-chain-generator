// Backward beam search for boards that fire an exact chain.
//
// Why no forward simulation is needed inside the search
// ------------------------------------------------------
// A step takes a board `post` and inserts a group of four cells of one colour
// into its columns (the cells above the insertion move up), giving `pre`.
// The step is accepted only if, on `pre`,
//   (i)  no group of four or more exists among the old puyos, and
//   (ii) no old puyo next to the inserted cells has their colour.
// Then the inserted group is the only thing that clears on `pre`, it is
// exactly four puyos, and after gravity the board is exactly `post` again.
// By induction a board made by n steps from a stable board R clears in exactly
// n waves of one group of four each, starts with the group inserted last, and
// ends as R. These facts hold for every chain length, extra count and colour
// count; verifySolution() checks them directly on every board that is saved.
#include "generator.h"
#include <algorithm>
#include <cstdlib>
#include <set>
#include <unordered_set>

namespace puyo {
namespace {

// One way to insert a group of four: the same for every board.
struct Placement {
    FieldBits ins;  // the four inserted cells, at their final position
    FieldBits low;  // shape columns: the cells below the insertion; other columns: all cells
    FieldBits mul;  // shape columns: 1 << (cells inserted in this column); other columns: 1
    FieldBits near; // visible cells next to the inserted ones
};

using Shape = std::vector<std::pair<int, int>>;

std::vector<Shape> makeTetrominoShapes() {
    // The five free tetrominoes, rotated and reflected: 19 fixed shapes.
    const std::vector<Shape> bases{{{0, 0}, {1, 0}, {2, 0}, {3, 0}},  // I
                                   {{0, 0}, {1, 0}, {0, 1}, {1, 1}},  // O
                                   {{0, 0}, {1, 0}, {2, 0}, {1, 1}},  // T
                                   {{0, 0}, {0, 1}, {0, 2}, {1, 0}},  // L
                                   {{1, 0}, {2, 0}, {0, 1}, {1, 1}}}; // S
    std::set<Shape> unique;
    for (const Shape& base : bases)
        for (int reflected = 0; reflected < 2; ++reflected)
            for (int rotation = 0; rotation < 4; ++rotation) {
                Shape s;
                int minX = 99, minY = 99;
                for (auto [px, py] : base) {
                    int x = reflected ? -px : px, y = py;
                    for (int r = 0; r < rotation; ++r) {
                        const int old = x;
                        x = -y;
                        y = old;
                    }
                    s.emplace_back(x, y);
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                }
                for (auto& [x, y] : s) x -= minX, y -= minY;
                std::sort(s.begin(), s.end());
                unique.insert(std::move(s));
            }
    return {unique.begin(), unique.end()};
}

std::vector<Placement> makePlacements() {
    std::vector<Placement> table;
    for (const Shape& shape : makeTetrominoShapes()) {
        int width = 0, height = 0;
        int bottom[W], count[W]{};
        std::fill(bottom, bottom + W, 99);
        for (auto [x, y] : shape) {
            width = std::max(width, x + 1);
            height = std::max(height, y + 1);
            bottom[x] = std::min(bottom[x], y);
            ++count[x];
        }
        // The cells of one column are inserted as one block, so they must be contiguous.
        bool contiguous = true;
        for (auto [x, y] : shape) contiguous &= y < bottom[x] + count[x];
        if (!contiguous) continue;
        for (int x0 = 0; x0 + width <= W; ++x0)
            for (int y0 = 0; y0 + height <= CLEAR_H; ++y0) {
                uint16_t ins[W]{}, low[W], mul[W];
                std::fill(low, low + W, uint16_t(0xFFFF));
                std::fill(mul, mul + W, uint16_t(1));
                for (int dx = 0; dx < width; ++dx) {
                    const int x = x0 + dx, slot = y0 + bottom[dx], n = count[dx];
                    const unsigned run = (1u << n) - 1;
                    ins[x] = uint16_t(run << slot);
                    low[x] = uint16_t((1u << slot) - 1);
                    mul[x] = uint16_t(1u << n);
                }
                Placement p;
                p.ins = FieldBits::fromColumns(ins);
                p.low = FieldBits::fromColumns(low);
                p.mul = FieldBits::fromColumns(mul);
                p.near = (p.ins.up() | p.ins.down() | p.ins.left() | p.ins.right()).without(p.ins) & MASK_12;
                table.push_back(p);
            }
    }
    return table;
}

const std::vector<Placement>& placements() {
    static const std::vector<Placement> table = makePlacements();
    return table;
}
// Which placements can be valid at all, found with a few word operations
// instead of a scan of the whole table.
//  - belowCell[x][y]: placements that insert into column x at row y or lower,
//    i.e. that move the puyo at (x, y).
//  - fits[x][h]: placements that are possible in column x when it holds h
//    puyos (the insertion starts at or below the top and the column does not
//    grow past the 13th row), or that do not use column x.
struct PlacementIndex {
    static constexpr int WORDS = 16; // 64 * 16 = 1024 placements at most
    using Set = std::array<uint64_t, WORDS>;
    Set belowCell[W][H]{};
    Set fits[W][H + 1]{};
    int words = 0;
};

const PlacementIndex& placementIndex() {
    static const PlacementIndex index = [] {
        PlacementIndex ix;
        const std::vector<Placement>& table = placements();
        if (table.size() > 64 * PlacementIndex::WORDS) std::abort();
        ix.words = int((table.size() + 63) / 64);
        for (size_t i = 0; i < table.size(); ++i) {
            uint16_t ins[W];
            table[i].ins.columns(ins);
            const uint64_t bit = 1ull << (i % 64);
            for (int x = 0; x < W; ++x) {
                if (!ins[x]) {
                    for (int h = 0; h <= H; ++h) ix.fits[x][h][i / 64] |= bit;
                    continue;
                }
                const int slot = std::countr_zero(unsigned(ins[x])), n = std::popcount(unsigned(ins[x]));
                for (int y = slot; y < H; ++y) ix.belowCell[x][y][i / 64] |= bit;
                for (int h = slot; h + n <= H; ++h) ix.fits[x][h][i / 64] |= bit;
            }
        }
        return ix;
    }();
    return index;
}

struct Node {
    BitField field;
    FieldBits trigger; // the group inserted last; empty for the board the chain ends with
};
struct Choice {
    uint16_t placement;
    Cell color;
};

// The board with the gap for placement p opened and still empty.
inline BitField openGap(const BitField& post, const Placement& p) {
    BitField q;
    for (int i = 0; i < 3; ++i)
        q.plane[i] = (post.plane[i] & p.low) | post.plane[i].without(p.low).scaled(p.mul);
    return q;
}
inline BitField fillGap(BitField q, const Placement& p, Cell color) {
    for (int i = 0; i < 3; ++i)
        if (color >> i & 1) q.plane[i] |= p.ins;
    return q;
}

// Calls f(placement index, colour) for every accepted insertion into `post`.
template <class F> void forEachPredecessor(const Node& post, int colorCount, F&& f) {
    const Placement* table = placements().data();
    const PlacementIndex& index = placementIndex();
    uint16_t columns[W], triggerColumns[W];
    post.field.occupied().columns(columns);
    post.trigger.columns(triggerColumns);
    // The group that clears first on `post` must not survive on `pre`: at
    // least one of its puyos has to move, so the insertion must be below one.
    PlacementIndex::Set candidates{};
    bool hasTrigger = false;
    for (int x = 0; x < W; ++x)
        if (triggerColumns[x]) {
            hasTrigger = true;
            const auto& below = index.belowCell[x][std::bit_width(unsigned(triggerColumns[x])) - 1];
            for (int w = 0; w < index.words; ++w) candidates[w] |= below[w];
        }
    if (!hasTrigger) candidates.fill(~0ull);
    for (int x = 0; x < W; ++x) {
        const auto& fits = index.fits[x][std::popcount(unsigned(columns[x]))];
        for (int w = 0; w < index.words; ++w) candidates[w] &= fits[w];
    }
    for (int w = 0; w < index.words; ++w)
        for (uint64_t rest = candidates[w]; rest; rest &= rest - 1) {
            const size_t i = size_t(w) * 64 + size_t(std::countr_zero(rest));
            const Placement& p = table[i];
            const BitField q = openGap(post.field, p);
            if (q.hasClear()) continue;
            for (Cell color = 1; color <= colorCount; ++color)
                if (q.colorMask(color).disjoint(p.near)) f(uint16_t(i), color);
        }
}

inline Node makeChild(const Node& post, Choice c) {
    const Placement& p = placements()[c.placement];
    return {fillGap(openGap(post.field, p), p, c.color), p.ins};
}

// The board the chain ends with: `extra` puyos, nothing clears, and the cell
// where pairs appear (third column, twelfth row) is free.
bool makeResidual(BitField& field, int extra, int colorCount, Rng& rng) {
    int h[W]{};
    for (int i = 0; i < extra; ++i) {
        int x;
        do x = int(rng.below(W)); while (h[x] >= (x == SPAWN_X ? CLEAR_H - 1 : H));
        const int start = int(rng.below(colorCount));
        bool placed = false;
        for (int n = 0; n < colorCount && !placed; ++n) {
            field.set(x, h[x], Cell(1 + (start + n) % colorCount));
            placed = !field.hasClear();
            if (!placed) field.set(x, h[x], EMPTY);
        }
        if (!placed) return false;
        ++h[x];
    }
    return true;
}

bool stopped(const std::atomic_bool* stop) { return stop && stop->load(std::memory_order_relaxed); }

} // namespace

FieldBits pairCells(const BitField& field, int id) {
    int h[W];
    field.heights(h);
    const PairPosition p = pairPosition(id);
    if (p.x1 == p.x2)
        return h[p.x1] >= 2 ? FieldBits::cell(p.x1, h[p.x1] - 1) | FieldBits::cell(p.x1, h[p.x1] - 2) : FieldBits();
    return h[p.x1] && h[p.x2] ? FieldBits::cell(p.x1, h[p.x1] - 1) | FieldBits::cell(p.x2, h[p.x2] - 1) : FieldBits();
}

unsigned firePairs(const BitField& field, FieldBits trigger) {
    // On a generated board the only group of four or more is the trigger, and
    // taking the topmost puyos away moves nothing. So "nothing clears without
    // the pair" is the same as "the pair holds a trigger puyo".
    int h[W];
    field.heights(h);
    uint16_t t[W];
    trigger.columns(t);
    unsigned result = 0;
    const auto reachable = [&](int id, int x1, int x2, int n1, int n2) {
        h[x1] -= n1;
        h[x2] -= n2;
        if (reachablePairs(h) >> id & 1) result |= 1u << id;
        h[x1] += n1;
        h[x2] += n2;
    };
    for (int x = 0; x < W; ++x)
        if (h[x] >= 2 && (t[x] >> (h[x] - 2) & 3)) reachable(x, x, x, 2, 0);
    for (int x = 0; x + 1 < W; ++x)
        if (h[x] && h[x + 1] && ((t[x] >> (h[x] - 1) & 1) || (t[x + 1] >> (h[x + 1] - 1) & 1)))
            reachable(W + x, x, x + 1, 1, 1);
    return result;
}

std::string verifySolution(const Solution& s) {
    const FieldBits occupied = s.field.occupied();
    uint16_t c[W];
    occupied.columns(c);
    for (int x = 0; x < W; ++x)
        if (c[x] & (c[x] + 1) || c[x] > COLUMN_13) return "column with a hole or a 14th-row puyo";
    if (!(s.field.normal() == occupied)) return "not a normal colour";
    if (s.trigger.count() != 4) return "trigger is not four cells";
    if (!s.firePairs || s.firePairs >> PAIR_POSITIONS) return "no last pair";
    for (int id = 0; id < PAIR_POSITIONS; ++id) {
        if (!(s.firePairs >> id & 1)) continue;
        const FieldBits pair = pairCells(s.field, id);
        if (pair.count() != 2) return "last pair is not two puyos";
        BitField before = s.field;
        before.erase(pair);
        if (before.hasClear()) return "the board clears before the last pair";
        if (before.get(SPAWN_X, CLEAR_H - 1)) return "the last pair cannot appear";
        int h[W];
        before.heights(h);
        if (!(reachablePairs(h) >> id & 1)) return "the last pair cannot reach its place";
    }
    BitField f = s.field;
    int waves = 0;
    for (Wave w; stepWave(f, w); ++waves) {
        if (w.groups != 1 || w.puyos != 4 || w.garbage.any()) return "a wave is not a single group of four";
        if (waves == 0 && !(w.cleared == s.trigger)) return "the first wave is not the trigger";
    }
    if (waves != s.targetChain) return "wrong number of waves";
    if (f.get(SPAWN_X, CLEAR_H - 1)) return "choked after the chain";
    return {};
}

std::optional<Solution> generateOne(const GeneratorConfig& config, Rng& rng, const std::atomic_bool* stop,
                                    std::atomic_int* depthOut, const std::atomic_bool* cancel) {
    const int colors = config.colorCount, target = config.targetChain;
    const size_t perParent = size_t(config.candidatesPerParent), beamWidth = size_t(config.beamWidth);
    std::vector<Node> beam(1), next;
    const int extra = config.maxExtraPuyos
        ? config.minExtraPuyos + int(rng.below(uint64_t(config.maxExtraPuyos - config.minExtraPuyos + 1)))
        : 0;
    if (!makeResidual(beam[0].field, extra, colors, rng)) return std::nullopt;

    std::vector<Choice> choices;
    std::unordered_set<BitField::Key, KeyHash> seen;
    for (int depth = 1; depth <= target; ++depth) {
        if (depthOut) depthOut->store(depth, std::memory_order_relaxed);
        next.clear();
        seen.clear();
        for (const Node& post : beam) {
            if (stopped(stop) || stopped(cancel)) return std::nullopt;
            choices.clear();
            if (depth < target) {
                forEachPredecessor(post, colors, [&](uint16_t i, Cell color) { choices.push_back({i, color}); });
                // The step before the last one keeps every child: few of them can be
                // completed by a last pair, and trying them all is cheaper than a new attempt.
                const size_t take = depth == target - 1 ? choices.size() : std::min(perParent, choices.size());
                rng.partialShuffle(choices.data(), choices.size(), take);
                for (size_t i = 0; i < take; ++i) {
                    const Node child = makeChild(post, choices[i]);
                    if (seen.insert(child.field.key()).second) next.push_back(child);
                }
                continue;
            }
            // Last step: keep only the boards whose final pair can really be placed.
            std::vector<unsigned> pairs;
            forEachPredecessor(post, colors, [&](uint16_t i, Cell color) {
                const Node child = makeChild(post, {i, color});
                if (const unsigned fire = firePairs(child.field, child.trigger)) {
                    choices.push_back({i, color});
                    pairs.push_back(fire);
                }
            });
            if (choices.empty()) continue;
            const size_t pick = size_t(rng.below(choices.size()));
            const Node child = makeChild(post, choices[pick]);
            return Solution{child.field, child.trigger, pairs[pick], target};
        }
        if (next.empty()) return std::nullopt;
        // A uniformly random subset of the new boards, in random order.
        const size_t width = depth == target - 1 ? next.size() : beamWidth;
        rng.partialShuffle(next.data(), next.size(), std::min(width, next.size()));
        if (next.size() > width) next.resize(width);
        beam.swap(next);
    }
    return std::nullopt;
}

} // namespace puyo
