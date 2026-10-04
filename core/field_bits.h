#pragma once
// One bit per cell of the 6 x 13 field (plus the 14th row), packed for SIMD.
//
// Layout: one 16-bit lane per column. Lane x (0..5) is column x counted from
// the left, bit y of a lane is row y counted from the bottom (0 = floor).
// Bits 0..11 are the twelve visible rows, bit 12 is the hidden 13th row and
// bit 13 is the 14th row. Lanes 6 and 7 are always zero.
//
// Vertical neighbours are a 16-bit shift inside each lane, horizontal
// neighbours are a 2-byte shift of the whole register. A horizontal shift can
// push bits into lane 6; callers AND the result with a field mask (whose
// lanes 6 and 7 are zero), so no explicit wall guard is needed.
#include <cstdint>
#include <bit>

// PUYO_FORCE_PORTABLE selects the plain 64-bit implementation (used by the tests).
#if !defined(PUYO_FORCE_PORTABLE) && (defined(_M_X64) || defined(__SSE4_1__))
#define PUYO_SSE 1
#include <immintrin.h>
#else
#define PUYO_SSE 0
#endif

namespace puyo {

constexpr int W = 6;        // columns
constexpr int H = 13;       // rows a puyo can rest in
constexpr int CLEAR_H = 12; // rows that connect and clear

constexpr uint16_t COLUMN_12 = 0x0FFF; // visible rows of one column
constexpr uint16_t COLUMN_13 = 0x1FFF; // rows a puyo can rest in

#if PUYO_SSE

struct FieldBits {
    __m128i v;

    FieldBits() : v(_mm_setzero_si128()) {}
    explicit FieldBits(__m128i value) : v(value) {}

    static FieldBits fromColumns(const uint16_t c[W]) {
        return FieldBits(_mm_setr_epi16(short(c[0]), short(c[1]), short(c[2]),
                                        short(c[3]), short(c[4]), short(c[5]), 0, 0));
    }
    static FieldBits fill(uint16_t column) {
        const short s = short(column);
        return FieldBits(_mm_setr_epi16(s, s, s, s, s, s, 0, 0));
    }
    static FieldBits cell(int x, int y) {
        uint16_t c[W]{};
        c[x] = uint16_t(1u << y);
        return fromColumns(c);
    }
    void columns(uint16_t c[W]) const {
        alignas(16) uint16_t lanes[8];
        _mm_store_si128(reinterpret_cast<__m128i*>(lanes), v);
        for (int x = 0; x < W; ++x) c[x] = lanes[x];
    }
    uint16_t column(int x) const {
        alignas(16) uint16_t lanes[8];
        _mm_store_si128(reinterpret_cast<__m128i*>(lanes), v);
        return lanes[x];
    }
    bool get(int x, int y) const { return (column(x) >> y) & 1; }

    uint64_t low() const { return uint64_t(_mm_cvtsi128_si64(v)); }                      // columns 0..3
    uint64_t high() const { return uint64_t(_mm_cvtsi128_si64(_mm_srli_si128(v, 8))); }  // columns 4..5
    static FieldBits fromHalves(uint64_t low, uint64_t high) {
        return FieldBits(_mm_set_epi64x(int64_t(high), int64_t(low)));
    }

    bool empty() const { return _mm_testz_si128(v, v) != 0; }
    bool any() const { return !empty(); }
    // True when the two sets share no cell.
    bool disjoint(FieldBits o) const { return _mm_testz_si128(v, o.v) != 0; }
    // True when every cell of this set is also in o.
    bool subsetOf(FieldBits o) const { return _mm_testc_si128(o.v, v) != 0; }
    int count() const { return std::popcount(low()) + std::popcount(high()); }
    bool operator==(FieldBits o) const {
        return _mm_movemask_epi8(_mm_cmpeq_epi8(v, o.v)) == 0xFFFF;
    }

    FieldBits operator&(FieldBits o) const { return FieldBits(_mm_and_si128(v, o.v)); }
    FieldBits operator|(FieldBits o) const { return FieldBits(_mm_or_si128(v, o.v)); }
    FieldBits operator^(FieldBits o) const { return FieldBits(_mm_xor_si128(v, o.v)); }
    // this AND NOT o
    FieldBits without(FieldBits o) const { return FieldBits(_mm_andnot_si128(o.v, v)); }
    FieldBits& operator&=(FieldBits o) { v = _mm_and_si128(v, o.v); return *this; }
    FieldBits& operator|=(FieldBits o) { v = _mm_or_si128(v, o.v); return *this; }
    FieldBits& operator^=(FieldBits o) { v = _mm_xor_si128(v, o.v); return *this; }

