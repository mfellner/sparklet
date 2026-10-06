#!/usr/bin/env python3
"""Build and package the committed Sparklet slot image, without flashing or including device data."""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "firmware/sparkdash"
PLATFORM = "https://github.com/mfellner/esp32-playground"
SLOT_ARGS = "sparklet-flash_args"
SLOT_OFFSET = "0x220000"


def output(*args, cwd=ROOT):
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "destination", type=Path, help="new bundle directory (typically releases/...)"
    )
    args = parser.parse_args()
    destination = args.destination.resolve()
    if destination.exists() or destination.with_suffix(".zip").exists():
        parser.error("destination already exists; refusing overwrite")
    if output("git", "status", "--porcelain"):
        parser.error("commit reviewed changes first; release source must be clean")
    sdk = Path(os.environ.get("IDF_PATH", ""))
    if not (sdk / "tools/idf.py").is_file():
        parser.error("activate ESP-IDF 5.5.3 first")
    sdk_revision = output("git", "rev-parse", "HEAD", cwd=sdk)
    if sdk_revision != "2c211b236707889e8400c4dc5644dd5c4ee071e0":
        parser.error("ESP-IDF revision does not match the pinned 5.5.3 SDK")
    revision = output("git", "rev-parse", "HEAD")
    subprocess.run(["idf.py", "-C", str(PROJECT), "build"], check=True)
    if output("git", "status", "--porcelain"):
        parser.error(
            "build changed tracked source/dependencies; review and commit before packaging"
        )
    if "CONFIG_SPARKDASH_TEST_COMMANDS=y" in (PROJECT / "sdkconfig").read_text():
        parser.error("test commands must be disabled in a release bundle")
    if "CONFIG_APP_REPRODUCIBLE_BUILD=y" not in (PROJECT / "sdkconfig").read_text():
        parser.error("reproducible build option must be enabled")
    build = PROJECT / "build"
    description = json.loads((build / "project_description.json").read_text())
    flash = json.loads((build / "flasher_args.json").read_text())
    if (
        description["target"] != "esp32c6"
        or flash["flash_settings"]["flash_size"] != "16MB"
    ):
        parser.error("unexpected build target or flash size")
    slot_args = (
        (build / SLOT_ARGS).read_text().splitlines()
        if (build / SLOT_ARGS).is_file()
        else []
    )
    if len(slot_args) != 2 or slot_args[1].split() != [SLOT_OFFSET, "sparkdash.bin"]:
        parser.error(
            f"build/{SLOT_ARGS} must write only sparkdash.bin at {SLOT_OFFSET} (platform_app_slot)"
        )
    if "--flash_size 16MB" not in slot_args[0]:
        parser.error(f"build/{SLOT_ARGS} has unexpected flash settings")
    # Relative layout matches a build directory, so `device.py install sparklet <bundle>` works.
    sources = {
        Path(name): build / name
        for name in [
            "sparkdash.bin",
            SLOT_ARGS,
            "partition_table/partition-table.bin",
        ]
    }
    for name in [
        "dependencies.lock",
        "partitions.csv",
        "sdkconfig",
        "sdkconfig.defaults",
        "sdkconfig.defaults.platform",
        "PROVENANCE.md",
    ]:
        sources[Path(name)] = PROJECT / name
    sources[Path("SOURCE_README.md")] = PROJECT / "README.md"
    sources[Path("esp32_serial.py")] = ROOT / "scripts/esp32_serial.py"
    sources[Path("validation.md")] = ROOT / "docs/sparkdash-validation.md"
    for relative, source in sources.items():
        if relative.is_absolute() or ".." in relative.parts or not source.is_file():
            parser.error(f"invalid or missing build artifact: {relative}")
    destination.mkdir(parents=True)
    for relative, source in sources.items():
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    metadata = {
        "application_revision": revision,
        "sdk_revision": sdk_revision,
        "sdk_version": "5.5.3",
        "application_version": description["project_version"],
        "target": description["target"],
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "soak_test": "Excluded by user; not performed",
        "validation": "See validation.md; packaging does not imply acceptance gates passed",
        "compiler": output(description["c_compiler"], "--version").splitlines()[0],
    }
    (destination / "manifest.json").write_text(json.dumps(metadata, indent=2) + "\n")
    # Subset read by the platform's device.py; the full file contains local absolute paths.
    project = {
        key: description[key]
        for key in ["version", "project_name", "project_version", "target", "app_bin"]
    }
    (destination / "project_description.json").write_text(
        json.dumps(project, indent=2) + "\n"
    )
    (destination / "README.md").write_text(f"""# Sparklet firmware bundle

Native read-only sparkDash dashboard for the Waveshare ESP32-C6-Touch-AMOLED-2.16.
This bundle updates only the `sparklet` app slot ({SLOT_OFFSET}) of the multi-app platform
from {PLATFORM}. It contains no bootloader, launcher or full flash image.

1. Read `validation.md` for tested facts and remaining limitations. The 24-hour soak is excluded.
2. Verify files: `shasum -a 256 -c SHA256SUMS`.
3. Enumerate the USB device: `uv run esp32_serial.py list`. Select serial D4:05:92:B9:04:28, VID/PID 303a:1001.
4. A device not yet on the platform layout (factory image or the standalone Sparklet 1.0.0) must first be migrated with the platform repository; see {PLATFORM}#build-and-install.
5. Follow `FLASH.txt` from this directory.
6. Scan the display's Wi-Fi QR, then the setup-page QR; enter credentials only in that portal.

Installing writes only the Sparklet slot. NVS (saved settings of every app), the launcher, the bootloader and the boot selection are preserved. Read the platform's USB instructions ({PLATFORM}/blob/main/docs/interaction.md) before flashing; full backup/restore is described in {PLATFORM}/blob/main/docs/recovery.md and needs separately protected backups.

`partition_table/partition-table.bin` is the platform table this build was configured against, for comparison with the device. `project_description.json` (a path-free subset of the build's file) lets the platform's `device.py` use this directory as a build directory. `manifest.json` records the source revision, compiler and SDK. `SOURCE_README.md` retains source-tree build/test instructions; its source-relative paths refer to the repository. No factory backup, NVS image, credentials or raw logs are included.
""")
    (
        destination / "FLASH.txt"
    ).write_text(f"""Verify checksums first with: shasum -a 256 -c SHA256SUMS
Enumerate the known board with: uv run esp32_serial.py list
Close all serial monitors. The device must already use the platform layout ({PLATFORM}).

Recommended, with a checkout of the platform repository at PLATFORM, from this bundle directory:
uv run PLATFORM/tools/device.py install sparklet .
It compares partition_table/partition-table.bin with the device's table, refuses an unmigrated
device, and writes only sparkdash.bin into the sparklet slot.

Manual fallback: first compare the device's partition table with the platform layout, e.g.
uv run PLATFORM/tools/device.py status
(eight partitions; sparklet app at {SLOT_OFFSET}). Then activate ESP-IDF 5.5.3 and run from this
directory with the discovered PORT:
python -m esptool --chip esp32c6 --port PORT --before default_reset --after hard_reset write_flash @{SLOT_ARGS}

Both paths write only the Sparklet slot at {SLOT_OFFSET}; NVS, the launcher, the bootloader and the
boot selection are preserved. The offset comes from the build, not the factory image. On a device still
on the single-app layout this offset lies inside the old application: migrate with the platform first.
This bundle contains no device credentials, NVS partition, raw logs, bootloader or factory backup.
""")
    files = sorted(p for p in destination.rglob("*") if p.is_file())
    checksums = [
        f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(destination)}"
        for p in files
    ]
    (destination / "SHA256SUMS").write_text("\n".join(checksums) + "\n")
    archive = destination.with_suffix(".zip")
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as bundle:
        for path in sorted(destination.rglob("*")):
            if path.is_file():
                bundle.write(
                    path, Path(destination.name) / path.relative_to(destination)
                )
    archive.with_suffix(".zip.sha256").write_text(
        hashlib.sha256(archive.read_bytes()).hexdigest() + "  " + archive.name + "\n"
    )
    print(f"Packaged {revision} to {destination} and {archive}")


if __name__ == "__main__":
    main()
