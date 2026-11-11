// The CP/M test stub from Z80.h / Z80.c (struct Cpm, cpmLoadRom, cpmHandleSysCalls,
// cpmReadMem8, cpmWriteMem8). Used to run zexdoc.com / prelim.com on the Z80 core.
// In C this lived inside the CPU behind `cpm_stub_enabled`; here it's the CPU's bus.

public final class Cpm: MemoryBus, IoBus {
    public var memory = [UInt8](repeating: 0, count: 0x10000)
    public let z80 = Z80()
    public private(set) var testFinished = false   // cpm_test_finished

    /// Text printed by the test program (BDOS calls 2 and 9).
    public var output: (String) -> Void = { print($0, terminator: "") }

    public init() {
        z80.connectBus(self)
        z80.connectIo(self)
        z80.cpmStubEnabled = true

        // inject "out 1,a" at 0x0000 (signal to stop the test)
        memory[0x0000] = 0xD3
        memory[0x0001] = 0x00

        // inject "in a,0" at 0x0005 (signal to output some characters)
        memory[0x0005] = 0xDB
        memory[0x0006] = 0x00
        memory[0x0007] = 0xC9 // RET at 0x7

        z80.pc = 0x100
        z80.af.value = 0xFFD7
        z80.sp = 0xFFFF

        z80.opcode = 0
        z80.extOpcode = 0
    }

    /// cpmLoadRom: the program is loaded at 0x100.
    public func loadRom(_ program: [UInt8]) {
        for (i, byte) in program.enumerated() where i + 0x100 < memory.count {
            memory[i + 0x100] = byte
        }
    }

    /// cpmHandleSysCalls
    public func handleSysCalls() {
        if z80.pc == 0x5 {
            if z80.bc.lo == 0x2 {
                output(String(UnicodeScalar(z80.de.lo)))
            } else if z80.bc.lo == 0x9 {
                var i = z80.de.value
                var text = ""
                while readMem8(i) != UInt8(ascii: "$") {
                    text.unicodeScalars.append(UnicodeScalar(readMem8(i)))
                    i &+= 1
                }
                output(text)
            }
        }
    }

    /// The cpm part of z80Clock: handle BDOS, stop when the program jumps to 0.
    @discardableResult
    public func clock() -> UInt16 {
        handleSysCalls()
        if z80.pc == 0 || z80.fault != nil {
            z80.halted = 1
            testFinished = true
            return 0
        }
        return z80.clock()
    }

    /// Runs until the test exits (or `maxInstructions` is reached).
    @discardableResult
    public func run(maxInstructions: Int = .max) -> Bool {
        var count = 0
        while !testFinished && count < maxInstructions {
            clock()
            count += 1
        }
        return testFinished
    }

    @inline(__always) public func writeMem8(_ address: UInt16, _ value: UInt8) {
        memory.withUnsafeMutableBufferPointer { $0[Int(address)] = value }
    }

    @inline(__always) public func readMem8(_ address: UInt16) -> UInt8 {
        memory.withUnsafeBufferPointer { $0[Int(address)] }
    }

    // MemoryBus / IoBus
    public func memoryBusReadU8(_ address: UInt16) -> UInt8 { readMem8(address) }
    public func memoryBusWriteU8(_ value: UInt8, _ address: UInt16) { writeMem8(address, value) }
    public func ioReadU8(_ address: UInt8) -> UInt8 { 0xFF }
    public func ioWriteU8(_ value: UInt8, _ address: UInt8) {}
}
