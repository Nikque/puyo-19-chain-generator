#pragma once
// Random generator of boards that fire an exact chain (1..19 waves).
//
// The search works backwards from the board left after the chain: every step
// inserts one group of four below/inside the existing columns so that this
// group, and nothing else, clears first. See generator.cpp for the argument
// why such a board needs no forward simulation.
#include "../core/bit_field.h"
#include "../core/reach.h"
#include "../core/rng.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace puyo {

constexpr int MIN_TARGET_CHAIN = 1;
constexpr int MAX_TARGET_CHAIN = 19;

struct Solution {
    BitField field;        // the board right after the last pair has been placed
    FieldBits trigger;     // the four puyos of the first wave
    unsigned firePairs = 0; // pair positions (reach.h ids) that can be the last pair
    int targetChain = MAX_TARGET_CHAIN;
};

// The two cells of pair position `id` on this board (its topmost puyos).
FieldBits pairCells(const BitField& field, int id);
// Pair positions that can be the last placement: removing the pair leaves a
// board on which nothing clears, the pair can appear, and the position is
// reachable. Uses the shortcuts that hold for boards made by this generator.
unsigned firePairs(const BitField& field, FieldBits trigger);
// Checks every condition of a valid result directly, by simulation, without
// relying on how the board was made. Returns an empty string when valid.
std::string verifySolution(const Solution& s);

struct GeneratorConfig {
    uint64_t initialSeed = 0;
    int colorCount = 0;
    int beamWidth = 0;
    int restarts = 0;
    int candidatesPerParent = 0;
    int targetSuccessCount = 0;
    int maxExtraPuyos = 0;
    int minExtraPuyos = 0;
    int targetChain = MAX_TARGET_CHAIN;
    int threads = 0; // 0 = automatic
};
GeneratorConfig loadConfig(const std::filesystem::path& path);
int effectiveThreads(const GeneratorConfig& config);

// One independent search. `stop` and `cancel` are polled; a stopped search returns nullopt.
std::optional<Solution> generateOne(const GeneratorConfig& config, Rng& rng,
                                    const std::atomic_bool* stop = nullptr,
                                    std::atomic_int* depthOut = nullptr,
                                    const std::atomic_bool* cancel = nullptr);

struct GeneratorProgress {
    int attempt = 0, totalAttempts = 0, depth = 0, targetChain = 0;
    int saved = 0, target = 0;
    std::string phase;
};
struct GeneratorHooks {
    const std::atomic_bool* cancel = nullptr;
    std::function<void(const GeneratorProgress&)> progress;
    std::function<void(const Solution&, uint64_t seed, int count)> saved;
};
// Exit codes: 0 target reached, 1 attempt limit reached, 2 error, 3 cancelled.
int runGenerator(const std::filesystem::path& configPath, const GeneratorHooks* hooks = nullptr,
                 const std::filesystem::path& outputDirectory = ".");

// ---- text output -----------------------------------------------------------
char colorChar(Cell color);
std::string fieldToUrl(const BitField& field); // ishikawapuyo.net board code
std::string pathToUtf8(const std::filesystem::path& path);
uint64_t parseSeed(const std::string& text);
int parsePositiveInt(const std::string& text, const std::string& key);
void printSolution(std::ostream& out, const Solution& solution, uint64_t seed);

} // namespace puyo
