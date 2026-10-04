#pragma once
// Small, fast, portable random numbers. Unlike std::shuffle and the standard
// distributions, the results are identical on every compiler and library.
#include <cstdint>
#include <utility>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace puyo {

constexpr uint64_t splitmix64(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// xoshiro256** (Blackman, Vigna)
class Rng {
    uint64_t s[4];
    static constexpr uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

public:
    // Independent stream `stream` of the generator seeded with `seed`.
    explicit Rng(uint64_t seed, uint64_t stream = 0) {
        uint64_t state = seed ^ (stream * 0xD1342543DE82EF95ull + 0x2545F4914F6CDD1Dull);
        (void)splitmix64(state);
        state ^= stream;
        for (auto& v : s) v = splitmix64(state);
    }
    uint64_t next() {
        const uint64_t result = rotl(s[1] * 5, 7) * 9, t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }
    // Uniform in [0, n), n > 0 (Lemire's multiply-shift with rejection: no bias).
    uint64_t below(uint64_t n) {
        uint64_t x = next(), hi, lo = mul(x, n, hi);
        if (lo < n) {
            const uint64_t threshold = (0 - n) % n;
            while (lo < threshold) {
                x = next();
                lo = mul(x, n, hi);
            }
        }
        return hi;
    }
    // Moves `count` uniformly chosen elements to the front, in random order.
    template <class T> void partialShuffle(T* data, size_t size, size_t count) {
        for (size_t i = 0; i < count && i + 1 < size; ++i)
            std::swap(data[i], data[i + below(size - i)]);
    }
    template <class T> void shuffle(T* data, size_t size) { partialShuffle(data, size, size); }

private:
    static uint64_t mul(uint64_t a, uint64_t b, uint64_t& high) {
#if defined(_MSC_VER) && defined(_M_X64)
        return _umul128(a, b, &high);
#elif defined(__SIZEOF_INT128__)
        const unsigned __int128 p = static_cast<unsigned __int128>(a) * b;
        high = uint64_t(p >> 64);
        return uint64_t(p);
#else
        const uint64_t a0 = uint32_t(a), a1 = a >> 32, b0 = uint32_t(b), b1 = b >> 32;
        const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
        const uint64_t mid = (p00 >> 32) + uint32_t(p01) + uint32_t(p10);
        high = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
        return (mid << 32) | uint32_t(p00);
#endif
    }
};

} // namespace puyo
