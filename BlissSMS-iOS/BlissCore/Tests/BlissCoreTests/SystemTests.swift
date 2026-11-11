import Foundation
import Testing
@testable import BlissCore

/// Writes a ROM (32 KB by default) with `program` at $0000 to a temp file and returns its path.
private func makeRom(_ program: [UInt8], size: Int = CART_32K) throws -> String {
    var rom = [UInt8](repeating: 0x00, count: size)
    rom.replaceSubrange(0..<program.count, with: program)
    let url = FileManager.default.temporaryDirectory
        .appendingPathComponent("bliss-test-\(UUID().uuidString).sms")
    try Data(rom).write(to: url)
    return url.path
}

@Suite struct SystemTests {

    @Test func vdpDrawsBackdropColour() throws {
        // di
        // out ($BF),$40 / out ($BF),$81   -> VDP register 1 = $40 (display on)
        // out ($BF),$00 / out ($BF),$C0   -> CRAM address 0
        // out ($BE),$03                   -> colour 0 = bright red
        // jr $
        let path = try makeRom([
            0xF3,
            0x3E, 0x40, 0xD3, 0xBF, 0x3E, 0x81, 0xD3, 0xBF,
            0x3E, 0x00, 0xD3, 0xBF, 0x3E, 0xC0, 0xD3, 0xBF,
            0x3E, 0x03, 0xD3, 0xBE,
            0x18, 0xFE,
        ])
        let sms = try System(cartPath: path)
        sms.runEmulation()
        sms.runEmulation()

        let red: UInt32 = 0xFF00_00FF // bytes R=255 G=0 B=0 A=255
        #expect(sms.vdp.framebuffer[100] == red)
        #expect(sms.vdp.framebuffer[191 * DISPLAY_WIDTH + 255] == red)
        #expect(sms.vdp.cram[0] == 0x03)
        #expect(sms.z80.fault == nil)
    }

    @Test func frameTakesOneFrameOfCycles() throws {
        let path = try makeRom([0x18, 0xFE]) // jr $
        let sms = try System(cartPath: path)
        let before = sms.vdp.vcounter
        sms.runEmulation()
        #expect(sms.vdp.vcounter == before)          // wrapped back to line 0
        #expect(sms.vdp.isFrameComplete() == 0)       // flag consumed by runEmulation
    }

    @Test func joypadReadsThroughIo() throws {
        let path = try makeRom([0x18, 0xFE])
        let sms = try System(cartPath: path)
        sms.buttonPressed(.up, true)
        sms.buttonPressed(.a, true)
        sms.runEmulation()
        #expect(sms.io.ioReadU8(0xDC) == 0xFF & ~(BIT_UP | BIT_A))
        sms.buttonPressed(.up, false)
        sms.buttonPressed(.a, false)
        sms.runEmulation()
        #expect(sms.io.ioReadU8(0xDC) == 0xFF)
    }

    @Test func gameSeesButtonsThroughInPort() throws {
        // loop: in a,($DC) ; ld ($C000),a ; jr loop
        let path = try makeRom([0xDB, 0xDC, 0x32, 0x00, 0xC0, 0x18, 0xF9])
        let sms = try System(cartPath: path)
        sms.runEmulation()
        #expect(sms.bus.memoryBusReadU8(0xC000) == 0xFF)

        sms.buttonPressed(.a, true)
        sms.buttonPressed(.right, true)
        sms.runEmulation()
        sms.runEmulation()
        #expect(sms.bus.memoryBusReadU8(0xC000) == 0xFF & ~(BIT_A | BIT_RIGHT))
    }

    @Test func biosSwitchIsIgnoredWithoutBios() throws {
        // Ninja Gaiden writes port $3E with the BIOS bit enabled. With no BIOS image loaded
        // the cart must stay visible, otherwise the CPU runs off into zeros.
        let path = try makeRom([0x3E, 0xA3, 0xD3, 0x3E, 0x18, 0xFE]) // ld a,$A3 ; out ($3E),a ; jr $ (bit 3 = 0: BIOS on)
        let sms = try System(cartPath: path)
        sms.runEmulation()
        #expect(sms.bus.biosEnabled == 1)                   // the game asked for the BIOS...
        #expect(sms.bus.memoryBusReadU8(0x0000) == 0x3E)    // ...but the cart is still there
        #expect(sms.z80.pc == 0x0004)                       // still looping on jr $
    }

    @Test func romBankingSwitchesSlot2() throws {
        var rom = [UInt8](repeating: 0, count: CART_64K)
        for bank in 0..<4 { rom[bank * 0x4000 + 0x10] = UInt8(0xA0 + bank) }
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("bliss-bank-\(UUID().uuidString).sms")
        try Data(rom).write(to: url)

        let sms = try System(cartPath: url.path)
        #expect(sms.bus.memoryBusReadU8(0x8010) == 0xA2)   // slot 2 defaults to bank 2
        sms.bus.memoryBusWriteU8(3, 0xFFFF)
        #expect(sms.bus.memoryBusReadU8(0x8010) == 0xA3)
        sms.bus.memoryBusWriteU8(0, 0xFFFE)
        #expect(sms.bus.memoryBusReadU8(0x4010) == 0xA0)
    }

