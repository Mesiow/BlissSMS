import Testing
@testable import BlissCore

/// 64 KB of RAM wired up as both the memory bus and the IO bus.
private final class TestBus: MemoryBus, IoBus {
    var memory = [UInt8](repeating: 0, count: 0x10000)
    func memoryBusReadU8(_ address: UInt16) -> UInt8 { memory[Int(address)] }
    func memoryBusWriteU8(_ value: UInt8, _ address: UInt16) { memory[Int(address)] = value }
    func ioReadU8(_ address: UInt8) -> UInt8 { 0xFF }
    func ioWriteU8(_ value: UInt8, _ address: UInt8) {}
}

/// Runs a short program from address 0. Returns the CPU and its bus (kept alive by the caller).
private func run(_ program: [UInt8], steps: Int, setup: (Z80) -> Void = { _ in }) -> (Z80, TestBus) {
    let bus = TestBus()
    for (i, b) in program.enumerated() { bus.memory[i] = b }
    let z80 = Z80()
    z80.connectBus(bus)
    z80.connectIo(bus)
    setup(z80)
    for _ in 0..<steps { z80.clock() }
    return (z80, bus)
}

@Suite struct Z80Tests {

    @Test func loadsAndAdds() {
        // ld a,$10 ; ld b,$22 ; add a,b
        let (cpu, bus) = run([0x3E, 0x10, 0x06, 0x22, 0x80], steps: 3)
        #expect(cpu.af.hi == 0x32)
        #expect(cpu.af.lo & FLAG_Z == 0)
        #expect(cpu.cycles == 4)
    }

    @Test func addOverflowAndCarryFlags() {
        // ld a,$7F ; add a,$01  -> $80, overflow, sign, half carry
        var (cpu, bus) = run([0x3E, 0x7F, 0xC6, 0x01], steps: 2)
        #expect(cpu.af.hi == 0x80)
        #expect(cpu.af.lo & FLAG_PV != 0)
        #expect(cpu.af.lo & FLAG_S != 0)
        #expect(cpu.af.lo & FLAG_H != 0)
        #expect(cpu.af.lo & FLAG_C == 0)

        // ld a,$FF ; add a,$01 -> 0, zero + carry
        (cpu, bus) = run([0x3E, 0xFF, 0xC6, 0x01], steps: 2)
        #expect(cpu.af.hi == 0)
        #expect(cpu.af.lo & FLAG_Z != 0)
        #expect(cpu.af.lo & FLAG_C != 0)
    }

    @Test func undocumentedFlagBitsAreCleared() {
        // ld sp,$100 ; ld bc,$00FF ; push bc ; pop af  -> F = $FF, but bits 3/5 cleared
        let (cpu, bus) = run([0x31, 0x00, 0x01, 0x01, 0xFF, 0x00, 0xC5, 0xF1], steps: 4)
        #expect(cpu.af.lo == 0xD7)
    }

    @Test func callAndReturn() {
        // 0: call $0010 ; 3: halt ... 10: ld a,$42 ; ret
        var program = [UInt8](repeating: 0, count: 0x20)
        program[0] = 0xCD; program[1] = 0x10; program[2] = 0x00
        program[3] = 0x76
        program[0x10] = 0x3E; program[0x11] = 0x42; program[0x12] = 0xC9
        let (cpu, bus) = run(program, steps: 4) { $0.sp = 0x8000 }
        #expect(cpu.af.hi == 0x42)
        #expect(cpu.halted == 1)
        #expect(cpu.sp == 0x8000)
    }

    @Test func djnzLoops() {
        // ld b,3 ; inc a ; djnz -3
        let (cpu, bus) = run([0x06, 0x03, 0x3C, 0x10, 0xFD], steps: 1 + 3 * 2)
        #expect(cpu.bc.hi == 0)
        #expect(cpu.af.hi == 3)
        #expect(cpu.pc == 5)
    }

    @Test func indexedLoadStore() {
        // ld ix,$4000 ; ld (ix+5),$AB ; ld a,(ix+5) ; ld (ix-1),a
        let (cpu, bus) = run([0xDD, 0x21, 0x00, 0x40,
                       0xDD, 0x36, 0x05, 0xAB,
                       0xDD, 0x7E, 0x05,
                       0xDD, 0x77, 0xFF], steps: 4)
        #expect(cpu.af.hi == 0xAB)
        #expect(bus.memoryBusReadU8(0x4005) == 0xAB)
        #expect(bus.memoryBusReadU8(0x3FFF) == 0xAB)
        #expect(cpu.cycles == 19)
    }

