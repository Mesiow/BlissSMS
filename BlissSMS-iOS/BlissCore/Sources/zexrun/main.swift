// Runs a CP/M Z80 test program on the Swift Z80 core.
//   swift run -c release zexrun /path/to/zexdoc.com
import Foundation
import BlissCore

guard CommandLine.arguments.count > 1 else {
    print("usage: zexrun <program.com>")
    exit(1)
}

let path = CommandLine.arguments[1]
guard let data = FileManager.default.contents(atPath: path) else {
    print("---CP/M file could not be found---")
    exit(1)
}

setvbuf(stdout, nil, _IONBF, 0)
let cpm = Cpm()
cpm.loadRom([UInt8](data))
print("Test \(path) loaded.")

let start = Date()
cpm.run()
let seconds = Date().timeIntervalSince(start)

print("\n--- finished in \(String(format: "%.1f", seconds))s ---")
if let fault = cpm.z80.fault {
    print("CPU stopped: \(fault)")
    exit(2)
}
