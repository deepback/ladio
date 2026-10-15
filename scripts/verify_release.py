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
