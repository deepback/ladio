# Installed C consumer

Build/install the SDK, then configure this separate project with `-DCMAKE_PREFIX_PATH=/path/to/install`. Build and run `ladio_consumer`. It checks the installed package and C ABI without importing private C++ headers. The header documents control/audio thread ownership; production applications supply their own audio callback.
