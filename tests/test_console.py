"""Exercise a real, hidden Windows console as well as UTF-8 redirected output."""
import pathlib
import subprocess
import sys
import tempfile
import shutil

helper, executable = map(str, map(pathlib.Path, sys.argv[1:3]))
with tempfile.TemporaryDirectory(prefix="puyo-console-") as temp:
    report = pathlib.Path(temp) / "report.txt"
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    result = subprocess.run([helper, str(report)], startupinfo=startup,
                            creationflags=subprocess.CREATE_NEW_CONSOLE, timeout=15)
    assert result.returncode == 0, (result.returncode, report.read_text() if report.exists() else "no report")
    print(report.read_text())
    # Redirection retains UTF-8 even when no console is attached.
    config = pathlib.Path(temp) / "config.ini"
    config.write_text("initial_seed=42\ncolor_count=4\nbeam_width=16\nrestarts=50\n"
                      "candidates_per_parent=8\ntarget_success_count=1\ntarget_chain=1\n", encoding="utf-8")
    result = subprocess.run([executable, str(config)], cwd=temp, capture_output=True,
                            encoding="utf-8", timeout=15)
    assert result.returncode == 0, result.stderr
    assert "目標数 1 件のユニークな1連鎖盤面を生成しました。" in result.stdout
    print("Redirected Japanese output is valid UTF-8.")
    shutil.copy2(executable, pathlib.Path(temp) / "random_19_chain.exe")
    shutil.copy2(pathlib.Path(__file__).parents[1] / "run.cmd", pathlib.Path(temp) / "run.cmd")
    for mode in ("exe", "cmd"):
        (pathlib.Path(temp) / "19chain_urls.txt").unlink(missing_ok=True)
        result = subprocess.run([helper, str(report), str(executable), mode], cwd=temp,
                                startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE, timeout=20)
        assert result.returncode == 0, (mode, result.returncode, report.read_text())
        print(f"{mode}: Japanese text verified in a real CP932 console.")
