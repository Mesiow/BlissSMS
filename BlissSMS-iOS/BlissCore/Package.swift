// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "BlissCore",
    platforms: [.iOS(.v17), .macOS(.v14)],
    products: [
        .library(name: "BlissCore", targets: ["BlissCore"]),
        .executable(name: "zexrun", targets: ["zexrun"]),
    ],
    targets: [
        .target(
            name: "BlissCore",
            swiftSettings: [
                // The emulator is too slow for 60 fps without optimisation, so the core is
                // always built with -O, even in Debug builds of the app.
                .unsafeFlags(["-O", "-enforce-exclusivity=unchecked"]),
            ]
        ),
        // Runs CP/M test programs (zexdoc.com, prelim.com) on the Swift Z80:
        //   swift run -c release zexrun path/to/zexdoc.com
        .executableTarget(name: "zexrun", dependencies: ["BlissCore"]),
        .testTarget(name: "BlissCoreTests", dependencies: ["BlissCore"]),
    ]
)
