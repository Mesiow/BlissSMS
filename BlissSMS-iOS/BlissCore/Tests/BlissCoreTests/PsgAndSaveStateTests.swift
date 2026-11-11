import Foundation
import Testing
@testable import BlissCore

private func writeRom(_ rom: [UInt8]) throws -> String {
    let url = FileManager.default.temporaryDirectory.appendingPathComponent("bliss-\(UUID().uuidString).sms")
    try Data(rom).write(to: url)
    return url.path
}

/// A small game-like ROM: VBlank interrupts on, the main loop counts in RAM and writes
/// to the VDP and PSG, the interrupt handler counts frames.
private func busyRom(seed: UInt8 = 0) -> [UInt8] {
    var rom = [UInt8](repeating: 0, count: CART_32K)
    func put(_ at: Int, _ bytes: [UInt8]) { rom.replaceSubrange(at..<at + bytes.count, with: bytes) }
    put(0x0000, [0xF3, 0xED, 0x56, 0x31, 0xF0, 0xDF, 0xC3, 0x00, 0x01])            // di; im 1; ld sp; jp $100
    put(0x0038, [0xF5, 0xDB, 0xBF, 0x3A, 0x01, 0xC0, 0x3C, 0x32, 0x01, 0xC0,       // irq: frame counter at $C001
                 0xF1, 0xFB, 0xED, 0x4D])
    put(0x0100, [0x3E, 0xE0, 0xD3, 0xBF, 0x3E, 0x81, 0xD3, 0xBF,                    // VDP reg 1 = $E0
                 0x3E, 0x90, 0xD3, 0x7F,                                            // PSG ch0 volume 0
                 0xFB,                                                              // ei
                 0x3A, 0x00, 0xC0, 0x3C, 0x32, 0x00, 0xC0,                          // loop: ($C000)++
                 0xD3, 0xBE, 0xE6, 0x0F, 0xF6, 0x80, 0xD3, 0x7F,                    // out ($BE),a ; ch0 tone latch
                 0x18, 0xEF])                                                       // jr loop
    rom[0x7FF0] = seed                                                              // makes ROMs differ
    return rom
}

@Suite struct PsgTests {

    @Test func toneChannelPlaysTheRightFrequency() {
        let psg = Psg()
        psg.sampleRate = 48_000
        psg.writePort(0x80 | 0x0E)        // ch0 tone low 4 bits = $E
        psg.writePort(0x0F)               // ch0 tone high 6 bits = $0F -> tone = $0FE (254)
        psg.writePort(0x90)               // ch0 volume 0 (loudest)

        // One second of CPU cycles
        var remaining = CPU_CLOCK
        while remaining > 0 { let c = min(remaining, 200); psg.update(UInt8(c)); remaining -= c }
        let samples = psg.takeSamples()
        #expect(abs(samples.count - 48_000) < 10)

        // Count rising zero crossings: f = CPU_CLOCK / (32 * 254) = 440.4 Hz
        var crossings = 0
        for i in 1..<samples.count where samples[i - 1] < 0 && samples[i] >= 0 { crossings += 1 }
        #expect(abs(crossings - 440) <= 3)
    }

    @Test func mutedChannelsAreSilent() {
        let psg = Psg()
        for ch: UInt8 in 0..<4 { psg.writePort(0x90 | ch << 5 | 0xF) }  // all volumes off
        for _ in 0..<10_000 { psg.update(16) }
        #expect(psg.takeSamples().allSatisfy { abs($0) < 0.001 })
    }

    @Test func noiseWriteResetsShiftRegister() {
        let psg = Psg()
        psg.writePort(0xF0)                // noise volume 0
        psg.writePort(0xE4)                // white noise, rate 0
        for _ in 0..<1000 { psg.update(16) }
        #expect(psg.lfsr != 0x8000)
        psg.writePort(0xE4)
        #expect(psg.lfsr == 0x8000)
        #expect(psg.tones[3] == 0x4)
    }

    @Test func gameWritesReachThePsgThroughIo() throws {
        let sms = try System(cartPath: try writeRom(busyRom()))
        sms.runEmulation()
        #expect(sms.psg.volume[0] == 0)            // the ROM set ch0 volume to 0
        #expect(!sms.psg.takeSamples().isEmpty)    // and samples were produced during the frame
    }
}

@Suite struct SaveStateTests {

    @Test func loadingAStateReplaysExactly() throws {
        let sms = try System(cartPath: try writeRom(busyRom()))
        for _ in 0..<30 { sms.runEmulation() }
        let state = sms.saveState()

        for _ in 0..<20 { sms.runEmulation() }
        let expected = (sms.z80.pc, sms.z80.af.value, sms.bus.systemRam, sms.vdp.vram, sms.vdp.framebuffer, sms.psg.tones)
        #expect(sms.bus.systemRam[1] >= 45)       // interrupts really ran

        try sms.loadState(state)
        for _ in 0..<20 { sms.runEmulation() }
        #expect(sms.z80.pc == expected.0)
        #expect(sms.z80.af.value == expected.1)
        #expect(sms.bus.systemRam == expected.2)
        #expect(sms.vdp.vram == expected.3)
        #expect(sms.vdp.framebuffer == expected.4)
        #expect(sms.psg.tones == expected.5)
    }

    @Test func stateFromAnotherGameIsRejected() throws {
        let a = try System(cartPath: try writeRom(busyRom(seed: 1)))
        let b = try System(cartPath: try writeRom(busyRom(seed: 2)))
        a.runEmulation()
        #expect(throws: SaveStateError.self) { try b.loadState(a.saveState()) }
    }

    @Test func damagedStateLeavesMachineUntouched() throws {
        let sms = try System(cartPath: try writeRom(busyRom()))
        for _ in 0..<5 { sms.runEmulation() }
        let good = sms.saveState()
        for _ in 0..<5 { sms.runEmulation() }
        let pcBefore = sms.z80.pc
        let ramBefore = sms.bus.systemRam

        let damaged = good.prefix(good.count / 2)
        #expect(throws: SaveStateError.self) { try sms.loadState(Data(damaged)) }
        #expect(sms.z80.pc == pcBefore)
        #expect(sms.bus.systemRam == ramBefore)
        #expect(throws: SaveStateError.self) { try sms.loadState(Data("not a state".utf8)) }
    }
}
