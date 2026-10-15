# LADIO engine specification

**Release:** 1.0.0 · **Language:** C++20 · **Public boundary:** C ABI v1 · **License:** MIT core

## 1. Purpose and scope

LADIO transforms annotated musical sources into a compiled transition and a playable session. The host provides decoded audio, storage and the audio device. The engine owns candidate selection, source/output maps, DSP automation, prepared PCM and playback state.

The distribution includes a portable core and an optional ONNX analyzer. It deliberately excludes the desktop app, Lane modification, commercial audio, private framework research and non-redistributable model exports. Existing internal compiler/graph revision strings are retained as cache compatibility identifiers; they are not the SDK's semantic version.

![Architecture](../assets/architecture.svg)

## 2. Modules and contracts

| Module | Input | Output | Responsibility |
|---|---|---|---|
| `src/analysis/native.cpp`, `structure.hpp` | PCM / track JSON | Coarse analysis / structural descriptors | Portable descriptors; native A1 fallback is not a learned beat tracker |
| `src/analysis/onnx` | 44.1 kHz stereo PCM, model root, analysis scope | Track JSON and reusable analysis data | Beat This inference, native descriptor integration, scoped preparation |
| `src/planner` | Tracks, measured anchors, policy, seed | Plan plus scene JSON | Candidate search, material suitability, timing, cost and variation |
| `src/core/scene.*` | Scene JSON | Validated voice, effect and timing graph | Reject inconsistent or unsupported execution plans |
| `src/core/compiler.cpp` | Sources and plan | Prepared source windows and manifests | Time-stretch, source mapping, compiled resources |
| `src/core/mixer.cpp` | Compiled scene | Planar stereo blocks | Dry paths, captures, filtering, modulation and effect returns |
| `src/core/player.cpp` | Session and controls | PCM plus snapshot | Worker/read-ahead, seek generations and bounded callback consumption |

C headers are authoritative for binary interfaces. Internal C++ headers and scene JSON are inspectable implementation formats, not a promise of C++ ABI stability.

## 3. Musical evidence

Tracks carry an identity/revision, content hash, PCM path, frame count, sample rate, loudness estimate, tempo hypotheses, beats, downbeats and cue candidates. Cue records can include bar position, boundary strength, entry/exit suitability, local chroma, rhythm/timbre descriptors and confidence. Optional protected vocal intervals and supplied stem windows extend that evidence.

The optional analyzer derives beat/downbeat logits with Beat This `small0`. Native code evaluates quarter-pulse hypotheses, local consistency and bar support instead of treating one global BPM as sufficient evidence. The lightweight native-only analysis function is a coarse fallback; its regular grid is not a measured neural grid and should not be presented as one.

Structural novelty and vocal-related proxies are fallible. Without an explicit activity model, stems or trusted annotations, the engine cannot reliably identify the end of every sung word. Local stereo harmony is the public default; role-specific harmony is available only when corresponding resource evidence exists.

## 4. Search and selection

The planner first evaluates useful arrival/entry material in B and a viable later exit in A. It prefers B's earlier portion and A's later portion, while requiring enough source continuation to finish the scene. Current direct-scene search bounds include an incoming anchor below 46% of duration and an outgoing cue beyond 55%, with additional runway constraints; these are heuristics, not universal musical truths.

Candidate scenes are evaluated against measured synchronization, harmonic compatibility, rhythm similarity, energy continuity, phrase protection, available introduction/runway, capture/loop quality and resource cost. Variation influences valid choices rather than overriding hard correctness constraints. A preferred style can constrain selection, but an unsafe requested style may be rejected. Plans retain decision and veto details so that fallback behavior is inspectable.

Queue planning must preserve playable source between an incoming transition's end and that track's later outgoing transition. Reordering a queue changes these constraints. The SDK does not promise that every arbitrary queue has a valid elaborate DJ scene.

## 5. Independent musical events

A scene distinguishes first audible B, hybrid/joint play, foundation exchange, outgoing phrase completion/release, incoming arrival, B lead, last audible A and recovery. These events can coincide but are not implicitly one timestamp.

