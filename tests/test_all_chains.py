"""Generate and independently verify every supported setting.

Chain lengths 1..19, four and five colours, and for each the smallest, an odd,
and the largest number of extra puyos (78 - 4 * chain). The independent
verifier checks the full definition of a valid board, so this run is what
confirms that the shortcuts taken inside the C++ search hold for every setting,
not only for 19 chains.
"""
import configparser
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from verify_output import verify

executable = str(Path(sys.argv[1]).resolve())
config = configparser.ConfigParser()
config.read(sys.argv[2], encoding="utf-8-sig")
defaults = {key: int(value) for key, value in config["Generator"].items()}
per_case = int(sys.argv[3]) if len(sys.argv) > 3 else 3
results = []
with tempfile.TemporaryDirectory(prefix="puyo-all-chains-") as temp:
    root = Path(temp)
    for chain in range(1, 20):
        capacity = 78 - 4 * chain
        for colors in (4, 5):
            for low, high in sorted({(0, 0), (1, 1), (capacity, capacity), (0, capacity)}):
                path = root / f"c{chain}-colors{colors}-extra{low}-{high}"
                path.mkdir()
                settings = dict(defaults, initial_seed=42, target_chain=chain,
                                color_count=colors, min_extra_puyos=low,
                                max_extra_puyos=high, target_success_count=per_case,
                                restarts=max(20000, defaults["restarts"]))
                source = path / "config.ini"
                source.write_text("".join(f"{key}={value}\n" for key, value in settings.items()))
                run = subprocess.run([executable, str(source)], cwd=path, capture_output=True,
                                     encoding="utf-8", timeout=120)
                assert run.returncode == 0, f"{settings}: {run.stdout[-500:]} {run.stderr}"
                verified = verify(run.stdout, True)
                assert sum(verified.values()) == per_case, (settings, verified)
                for key in verified:
                    cells = int(key.split("/")[1].split("-")[0])
                    assert key.startswith(f"{chain}-chain/") and 4 * chain + low <= cells <= 4 * chain + high, (settings, verified)
                assert len((path / "19chain_urls.txt").read_text().splitlines()) == per_case
                results.append(dict(chain=chain, colors=colors, min_extra=low, max_extra=high, verified=per_case))
                print(f"Verified chain={chain}, colors={colors}, extra={low}..{high}: {verified}", flush=True)
print(json.dumps(dict(cases=len(results), boards=per_case * len(results))))
