"""Integration checks for flexible counts, odd preparation and config compatibility."""
import pathlib
import subprocess
import sys
import tempfile
from verify_output import verify

executable = str(pathlib.Path(sys.argv[1]).resolve())
base = dict(initial_seed=42, color_count=4, beam_width=16,
            candidates_per_parent=8, restarts=50, target_success_count=1)


def run(settings, directory):
    config = directory / "config.ini"
    config.write_text("".join(f"{k}={v}\n" for k, v in settings.items()), encoding="utf-8")
    return subprocess.run([executable, str(config)], cwd=directory,
                          capture_output=True, encoding="utf-8", timeout=30)


with tempfile.TemporaryDirectory(prefix="puyo-cli-test-") as temp:
    root = pathlib.Path(temp)
    for index, (target, extra) in enumerate([(1, 0), (1, 1), (1, 2), (5, 3), (10, 2)]):
        directory = root / str(index)
        directory.mkdir()
        settings = dict(base, target_chain=target, min_extra_puyos=extra, max_extra_puyos=extra)
        result = run(settings, directory)
        assert result.returncode == 0, result.stderr
        counts = verify(result.stdout, True)
        assert counts == {f"{target}-chain/{4 * target + extra}-cell": 1}, counts
    # Old six-key files retain target 19, zero extras. One narrow attempt may
    # fail to find a field; it must still parse and execute without code 2.
    directory = root / "legacy"
    directory.mkdir()
    result = run(dict(base, beam_width=1, candidates_per_parent=1, restarts=1), directory)
    assert result.returncode in (0, 1)
    assert "target_chain=19" in result.stdout and "max_extra_puyos=0" in result.stdout
    for index, overrides in enumerate([
        dict(target_chain=0), dict(target_chain=20), dict(max_extra_puyos=-1),
        dict(target_chain=19, max_extra_puyos=3),
        dict(target_chain=18, max_extra_puyos=7),
        dict(min_extra_puyos=2, max_extra_puyos=1),
    ]):
        directory = root / f"invalid-{index}"
        directory.mkdir()
        result = run(dict(base, **overrides), directory)
        assert result.returncode == 2
        assert not (directory / "19chain_urls.txt").exists()
    directory = root / "日本語🟢"
    directory.mkdir()
    settings = dict(base, target_chain=1)
    config = directory / "設定🟢.ini"
    config.write_text("".join(f"{k}={v}\n" for k, v in settings.items()), encoding="utf-8-sig")
    result = subprocess.run([executable, str(config)], cwd=directory,
                            capture_output=True, encoding="utf-8", timeout=30)
    assert result.returncode == 0, result.stderr
    assert verify(result.stdout, True) == {"1-chain/4-cell": 1}
    missing = directory / "存在しない🟢.ini"
    result = subprocess.run([executable, str(missing)], cwd=directory,
                            capture_output=True, encoding="utf-8", timeout=30)
    assert result.returncode == 2 and str(missing) in result.stderr

    for index, ending in enumerate(["", "\r", "\n", "\r\n"]):
        directory = root / f"history-{index}"
        directory.mkdir()
        history = directory / "19chain_urls.txt"
        old_url = "https://ishikawapuyo.net/simu/pe.html?abc"
        history.write_bytes((old_url + ending).encode("ascii"))
        result = run(settings, directory)
        assert result.returncode == 0, result.stderr
        assert len(history.read_text().splitlines()) == 2
        assert history.read_text().splitlines()[0] == old_url
        first_urls = set(history.read_text().splitlines())
        result = run(settings, directory)
        assert result.returncode == 0, result.stderr
        assert "重複したため保存をスキップ" in result.stdout
        assert len(set(history.read_text().splitlines())) == 3
        assert first_urls <= set(history.read_text().splitlines())

    directory = root / "unwritable-output"
    directory.mkdir()
    (directory / "19chain_urls.txt").mkdir()
    result = run(settings, directory)
    assert result.returncode == 2
    assert "output file" in result.stderr
print("CLI integration tests passed (generation, Unicode/BOM paths, invalid config, URL append/deduplication, I/O errors).")