Voices distinguish the two main readers and bounded independent captures/teasers. A teaser does not advance the main incoming playhead. Captured material must remain complete under changed tempo; its output duration follows its time map. An outgoing tail can continue after dry A releases, while its lows and rhythm are removed before an incompatible new pulse.

![Scene](../assets/scene-score.svg)

## 6. Tempo and phase

Each active source uses a monotonic source/output map tied to the scene's quarter-note clock. Compatible pairs can begin at A's pulse and move smoothly toward B's pulse. B can enter already mapped to that shared pulse, then reach a rate of 1 at arrival. Captures and loop stages use the same transport decisions.

Half/double-time is a musical interpretation, not permission to play the recording at twice its speed. The current direct DJ implementation allows a maximum recording-rate deviation up to 18%; the general policy structure's default is 6%. Inspect the chosen plan rather than assuming a fixed 6% bound for every DJ scene. If a continuous match exceeds the direct budget or contradicts measured phase, a bounded pulse bridge is considered.

Phase correction must bind to measured bars, not invented half-bar accents. Stretch latency and source lookahead belong to compilation. At the end of recovery, native B must continue at the same source position, level and speed as the scene's prepared B.

![Tempo](../assets/tempo-map.svg)

## 7. Holds, repeats and FX

A useful hold loop needs measured bar boundaries, stable material, low seam discontinuity and a clear exit. Loop extension is conditional on B having useful development/runway. Repeat compression is a separate final-stage gesture; it does not justify shredding the whole bass line throughout a scene. Source-loop stages and independent clips are represented explicitly.

The DSP has per-deck filters/EQ, flanging, rhythmic gating, tempo delay, room/reverb and routed sends/returns. Capture feed and return gain are distinct. Effect return can remain audible after dry release. Automation is represented by time-varying curves, not a single enabled flag. A scene must budget low-frequency overlap and preserve incoming attacks rather than masking errors with a limiter.

The effects are LADIO's native implementations, not binaries or bit-exact replacements for Apple Audio Units. The [catalogue](TRANSITIONS.md) maps constructions to those processors.

## 8. Threading, playback and seek

One serialized control producer issues player commands. One audio consumer calls `ladio_player_pull` with separate preallocated left/right buffers. The pull path is bounded, lock-free and performs no ML, file I/O or heap allocation. Worker rendering and resource preparation happen off the audio thread. Read-ahead is specified in frames; the host must size its callback and buffer policy appropriately and zero any unfilled callback region.

Seek changes a playback generation. Prepared transitions remain part of the session's output timeline; seeking near or into them does not ask the planner to invent a new transition. DSP state is reconstructed where supported, including tested effect tails. Preparation destruction joins a worker and therefore belongs off the real-time audio callback and UI-sensitive paths.

The optional analyzer serializes operations process-wide, uses bounded inference threads and supports cancellation from another thread. The caller must wait for an operation to return before destroying its handle.

## 9. Storage and lifecycle

Uncompressed stereo float32 PCM at 44.1 kHz consumes 352,800 bytes/s, approximately 21.17 MB/minute. It is an execution format, not a compressed library format. Compiled resources should cover needed windows, and the application should retain original compressed assets separately.

Model files are relative to a model root and covered by a size/SHA-256 manifest. Analysis reuse must remain bound to audio content, frontend/model revision and scope. Output manifests are published through a temporary file and replacement. The library does not automatically garbage-collect a user's music or choose a global cache quota: the host should evict inactive generated resources with an LRU/size budget and avoid deleting live session assets.

## 10. Acceptance and non-claims

A technically valid scene is not automatically a pleasing transition. Appropriate acceptance includes inspecting selected events, listening through preparation/arrival/recovery, comparing matched loudness, checking active effect buses and measuring preparation cost on the target device.

The repository's automated checks cover reproducible invariants and synthetic signal behavior. They do not establish perceptual equivalence to Apple Music, universal beat accuracy, a guaranteed 20-second phone preparation budget or zero battery impact. See [validation](../VALIDATION.md) for the release's actual evidence.
