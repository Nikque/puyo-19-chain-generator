# Puyo Chain Generator

[日本語 README](README.md)

## Windows GUI (v0.3.0)

Run `PuyoChainGenerator.exe` for settings, asynchronous generation/cancellation, a result list,
6×13 board display, construction navigation and chain playback. The original CLI
remains available. See [GUI_README.md](GUI_README.md) for usage and build instructions.
Each GUI run uses a new folder; existing CLI configuration and URL history are untouched.
The GUI links the same C++ engine directly, uses typed callbacks, and needs no additional runtime.
The board editor supports five colors, garbage, point, hard, iron Puyo and fixed walls.
It includes undo/redo, simulation playback and versioned .puyoboard save/load.
Point Puyo export is enabled only for the mattulwan mirror; unsupported destinations are disabled.
The GUI can export completed boards to Ishikawa Puyo, Puyo Park, and the pndsng mattulwan mirror.
See [URL_FORMATS.md](URL_FORMATS.md) for destination selection and compatibility checks.

A C++20 randomized inverse-chain beam search for playable constructions using pairs of puyos. Each of the configured 1–19 waves clears exactly one group of four. The default run generates **100 new 19-chain, 76-cell boards using four colors**. Successful URLs are appended to `19chain_urls.txt` in the working directory. Shorter chains use the same filename for compatibility.

## Windows release (v0.3.0)

