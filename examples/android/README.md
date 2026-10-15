# Android NDK: подключение ядра

Соберите ядро Android NDK r26 или новее и CMake:

```sh
cmake -S . -B build/android \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DLADIO_BUILD_TOOLS=OFF -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/android --parallel 2
```

Добавьте ядро как CMake-подкаталог приложения либо поместите библиотеку в соответствующий `jniLibs`. Ядро задаёт максимальное выравнивание страницы 16 КБ. Добавьте C++ runtime NDK, необходимый выбранному способу линковки.

`bridge.cpp` показывает JNI-управление и передачу ошибок. Замените пример имени пакета. Колбэк Oboe/AAudio должен напрямую вызывать `ladio_player_pull` с заранее выделенными раздельными буферами. Преобразуйте формат только при необходимости. Выделение памяти JNI и вызовы Java в аудиоколбэке недопустимы.

Для нейросетевого анализа поставьте Android ONNX Runtime SDK и дополнительную SoXR, скопируйте проверенную модель и запускайте анализ в ограниченном фоновом потоке. Пример не является APK: foreground service, audio focus, MediaSession, декодирование, разрешения и очистку кэша реализует приложение. Измеренный срок подготовки на телефоне не заявляется.
