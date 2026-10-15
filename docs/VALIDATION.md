# Release validation — 1.0.0

Evidence recorded on 2026-10-15. [Machine-readable record](validation.json). The [CI workflow](https://github.com/deepback/ladio/actions/workflows/ci.yml) is the authoritative public record for each pushed revision.

| Check | Evidence | Scope |
|---|---|---|
| Portable core CTest | 7/7 passing locally | Core, scene, independent DJ, harmony, measured grid, C ABI, model integrity |
| Optional ONNX build | Successful on macOS arm64 | Public C analyzer API and native CLI linked to external ONNX Runtime |
| Android cross-compilation | Successful, NDK 27.3, API 26, arm64-v8a | Core library; no phone runtime claim |
| iOS cross-compilation | Successful, iOS 27 SDK, arm64, deployment target 15.0 | Static core; no device run |
| Installed C consumer | Prints `LADIO 1.0.0 / C ABI 1` | Separate `find_package(Ladio)` projects with both shared and static installed cores |
| Installed ONNX analyzer | Real-audio entry analysis completed with external runtime | Installed executable and its library dependencies |
| Python facade | Version and native error propagation checked | ctypes boundary; no real-time Python audio promise |
| Swift example | Type-checks with installed C module | C import/ownership skeleton; no device audio claim |
| Public model bundle | 1 ONNX model, 4 hashed execution assets | Byte sizes, SHA-256, registration and relative-path integrity |
| Real-audio analysis | Entry scope completed; 240 beats, 60 downbeats, 7 cues | Local 248.04 s input; private audio and full analysis are not distributed |
| Original synthetic mix | 197.47 s, zero nonfinite samples | Known annotations, planner → compiler → full WAV render |

The synthetic render took approximately 1.65 seconds on this development host, excluding fixture generation, analysis and scene preparation. This is a single local observation, not a mobile performance benchmark. Peak was approximately −9.93 dBFS. Detailed render measurements are in the JSON record.

A repetitive synthetic signal was rejected for ambiguous measured bar support even though its beat pulse was detected. That result is retained as a limit of the evidence; the demo supplies known annotations rather than claiming that generated material establishes neural downbeat accuracy.

## Reproduce

```sh
python3 scripts/verify_release.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 examples/generate_fixture.py build/demo
build/ladio prepare build/demo/library.json build/demo/mix dj 42
build/ladio render build/demo/mix/session.json build/demo/mix.wav build/demo/report.json
```

Model integrity and a clean static-core build also passed from a cloned second source directory with spaces in its name. Installed-package consumers must not include headers from the original AutoMix project. Public release archives contain source/model assets; generated PCM, commercial audio, temporary SDK binaries and device logs are excluded.

## Remaining acceptance

The Android core build is not an APK or Pixel playback test. iOS physical playback, mobile background lifecycle, sustained thermal/battery cost and deadline guarantees are not established by this packaging release. Listening quality on arbitrary songs, learned phrase boundaries and equivalence to Apple's undisclosed internal planner remain unproven. GitHub-hosted macOS/Linux/Windows jobs could not start because GitHub reported an account billing lock. The workflow is included, but those hosted results are not passing-build evidence.
