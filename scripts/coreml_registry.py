#!/usr/bin/env python3
"""Index host-supplied compiled Core ML packages without changing them."""
import argparse
import hashlib
import json
from pathlib import Path

ROLES = {
    "DownbeatTracker": ("downbeat", "host-verified-downbeat-conditioning-v1"),
    "KeyModel": ("key", "raw-mono-22050-661504-v1"),
    "InstrumentActivityModel": ("activity", "magnitude-slaney-A-dB-24k-v1"),
    "StructuralFeaturesModel": ("structure", "magnitude-slaney-A-dB-24k-v1"),
}

def registry(root):
    models = []
    for name, (role, frontend) in ROLES.items():
        directory = root / (name + ".mlmodelc")
        if not directory.is_dir():
            continue
        if directory.is_symlink():
            raise ValueError(f"Symlink model directory: {directory}")
        files = {}
        for path in sorted(directory.rglob("*")):
            if path.is_symlink():
                raise ValueError(f"Symlink in compiled model: {path}")
            if path.is_file():
                files[path.relative_to(directory).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
        if not files:
            raise ValueError(f"Empty compiled model: {directory}")
        models.append(dict(id=name, role=role, directory=directory.name, frontend=frontend, files=files))
    if not models:
        raise ValueError("No supported .mlmodelc directories found")
    return {"schema_version": 1, "models": models}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="Directory containing local .mlmodelc packages")
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    output = root / "registry.json"
    output.write_text(json.dumps(registry(root), ensure_ascii=False, indent=2) + "\n")
    print(output)
