# Contributing

Start with a small reproducible musical problem: an early vocal cutoff, a late incoming attack, an inaudible return, a source-map discontinuity or an unnecessary preparation step. Include selected scene/decision metadata and a minimal synthetic signal or legally shareable audio excerpt.

Build the core and run CTest before submitting. If changing the analyzer, validate model/frontend checksums and compare the resulting grid and audible scene, not only global BPM. If changing DSP, compare level-matched output before, during and after the transition. Add a regression only when it captures a meaningful failure.

Do not submit private framework binaries, private presets, restricted model weights, commercial tracks, device logs containing account data or font files. New models need provenance, license, frontend/output contracts, hashes and measured evaluation. Model accuracy and subjective quality claims need evidence.

Keep inference and file I/O outside the audio callback. Preserve deterministic selection and inspectable veto reasons. Public C ABI changes require a compatibility review. Contributions are accepted under the project's MIT license, retaining third-party notices where applicable.
