# Embedding LADIO

## Build products

`Ladio::Core` contains the planner, compiler, DSP, player and native analysis entry point. It has no ONNX dependency. `Ladio::Analysis` is optional and adds Beat This analysis and native preparation through a C API. The host supplies ONNX Runtime for its target architecture. SoXR is built as a separate shared library for this optional path.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/build/install"
cmake --build build --config Release --parallel 2
cmake --install build --config Release
```

Consumers use `find_package(Ladio 1 CONFIG REQUIRED)` with `CMAKE_PREFIX_PATH` pointing at that prefix. Link `Ladio::Core`; if the package was built with ONNX, `Ladio::Analysis` is also exported. Shared-library deployment must include the platform-specific runtime libraries and preserve dependency notices. Static core builds are supported with `BUILD_SHARED_LIBS=OFF`.

## Source representation

The host decodes audio to flat interleaved stereo float32 little-endian PCM. Use 44,100 Hz for the optional analyzer; its supported duration is 20 seconds–6 minutes. The native player emits noninterleaved stereo floats. Audio resampling/device formats are host responsibilities.

```json
{
  "schema_version": 1,
  "tracks": [{
    "id": "track-a", "source_hash": "content-identity", "pcm_path": "a.f32",
    "sample_rate": 44100, "frames": 6174000, "tempo": 120,
    "tempo_confidence": 0.95, "downbeat_confidence": 0.95,
    "loudness_lufs": -14, "beats": [], "downbeats": [], "cues": []
  }]
}
```

This fragment documents required identity/audio fields; empty annotations are not a usable DJ grid. See `examples/generate_fixture.py` for a complete original annotated example. Relative PCM paths in a library resolve against the library directory. Recompute identity after changing audio.

For learned analysis, invoke `ladio_analyzer_analyze` for each PCM and combine its track JSON outputs into `{"schema_version":1,"tracks":[...]}`. The CLI's `analyze` command uses the PCM filename stem as ID; filenames must be unique within a queue. With the C API, supply your library's stable unique ID explicitly. Store absolute PCM paths or make them correctly relative to the library you write.

## Preparation and playback

Call `ladio_prepare_library(library, output, "dj", seed)` off the UI/audio threads, or use the asynchronous `LadioPreparation` handle. Read progress/state, copy any error string before destroying the handle, and destroy only on a thread allowed to join the worker. Preparation creates `session.json` plus its generated resources.

Open that session with `ladio_player_create`, then call `play`. In the device callback call only `pull` with preallocated planar buffers. Zero any frames not returned; never decode, plan, seek, destroy or run inference in that callback. Poll `LadioSnapshot` for UI state, source positions, transition progress, peaks and underruns.

Only one producer may call control functions at a time. The application owns audio focus, interruptions, background/foreground lifecycle and device route changes. A completed offline render does not implement those mobile responsibilities.

## Examples

- [`examples/c`](../examples/c): installed-package consumer and version/API smoke check.
- [`bindings/python/ladio.py`](../bindings/python/ladio.py): standard-library ctypes wrapper; no Python inference dependency.
- [`examples/python`](../examples/python): prepare and render with that wrapper.
- [`examples/swift`](../examples/swift): C module import and ownership skeleton for an Apple host.
- [`examples/android`](../examples/android): NDK build/link notes and JNI control bridge skeleton.

macOS, Linux and Windows are native build targets. iOS and Android use the same core through their native toolchains, but this release does not include a complete mobile player, signed app or measured phone performance report. WebAssembly is outside this release's verified target set.

## ONNX analyzer deployment

The model root can be `models/` or `models/beat-this-small0/`. Validate the bundle with `scripts/verify_release.py` before distribution. Never mix its frontend table or metadata with a different checkpoint.

The analyzer runs one operation process-wide. Set a progress callback before starting. For cancellation, call `ladio_analyzer_cancel` from another thread, wait for the operation to return, then destroy. The callback is delivered on the analysis worker and must marshal to your UI thread.

Ship ONNX Runtime and its notices yourself. Ship the optional SoXR shared library with its notices and corresponding source/relinking arrangements required by LGPL for your distribution. `Ladio::Core` itself remains MIT-only apart from its MIT vendored components.
