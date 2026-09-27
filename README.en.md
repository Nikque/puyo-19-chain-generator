# Puyo Puyo 19-Chain Field Generator

[日本語README](README.md)

A C++20 console program that randomly generates 19-chain fields from an empty board using four or five colors. Successful field URLs are appended to `19chain_urls.txt`. This is an independent project released under the MIT License.

## How it works

- The field has 6 columns and 13 rows. Puyos in row 13 can fall, but do not participate in connection or clearing checks.
- A field is accepted only when it produces 19 clearing waves, each removing exactly one connected group of four puyos.
- Beam search starts from the empty post-chain field and adds four-puyo predecessor groups in reverse. The completed field is simulated again to verify all 19 waves.
- At least one puyo in the initial trigger group must have no more than one puyo above it. The program also checks the specified restrictions on rows 12 and 13 of column 3.
- It searches for a gravity-compatible sequence of two-puyo placements whose final pair touches the trigger group.
- Duplicate URLs are skipped within the current run and against existing entries in `19chain_urls.txt`.

The placement search checks how pairs stack. It does not simulate the exact left/right movement and rotation inputs needed during play. Nuisance puyos are not supported.

## Build

Use a C++20 compiler such as Visual Studio 2022 and CMake 3.16 or newer. In PowerShell:

```powershell
cmake -S . -B build
cmake --build build --config Release
cd build/Release
.\random_19_chain.exe
```

You can also open this folder as a CMake project in Visual Studio. The build copies `config.ini` next to the executable. The `build/Release` path above is for a Visual Studio generator; with another generator, run the executable from its actual output directory.

## Configuration and output

Edit the `config.ini` next to the executable before running. The file includes Japanese guidance for every setting.

| Setting | Meaning |
| --- | --- |
| `target_success_count` | Number of new, distinct fields to save in this run |
| `initial_seed` | Starting random seed; the same settings reproduce the search order |
| `color_count` | Number of normal colors: 4 or 5 |
| `beam_width` | Maximum number of fields retained at each chain depth |
| `restarts` | Maximum search attempts; each attempt saves at most one field |
| `candidates_per_parent` | Number of candidates retained from each parent field |

If the attempt limit is reached before the target, the program keeps the URLs already written and exits. For more results, set `restarts` well above the target, then raise `beam_width` and `candidates_per_parent` gradually. Try both color counts or another `initial_seed` to explore different fields. Wider searches use more time and memory.

The program appends one URL per line to `19chain_urls.txt` in the **current working directory**. Existing URLs are excluded from new results and do not count toward `target_success_count`. The console shows the field, trigger positions, pair placement order, and search progress.

## License

[MIT License](LICENSE) © 2026 Nikque