    @Test func cartRamIsSavedToSavFile() throws {
        // 64 KB: on 32 KB carts the C bus never reads slot 2, so cart ram can't be read back
        let path = try makeRom([0x18, 0xFE], size: CART_64K)
        let sms = try System(cartPath: path)
        sms.bus.memoryBusWriteU8(0x08, 0xFFFC)  // map cart ram into slot 2
        sms.bus.memoryBusWriteU8(0x5A, 0x8000)
        #expect(sms.bus.memoryBusReadU8(0x8000) == 0x5A)
        sms.free()

        let sav = (path as NSString).deletingPathExtension + ".sav"
        let saved = try #require(FileManager.default.contents(atPath: sav))
        #expect(saved.count == 0x8000)
        #expect(saved.first == 0x5A)

        // Loading again picks the save back up
        let again = try System(cartPath: path)
        #expect(again.cart.usesSram == 1)
        #expect(again.cart.ramBanks[0] == 0x5A)
    }

    @Test func unsupportedRomSizeThrows() throws {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("bliss-odd-\(UUID().uuidString).sms")
        try Data(count: 0x6000).write(to: url)
        #expect(throws: CartError.self) { try System(cartPath: url.path) }
    }
}

/// Renders a ROM headlessly and writes the last frame as a .ppm image:
///   BLISS_RENDER_ROM=/path/game.sms BLISS_RENDER_OUT=/tmp/frame.ppm swift test --filter RenderTests
@Suite struct RenderTests {
    @Test(.enabled(if: ProcessInfo.processInfo.environment["BLISS_RENDER_ROM"] != nil))
    func renderFrames() throws {
        let env = ProcessInfo.processInfo.environment
        let sms = try System(cartPath: env["BLISS_RENDER_ROM"]!)
        let frames = Int(env["BLISS_RENDER_FRAMES"] ?? "") ?? 120

        sms.psg.sampleRate = 44_100
        var audio: [Float] = []
        let start = Date()
        for _ in 0..<frames {
            sms.runEmulation()
            audio += sms.psg.takeSamples()
        }
        let seconds = Date().timeIntervalSince(start)
        let rms = (audio.reduce(0) { $0 + $1 * $1 } / Float(max(1, audio.count))).squareRoot()
        print("audio: \(audio.count) samples, rms \(String(format: "%.4f", rms))")
        if let wav = env["BLISS_RENDER_WAV"] { try writeWav(audio, rate: 44_100, to: wav) }
        print(sms.z80.debugOutput())
        print("halted=\(sms.z80.halted) iff1=\(sms.z80.iff1) im=\(sms.z80.interruptMode) vdp regs=\(sms.vdp.registers.map { String($0, radix: 16) }) fault=\(String(describing: sms.z80.fault))")
        print("\(frames) frames in \(String(format: "%.3f", seconds))s (\(Int(Double(frames) / seconds)) fps)")
        #expect(sms.z80.fault == nil)

        if let out = env["BLISS_RENDER_OUT"] {
            var ppm = Data("P6\n\(DISPLAY_WIDTH) \(DISPLAY_HEIGHT)\n255\n".utf8)
            for px in sms.vdp.framebuffer {
                ppm.append(contentsOf: [UInt8(px & 0xFF), UInt8((px >> 8) & 0xFF), UInt8((px >> 16) & 0xFF)])
            }
            try ppm.write(to: URL(fileURLWithPath: out))
        }
    }
}

/// 16-bit mono WAV writer for checking PSG output by ear.
private func writeWav(_ samples: [Float], rate: Int, to path: String) throws {
    var d = Data()
    func u32(_ v: UInt32) { withUnsafeBytes(of: v.littleEndian) { d.append(contentsOf: $0) } }
    func u16(_ v: UInt16) { withUnsafeBytes(of: v.littleEndian) { d.append(contentsOf: $0) } }
    d.append(contentsOf: Array("RIFF".utf8)); u32(UInt32(36 + samples.count * 2))
    d.append(contentsOf: Array("WAVEfmt ".utf8)); u32(16); u16(1); u16(1)
    u32(UInt32(rate)); u32(UInt32(rate * 2)); u16(2); u16(16)
    d.append(contentsOf: Array("data".utf8)); u32(UInt32(samples.count * 2))
    for s in samples { u16(UInt16(bitPattern: Int16(max(-1, min(1, s)) * 32767))) }
    try d.write(to: URL(fileURLWithPath: path))
}
