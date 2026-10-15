# Beat This small0 · LADIO 1.0.0

**Role:** beat/downbeat estimation. **License:** MIT. **Upstream authors:** Institute of Computational Perception, JKU Linz. [Source](https://github.com/CPJKU/beat_this).

This is an ONNX FP32 export of the upstream `small0` checkpoint, not a model trained by LADIO. Exact byte hashes and the upstream checkpoint hash are in `../manifest.json` and `beat-model.json`. No ensemble, Whisper or separator is loaded.

| Contract | Value |
|---|---|
| Input | `spect`, float32 `[1, frames, 128]` |
| Outputs | `beat`, `downbeat`, unthresholded frame logits |
| Audio frontend | Mono, 22,050 Hz, FFT 1024, hop 441, Hann window |
| Mel | Included 513 × 128 filterbank; `log1p(1000 × mel)` |
| Context | Up to 1500 frames, six-frame borders, keep-first overlap |
| Native postprocessing | Peak/event measurements, quarter-pulse hypotheses, bar support and local-grid quality checks |
| Intended input | Musical audio; optional analyzer accepts 20 seconds–6 minutes at 44.1 kHz stereo PCM |

The native runtime reuses content-bound logits and only calculates missing chunks for entry/exit analysis scopes. Inference uses at most two intra-op threads, one inter-op thread, sequential execution and disabled thread spinning/telemetry. The model does not identify lyrics, isolate instruments or guarantee vocal phrase ends. Irregular meter, live drift and ambiguous half-time material need application-level review.

Export checks used 257, 749 and 1500 input frames; the largest recorded absolute logit error is about `1.12e-5`. This measures ONNX agreement on those tensors, not beat-tracking F-measure or unseen-song performance.