Extract [the release](https://github.com/Nikque/puyo-19-chain-generator/releases/tag/v0.3.0)'s `puyo-chain-generator-v0.3.0-windows-x64.zip`. Run `PuyoChainGenerator.exe` for the Japanese GUI, or `run.cmd` for the original CLI. No additional runtime is required. The fixed default seed is **20261004**, matching the release date (October 4, 2026); loaded configurations retain their own seed. See [CHANGELOG.md](CHANGELOG.md) for changes.

Direct execution of the CLI exe also displays Japanese correctly: attached consoles use UTF-8 during execution, then regain their original output code page. Redirected logs remain UTF-8. Progress lines fit the console width to prevent wrapping during updates.

Windows configuration paths support Japanese and emoji. The launcher changes to its own directory to read settings and save URLs; the exe without arguments reads `config.ini` in its working directory. Existing settings remain compatible. An existing URL file without a final newline receives a separator before the next URL. Existing-file read failures are reported as errors.

Every CMake build synchronizes both `config.ini` and `run.cmd` beside the executable, including when only settings changed. Windows tests verify actual Japanese text emitted by both the exe and launcher in real consoles initially set to CP932, code-page restoration, and progress without wrapping. Additional integration checks cover Unicode/BOM configuration paths, unterminated URL history, duplicate exclusion, and I/O errors.

## Board rules and verification

- The board is six columns by 13 rows. Row 13 falls when supporting cells disappear but does not participate in matching.
- Each final chain wave clears one group of four, without simultaneous groups. The board before ignition contains `4 * target_chain + extra` cells, leaving `extra` cells after the chain.
- Even-cell boards are built directly from empty. Odd-cell boards use a planned five-clear with their first three pairs, leaving one cell. Subsequent placements remain stable until the final firing pair. Unplanned clears and death-cell occupancy in column 3, row 12 are rejected.
- Each pair has a route from the Tsu spawn (axis in column 3, row 12; child above), using horizontal movement, falling, rotations, side/floor kicks and two-input quick turns. The axis cannot enter row 14 or cross a wall filled through row 13.
- Horizontal placements may split across adjacent columns of unequal heights. The first three pairs use at most three colors, including in five-color mode.
- Every accepted solution replays its emitted controls from an empty board and verifies the constructed field and final chain. Previously saved URLs and duplicates from the current run do not count as new results.

**Scope:** This is a discrete placement model. Automatic fall timing, lock delays, per-version input timing and actual in-game RNG/tsumo-seed matching are not modeled. The output specifies required incoming colors and controls. No puyo is permanently placed in row 14; a child can temporarily pass through it while moving. Garbage puyos are not generated.

A 77-cell 19-chain uses a parity adjustment: two separated vertical AA pairs followed by AB connect five A cells and leave B. The remaining 38 pairs construct and ignite the board. There are 41 incoming pairs (82 cells), five preparation cells disappear, and the final 19-chain begins with 77. The preparation clear is not part of the target chain and is marked `setup_clear=5` in the output. Every preparation placement is replayed too. For a final board with N cells, even boards require N/2 pairs and odd boards require (N+5)/2 pairs.

## Build, run and test

Requires CMake 3.16 or newer and a C++20 compiler, such as Visual Studio 2022. Use **Release** for generation and performance measurements.

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cd build/Release
.\random_19_chain.exe
```

The `build/Release` path is for the Visual Studio multi-configuration generator. Single-configuration generators place the executable in `build`; configure those with `-DCMAKE_BUILD_TYPE=Release`. CMake copies `config.ini` beside the executable. You can also open the repository as a CMake project in Visual Studio.

The program reads `config.ini` from the working directory unless a path is passed as the first argument. Saved URLs always go to the working directory, even when the configuration is elsewhere.

```powershell
.\random_19_chain.exe C:\path\to\config.ini
```

Exit codes: **0** target reached; **1** restart limit reached with partial results retained; **2** configuration or I/O error. Increase `restarts` or change `initial_seed` when the target is not reached.

Emitted coordinates and colors use axis/child order. `L/R` move horizontally, `D` descends one row, and `A/B` turn clockwise/counterclockwise. Quick turns emit two equal rotation inputs. Lock after the route, then allow horizontal pairs to split and fall. Controls specify input order rather than frame durations.

## Configuration reference

Save `config.ini` as UTF-8, with or without a BOM. Lines use `key = value`; blank lines and text following `;` or `#` are ignored. Section headings such as `[Generator]` are ignored, so keys must be unique across the whole file. Key names are case-sensitive. Unknown keys, duplicates, missing required keys and invalid numbers cause errors.

| Key | Accepted values | Shipped value | Purpose |
| --- | --- | ---: | --- |
| `target_chain` | 1–19 | 19 | Number of final chain waves, excluding preparation. Optional; omitted value is 19. |
| `target_success_count` | Positive integer | 100 | Number of new unique boards to save in this run. Existing URLs and duplicates do not count. Generation stops on reaching this target. |
| `initial_seed` | Decimal integer, 0–18446744073709551615 | 20261004 | Random seed for search. The same implementation, settings and URL history reproduce the search. This is not an in-game tsumo seed. Change it to explore different boards. |
| `color_count` | 4 or 5 | 4 | Number of regular colors. Four matches ordinary Tsu matches; five is also supported. The first three pairs still use at most three colors. |
| `beam_width` | Positive integer | 48 | Maximum fields retained at each inverse-search depth. Wider beams retain more alternatives but increase time and memory per attempt. Wider does not always mean faster output. |
| `restarts` | Positive integer | 1000 | Maximum independent attempts. Each attempt yields at most one new board; failures and duplicates also consume attempts. The limit must at least equal the output target, and usually needs to be much larger. Partial results are retained. |
| `candidates_per_parent` | Positive integer | 2 | Candidates passed from each parent to the next depth. All candidates at the final depth are inspected. Tune together with beam width; the small default performed better per elapsed second. |
| `min_extra_puyos` | Nonnegative integer within capacity | 0 | Lower bound for cells beyond `4 * target_chain` immediately before ignition. Optional; omitted value is 0. |
| `max_extra_puyos` | Nonnegative integer within capacity | 0 | Upper bound, at most `78 - 4 * target_chain`. Optional; omitted value is 0. |

The six original keys (`target_success_count`, `initial_seed`, `color_count`, `beam_width`, `restarts`, `candidates_per_parent`) are required. Old configuration files remain supported. Positive integer keys other than `initial_seed` are limited to 2147483647, with the additional chain/color/capacity restrictions above.

Extra bounds must satisfy `0 <= min_extra_puyos <= max_extra_puyos <= 78 - 4 * target_chain`. Equal bounds request an exact count. Otherwise each attempt chooses uniformly within the range; successful counts need not be distributed uniformly. Extra cells remain after the final chain and do not include the five cells consumed by odd-board preparation. A valid configuration does not guarantee fast generation, especially for near-capacity boards. Increase `restarts` if a different seed or extra count fails to produce 100 results.

Complete shipped configuration:

```ini
[Generator]
target_chain = 19
target_success_count = 100
initial_seed = 20261004
color_count = 4
beam_width = 48
restarts = 1000
candidates_per_parent = 2
min_extra_puyos = 0
max_extra_puyos = 0
```

To request only 77-cell 19-chains, keep target 19 and set both extra bounds to 1. For 76–78 cells use min 0, max 2. For 18-chains with 72–78 cells use target 18, min 0, max 6. For exactly 75 cells at target 18, set both bounds to 3. The same capacity formula supports all 1–19 chain lengths.

## Search improvements and regression tests

Inverse search starts from a stable residual field containing the requested extra cells. New groups are inserted so that clearing exactly those four cells restores the previous field. Existing components are checked once before assigning colors; colors that merge with the inserted tetromino are rejected. Compact insertion descriptions are shuffled before materializing only the sampled fields, avoiding allocations for candidates that would be discarded. This preserves selection order and random state. Fixed scratch arrays reduce component allocations. Beam selection remains randomized to retain diversity; prioritizing balanced heights performed worse and was not adopted.

CTest checks hidden-row physics, death, both directions of blocked wall crossing, legal row-12 quick turns, split placement, premature clears and corrupted routes. It compares predecessor sets against the original exhaustive implementation on 160 fields and checks sampled order and RNG state. With Python 3 available, all 1–19 chain lengths are generated and independently replayed in **76 cases** (four/five colors, zero/one extra cell), using the shipped search parameters. Additional tests cover invalid/legacy configurations and recorded 76/77/78-cell solutions.

```powershell
python tests/verify_output.py tests/fixtures/solutions.log --allow-extra
.\build\Release\random_19_chain.exe config.ini > generator.log
python tests/verify_output.py generator.log --allow-extra
```

## Generation speed measurements

Measured on 2026-09-30, Windows x64, Visual Studio 2022 / MSVC 19.44, Release / O2. Each run starts with an empty URL history and produces 20 four-color, 76-cell 19-chains. Startup and failed attempts are included. Final comparisons run one generator process at a time. First/fifth output times are medians over the seeds tested.

| Beam / candidates per parent | Seeds | Verified outputs | Total seconds | Outputs/second | First output (s) | Fifth output (s) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 128 / 32 | 3 | 60 | 59.29 | 1.01 | 0.30 | 5.04 |
| 32 / 2 | 6 | 120 | 47.41 | 2.53 | 0.39 | 1.79 |
| **48 / 2 (selected)** | 6 | 120 | 43.31 | **2.77** | **0.47** | **1.81** |

Using the same three seeds, 48/2 provides about 2.49 times the outputs/second of 128/32. The first-result latency varies; 128/32 is faster for some seeds. Defaults balance sustained output speed with the wait for five results. Initial comparisons included four/five colors, beams 4–4096 and parent limits 2–32. Very wide beams improved per-attempt success probability but reduced output speed.

The v0.2.0–v0.2.1 shipped settings (target 100, restart limit 1000, seed 20260925) also produced **100 boards in 36.29 seconds over 713 attempts**. The first output arrived at 0.57 seconds and the fifth at 1.71 seconds. All 100 constructions and chains passed independent replay. [Raw 100-board results](benchmarks/default-100-results.json). These historical measurements are not speed guarantees for v0.3.0's default seed 20261004.

Extra-cell measurements with the same 48/2 parameters, three seeds and ten results per seed:

| Request | Verified outputs | Total seconds | Outputs/second |
| --- | ---: | ---: | ---: |
| 19-chain, exactly 77 cells | 30 | 15.36 | 1.95 |
| 19-chain, exactly 78 cells | 30 | 33.19 | 0.90 |
| 19-chain, 76–78 cells | 30 | 14.72 | 2.04 |
| 18-chain, 72–78 cells | 30 | 8.61 | 3.48 |
| 10-chain, exactly 43 cells | 30 | 0.45 | 66.77 |

An additional five 77-cell 18-chains were verified in 3.07 seconds with seed 42, covering every board size from 72 to 78 for target 18. All outputs are replayed by an independent Python simulator, checking movement routes, intermediate clears, final cell counts and every chain wave. Performance depends on CPU, build, seed, saved history, chain length and extra count.

See [BENCHMARK.md](BENCHMARK.md) for the full measurement record and original-code comparison. Raw data are in [benchmarks/sequential-results.json](benchmarks/sequential-results.json) and [benchmarks/extra-results.json](benchmarks/extra-results.json).

Reproduce measurements in temporary directories without modifying normal URL history:

```powershell
python tools/benchmark.py build/Release/random_19_chain.exe --target 100 --restarts 1000 --seconds 180 --save-log run100.log
python tests/verify_output.py run100.log --allow-extra
python tools/tune_parameters.py build/Release/random_19_chain.exe benchmarks/refine-plan.json measurements --workers 1
```

Use `--seed`, `--colors`, `--beam`, `--per-parent`, `--chain`, `--min-extra` and `--extra` to select parameters. JSON records elapsed time, successes, completed attempts, first/fifth arrival times and timeout status. For fair comparisons, use several seeds and run one process at a time on the same PC.

## Rule references

- [Fish1201: Tsu rotations and off-screen controls](https://puyo-camp.jp/posts/65520): axis cannot enter row 14; row-14 child behavior. This generator excludes permanent row-14 placements.
- [APES author's rotation implementation notes](https://w.atwiki.jp/apes_puyo2/pages/15.html): row-13 falling/matching, floor kicks and axis/child swapping in quick turns. The APES-specific downward kick from row 14 is not used.
- [SEGA: Puyo Puyo Tsu](https://vc.sega.jp/3ds/puyo2/): death cell in column 3 and quick turns.

## License

[MIT License](LICENSE) © 2026 Nikque
