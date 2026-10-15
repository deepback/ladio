# Swift / Apple host

Install the core and add its include directory to Swift's import paths and the library to linker settings. `include/module.modulemap` exports `LadioCore`. `Player.swift` is an ownership/control skeleton, not a complete AVAudioEngine application.

For a local type check: `swiftc -typecheck -I build/install/include examples/swift/Player.swift`.

Compile the core with the appropriate Apple toolchain for macOS or iOS; package architecture slices/XCFramework in the host build. Handle session interruptions, background audio entitlement, output route and realtime callback buffers in the app. Keep the session alive while its callback can run, stop the device callback before destruction, and destroy away from the callback.
