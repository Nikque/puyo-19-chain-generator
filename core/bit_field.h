#pragma once
// The whole field in three bit planes (48 bytes, no heap).
//
// Each cell holds a 3-bit code, bit i of the code stored in plane[i]:
//   0 empty, 1..5 normal colours (R G B Y P), 6 garbage (ojama), 7 reserved.
// Holes inside a column are representable, so the same type serves the
// generator (compact columns) and the editor / game (floating cells).
//
// Rules implemented here (Puyo Puyo Tsu):
//  - only the twelve visible rows connect and clear; the 13th row never does,
//    but it falls when the cells below it disappear;
//  - a wave clears every group of four or more same-coloured normal puyos,
//    together with the garbage puyos touching them.
#include "field_bits.h"
#include <array>
#include <cstring>
#if defined(_MSC_VER)
#include <intrin.h>
#elif PUYO_SSE
#include <cpuid.h>
#endif

namespace puyo {

using Cell = uint8_t;
constexpr Cell EMPTY = 0, GARBAGE = 6;
constexpr int MAX_COLORS = 5;

// BMI2 PEXT/PDEP is fast on Intel since Haswell and AMD since Zen 3, and very
// slow (microcoded) on AMD Zen 1 / Zen 2. Decided once at start-up.
namespace detail {
#if PUYO_SSE
inline void cpuid(unsigned leaf, unsigned r[4]) {
#if defined(_MSC_VER)
    __cpuidex(reinterpret_cast<int*>(r), int(leaf), 0);
#else
    __cpuid_count(leaf, 0, r[0], r[1], r[2], r[3]);
#endif
}
inline bool detectFastPext() {
    unsigned r[4];
    cpuid(0, r);
    if (r[0] < 7) return false;
    const bool amd = r[1] == 0x68747541; // "Auth"enticAMD
    cpuid(7, r);
    if (!(r[1] & (1u << 8))) return false; // BMI2
    cpuid(1, r);
    const unsigned family = ((r[0] >> 8) & 0xF) + ((r[0] >> 20) & 0xFF);
    return !amd || family >= 0x19;
}
#if defined(_MSC_VER)
inline uint64_t pext64(uint64_t v, uint64_t m) { return _pext_u64(v, m); }
inline uint64_t pdep64(uint64_t v, uint64_t m) { return _pdep_u64(v, m); }
#else
__attribute__((target("bmi2"))) inline uint64_t pext64(uint64_t v, uint64_t m) { return _pext_u64(v, m); }
__attribute__((target("bmi2"))) inline uint64_t pdep64(uint64_t v, uint64_t m) { return _pdep_u64(v, m); }
#endif
inline bool fastPext = detectFastPext(); // tests switch it off to cover the portable path
#else
inline bool fastPext = false;
inline uint64_t pext64(uint64_t, uint64_t) { return 0; }
inline uint64_t pdep64(uint64_t, uint64_t) { return 0; }
#endif
// Portable PEXT for one 16-bit column.
inline uint16_t compactColumn(unsigned value, unsigned keep) {
    unsigned out = 0, bit = 1;
    for (; keep; keep &= keep - 1, bit <<= 1)
        if (value & keep & (0u - keep)) out |= bit;
    return uint16_t(out);
}
} // namespace detail

// Same-colour adjacency of normal puyos inside the twelve visible rows.
struct Links {
    FieldBits u, d, l, r; // the cell has a same-coloured neighbour above / below / left / right
};

struct BitField {
    std::array<FieldBits, 3> plane;

    FieldBits occupied() const { return plane[0] | plane[1] | plane[2]; }
    FieldBits colorMask(Cell code) const {
        FieldBits m = code & 1 ? plane[0] : occupied().without(plane[0]);
        m = code & 2 ? m & plane[1] : m.without(plane[1]);
        return code & 4 ? m & plane[2] : m.without(plane[2]);
    }
    // Colours 1..5 (codes 6 and 7 have both upper planes set).
    FieldBits normal() const { return occupied().without(plane[1] & plane[2]); }
    FieldBits garbage() const { return (plane[1] & plane[2]).without(plane[0]); }

