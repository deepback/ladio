# LADIO

The primary project documentation is written in Russian: **[open the project overview](README.md)**. The public visual introduction, specifications, integration guide and transition catalogue are also in Russian: [LADIO visual paper](https://deepback.github.io/ladio/).

LADIO is an embeddable C++20 engine that analyzes musical timing, plans transition scenes and renders them with a shared musical clock. It grew from an independent AutoMix research prototype. Apple Music AutoMix is a product reference; LADIO does not include Apple code, private presets or model weights and does not claim sonic equivalence.

The released engine includes 17 automatic DJ styles, 8 continuity/manual constructions, a C ABI, CMake package, native filters, delay, room/reverb, flanging and rhythmic effects. Optional beat analysis uses the upstream Beat This `small0` ONNX checkpoint, with its original authorship and MIT notice preserved. See [release 1.0.0](https://github.com/deepback/ladio/releases/tag/v1.0.0), the [integration guide](docs/INTEGRATION.md), [engine specification](docs/specification/ENGINE.md), [transition catalogue](docs/specification/TRANSITIONS.md) and [third-party notices](THIRD_PARTY_NOTICES.md).

## Build

```sh
python3 scripts/verify_release.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

The portable core has no ONNX Runtime dependency. For project limits, validation evidence, model provenance and licensing, start with the Russian [README](README.md) and [validation record](docs/VALIDATION.md).
