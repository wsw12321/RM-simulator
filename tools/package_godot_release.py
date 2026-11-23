#!/usr/bin/env python3
"""Build portable Godot desktop/Android artifacts from the checked-out project.

The script deliberately discovers the Godot project and export preset names instead
of assuming a particular repository layout.  This lets the simulator keep its
native C++ tools alongside the game while the game migrates between a root project
and a dedicated `godot/` directory.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path


def project_root(repo: Path) -> Path:
    candidates = [repo / "project.godot", repo / "godot" / "project.godot"]
    candidates += sorted(repo.glob("*/project.godot"))
    for candidate in candidates:
        if candidate.is_file():
            return candidate.parent
    raise SystemExit("Godot project.godot was not found")


def presets(root: Path) -> dict[str, str]:
    cfg = root / "export_presets.cfg"
    if not cfg.is_file():
        raise SystemExit(f"missing export presets: {cfg}")
    current: str | None = None
    result: dict[str, str] = {}
    for line in cfg.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"\[preset\.(\d+)\]", line.strip())
        if match:
            current = match.group(1)
            continue
        if current is None:
            continue
        match = re.match(r'\s*name\s*=\s*"([^"]+)"', line)
        if match:
            result[match.group(1).lower()] = match.group(1)
    return result


def find_preset(names: dict[str, str], *needles: str) -> str:
    for key, value in names.items():
        if all(needle in key for needle in needles):
            return value
    raise SystemExit(f"export preset containing {needles!r} was not found; available: {sorted(names.values())}")


def run(cmd: list[str], *, cwd: Path) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True)


def zip_file(source: Path, destination: Path, *, root_name: str) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.write(source, f"{root_name}/{source.name}")
        readme = source.parent / "QUICKSTART.md"
        if readme.is_file():
            archive.write(readme, f"{root_name}/QUICKSTART.md")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--godot", default=os.environ.get("GODOT_BIN", "godot4"))
    parser.add_argument("--version", default=os.environ.get("GITHUB_REF_NAME", "dev"))
    parser.add_argument("--output", type=Path, default=Path("dist/godot"))
    parser.add_argument("--skip-android", action="store_true")
    args = parser.parse_args()

    repo = args.repo.resolve()
    root = project_root(repo)
    names = presets(root)
    linux_preset = find_preset(names, "linux")
    windows_preset = find_preset(names, "windows")
    android_preset = None if args.skip_android else find_preset(names, "android")
    output = (repo / args.output).resolve()
    build = output / "build"
    shutil.rmtree(output, ignore_errors=True)
    build.mkdir(parents=True)

    # Import every resource and run the project's optional deterministic probe.
    run([args.godot, "--headless", "--path", str(root), "--editor", "--quit", "--display-driver", "headless", "--audio-driver", "Dummy"], cwd=repo)
    for probe in (root / "scripts").glob("*smoke*.gd"):
        run([args.godot, "--headless", "--path", str(root), "--script", str(probe.relative_to(root)), "--display-driver", "headless", "--audio-driver", "Dummy"], cwd=repo)

    linux = build / "rm-simulator-linux-x86_64"
    windows = build / "rm-simulator-windows-x86_64.exe"
    run([args.godot, "--headless", "--path", str(root), "--export-release", linux_preset, str(linux)], cwd=repo)
    run([args.godot, "--headless", "--path", str(root), "--export-release", windows_preset, str(windows)], cwd=repo)
    linux.chmod(linux.stat().st_mode | 0o111)

    artifacts: list[Path] = []
    for executable, label in ((linux, "linux-x86_64"), (windows, "windows-x86_64")):
        archive = output / f"rm-simulator-godot-{args.version}-{label}.zip"
        zip_file(executable, archive, root_name=f"rm-simulator-godot-{args.version}-{label}")
        artifacts.append(archive)

    if android_preset:
        apk = build / "rm-simulator-android-arm64.apk"
        run([args.godot, "--headless", "--path", str(root), "--export-debug", android_preset, str(apk)], cwd=repo)
        if not apk.is_file():
            raise SystemExit(f"Android export did not produce {apk}")
        android_archive = output / f"rm-simulator-godot-{args.version}-android-arm64.zip"
        zip_file(apk, android_archive, root_name=f"rm-simulator-godot-{args.version}-android-arm64")
        artifacts.append(android_archive)

    checksums = output / "SHA256SUMS.txt"
    checksums.write_text("".join(f"{sha256(path)}  {path.name}\n" for path in artifacts), encoding="utf-8")
    print("GODOT_RELEASE_OK", " ".join(path.name for path in artifacts), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