    Cell get(int x, int y) const {
        return Cell(int(plane[0].get(x, y)) | int(plane[1].get(x, y)) << 1 | int(plane[2].get(x, y)) << 2);
    }
    void set(int x, int y, Cell code) {
        const FieldBits bit = FieldBits::cell(x, y);
        for (int i = 0; i < 3; ++i)
            plane[i] = code >> i & 1 ? plane[i] | bit : plane[i].without(bit);
    }
    bool empty() const { return occupied().empty(); }
    int count() const { return occupied().count(); }
    // Number of cells in each column; equals the height when there are no holes.
    void heights(int h[W]) const {
        uint16_t c[W];
        occupied().columns(c);
        for (int x = 0; x < W; ++x) h[x] = std::popcount(c[x]);
    }
    bool operator==(const BitField& o) const {
        return plane[0] == o.plane[0] && plane[1] == o.plane[1] && plane[2] == o.plane[2];
    }

    Links links() const {
        const FieldBits n = normal() & MASK_12;
        Links k;
        k.u = (n & n.down()).without((plane[0] ^ plane[0].down()) | (plane[1] ^ plane[1].down()) |
                                     (plane[2] ^ plane[2].down()));
        k.r = (n & n.left()).without((plane[0] ^ plane[0].left()) | (plane[1] ^ plane[1].left()) |
                                     (plane[2] ^ plane[2].left()));
        k.d = k.u.up();
        k.l = k.r.right();
        return k;
    }

    // Every cell of a group of four or more is either a "seed" or next to one.
    // A seed is a cell with three or more same-coloured neighbours, or one of
    // two linked cells that both have two or more. (A grid has no triangles,
    // so two linked cells with two neighbours each span at least four cells.)
    struct Seeds { FieldBits three, pairU, pairR; };
    static Seeds seeds(const Links& k) {
        const FieldBits udAnd = k.u & k.d, lrAnd = k.l & k.r, udOr = k.u | k.d, lrOr = k.l | k.r;
        const FieldBits two = udAnd | lrAnd | (udOr & lrOr);
        return {(udAnd & lrOr) | (lrAnd & udOr), two & k.u & two.down(), two & k.r & two.left()};
    }
    // Is there any group of four or more? No flood fill.
    bool hasClear() const {
        const Seeds s = seeds(links());
        return (s.three | s.pairU | s.pairR).any();
    }
    // All normal puyos that belong to a group of four or more.
    FieldBits clearMask() const {
        const Links k = links();
        const Seeds s = seeds(k);
        const FieldBits seed = s.three | s.pairU | s.pairU.up() | s.pairR | s.pairR.right();
        return seed | (seed & k.u).up() | (seed & k.d).down() | (seed & k.r).right() | (seed & k.l).left();
    }
    // The group containing `start`, following the links.
    static FieldBits group(FieldBits start, const Links& k) {
        for (FieldBits g = start;;) {
            const FieldBits next = g | (g & k.u).up() | (g & k.d).down() | (g & k.r).right() | (g & k.l).left();
            if (next == g) return g;
            g = next;
        }
    }
    // Garbage puyos (visible rows only) touching the given cleared cells.
    FieldBits garbageNextTo(FieldBits cleared) const {
        return garbage() & MASK_12 & (cleared.up() | cleared.down() | cleared.left() | cleared.right());
    }

    void erase(FieldBits cells) {
        for (auto& p : plane) p = p.without(cells);
    }
    // Gravity: every column is compacted towards the floor (holes disappear).
    void drop() {
        const FieldBits occ = occupied();
        const uint64_t keep[2] = {occ.low(), occ.high()};
        uint16_t c[W];
        occ.columns(c);
        uint16_t full[W];
        for (int x = 0; x < W; ++x) full[x] = uint16_t((1u << std::popcount(c[x])) - 1);
        const FieldBits target = FieldBits::fromColumns(full);
        if (target == occ) return;
        if (detail::fastPext) {
            const uint64_t to[2] = {target.low(), target.high()};
            for (auto& p : plane)
                p = FieldBits::fromHalves(detail::pdep64(detail::pext64(p.low(), keep[0]), to[0]),
                                          detail::pdep64(detail::pext64(p.high(), keep[1]), to[1]));
        } else {
            for (auto& p : plane) {
                uint16_t v[W];
                p.columns(v);
                for (int x = 0; x < W; ++x)
                    if (c[x] != full[x]) v[x] = detail::compactColumn(v[x], c[x]);
                p = FieldBits::fromColumns(v);
            }
        }
    }
    // Opens `count` empty cells at row `slot` of column x and fills them.
    void insert(int x, int slot, int count, Cell code) {
        const unsigned below = (1u << slot) - 1, added = ((1u << count) - 1) << slot;
        for (int i = 0; i < 3; ++i) {
            uint16_t c[W];
            plane[i].columns(c);
            c[x] = uint16_t((c[x] & below) | ((c[x] & ~below) << count) | (code >> i & 1 ? added : 0));
            plane[i] = FieldBits::fromColumns(c);
        }
    }

