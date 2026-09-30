"""Generate and independently replay every supported chain length.

Both four/five colors and even/odd board sizes are covered (76 cases).
The actual shipped defaults are read from config.ini, so parameter changes are
also exercised. Odd boards must have exactly the planned five-clear preparation.
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
results = []
with tempfile.TemporaryDirectory(prefix="puyo-all-chains-") as temp:
    root = Path(temp)
    for chain in range(1, 20):
        for colors in (4, 5):
            for extra in (0, 1):
                path = root / f"c{chain}-colors{colors}-extra{extra}"
                path.mkdir()
                settings = dict(defaults, initial_seed=42, target_chain=chain,
                                color_count=colors, min_extra_puyos=extra,
                                max_extra_puyos=extra, target_success_count=1,
                                restarts=max(100, defaults["restarts"]))
                source = path / "config.ini"
                source.write_text("".join(f"{key}={value}\n" for key, value in settings.items()))
                run = subprocess.run([executable, str(source)], cwd=path, capture_output=True,
                                     encoding="utf-8", timeout=60)
                assert run.returncode == 0, f"{settings}: {run.stderr}"
                verified = verify(run.stdout, True)
                expected = {f"{chain}-chain/{4 * chain + extra}-cell": 1}
                assert verified == expected, (settings, verified)
                assert len((path / "19chain_urls.txt").read_text().splitlines()) == 1
                results.append(dict(chain=chain, colors=colors, extra=extra,
                                    puyos=4 * chain + extra, verified=True))
                print(f"Verified chain={chain}, colors={colors}, extra={extra}", flush=True)
print(json.dumps(dict(cases=len(results), chain_lengths=list(range(1, 20)), results=results)))
