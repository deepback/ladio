#!/usr/bin/env python3
"""Package an already built core SDK; ONNX Runtime is intentionally external."""
import argparse
import hashlib
from pathlib import Path
import shutil
import tarfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("platform", choices=["android-arm64", "ios-arm64"])
parser.add_argument("library", type=Path)
parser.add_argument("--output", type=Path, default=ROOT / "dist")
args = parser.parse_args()
if not args.library.is_file():
    parser.error("Build the library before packaging")
name = "ladio-1.0.0-" + args.platform
folder = args.output / name
(folder / "lib").mkdir(parents=True, exist_ok=True)
shutil.copy2(args.library, folder / "lib" / args.library.name)
for entry in ["include", "models", "licenses"]:
    shutil.copytree(ROOT / entry, folder / entry, dirs_exist_ok=True)
for entry in ["LICENSE", "THIRD_PARTY_NOTICES.md"]:
    shutil.copy2(ROOT / entry, folder / entry)
shutil.copy2(ROOT / "examples" / ("android" if args.platform.startswith("android") else "swift") / "README.md", folder / "INTEGRATION.md")
if args.platform == "ios-arm64":
    shutil.copytree(ROOT / "bindings/swift", folder / "bindings/swift", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns(".build", ".swiftpm"))
    shutil.copytree(ROOT / "examples/swift", folder / "examples/swift", dirs_exist_ok=True)
    shutil.copy2(ROOT / "docs/COREML_IOS_RU.md", folder / "COREML_IOS_RU.md")
(folder / "README.txt").write_text("LADIO 1.0.0 core SDK. Native library, headers, licensed Beat This model and notices.\nNo ONNX Runtime or analyzer binary included. Build the optional analyzer from source.\nKeep all third-party notices. Android hosts supply their matching NDK C++ runtime.\nSee https://github.com/deepback/ladio for source, build settings and integration.\n")
archive = args.output / (name + ".tar.gz")
with tarfile.open(archive, "w:gz") as output:
    output.add(folder, arcname=name)
print(hashlib.sha256(archive.read_bytes()).hexdigest(), archive.name)
