#ifndef LADIO_C_API_H
#define LADIO_C_API_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct LadioPlayer LadioPlayer;
typedef struct LadioPreparation LadioPreparation;
enum LadioPreparationState { LADIO_PREPARING=0, LADIO_READY=1, LADIO_CANCELLED=2, LADIO_FAILED=3 };
enum LadioState { LADIO_PAUSED=0, LADIO_PLAYING=1, LADIO_MIXING=2, LADIO_BUFFERING=3, LADIO_ENDED=4 };
typedef struct LadioSnapshot {
    uint64_t generation;
    int64_t output_frame;
    int64_t total_frames;
    int32_t sample_rate;
    int32_t state;
    int32_t track_a;
    int32_t track_b;
    int32_t transition_index;
    double source_a_seconds;
    double source_b_seconds;
    float mix_progress;
    float peak_left;
    float peak_right;
    uint64_t underruns;
    uint64_t rendered_frames;
    double worker_p99_ms;
} LadioSnapshot;
/* All control functions: one serialized producer (e.g. Swift MainActor).
   Create/destroy/plan/analyze are never called from an audio render callback.
   pull: one consumer, bounded, lock-free, no I/O/heap/ML. Stereo noninterleaved. */
LadioPlayer *ladio_player_create(const char *session_path, uint32_t read_ahead_frames);
void ladio_player_destroy(LadioPlayer *player);
int ladio_player_play(LadioPlayer *player);
int ladio_player_pause(LadioPlayer *player);
int ladio_player_seek(LadioPlayer *player, int64_t output_frame);
int ladio_player_skip(LadioPlayer *player, int direction);
int ladio_player_set_loop(LadioPlayer *player, int enabled);
uint32_t ladio_player_pull(LadioPlayer *player, float *left, float *right, uint32_t frames);
LadioSnapshot ladio_player_snapshot(const LadioPlayer *player);
const char *ladio_last_error(void);
/* Offline preparation; same planner/compiler/DSP as streamed playback. */
int ladio_prepare_library(const char *library_json, const char *output_directory,
                       const char *mode, uint64_t seed);
/* Each job owns one preparation thread. Ready manifests publish atomically.
   Cancellation checked between DSP blocks; destroy joins outside audio/UI threads. */
LadioPreparation *ladio_preparation_start(const char *library_json, const char *output_directory,
                                    const char *mode, uint64_t seed);
void ladio_preparation_cancel(LadioPreparation *job);
int ladio_preparation_state(const LadioPreparation *job);
float ladio_preparation_progress(const LadioPreparation *job);
const char *ladio_preparation_error(const LadioPreparation *job);
void ladio_preparation_destroy(LadioPreparation *job);
int ladio_render_session(const char *session_json, const char *wav_path, const char *report_path);
/* Coarse native analysis. Optional ladio_analysis adds learned beat/downbeat analysis. */
int ladio_analyze_pcm(const float *interleaved_stereo, int64_t frames, int32_t sample_rate,
                  const char *asset_id, const char *output_json);
/* Merge full-track Core ML/other model observations, then recompute musical
   structure with the same postprocessing as the ONNX analyzer. Files contain
   track JSON and 50 ms activity/structure probabilities bound to source_hash.
   Returns 0 on invalid identity/clock/probabilities; does not write on failure.
   Background worker only; preserves the measured beat grid and DSP harmony. */
int ladio_apply_model_events(const char *track_json, const char *observations_json,
                             const char *output_json);
#ifdef __cplusplus
}
#endif
#endif
