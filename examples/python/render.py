#!/usr/bin/env python3
"""Run with SHARED_LIBRARY LIBRARY.json OUTPUT_DIR. Runs synchronously off the audio thread."""
import argparse
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "bindings/python"))
from ladio import Ladio
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("shared_library", type=Path)
parser.add_argument("library", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
engine = Ladio(args.shared_library.resolve())
args.output.mkdir(parents=True, exist_ok=True)
engine.prepare(args.library.resolve(), args.output.resolve())
engine.render(args.output / "session.json", args.output / "mix.wav", args.output / "report.json")
print(f"LADIO {engine.version}: {args.output / 'mix.wav'}")
