#include "../gui/gui_urls.h"
#include <iostream>
#include <thread>
using namespace puyo_gui;
static void require(bool v, const char* message) {
    if (!v) throw std::runtime_error(message);
}
// "frames ignition" then one line per frame: 0, chain and the 78 cells from the top row.
// (The leading 0 is the pair counter of the v0.3.0 format, kept for the Python checker.)
static void saveTimeline(const std::filesystem::path& path, const std::vector<Frame>& frames, size_t ignition) {
    std::ofstream out(path, std::ios::binary);
    out << frames.size() << ' ' << ignition << '\n';
    for (const auto& f : frames) {
        out << 0 << ' ' << f.chain;
        for (int y = 12; y >= 0; --y)
            for (int x = 0; x < 6; ++x) out << ' ' << int(f.board.cells[x][y]);
        out << '\n';
    }
    out.flush();
    require(bool(out), "cannot save displayed timeline");
}
int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 2, "need a writable test directory");
        std::filesystem::path root = argv[1];
        std::filesystem::create_directories(root);
        auto c = defaults();
        writeConfig(root / L"設定😀.ini", c);
        auto loaded = loadConfig(root / L"設定😀.ini");
        require(configText(c) == configText(loaded), "config defaults roundtrip");
        BitField colors;
        for (int x = 0; x < 6; ++x) colors.set(x, 0, Cell(x % 5 + 1));
        require(simulatorUrl(colors, UrlFormat::PuyoPark) == "https://www.puyop.com/s/asF", "puyop five-color golden URL");
        require(simulatorUrl(colors, UrlFormat::Mattulwan) == "https://www.pndsng.com/puyo/index.html?a72becdfb", "mattulwan five-color golden URL");
        require(simulatorUrl(BitField{}, UrlFormat::Mattulwan) == "https://www.pndsng.com/puyo/index.html?a78", "empty field run length");
        require(simulatorUrl(colors, UrlFormat::Ishikawa) == fieldToUrl(colors), "legacy format unchanged");
        require(boardFromBits(bitsFromBoard(boardFromBits(colors))) == boardFromBits(colors), "board conversion roundtrip");
        std::array<std::string, INPUT_COUNT> values{"19", "100", "18446744073709551615", "4", "0", "0", "48", "2", "1000", "0"};
        require(parseInputs(values).initialSeed == UINT64_MAX && parseInputs(values).threads == 0, "uint64 precision");
        for (auto [index, value] : std::initializer_list<std::pair<int, std::string>>{
                 {0, "0"}, {0, "20"}, {1, "0"}, {2, "18446744073709551616"}, {2, "1.5"}, {2, "-1"}, {3, "3"},
                 {4, "3"}, {5, "3"}, {6, "2147483648"}, {7, "abc"}, {8, "0"}, {9, "257"}, {9, "-1"}}) {
            auto t = values;
            t[index] = value;
            bool rejected = false;
            try { parseInputs(t); } catch (...) { rejected = true; }
            require(rejected, "invalid input accepted");
        }
        std::ofstream log(root / L"combined.log", std::ios::binary);
        auto old = std::cout.rdbuf(log.rdbuf());
        auto err = std::cerr.rdbuf(log.rdbuf());
        struct Restore {
            std::streambuf *a, *b;
            ~Restore() { std::cout.rdbuf(a); std::cerr.rdbuf(b); }
        } restore{old, err};
        int cases = 0;
        for (int chain : {1, 19})
            for (int colorCount : {4, 5})
                for (int extra : {0, 1, 2}) {
                    auto dir = root / std::to_wstring(++cases);
                    std::filesystem::create_directory(dir);
                    auto config = defaults();
                    config.targetChain = chain;
                    config.colorCount = colorCount;
                    config.targetSuccessCount = 1;
                    config.minExtraPuyos = config.maxExtraPuyos = extra;
                    config.threads = 1 + cases % 3; // the result must not depend on the thread count
                    writeConfig(dir / L"config.ini", config);
                    int count = 0;
                    GeneratorHooks hooks;
                    hooks.saved = [&](const Solution& sol, uint64_t, int n) {
                        ++count;
                        require(n == 1, "unexpected count");
                        require(verifySolution(sol).empty(), "saved board must verify");
                        auto frames = timeline(sol);
                        auto ignition = ignitionFrame(sol);
                        require(ignition == 0 && frames[0].board == boardFromBits(sol.field), "ignition display mismatch");
                        require(frames[0].marked.size() == 4 && frames[0].pair.size() == 2, "trigger and last pair are marked");
                        require(frames.back().chain == chain && frames.size() == size_t(1 + 2 * chain), "playback chain count");
                        require(bitsFromBoard(frames.back().board).count() == extra, "residual display mismatch");
                        int waves = 0;
                        for (auto& f : frames)
                            if (f.chain) {
                                ++waves;
                                require(f.marked.empty() && f.pair.empty(), "no highlight after ignition");
                            }
                        require(waves == 2 * chain, "post-clear and post-drop frames per chain");
                        require(frames.back().score > 0 && (chain > 1 || frames.back().score == 40), "score of the chain");
                        saveTimeline(dir / L"timeline.txt", frames, ignition);
                        saveRecord(dir / L"盤面.puyo", sol, UINT64_MAX);
                        auto r = loadRecord(dir / L"盤面.puyo");
                        require(r.seed == UINT64_MAX && r.solution.field == sol.field && r.solution.trigger == sol.trigger &&
                                r.solution.firePairs == sol.firePairs && r.solution.targetChain == sol.targetChain, "archive roundtrip");
                    };
                    require(runGenerator(dir / L"config.ini", &hooks, dir) == 0 && count == 1, "generate and play boundary cases");
                    std::vector<Record> records{loadRecord(dir / L"盤面.puyo")};
                    for (int format = 0; format < 3; ++format)
                        writeSimulatorUrls(dir / (std::string("urls-") + urlFormatIds[format] + ".txt"), records, UrlFormat(format));
                }
        // Cancellation: the search must stop promptly and report code 3.
        auto dir = root / L"cancel";
        std::filesystem::create_directory(dir);
        auto endless = defaults();
        endless.targetSuccessCount = 100000000;
        endless.restarts = 2000000000;
        writeConfig(dir / L"config.ini", endless);
        std::atomic_bool cancel = false;
        std::atomic_int saved = 0;
        GeneratorHooks hooks;
        hooks.cancel = &cancel;
        std::chrono::steady_clock::time_point cancelTime;
        hooks.saved = [&](const Solution&, uint64_t, int n) {
            saved = n;
            if (n == 5) {
                cancelTime = std::chrono::steady_clock::now();
                cancel = true;
            }
        };
        require(runGenerator(dir / L"config.ini", &hooks, dir) == 3, "cooperative cancellation");
        require(std::chrono::steady_clock::now() - cancelTime < std::chrono::seconds(1), "slow cancel");
        require(saved >= 5, "results before cancellation are kept");
        // Archives: a corrupt file, and a version 1 file made by v0.3.0.
        std::ofstream broken(root / L"bad.puyo");
        broken << "PUYO_GUI 2\n0 19\n9999999";
        broken.close();
        bool rejected = false;
        try { loadRecord(root / L"bad.puyo"); } catch (...) { rejected = true; }
        require(rejected, "corrupt archive accepted");
        std::ofstream v1(root / L"v1.puyo", std::ios::binary);
        v1 << "PUYO_GUI 1\n7 1\n1 1\n1 1\n1 1\n1 1\n0\n0\n0 0\n1 0\n2 0\n3 0\n2\n"
              "0 0 1 0 1 1 H 0 \"LL\"\n2 0 3 0 1 1 H 0 \"\"\n";
        v1.close();
        auto oldRecord = loadRecord(root / L"v1.puyo");
        require(oldRecord.seed == 7 && oldRecord.solution.field.count() == 4 && oldRecord.solution.firePairs != 0,
                "version 1 archive loads without its build sequence");
        log.flush();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
