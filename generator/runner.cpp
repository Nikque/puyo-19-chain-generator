// Configuration, text output and the (optionally multi-threaded) run loop.
#include "generator.h"
#include "../app/console_output.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace puyo {

char colorChar(Cell color) { return ".RGBYP"[color]; }

std::string fieldToUrl(const BitField& field) {
    // ishikawapuyo.net board code: two cells (left * 8 + right) per character,
    // from the 13th row down, three characters per row; leading zeros dropped.
    static constexpr char DIGITS[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-";
    std::string encoded;
    for (int y = H - 1; y >= 0; --y)
        for (int x = 0; x < W; x += 2) {
            const char digit = DIGITS[field.get(x, y) * 8u + field.get(x + 1, y)];
            if (encoded.empty() && digit == '0') continue;
            encoded.push_back(digit);
        }
    return "https://ishikawapuyo.net/simu/pe.html?" + encoded;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

void printSolution(std::ostream& out, const Solution& solution, uint64_t seed) {
    BitField copy = solution.field;
    const int puyos = solution.field.count();
    out << "seed=" << seed << "\n";
    out << "chains=" << runChain(copy).chains << "\n";
    out << "puyos=" << puyos << ", extra_puyos=" << puyos - 4 * solution.targetChain << "\n";
    out << "field (top row first; columns left to right):\n";
    for (int y = H - 1; y >= 0; --y) {
        for (int x = 0; x < W; ++x) out << colorChar(solution.field.get(x, y)) << ' ';
        out << '\n';
    }
    out << "URL: " << fieldToUrl(solution.field) << "\n";
    out << "trigger cells (x from left, y from bottom, 1-based):";
    forEachCell(solution.trigger, [&](int x, int y) { out << " (" << x + 1 << ',' << y + 1 << ')'; });
    out << "\n";
    // Every way the board can be completed by one last pair that fires the chain.
    out << "last pair options (V vertical, H horizontal):";
    for (int id = 0; id < PAIR_POSITIONS; ++id) {
        if (!(solution.firePairs >> id & 1)) continue;
        out << ' ' << (id < W ? 'V' : 'H');
        forEachCell(pairCells(solution.field, id), [&](int x, int y) { out << '(' << x + 1 << ',' << y + 1 << ')'; });
    }
    out << "\n";
}

namespace {

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    const auto first = std::find_if(value.begin(), value.end(), notSpace);
    if (first == value.end()) return {};
    const auto last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return std::string(first, last);
}

constexpr const char* URL_FILE = "19chain_urls.txt";

std::unordered_set<std::string> loadExistingUrls(const std::filesystem::path& path) {
    std::unordered_set<std::string> urls;
    std::ifstream input(path);
    if (!input) {
        if (std::filesystem::exists(path)) throw std::runtime_error("cannot read output file: " + pathToUtf8(path));
        return urls; // First run: there is no output file yet.
    }
    const std::string prefix = "https://ishikawapuyo.net/simu/pe.html?";
    for (std::string line; std::getline(input, line);) {
        line = trim(std::move(line));
        if (line.compare(0, prefix.size(), prefix) == 0) urls.insert(std::move(line));
    }
    if (input.bad()) throw std::runtime_error("failed reading output file: " + pathToUtf8(path));
    return urls;
}

// Appends URLs to the history file, one per line, flushing after each so that
// an interrupted run keeps what it found. The file is opened on first use.
class UrlWriter {
    std::filesystem::path path;
    std::ofstream output;

public:
    explicit UrlWriter(std::filesystem::path p) : path(std::move(p)) {}
    void append(const std::string& url) {
        if (!output.is_open()) {
            // An edited/copied URL file may not end with a newline. Keep URLs separate.
            bool needsNewline = false;
            if (std::filesystem::exists(path)) {
                std::ifstream previous(path, std::ios::binary);
                if (!previous) throw std::runtime_error("cannot read output file: " + pathToUtf8(path));
                previous.seekg(0, std::ios::end);
                if (previous.tellg() > 0) {
                    previous.seekg(-1, std::ios::end);
                    char last = 0;
                    if (!previous.get(last)) throw std::runtime_error("failed reading output file: " + pathToUtf8(path));
                    needsNewline = last != '\n';
                }
            }
            output.open(path, std::ios::binary | std::ios::app);
            if (!output) throw std::runtime_error("cannot open output file: " + pathToUtf8(path));
            if (needsNewline) output << '\n';
        }
        output << url << '\n';
        output.flush();
        if (!output) throw std::runtime_error("failed writing output file: " + pathToUtf8(path));
    }
};

} // namespace

uint64_t parseSeed(const std::string& text) {
    if (text.empty() || text.front() == '-')
        throw std::runtime_error("initial_seed must be an unsigned 64-bit integer");
    size_t used = 0;
    const unsigned long long value = std::stoull(text, &used, 10);
    if (used != text.size()) throw std::runtime_error("invalid trailing characters in initial_seed");
    return uint64_t(value);
}

int parsePositiveInt(const std::string& text, const std::string& key) {
    size_t used = 0;
    const long long value = std::stoll(text, &used, 10);
    if (used != text.size() || value < 1 || value > std::numeric_limits<int>::max())
        throw std::runtime_error(key + " must be a positive integer");
    return int(value);
}

GeneratorConfig loadConfig(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open config file: " + pathToUtf8(path));

    GeneratorConfig config;
    std::unordered_set<std::string> seen;
    std::string line;
    for (int lineNumber = 1; std::getline(file, line); ++lineNumber) {
        // Accept a UTF-8 byte order mark.
        if (lineNumber == 1 && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        const size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos) line.erase(comment);
        line = trim(std::move(line));
        if (line.empty()) continue;
        // Section headers are skipped; key names are unique in the whole file.
        if (line.front() == '[' && line.back() == ']') continue;
        const size_t equal = line.find('=');
        if (equal == std::string::npos)
            throw std::runtime_error("expected key=value at config.ini line " + std::to_string(lineNumber));
        const std::string key = trim(line.substr(0, equal)), value = trim(line.substr(equal + 1));
        if (key.empty() || value.empty())
            throw std::runtime_error("empty key or value at config.ini line " + std::to_string(lineNumber));
        if (!seen.insert(key).second) throw std::runtime_error("duplicate config key: " + key);
        const auto orZero = [&] { return value == "0" ? 0 : parsePositiveInt(value, key); };
        if (key == "initial_seed") config.initialSeed = parseSeed(value);
        else if (key == "color_count") config.colorCount = parsePositiveInt(value, key);
        else if (key == "beam_width") config.beamWidth = parsePositiveInt(value, key);
        else if (key == "restarts") config.restarts = parsePositiveInt(value, key);
        else if (key == "candidates_per_parent") config.candidatesPerParent = parsePositiveInt(value, key);
        else if (key == "max_extra_puyos") config.maxExtraPuyos = orZero();
        else if (key == "min_extra_puyos") config.minExtraPuyos = orZero();
        else if (key == "target_chain") config.targetChain = parsePositiveInt(value, key);
        else if (key == "target_success_count") config.targetSuccessCount = parsePositiveInt(value, key);
        else if (key == "threads") config.threads = orZero();
        else throw std::runtime_error("unknown config key: " + key);
    }
    if (file.bad()) throw std::runtime_error("failed reading config file: " + pathToUtf8(path));

    for (const char* key : {"initial_seed", "color_count", "beam_width", "restarts", "candidates_per_parent",
                            "target_success_count"})
        if (!seen.contains(key)) throw std::runtime_error(std::string("missing config key: ") + key);
    if (config.colorCount != 4 && config.colorCount != 5) throw std::runtime_error("color_count must be 4 or 5");
    if (config.targetChain < MIN_TARGET_CHAIN || config.targetChain > MAX_TARGET_CHAIN)
        throw std::runtime_error("target_chain must be between 1 and 19");
    if (config.maxExtraPuyos > W * H - 4 * config.targetChain)
        throw std::runtime_error("max_extra_puyos exceeds 78 - 4 * target_chain");
    if (config.minExtraPuyos > config.maxExtraPuyos)
        throw std::runtime_error("min_extra_puyos must not exceed max_extra_puyos");
    if (config.threads > 256) throw std::runtime_error("threads must be at most 256");
    return config;
}

int effectiveThreads(const GeneratorConfig& config) {
    if (config.threads > 0) return config.threads;
    // Automatic: half of the logical processors, which is the number of cores
    // on a processor with two threads per core.
    return int(std::max(1u, std::thread::hardware_concurrency() / 2));
}

int runGenerator(const std::filesystem::path& configPath, const GeneratorHooks* hooks,
                 const std::filesystem::path& outputDirectory) {
    ConsoleOutputEncoding consoleEncoding;
    try {
        const GeneratorConfig config = loadConfig(configPath);
        const int threads = effectiveThreads(config);
        std::cout << "seed=" << config.initialSeed << ", colors=" << config.colorCount
                  << ", beam_width=" << config.beamWidth << ", restarts=" << config.restarts
                  << ", candidates_per_parent=" << config.candidatesPerParent
                  << ", target_success_count=" << config.targetSuccessCount
                  << ", max_extra_puyos=" << config.maxExtraPuyos << ", min_extra_puyos=" << config.minExtraPuyos
                  << ", target_chain=" << config.targetChain << ", threads=" << threads << "\n";

        const auto outputPath = outputDirectory / URL_FILE;
        std::unordered_set<std::string> seenUrls = loadExistingUrls(outputPath);
        UrlWriter urlWriter(outputPath);

        // Attempt k always uses random stream k, and attempts are committed in
        // numerical order, so the results do not depend on the thread count.
        std::mutex mutex;
        std::map<int, std::optional<Solution>> finished;
        std::atomic_int nextAttempt{1}, depth{0};
        std::atomic_bool stop{false};
        std::exception_ptr failure;
        int committed = 0, successCount = 0, duplicateCount = 0;
        bool progressShown = false;
        auto lastReport = std::chrono::steady_clock::time_point{};

        const auto cancelled = [&] { return hooks && hooks->cancel && hooks->cancel->load(); };
        const auto report = [&](int attempt, bool force) { // call with the mutex held
            const auto now = std::chrono::steady_clock::now();
            if (!force && now - lastReport < std::chrono::milliseconds(300)) return;
            lastReport = now;
            GeneratorProgress p{attempt, config.restarts, depth.load(), config.targetChain,
                                successCount, config.targetSuccessCount, "探索中"};
            if (hooks && hooks->progress) {
                hooks->progress(p);
                return;
            }
            std::ostringstream status;
            status << "再試行 " << p.attempt << '/' << p.totalAttempts << " | 段階 " << p.depth << '/'
                   << p.targetChain << " | 生成 " << p.saved << '/' << p.target;
            writeProgressLine(status.str());
            progressShown = true;
        };
        const auto commit = [&](int attempt, std::optional<Solution> result) { // mutex held
            finished.emplace(attempt, std::move(result));
            for (auto it = finished.find(committed + 1); it != finished.end() && !stop;
                 it = finished.find(committed + 1)) {
                const std::optional<Solution> solution = std::move(it->second);
                finished.erase(it);
                ++committed;
                if (solution) {
                    if (progressShown) std::cout << '\n';
                    progressShown = false;
                    const std::string url = fieldToUrl(solution->field);
                    if (!seenUrls.insert(url).second) {
                        ++duplicateCount;
                        std::cout << "既存または今回の生成結果と重複したため保存をスキップします。\n";
                    } else {
                        const std::string problem = verifySolution(*solution);
                        if (!problem.empty()) throw std::logic_error("generated board failed verification: " + problem);
                        urlWriter.append(url);
                        ++successCount;
                        printSolution(std::cout, *solution, config.initialSeed);
                        std::cout.flush();
                        if (hooks && hooks->saved) hooks->saved(*solution, config.initialSeed, successCount);
                        std::cout << "URLを" << URL_FILE << "に追記しました。\n"
                                  << "今回のユニークな生成数: " << successCount << '/' << config.targetSuccessCount
                                  << "\n";
                    }
                }
                if (successCount == config.targetSuccessCount || committed == config.restarts) stop = true;
            }
        };
        const auto worker = [&] {
            try {
                for (;;) {
                    const int attempt = nextAttempt.fetch_add(1);
                    if (attempt > config.restarts || stop || cancelled()) break;
                    {
                        std::lock_guard lock(mutex);
                        report(attempt, attempt == 1);
                    }
                    Rng rng(config.initialSeed, uint64_t(attempt));
                    std::optional<Solution> result = generateOne(config, rng, &stop, &depth,
                                                                 hooks ? hooks->cancel : nullptr);
                    if (stop || cancelled()) break; // an interrupted attempt is not a result
                    std::lock_guard lock(mutex);
                    commit(attempt, std::move(result));
                }
            } catch (...) {
                std::lock_guard lock(mutex);
                if (!failure) failure = std::current_exception();
                stop = true;
            }
        };

        if (threads == 1) {
            worker();
        } else {
            std::vector<std::thread> pool;
            for (int i = 0; i < threads; ++i) pool.emplace_back(worker);
            for (auto& t : pool) t.join();
        }
        if (failure) std::rethrow_exception(failure);
        if (progressShown) std::cout << '\n';

        if (successCount == config.targetSuccessCount) {
            std::cout << "目標数 " << config.targetSuccessCount << " 件のユニークな" << config.targetChain
                      << "連鎖盤面を生成しました。\n"
                      << "試行数: " << committed << ", 重複スキップ数: " << duplicateCount << "\n";
            return 0;
        }
        if (cancelled()) {
            std::cout << "Cancelled; saved results are retained.\n" << std::flush;
            return 3;
        }
        std::cerr << "試行上限に到達しました。ユニークな生成数: " << successCount << '/' << config.targetSuccessCount
                  << ", 試行数: " << committed << ", 重複スキップ数: " << duplicateCount << "\n"
                  << "config.iniのrestartsを増やすか、initial_seedを変更してください。\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Configuration or generation error: " << e.what() << "\n";
        return 2;
    }
}

} // namespace puyo
