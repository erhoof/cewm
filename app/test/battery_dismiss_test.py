"""Headless WM6.1 QVGA regression: dismiss low battery, then open Start.

Usage: CEWM_DATA_DIR=... python3 battery_dismiss_test.py /path/to/cewm-probe /path/to/ROM [--unicorn]
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

probe, rom = map(lambda arg: str(Path(arg).resolve()), sys.argv[1:3])
env = {key: value for key, value in os.environ.items() if not key.startswith("CEWM_") or key == "CEWM_DATA_DIR"}
env.update(CEWM_BOARD="devemu", CEWM_SIZE="240x320", CEWM_BATT="10", CEWM_CHG="0",
           CEWM_EXTRA="35", CEWM_TEST_TAPS="40:303,20:15", SDL_AUDIODRIVER="dummy")
if "--unicorn" in sys.argv[3:]:
    env["CEWM_UNICORN"] = "1"
with tempfile.TemporaryDirectory(prefix="cewm-battery-") as work:
    result = subprocess.run([probe, rom, "20"], env=env, cwd=work,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180)
    print(result.stdout)
    assert result.returncode == 0, f"emulator exited with {result.returncode}"
    boot = re.findall(r"t\+\d+s frame hash ([0-9a-f]+)", result.stdout)
    taps = re.findall(r"tap \d+,\d+ frame hash ([0-9a-f]+)", result.stdout)
    assert boot and len(taps) == 2, "missing frames"
    assert boot[-1] != taps[0], "Dismiss did not change the frame"
    assert taps[0] != taps[1], "guest did not redraw after subsequent Start input"
print("PASS: low-battery dismissal and subsequent input")
