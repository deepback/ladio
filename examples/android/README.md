# Android NDK integration

Build the same core with Android NDK r26 or newer and CMake, targeting `arm64-v8a` for current phones:

```sh
cmake -S . -B build/android \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DLADIO_BUILD_TOOLS=OFF -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/android --parallel 2
```

Add the core as a CMake subdirectory in the host app or package the library in the matching `jniLibs` ABI directory. The core sets 16 KB maximum page alignment on Android. Include the NDK C++ runtime required by your chosen linkage.

`bridge.cpp` demonstrates JNI control ownership and error propagation. Replace its example package name with the host's package. The host's Oboe/AAudio callback should call `ladio_player_pull` directly with preallocated planar buffers and convert/interleave only as required. JNI allocation and Java calls do not belong inside that callback.

For learned analysis, supply the Android ONNX Runtime SDK and optional SoXR library, copy the verified model bundle to an accessible model directory, and run analysis on a bounded background worker. The example is not an APK and does not implement foreground service/audio focus, MediaSession, decoding, permissions or cache eviction. This release does not claim a measured phone preparation deadline.
