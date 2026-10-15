#include <jni.h>
#include <cstdint>
#include <ladio/ladio.h>
extern "C" JNIEXPORT jlong JNICALL
Java_org_example_ladio_NativeEngine_open(JNIEnv *env, jclass, jstring path) {
    if (!path) return 0;
    const char *text = env->GetStringUTFChars(path, nullptr);
    if (!text) return 0;
    auto *player = ladio_player_create(text, 65536);
    env->ReleaseStringUTFChars(path, text);
    if (!player) {
        auto error = env->FindClass("java/lang/IllegalStateException");
        if (error) env->ThrowNew(error, ladio_last_error());
    }
    return reinterpret_cast<jlong>(player);
}
extern "C" JNIEXPORT void JNICALL
Java_org_example_ladio_NativeEngine_close(JNIEnv *, jclass, jlong handle) {
    ladio_player_destroy(reinterpret_cast<LadioPlayer *>(handle));
}
extern "C" JNIEXPORT jboolean JNICALL
Java_org_example_ladio_NativeEngine_play(JNIEnv *, jclass, jlong handle) {
    return ladio_player_play(reinterpret_cast<LadioPlayer *>(handle)) ? JNI_TRUE : JNI_FALSE;
}
