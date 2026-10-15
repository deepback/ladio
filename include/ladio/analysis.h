#ifndef LADIO_ANALYSIS_H
#define LADIO_ANALYSIS_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct LadioAnalyzer LadioAnalyzer;
typedef void (*LadioProgressCallback)(void *context, const char *phase, int completed, int total);
/* Optional ladio_analysis library. One operation at a time per process.
 * All analyze/prepare/destroy calls belong on a background worker.
 * Model root: models/beat-this-small0. PCM: 44.1 kHz stereo float32 LE.
 * Output strings are owned by the handle; copy before the next operation.
 */
LadioAnalyzer *ladio_analyzer_create(const char *model_root, int threads);
const char *ladio_analyzer_create_error(void);
void ladio_analyzer_destroy(LadioAnalyzer *analyzer);
void ladio_analyzer_set_progress(LadioAnalyzer *, LadioProgressCallback, void *context);
int ladio_analyzer_analyze(LadioAnalyzer *, const char *pcm, const char *asset_id,
                          const char *output_json, const char *scope);
int ladio_analyzer_prepare(LadioAnalyzer *, const char *request_json, const char *output_json);
/* Safe from another thread. Destroy only after the worker has returned. */
void ladio_analyzer_cancel(LadioAnalyzer *);
const char *ladio_analyzer_error(const LadioAnalyzer *);
#ifdef __cplusplus
}
#endif
#endif
