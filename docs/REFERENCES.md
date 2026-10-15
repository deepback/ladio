# Reference and provenance notes

LADIO's product reference is the listening experience of dynamic transitions in Apple Music. Apple's public documentation describes AutoMix, beatmatching and time-stretching, but does not disclose a complete internal planner. These sources do not justify claims about private scene selection, stems or specific effects.

1. **Apple Support — transition between songs.** Describes AutoMix and compatibility-dependent behavior. [Support article](https://support.apple.com/en-ie/105067).
2. **Apple — macOS Tahoe feature overview.** Public description of AutoMix beatmatching and time-stretching. [Feature document](https://www.apple.com/nz/os/pdf/All_New_Features_macOS_Tahoe_Sept_2025_NZ_Final.pdf).
3. **CPJKU — Beat This.** Upstream model, checkpoint family, frontend and MIT code/published-weight licensing. [Repository](https://github.com/CPJKU/beat_this), [license](https://github.com/CPJKU/beat_this/blob/main/LICENSE).
4. **Signalsmith Audio — Signalsmith Stretch.** Upstream time-stretch implementation used by LADIO. [Repository and documentation](https://github.com/Signalsmith-Audio/signalsmith-stretch).
5. **Microsoft — ONNX Runtime.** External native inference runtime. [Repository](https://github.com/microsoft/onnxruntime).
6. **SoXR.** Optional resampler implementation and licensing. [Repository](https://github.com/chirlu/soxr).
7. **nlohmann/json.** Included JSON implementation. [Repository](https://github.com/nlohmann/json).

## What was extracted

This release packages LADIO's existing C++ planner/compiler/DSP and its portable ONNX integration into a new standalone SDK. It is not a transcription of Apple source code, an Apple AU redistribution or a bit-exact Apple port. Earlier private framework/model research is not part of this public repository. The historical term “clean-room” should not be taken as a formal independent-team provenance attestation.

The public checkpoint was trained by Beat This authors; the bounded attention ONNX export and native integration were prepared in the AutoMix project. Export metadata records numerical checks. DTTNet, SCNet and HTDemucs are not bundled or automatically run in this release. Describing them in earlier project discussions did not make them implemented dependencies.

## Research questions left open

How much do cue accuracy, phrase protection and scene preference improve listening outcomes on a genre-balanced public corpus? Which scenes justify separation cost? What is the energy/time profile on midrange phones? Those questions require reproducible evaluation beyond source extraction and unit tests. Contributions should report data, model/frontend versions, hardware and listening methodology rather than a generic “10/10” rating.