    @Test func indexedBitOps() {
        // ld iy,$4000 ; set 3,(iy+2) ; bit 3,(iy+2) ; res 3,(iy+2)
        let (cpu, bus) = run([0xFD, 0x21, 0x00, 0x40,
                       0xFD, 0xCB, 0x02, 0xDE,
                       0xFD, 0xCB, 0x02, 0x5E], steps: 3)
        #expect(bus.memoryBusReadU8(0x4002) == 0x08)
        #expect(cpu.af.lo & FLAG_Z == 0)
        #expect(cpu.pc == 12)
    }

    @Test func ixHalfRegisters() {
        // ld ix,$1234 ; ld a,ixh ; ld b,ixl ; inc ixl
        let (cpu, bus) = run([0xDD, 0x21, 0x34, 0x12, 0xDD, 0x7C, 0xDD, 0x45, 0xDD, 0x2C], steps: 4)
        #expect(cpu.af.hi == 0x12)
        #expect(cpu.bc.hi == 0x34)
        #expect(cpu.ix.value == 0x1235)
    }

    @Test func ldirCopiesBlock() {
        // ld hl,$100 ; ld de,$200 ; ld bc,4 ; ldir
        var program = [UInt8](repeating: 0, count: 0x110)
        program.replaceSubrange(0..<11, with: [0x21, 0x00, 0x01, 0x11, 0x00, 0x02, 0x01, 0x04, 0x00, 0xED, 0xB0])
        program.replaceSubrange(0x100..<0x104, with: [1, 2, 3, 4])
        let (cpu, bus) = run(program, steps: 3 + 4)
        #expect(cpu.bc.value == 0)
        #expect((0..<4).map { bus.memoryBusReadU8(0x200 + UInt16($0)) } == [1, 2, 3, 4])
        #expect(cpu.pc == 11)
    }

    @Test func daaAfterAdd() {
        // ld a,$15 ; add a,$27 ; daa  -> $42 BCD
        let (cpu, bus) = run([0x3E, 0x15, 0xC6, 0x27, 0x27], steps: 3)
        #expect(cpu.af.hi == 0x42)
    }

    @Test func interruptJumpsToVector() {
        // ei ; nop ; nop
        let (cpu, bus) = run([0xFB, 0x00, 0x00], steps: 2) { $0.sp = 0x8000 }
        cpu.handleInterrupts(true)
        #expect(cpu.pc == 0x38)
        #expect(cpu.iff1 == 0)
        #expect(bus.memoryBusReadU8(0x7FFE) == 0x02)
    }

    @Test func unimplementedOpcodeStopsCPU() {
        // ED 46 (IM 0) isn't implemented in the C core either
        let (cpu, bus) = run([0xED, 0x46, 0x00], steps: 2)
        #expect(cpu.halted == 1)
        #expect(cpu.fault?.table == "Extended")
        #expect(cpu.fault?.opcode == 0x46)
    }

    @Test func romHeaderParsing() {
        var rom = [UInt8](repeating: 0, count: 0x8000)
        rom.replaceSubrange(0x7FF0..<0x7FF8, with: Array("TMR SEGA".utf8))
        rom[0x7FFA] = 0x34; rom[0x7FFB] = 0x12
        rom[0x7FFF] = 0x4C
        let info = RomInfo(rom: rom)
        #expect(info.hasHeader)
        #expect(info.checksum == 0x1234)
        #expect(info.region == .smsExport)
        #expect(info.sizeDescription == "32 KB")
    }
}

/// Runs the CP/M test ROMs when they're available:
///   BLISS_PRELIM=/path/prelim.com swift test
@Suite struct CPMTests {
    @Test(.enabled(if: ProcessInfo.processInfo.environment["BLISS_PRELIM"] != nil))
    func prelimPasses() throws {
        let path = ProcessInfo.processInfo.environment["BLISS_PRELIM"]!
        let data = try #require(FileManager.default.contents(atPath: path))
        var output = ""
        let cpm = Cpm()
        cpm.output = { output += $0 }
        cpm.loadRom([UInt8](data))
        cpm.run(maxInstructions: 50_000_000)
        #expect(cpm.testFinished)
        #expect(cpm.z80.fault == nil)
        #expect(output.contains("Preliminary tests complete"))
    }
}

import Foundation
