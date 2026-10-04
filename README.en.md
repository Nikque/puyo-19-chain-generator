# Puyo Chain Generator

[日本語 README](README.md)

A C++20 program that randomly generates Puyo Puyo boards (four or five colours) which fire an exact
chain of 1 to 19 waves when one last pair is placed. The URL of every board found is appended to
`19chain_urls.txt`. The shipped settings generate 100 new 19-chain boards of 76 puyos in four colours.

All board work is done on bitboards: one 16-bit lane per column, six columns in one SSE register,
three registers for the colour code (48 bytes per board). The same core (`core/`) is meant to be reused
for solo play, versus play and an AI.

## Windows GUI

`PuyoChainGenerator.exe` offers settings, asynchronous generation with cancellation, a result list, a
6×13 board and chain playback. The CLI (`random_19_chain.exe` / `run.cmd`) remains available. See
[GUI_README.md](GUI_README.md) (Japanese) for usage and [URL_FORMATS.md](URL_FORMATS.md) for the simulator URLs.
The board editor supports five colours, garbage, point, hard and iron puyos and fixed walls; these
special kinds are never generated.

## What a generated board satisfies

The output is the board **right after the last pair has been placed**.

1. 6 columns × 13 rows, no gaps inside a column, nothing in the 14th row.
2. Firing it gives exactly the requested number of waves, and every wave clears **one group of exactly four**.
3. The 13th row never connects or clears, but falls when the puyos below it disappear.
4. After the chain, the cell where pairs appear (third column, twelfth row) is empty.
5. The board holds `4 * target_chain + extra` puyos; `extra` puyos remain after the chain.
6. There is at least one **last pair**: the two topmost puyos of one column, or the topmost puyos of two
   neighbouring columns, such that without them
   - nothing clears,
   - the cell where pairs appear is empty, and
   - the pair can travel to its place (reach rule below).

The board itself may cover the cell where pairs appear: the last pair may be placed there as long as the
chain frees it again (item 4). For each board the program prints the four puyos that clear first and
**every** last pair that satisfies item 6.

### Reach rule

The pair appears in the third column (axis in row 12, child in row 13). Whether a position can be reached
is decided from the column heights alone, following puyoai's `PuyoController::isReachable`:

- a column of height 11 or less can always be passed;
- a column of height 13 can never be passed (the axis puyo cannot enter the 14th row);
- a column of height 12 can be passed only while the axis puyo is lifted to the 13th row: right after
  the spawn when both neighbours of the third column are 12 or higher (quick turn), when the previous
  column is exactly 11 high (floor kick), or across a one-column gap from another 12-high column;
- a vertical pair needs a column of height 11 or less (nothing is left in the 14th row).

Every placement this rule allows is also allowed by the movement model of v0.3.0 (kicks and quick turns
searched without gravity); this was checked for all 6.45 million height combinations with
`tools/compare_reach.cpp`. The only difference is hovering across a low column.

### Not guaranteed

- **No build sequence from an empty board is produced** (v0.3.0 did). The guarantee is only that placing
  the last pair fires the chain.
- Frame timing, lock delay and per-platform differences are not modelled. Garbage puyos are not generated.

### Every board is generated independently

Each attempt uses its own random stream and starts from the board that remains after the chain; one
attempt saves at most one board. Found boards are never varied to mass-produce similar ones. URLs already
in `19chain_urls.txt` are skipped.

## How the search works

The chain is grown backwards. Starting from the residual board (only the extra puyos, nothing clears),
each step inserts a group of four cells into the columns. A step is accepted only if, on the new board,
(i) no group of four or more exists among the old puyos and (ii) no old puyo next to the inserted cells
has their colour. Then the inserted group is the only thing that clears, and gravity gives back the
previous board exactly. By induction a board made by n steps clears in exactly n waves of one group of
four, starting with the group inserted last and ending as the residual board - for every chain length,
extra count and colour count. So no chain simulation runs inside the search:

| Condition | Checked during the search |
| --- | --- |
| number of waves, single groups of four, trigger | follows from the construction: not checked |
| spawn cell empty after the chain | the final board is the residual board, which is built with at most 11 puyos in the third column |
| nothing in the 14th row | insertions keep every column at 13 or lower |
| nothing clears without the last pair | equivalent to "the pair holds a trigger puyo": one mask test |
| the last pair can appear and reach its place | from the heights, per candidate pair |

Insertion is a per-column multiplication, "is there a group of four or more" is derived from four
same-colour-neighbour masks without flood fill, and candidate positions come from precomputed bit sets.
Each step keeps `candidates_per_parent` uniformly chosen children per board and at most `beam_width`
boards; the step before the last keeps every child.

On a nearly full board the last pair has very little room: with 76 puyos a trigger puyo of a valid last
pair must sit at row 10 or higher in the third column or at row 12 in the second or fourth, and nowhere
else. So for boards of **72 puyos or more**, the six steps before the last use only insertions that leave
at most one old puyo above the new group in one of its columns. This raises the share of successful
attempts for 19 chains / 76 puyos from 24% to 52%. Smaller boards, where the rules leave the trigger almost
free, are searched without this guidance so that their variety is not narrowed.

**Every saved board is checked against the full definition directly** (`verifySolution`), and an
independent Python cell simulator (`tests/verify_output.py`) checks the same conditions, including that
the printed last pairs are exactly all valid ones.

Attempts run on several threads. Attempt k always uses random stream k and results are committed in
order, so **the same settings give the same boards in the same order for any thread count and compiler**.

## Build and test

A C++20 compiler (e.g. Visual Studio 2022) and CMake 3.16 or later. On x64 the code uses SSE4.1 and
POPCNT; PEXT/PDEP is chosen at run time. A plain 64-bit implementation is included and tested as well.

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The tests compare the bitboard core with cell-by-cell references (including the unchanged v0.3.0 engine
in `tests/reference/`), generate and independently verify 152 settings (chains 1-19, 4/5 colours, minimum,
odd and maximum extras), and cover configuration, history files, thread independence and the GUI.

## Settings

| Key | Meaning | Shipped |
| --- | --- | --- |
| `target_chain` | number of waves, 1-19 | 19 |
| `target_success_count` | new unique boards to save in this run | 100 |
| `initial_seed` | random seed (unsigned 64-bit) | 20261004 |
| `color_count` | 4 or 5 | 4 |
| `beam_width` | boards kept per step | 48 |
| `restarts` | maximum number of independent attempts | 100000 |
| `candidates_per_parent` | children kept per board | 2 |
| `min_extra_puyos` / `max_extra_puyos` | range of extra puyos, at most `78 - 4 * target_chain` | 0 / 0 |
| `threads` | search threads, 0 = automatic (half of the logical processors) | 0 |

The first six keys (as in older files) are required; older configuration files work unchanged.
Exit codes: 0 target reached, 1 attempt limit reached (URLs found so far are kept), 2 configuration or I/O error.

## Speed

Windows 11 x64, Ryzen 9 9950X, Visual Studio 2022, Release, 2026-10-04, seed 20261004, beam 48 / 2.
Other work was running on the machine, so the numbers are indicative.

| Setting | v0.3.0 (20 boards) | new, 1 thread (2000 boards) | new, 16 threads (2000 boards) |
| --- | ---: | ---: | ---: |
| 19 chains, 76 puyos, 4 colours | 3.81 s (5.2/s) | 2.52 s (794/s) | 0.23 s |
| 19 chains, 77 puyos | 8.65 s (2.3/s) | 3.13 s (639/s) | 0.28 s |
| 19 chains, 78 puyos | 17.54 s (1.1/s) | 4.76 s (420/s) | 0.41 s |
| 18 chains, 72-78 puyos | 2.87 s (7.0/s) | 2.15 s (930/s) | 0.20 s |
| 19 chains, 76 puyos, 5 colours | 5.51 s (3.6/s) | 2.96 s (676/s) | 0.27 s |

v0.3.0 also searched for a build sequence from an empty board, so this is not a like-for-like comparison.
See [BENCHMARK.md](BENCHMARK.md) (Japanese).

## License

[MIT License](LICENSE) © 2026 Nikque
