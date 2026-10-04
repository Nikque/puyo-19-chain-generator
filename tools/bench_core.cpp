// Speed of the bitboard core on the operations a game or an AI needs:
// simulating a whole chain, testing whether anything clears, and listing the
// reachable pair positions. Build together with generator/*.cpp.
#include "../generator/generator.h"
#include <chrono>
#include <cstdio>
#include <vector>

using namespace puyo;

template <class F> static double nanosecondsPerCall(int calls, F&& f) {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < calls; ++i) f(i);
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / calls;
}

int main() {
    // 19-chain boards from the generator as test material.
    GeneratorConfig config;
    config.colorCount = 4;
    config.beamWidth = 48;
    config.candidatesPerParent = 2;
    config.targetChain = 19;
    std::vector<BitField> boards;
    for (int attempt = 1; boards.size() < 256; ++attempt) {
        Rng rng(1, uint64_t(attempt));
        if (const auto s = generateOne(config, rng)) boards.push_back(s->field);
    }
    long long sink = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const double chain = nanosecondsPerCall(200000, [&](int i) {
            BitField f = boards[size_t(i) % boards.size()];
            sink += runChain(f).score;
        });
        const double chainNoScore = nanosecondsPerCall(200000, [&](int i) {
            BitField f = boards[size_t(i) % boards.size()];
            Wave w;
            while (stepWave(f, w, false)) ++sink;
        });
        const double clear = nanosecondsPerCall(5000000, [&](int i) { sink += boards[size_t(i) % boards.size()].hasClear(); });
        const double reach = nanosecondsPerCall(5000000, [&](int i) {
            int h[W];
            boards[size_t(i) % boards.size()].heights(h);
            h[i % W] -= 2;
            sink += reachablePairs(h);
        });
        std::printf("%s\n", pass ? "portable gravity (no PEXT/PDEP):" : (detail::fastPext ? "with PEXT/PDEP:" : "PEXT/PDEP not used on this CPU:"));
        std::printf("  19-chain simulation with score : %7.0f ns  (%.1f ns per wave)\n", chain, chain / 19);
        std::printf("  19-chain simulation, no details: %7.0f ns  (%.1f ns per wave)\n", chainNoScore, chainNoScore / 19);
        std::printf("  does anything clear?           : %7.1f ns\n", clear);
        std::printf("  heights + reachable positions  : %7.1f ns\n", reach);
        if (!detail::fastPext) break;
        detail::fastPext = false;
    }
    return int(sink & 1);
}
