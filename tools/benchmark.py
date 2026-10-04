"""Run a bounded, reproducible generation benchmark in a fresh directory.

python tools/benchmark.py path/to/random_19_chain.exe --seed 20260925 --colors 4
Use --legacy to omit the newer keys when benchmarking an old executable.
"""
import argparse
import json
import re
from pathlib import Path
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("executable", type=Path)
parser.add_argument("--seed", type=int, default=20260925)
parser.add_argument("--colors", type=int, choices=[4, 5], default=4)
parser.add_argument("--beam", type=int, default=48)
parser.add_argument("--per-parent", type=int, default=2)
parser.add_argument("--extra", type=int, default=0)
parser.add_argument("--min-extra", type=int, default=0)
parser.add_argument("--chain", type=int, default=19)
parser.add_argument("--restarts", type=int, default=100)
parser.add_argument("--target", type=int)
parser.add_argument("--seconds", type=float, default=60)
parser.add_argument("--threads", type=int, default=1, help="search threads, 0 = automatic")
parser.add_argument("--legacy", action="store_true")
parser.add_argument("--save-log", type=Path)
args = parser.parse_args()
executable = args.executable.resolve(strict=True)
settings = dict(initial_seed=args.seed, color_count=args.colors, beam_width=args.beam,
                candidates_per_parent=args.per_parent, restarts=args.restarts,
                target_success_count=args.target if args.target is not None else args.restarts)
if not args.legacy:
    settings["max_extra_puyos"] = args.extra
    settings["min_extra_puyos"] = args.min_extra
    settings["target_chain"] = args.chain
    settings["threads"] = args.threads
with tempfile.TemporaryDirectory(prefix="puyo-benchmark-") as directory:
    work = Path(directory)
    config = work / "config.ini"
    config.write_text("".join(f"{key}={value}\n" for key, value in settings.items()))
    log = work / "generator.log"
    start = time.perf_counter()
    timeout = False
    arrivals = []
    urls_path = work / "19chain_urls.txt"
    with log.open("wb") as output:
        process = subprocess.Popen([str(executable), str(config)], cwd=work,
                                   stdout=output, stderr=output)
        while True:
            now = time.perf_counter()
            if urls_path.exists():
                count = len(urls_path.read_text().splitlines())
                arrivals.extend([now - start] * max(0, count - len(arrivals)))
            if process.poll() is not None:
                break
            if now - start >= args.seconds:
                timeout = True
                process.kill()
                process.wait()
                break
            try:
                process.wait(timeout=0.01)
            except subprocess.TimeoutExpired:
                pass
    elapsed = time.perf_counter() - start
    urls = urls_path.read_text().splitlines() if urls_path.exists() else []
    text = log.read_text(encoding="utf-8", errors="replace")
    summaries = re.findall(r"試行数: (\d+)", text)
    completed = (int(summaries[-1]) if summaries else
                 len(urls) + len(re.findall(r"再試行 \d+/\d+ は解を見つけられませんでした", text))
                 + text.count("重複したため保存をスキップ"))
    if args.save_log:
        args.save_log.write_bytes(log.read_bytes())
    print(json.dumps(dict(settings=settings, seconds=elapsed, successes=len(urls),
                          completed_attempts=completed,
                          first_success_seconds=arrivals[0] if arrivals else None,
                          fifth_success_seconds=arrivals[4] if len(arrivals) >= 5 else None,
                          unique=len(set(urls)), timed_out=timeout, exit_code=process.returncode),
                     indent=2))
