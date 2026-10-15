// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "LadioCoreML",
    platforms: [.iOS("18.0"), .macOS("15.0")],
    products: [.library(name: "LadioCoreML", targets: ["LadioCoreML"])],
    targets: [
        .target(name: "LadioCoreML"),
        .testTarget(name: "LadioCoreMLTests", dependencies: ["LadioCoreML"])
    ]
)
