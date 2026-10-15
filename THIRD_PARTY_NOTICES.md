# Third-party notices

LADIO's original engine, bindings and documentation are MIT licensed. Third-party components retain their own notices. The root MIT license does not replace them.

| Component | Included | License | Source / notice |
|---|---|---|---|
| Beat This `small0` | FP32 ONNX export, frontend table, export metadata | MIT, code and published weights | [CPJKU/beat_this](https://github.com/CPJKU/beat_this), [included license](models/beat-this-small0/LICENSE) |
| Signalsmith Stretch | Vendored headers | MIT | [upstream](https://github.com/Signalsmith-Audio/signalsmith-stretch), `third_party/signalsmith/LICENSE.txt` |
| Signalsmith Linear | Vendored headers | MIT | [upstream](https://github.com/Signalsmith-Audio/linear), `third_party/signalsmith-linear/LICENSE.txt` |
| nlohmann/json | Vendored single header | MIT | [upstream](https://github.com/nlohmann/json), `third_party/json.LICENSE.MIT` |
| SoXR 0.1.3 | Optional analyzer; full source, separate shared library | LGPL-2.1-or-later, with embedded subcomponent notices | [upstream](https://github.com/chirlu/soxr), `third_party/soxr/LICENCE`, `COPYING.LGPL` |
| ONNX Runtime | External dependency; no runtime binary bundled | MIT | [upstream](https://github.com/microsoft/onnxruntime), retain its distribution notices when shipping |
| librosa reference | No Python dependency at runtime; frontend provenance | ISC | [upstream](https://github.com/librosa/librosa), `licenses/Librosa-ISC.txt` |

The vendored SoXR tree originates from commit `945b592b70470e29f917f4de89b4281fbbd540c0` and includes local build-compatibility changes. Its source is provided in full. Keep SoXR replaceable when distributing the optional analyzer; static/mobile distributions need the corresponding LGPL relinking arrangements. The core does not link SoXR or ONNX Runtime.

The ONNX export adapts the MIT Beat This model's attention evaluation to bounded query tiles. It retains the checkpoint's full context, FP32 precision and beat/downbeat output contract. Export parity values are recorded in `models/beat-this-small0/beat-model.json`; they are export checks, not musical accuracy scores.

No Apple framework, private Audio Unit state, Apple model weights, commercial audio, Lane APK, account material or device diagnostics is included. Apple Music AutoMix is a product reference, not a dependency or an endorsement. SF Pro is requested as an installed system font at weight 760; font files are not redistributed. The supplied LADIO symbol and original diagrams are covered by the project license.
