#!/usr/bin/env python3
"""Generate original synthetic audio with exact annotations; no external packages."""
import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import sys

SR = 44100

def make(root, name, bpm, frequency):
    seconds = 140
    frames = seconds * SR
    path = root / (name + ".f32")
    digest = hashlib.sha256()
    with path.open("wb") as out:
        for start in range(0, frames, SR):
            block = array("f")
            for frame in range(start, min(start + SR, frames)):
                t = frame / SR
                beat_time = (t * bpm / 60) % 1 * 60 / bpm
                kick = 0.17 * math.exp(-beat_time * 36) * math.sin(2 * math.pi * (52 * beat_time + 1.3 * (1 - math.exp(-beat_time * 45))))
                chord = 0.045 * sum(math.sin(2 * math.pi * frequency * ratio * t) for ratio in (1, 1.25, 1.5))
                hat = 0.018 * math.exp(-((t * bpm / 30) % 1) * 32) * math.sin(2 * math.pi * 6800 * t)
                value = kick + chord + hat
                block.extend((value, value * 0.98))
            if sys.byteorder != "little":
                block.byteswap()
            data = block.tobytes()
            out.write(data)
            digest.update(data)
    bars = [i * 240 / bpm for i in range(math.ceil(seconds * bpm / 240))]
    cues = []
    for k in (16, 40, 48, 56):
        if k >= len(bars):
            continue
        cues.append({"time": bars[k], "bar": k, "kind": "phrase", "confidence": .95,
                     "structure_confidence": .95, "section_boundary": True,
                     "boundary_strength": .9, "entry_suitability": .85, "exit_suitability": .85,
                     "energy": .1, "vocal_before": .5,
                     "key": {"chroma": [1] + [0] * 11, "confidence": .9}})
    return {"id": name, "title": "Procedural " + name, "source_hash": digest.hexdigest(),
            "pcm_path": path.name, "sample_rate": SR, "frames": frames,
            "tempo": bpm, "tempo_confidence": .95, "downbeat_confidence": .95,
            "loudness_lufs": -14, "downbeats": bars,
            "beats": [i * 60 / bpm for i in range(math.ceil(seconds * bpm / 60))],
            "cues": cues, "protected_vocal_phrases": [],
            "curves": [{"time": i * .2, "rms": .1, "vocal": .5} for i in range(seconds * 5)]}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    tracks = [make(args.output, "a", 120, 220), make(args.output, "b", 124, 220)]
    (args.output / "library.json").write_text(json.dumps({"schema_version": 1, "tracks": tracks}, indent=2))
    print(args.output / "library.json")