    // 36-byte exact key (three 96-bit planes) for hashing and equality.
    struct Key {
        uint64_t a[4];
        uint32_t b;
        bool operator==(const Key& o) const { return !std::memcmp(a, o.a, sizeof a) && b == o.b; }
    };
    Key key() const {
        const uint64_t h0 = plane[0].high(), h1 = plane[1].high(), h2 = plane[2].high();
        return {{plane[0].low(), plane[1].low(), plane[2].low(), h0 | h1 << 32}, uint32_t(h2)};
    }
};

struct KeyHash {
    size_t operator()(const BitField::Key& k) const {
        uint64_t h = 0x9E3779B97F4A7C15ull;
        for (uint64_t v : {k.a[0], k.a[1], k.a[2], k.a[3], uint64_t(k.b)}) {
            h ^= v;
            h *= 0xFF51AFD7ED558CCDull;
            h ^= h >> 32;
        }
        return size_t(h);
    }
};

// ---- One wave and a whole chain ------------------------------------------

struct Wave {
    FieldBits cleared; // normal puyos that disappeared
    FieldBits garbage; // garbage puyos that disappeared with them
    int puyos = 0;     // number of normal puyos cleared
    int colors = 0;    // number of distinct colours cleared
    int groups = 0;    // number of groups cleared
    int groupBonus = 0;
};

// Tsu scoring tables.
constexpr int CHAIN_BONUS[20] = {0, 0, 8, 16, 32, 64, 96, 128, 160, 192, 224,
                                 256, 288, 320, 352, 384, 416, 448, 480, 512};
constexpr int COLOR_BONUS[6] = {0, 0, 3, 6, 12, 24};
constexpr int groupBonus(int size) { return size >= 11 ? 10 : size >= 5 ? size - 3 : 0; }
inline int waveScore(const Wave& w, int chain) {
    const int c = chain < 1 ? 1 : chain > 19 ? 19 : chain;
    const int bonus = CHAIN_BONUS[c] + COLOR_BONUS[w.colors] + w.groupBonus;
    return 10 * w.puyos * (bonus < 1 ? 1 : bonus);
}

// Finds what the next wave clears without changing the field.
// With details=false only `cleared`, `garbage` and `puyos` are filled in.
inline bool findWave(const BitField& f, Wave& w, bool details = true) {
    const Links k = f.links();
    const BitField::Seeds s = BitField::seeds(k);
    if ((s.three | s.pairU | s.pairR).empty()) return false;
    const FieldBits seed = s.three | s.pairU | s.pairU.up() | s.pairR | s.pairR.right();
    w = {};
    w.cleared = seed | (seed & k.u).up() | (seed & k.d).down() | (seed & k.r).right() | (seed & k.l).left();
    w.garbage = f.garbageNextTo(w.cleared);
    w.puyos = w.cleared.count();
    if (!details) return true;
    for (Cell color = 1; color <= MAX_COLORS; ++color) {
        FieldBits rest = w.cleared & f.colorMask(color);
        if (rest.empty()) continue;
        ++w.colors;
        while (rest.any()) {
            uint16_t c[W];
            rest.columns(c);
            int x = 0;
            while (!c[x]) ++x;
            const FieldBits g = BitField::group(FieldBits::cell(x, std::countr_zero(unsigned(c[x]))), k);
            ++w.groups;
            w.groupBonus += groupBonus(g.count());
            rest = rest.without(g);
        }
    }
    return true;
}

// Clears one wave and applies gravity. Returns false when nothing clears.
inline bool stepWave(BitField& f, Wave& w, bool details = true) {
    if (!findWave(f, w, details)) return false;
    f.erase(w.cleared | w.garbage);
    f.drop();
    return true;
}

struct ChainResult {
    int chains = 0;
    int score = 0;
};
inline ChainResult runChain(BitField& f) {
    ChainResult r;
    for (Wave w; stepWave(f, w);) r.score += waveScore(w, ++r.chains);
    return r;
}

} // namespace puyo
