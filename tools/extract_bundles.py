#!/usr/bin/env python3
"""Extract ROM binaries under bundled/devices/<dev>/ via the
references/extract-wince-rom submodule, in parallel.

For each .nb0 / .bin file under bundled/devices/<dev>/, runs:
  python references/extract-wince-rom/extract_wince_rom.py <bin>
    -o references/extracted-roms/<dev>/<bin_filename>/

Then, if bundled/devices/<dev>/pdbs/ exists and contains files, copies all
of its contents into references/extracted-roms/<dev>/fs/Windows/.

Usage:
  python tools/extract_bundles.py [--machine=ARCH] <device_name> [device_name ...]

At least one device name is required. --machine is forwarded to the
extractor and applies to every named device.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent
BUNDLED = REPO / "bundled" / "devices"
EXTRACTED = REPO / "references" / "extracted-roms"
EXTRACTOR = REPO / "references" / "extract-wince-rom" / "extract_wince_rom.py"

ROM_EXTS = {".nb0", ".bin"}


def find_rom_files(device_dir: Path) -> list[Path]:
    return sorted(
        p for p in device_dir.iterdir()
        if p.is_file() and p.suffix.lower() in ROM_EXTS
    )


def run_extractor(rom_path: Path, out_dir: Path,
                  machine: str | None) -> tuple[Path, int, str]:
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [sys.executable, str(EXTRACTOR), str(rom_path), "-o", str(out_dir)]
    if machine is not None:
        cmd += ["--machine", machine]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    output = proc.stdout + proc.stderr
    return rom_path, proc.returncode, output


def copy_pdbs(device_dir: Path, device_out: Path, roms: list[Path]) -> int:
    pdbs = device_dir / "pdbs"
    if not pdbs.is_dir():
        return 0
    entries = list(pdbs.iterdir())
    if not entries:
        return 0
    if len(roms) != 1:
        rom_list = ", ".join(r.name for r in roms) if roms else "(none)"
        raise RuntimeError(
            f"{device_dir.name}: pdbs/ has {len(entries)} entr(y/ies) but "
            f"device has {len(roms)} ROM(s) [{rom_list}]; PDBs target a single "
            f"ROM's fs/Windows/ and the destination is ambiguous"
        )
    dest = device_out / roms[0].name / "fs" / "Windows"
    dest.mkdir(parents=True, exist_ok=True)
    count = 0
    for entry in entries:
        target = dest / entry.name
        if entry.is_dir():
            shutil.copytree(entry, target, dirs_exist_ok=True)
        else:
            shutil.copy2(entry, target)
        count += 1
    return count


def collect_jobs(devices: list[str]) -> list[tuple[str, Path, Path]]:
    jobs: list[tuple[str, Path, Path]] = []
    for dev in devices:
        device_dir = BUNDLED / dev
        if not device_dir.is_dir():
            print(f"[{dev}] skip: not a directory under bundled/devices/")
            continue
        roms = find_rom_files(device_dir)
        if not roms:
            print(f"[{dev}] no .nb0/.bin files at top level")
            continue
        device_out = EXTRACTED / dev
        for rom in roms:
            jobs.append((dev, rom, device_out / rom.name))
    return jobs


def main() -> int:
    if not EXTRACTOR.is_file():
        print(f"ERROR: extractor not found at {EXTRACTOR}")
        print("       Run: git submodule update --init references/extract-wince-rom")
        return 1

    machine: str | None = None
    devices: list[str] = []
    argv = sys.argv[1:]
    i = 0
    while i < len(argv):
        a = argv[i]
        if a.startswith("--machine="):
            machine = a.split("=", 1)[1]
        elif a == "--machine":
            i += 1
            if i >= len(argv):
                print("ERROR: --machine requires an architecture argument")
                return 1
            machine = argv[i]
        else:
            devices.append(a)
        i += 1

    if not devices:
        print(__doc__.strip())
        print("\nERROR: name at least one device under bundled/devices/")
        return 1

    jobs = collect_jobs(devices)
    failures = 0
    if not jobs:
        print("no ROM files to extract")
    else:
        workers = os.cpu_count() or 1
        print(f"extracting {len(jobs)} ROM(s) across {workers} worker(s)")
        with ThreadPoolExecutor(max_workers=workers) as pool:
            futs = {pool.submit(run_extractor, rom, out, machine): (dev, rom)
                    for dev, rom, out in jobs}
            for fut in as_completed(futs):
                dev, rom = futs[fut]
                rom_path, rc, output = fut.result()
                tag = f"[{dev}/{rom.name}]"
                if rc == 0:
                    print(f"{tag} OK")
                else:
                    failures += 1
                    print(f"{tag} FAILED (rc={rc})")
                    if output.strip():
                        for line in output.rstrip().splitlines():
                            print(f"{tag}   {line}")
        if failures:
            print(f"{failures} extraction(s) failed")

    pdb_errors = 0
    for dev in devices:
        device_dir = BUNDLED / dev
        if not device_dir.is_dir():
            continue
        try:
            copied = copy_pdbs(device_dir, EXTRACTED / dev, find_rom_files(device_dir))
        except RuntimeError as e:
            pdb_errors += 1
            print(f"[{dev}] PDB copy ERROR: {e}")
            continue
        if copied:
            rom_name = find_rom_files(device_dir)[0].name
            print(f"[{dev}] copied {copied} PDB entr(y/ies) into "
                  f"references/extracted-roms/{dev}/{rom_name}/fs/Windows/")

    return 1 if (failures or pdb_errors) else 0


if __name__ == "__main__":
    sys.exit(main())
