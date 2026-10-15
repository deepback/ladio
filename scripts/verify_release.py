#!/usr/bin/env python3
"""Validate movable public assets without ML dependencies or user audio."""
import argparse
import hashlib
import json
from pathlib import Path

def verify(root):
    model_root = root / "models"
    manifest = json.loads((model_root / "manifest.json").read_text())
    if manifest.get("format") != "ladio-model-kit" or manifest.get("schema_version") != 1:
        raise ValueError("Unsupported model manifest")
    paths = set()
    for name, expected in manifest["files"].items():
        relative = Path(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("Model asset escapes bundle")
        asset = (model_root / relative).resolve(strict=True)
        if not asset.is_relative_to(model_root.resolve()):
            raise ValueError("Symlink escapes model directory")
        if asset.stat().st_size != expected["bytes"] or hashlib.sha256(asset.read_bytes()).hexdigest() != expected["sha256"]:
            raise ValueError(f"Model integrity mismatch: {name}")
        paths.add(asset)
    actual = {p.resolve() for p in model_root.rglob("*.onnx")}
    declared = {(model_root / m["file"]).resolve() for m in manifest["models"].values()}
    if actual != declared or not declared.issubset(paths):
        raise ValueError("Unregistered ONNX model in release")
    if any(m.get("license") != "MIT" for m in manifest["models"].values()):
        raise ValueError("Public model requires explicit distribution review")
    event_spec = model_root / manifest["runtime"]["model_directory"] / "event-models.json"
    if event_spec.is_file():
        event = json.loads(event_spec.read_text())
        if event.get("enabled"):
            for filename, identity in event["models"].items():
                relative = Path(manifest["runtime"]["model_directory"]) / filename
                asset = (model_root / relative).resolve(strict=True)
                if not asset.is_relative_to(model_root.resolve()):
                    raise ValueError("Event model escapes bundle")
                digest = hashlib.sha256(asset.read_bytes()).hexdigest()
                model_entry = next((entry for entry in manifest["models"].values()
                                    if entry["file"] == relative.as_posix()), None)
                if (asset.stat().st_size != identity["bytes"]
                        or digest != identity["sha256"]
                        or model_entry is None
                        or digest != model_entry.get("sha256")):
                    raise ValueError(f"Event model identity mismatch: {filename}")
                fnv = 14695981039346656037
                with asset.open("rb") as source:
                    for block in iter(lambda: source.read(65536), b""):
                        for byte in block:
                            fnv = ((fnv ^ byte) * 1099511628211) & ((1 << 64) - 1)
                if f"{fnv:016x}" != identity["fnv1a64"]:
                    raise ValueError(f"Event model runtime checksum mismatch: {filename}")
    for needed in ["LICENSE", "THIRD_PARTY_NOTICES.md", "include/ladio/ladio.h", "docs/specification/ENGINE.md"]:
        if not (root / needed).is_file():
            raise ValueError(f"Missing release file: {needed}")
    return {"passed": True, "release": manifest["release"], "verified_assets": len(paths),
            "onnx_models": len(declared), "bytes": sum(p.stat().st_size for p in paths)}

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    print(json.dumps(verify(args.root.resolve()), indent=2))