    // Each cell moves one step; cells leaving a 16-bit lane are dropped.
    FieldBits up() const { return FieldBits(_mm_slli_epi16(v, 1)); }
    FieldBits down() const { return FieldBits(_mm_srli_epi16(v, 1)); }
    FieldBits right() const { return FieldBits(_mm_slli_si128(v, 2)); }
    FieldBits left() const { return FieldBits(_mm_srli_si128(v, 2)); }

    // Per column: multiply by a power of two, i.e. shift that column up.
    FieldBits scaled(FieldBits factor) const { return FieldBits(_mm_mullo_epi16(v, factor.v)); }
};

#else // portable fallback: two 64-bit halves (columns 0..3 and 4..5)

struct FieldBits {
    uint64_t lo = 0, hi = 0;
    static constexpr uint64_t LO_BIT0 = 0x0001000100010001ull, HI_BIT0 = 0x0000000000010001ull;

    FieldBits() = default;
    FieldBits(uint64_t l, uint64_t h) : lo(l), hi(h) {}

    static FieldBits fromColumns(const uint16_t c[W]) {
        return {uint64_t(c[0]) | uint64_t(c[1]) << 16 | uint64_t(c[2]) << 32 | uint64_t(c[3]) << 48,
                uint64_t(c[4]) | uint64_t(c[5]) << 16};
    }
    static FieldBits fill(uint16_t column) { return {LO_BIT0 * column, HI_BIT0 * column}; }
    static FieldBits cell(int x, int y) {
        uint16_t c[W]{};
        c[x] = uint16_t(1u << y);
        return fromColumns(c);
    }
    uint16_t column(int x) const { return uint16_t(x < 4 ? lo >> (16 * x) : hi >> (16 * (x - 4))); }
    void columns(uint16_t c[W]) const { for (int x = 0; x < W; ++x) c[x] = column(x); }
    bool get(int x, int y) const { return (column(x) >> y) & 1; }

    uint64_t low() const { return lo; }
    uint64_t high() const { return hi; }
    static FieldBits fromHalves(uint64_t low, uint64_t high) { return {low, high}; }

    bool empty() const { return !(lo | hi); }
    bool any() const { return (lo | hi) != 0; }
    bool disjoint(FieldBits o) const { return !((lo & o.lo) | (hi & o.hi)); }
    bool subsetOf(FieldBits o) const { return !((lo & ~o.lo) | (hi & ~o.hi)); }
    int count() const { return std::popcount(lo) + std::popcount(hi); }
    bool operator==(FieldBits o) const { return lo == o.lo && hi == o.hi; }

    FieldBits operator&(FieldBits o) const { return {lo & o.lo, hi & o.hi}; }
    FieldBits operator|(FieldBits o) const { return {lo | o.lo, hi | o.hi}; }
    FieldBits operator^(FieldBits o) const { return {lo ^ o.lo, hi ^ o.hi}; }
    FieldBits without(FieldBits o) const { return {lo & ~o.lo, hi & ~o.hi}; }
    FieldBits& operator&=(FieldBits o) { lo &= o.lo; hi &= o.hi; return *this; }
    FieldBits& operator|=(FieldBits o) { lo |= o.lo; hi |= o.hi; return *this; }
    FieldBits& operator^=(FieldBits o) { lo ^= o.lo; hi ^= o.hi; return *this; }

    FieldBits up() const { return {(lo << 1) & ~LO_BIT0, (hi << 1) & ~HI_BIT0}; }
    FieldBits down() const { return {(lo >> 1) & ~(LO_BIT0 << 15), (hi >> 1) & ~(HI_BIT0 << 15)}; }
    // Unlike the SSE version nothing is pushed into lanes 6 and 7.
    FieldBits right() const { return {lo << 16, (hi << 16 | lo >> 48) & 0xFFFFFFFFull}; }
    FieldBits left() const { return {lo >> 16 | hi << 48, hi >> 16}; }

    FieldBits scaled(FieldBits factor) const {
        uint16_t a[W], f[W];
        columns(a);
        factor.columns(f);
        for (int x = 0; x < W; ++x) a[x] = uint16_t(a[x] * f[x]);
        return fromColumns(a);
    }
};

#endif

inline const FieldBits MASK_12 = FieldBits::fill(COLUMN_12);
inline const FieldBits MASK_13 = FieldBits::fill(COLUMN_13);

// Calls f(x, y) for every cell of the set, columns left to right, rows bottom up.
template <class F> inline void forEachCell(FieldBits bits, F&& f) {
    uint16_t c[W];
    bits.columns(c);
    for (int x = 0; x < W; ++x)
        for (unsigned m = c[x]; m; m &= m - 1) f(x, std::countr_zero(m));
}

} // namespace puyo
