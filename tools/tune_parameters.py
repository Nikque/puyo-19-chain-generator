"""Compare parameter sets over fixed seeds, verifying every generated solution.

python tools/tune_parameters.py EXECUTABLE PLAN.json OUTPUT_DIR --workers 2
PLAN contains seeds, attempts and settings: a list of benchmark overrides.
Results and logs are retained; no normal URL history is changed.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from verify_output import verify

parser = argparse.ArgumentParser()
parser.add_argument("executable", type=Path)
parser.add_argument("plan", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--workers", type=int, default=2)
args = parser.parse_args()
plan = json.loads(args.plan.read_text(encoding="utf-8"))
args.output.mkdir(parents=True, exist_ok=True)
benchmark = Path(__file__).with_name("benchmark.py")
executable_digest = hashlib.sha256(args.executable.read_bytes()).hexdigest()
benchmark_digest = hashlib.sha256(benchmark.read_bytes()).hexdigest()
time_limit = plan.get("seconds", 180)


def trial(index, settings, seed):
    name = f"p{index:02d}-seed{seed}"
    path = args.output / name
    path.mkdir(exist_ok=True)
    config = path / "result.json"
    # Resume only completed results from the identical plan.
    if config.exists():
        old = json.loads(config.read_text())
        if (old.get("overrides") == settings and
                old.get("planned_attempts") == plan["attempts"] and
                old.get("time_limit") == time_limit and
                old.get("executable_sha256") == executable_digest and
                old.get("benchmark_sha256") == benchmark_digest):
            return old
    command = [sys.executable, str(benchmark), str(args.executable.resolve()),
               "--seed", str(seed), "--restarts", str(plan["attempts"]),
               "--seconds", str(time_limit),
               "--save-log", str(path / "run.log")]
    for key, value in settings.items():
        command += ["--" + key.replace("_", "-"), str(value)]
    run = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
    if run.returncode:
        raise RuntimeError(run.stderr)
    result = json.loads(run.stdout)
    if result["exit_code"] not in (0, 1) and not result["timed_out"]:
        raise RuntimeError(f"{name}: generation failed: {result}")
    log = (path / "run.log").read_text(encoding="utf-8")
    result["verified_counts"] = verify(log, True) if result["successes"] else {}
    if sum(result["verified_counts"].values()) != result["successes"]:
        raise RuntimeError(f"{name}: saved count differs from verified output count")
    result.update(overrides=settings, planned_attempts=plan["attempts"], trial=name,
                  executable_sha256=executable_digest, benchmark_sha256=benchmark_digest,
                  time_limit=time_limit)
    config.write_text(json.dumps(result, indent=2), encoding="utf-8")
    return result


records = []
with ThreadPoolExecutor(max_workers=args.workers) as pool:
    jobs = [pool.submit(trial, index, settings, seed)
            for index, settings in enumerate(plan["settings"]) for seed in plan["seeds"]]
    for job in as_completed(jobs):
        record = job.result()
        records.append(record)
        print(json.dumps(dict(trial=record["trial"], settings=record["overrides"],
                              successes=record["successes"], attempts=record["completed_attempts"],
                              seconds=round(record["seconds"], 3))), flush=True)
        (args.output / "results.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
groups = {}
for row in records:
    key = json.dumps(row["overrides"], sort_keys=True)
    group = groups.setdefault(key, dict(settings=row["overrides"], successes=0, attempts=0, seconds=0,
                                       first_success_seconds=[], fifth_success_seconds=[]))
    group["successes"] += row["successes"]
    group["attempts"] += row["completed_attempts"]
    group["seconds"] += row["seconds"]
    for key in ("first_success_seconds", "fifth_success_seconds"):
        if row.get(key) is not None:
            group[key].append(row[key])
for group in groups.values():
    group["success_rate"] = group["successes"] / group["attempts"] if group["attempts"] else 0
    group["successes_per_second"] = group["successes"] / group["seconds"]
summary = sorted(groups.values(), key=lambda x: (x["successes_per_second"], x["success_rate"]), reverse=True)
(args.output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(dict(summary=summary), indent=2), flush=True)
