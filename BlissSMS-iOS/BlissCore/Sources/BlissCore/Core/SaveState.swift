// Save states (not in the C code).
//
// A save state is a snapshot of every chip: Z80 registers, RAM and mapper registers,
// cart ram, VDP memory/registers and the PSG. Format (little endian):
//
//   "BLSS"  magic
//   u16     version
//   u64     ROM hash (a state only loads into the game it came from)
//   ...     each part in the order of System.saveState()

import Foundation

public enum SaveStateError: Error, CustomStringConvertible {
    case notASaveState
    case unsupportedVersion(UInt16)
    case differentGame
    case truncated

    public var description: String {
        switch self {
        case .notASaveState: return "This file isn't a BlissSMS save state."
        case .unsupportedVersion(let v): return "Save state version \(v) isn't supported."
        case .differentGame: return "This save state belongs to a different game."
        case .truncated: return "The save state file is damaged."
        }
    }
}

public extension System {
    static let saveStateVersion: UInt16 = 1

    /// Captures the whole machine.
    func saveState() -> Data {
        var w = StateWriter()
        w.bytes(Array("BLSS".utf8), counted: false)
        w.u16(System.saveStateVersion)
        w.u64(romHash)

        z80.save(&w)
        bus.save(&w)
        cart.save(&w)
        vdp.save(&w)
        io.save(&w)
        joy.save(&w)
        psg.save(&w)
        return w.data
    }

    /// Restores a snapshot made by saveState(). Throws without changing anything if the
    /// data is damaged or from another game.
    func loadState(_ data: Data) throws {
        var header = StateReader(data)
        try System.readHeader(&header, romHash: romHash)

        // If the file turns out to be damaged part way through, put the machine back
        // the way it was instead of leaving it half loaded.
        let backup = saveState()
        do {
            try restore(data)
        } catch {
            try? restore(backup)
            throw error
        }
    }

    private func restore(_ data: Data) throws {
        var r = StateReader(data)
        try System.readHeader(&r, romHash: romHash)
        try z80.load(&r)
        try bus.load(&r)
        try cart.load(&r)
        try vdp.load(&r)
        try io.load(&r)
        try joy.load(&r)
        try psg.load(&r)

        // Buttons held when the state was made would otherwise stay pressed
        joy.joypadPort = 0xFF
        joy.joypadTemp = 0xFF
    }

    /// FNV-1a hash of the ROM, used to match states to games.
    var romHash: UInt64 {
        var hash: UInt64 = 0xcbf2_9ce4_8422_2325
        for byte in cart.memory {
            hash ^= UInt64(byte)
            hash = hash &* 0x100_0000_01b3
        }
        return hash
    }

    private static func readHeader(_ r: inout StateReader, romHash: UInt64) throws {
        guard try r.bytes(4) == Array("BLSS".utf8) else { throw SaveStateError.notASaveState }
        let version = try r.u16()
        guard version == saveStateVersion else { throw SaveStateError.unsupportedVersion(version) }
        guard try r.u64() == romHash else { throw SaveStateError.differentGame }
    }
}

// MARK: - Per-chip state

extension Z80 {
    func save(_ w: inout StateWriter) {
        for reg in [shadowedRegs.af, shadowedRegs.bc, shadowedRegs.de, shadowedRegs.hl,
                    af, bc, de, hl, ix, iy, ir] {
            w.u16(reg.value)
        }
        w.u16(sp); w.u16(pc); w.u16(memPtr)
        w.u8(interruptMode.rawValue)
        w.u16(cycles)
        w.u8(iff1); w.u8(iff2)
        w.u8(processInterruptDelay); w.u8(halted); w.u8(irqRequested)
        w.u8(serviceNmi); w.u8(lastDaaOperation); w.u8(opcode); w.u8(extOpcode)
    }

    func load(_ r: inout StateReader) throws {
        shadowedRegs.af.value = try r.u16(); shadowedRegs.bc.value = try r.u16()
        shadowedRegs.de.value = try r.u16(); shadowedRegs.hl.value = try r.u16()
        af.value = try r.u16(); bc.value = try r.u16(); de.value = try r.u16(); hl.value = try r.u16()
        ix.value = try r.u16(); iy.value = try r.u16(); ir.value = try r.u16()
        sp = try r.u16(); pc = try r.u16(); memPtr = try r.u16()
        interruptMode = IntMode(rawValue: try r.u8()) ?? .one
        cycles = try r.u16()
        iff1 = try r.u8(); iff2 = try r.u8()
        processInterruptDelay = try r.u8(); halted = try r.u8(); irqRequested = try r.u8()
        serviceNmi = try r.u8(); lastDaaOperation = try r.u8(); opcode = try r.u8(); extOpcode = try r.u8()
        fault = nil
    }
}

extension Bus {
    func save(_ w: inout StateWriter) {
        w.bytes(systemRam)
        for v in [cartSlotEnabled, wramEnabled, biosEnabled, ioEnabled,
                  page2Ram, cartRamPage, romBank0Register, romBank1Register, romBank2Register] {
            w.u8(v)
        }
    }

    func load(_ r: inout StateReader) throws {
        systemRam = try r.bytes(count: SYSRAM_SIZE)
        cartSlotEnabled = try r.u8(); wramEnabled = try r.u8(); biosEnabled = try r.u8(); ioEnabled = try r.u8()
        page2Ram = try r.u8(); cartRamPage = try r.u8()
        romBank0Register = try r.u8(); romBank1Register = try r.u8(); romBank2Register = try r.u8()
    }
}

extension Cart {
    func save(_ w: inout StateWriter) {
        w.bytes(ramBanks)
        w.u8(usesSram); w.u8(banksSram)
    }

    func load(_ r: inout StateReader) throws {
        ramBanks = try r.bytes(count: 0x8000)
        usesSram = try r.u8(); banksSram = try r.u8()
    }
}

extension Vdp {
    func save(_ w: inout StateWriter) {
        w.bytes(vram); w.bytes(cram); w.bytes(registers)
        w.u8(state.rawValue); w.u8(mode.rawValue)
        w.u16(cycles); w.u16(vdpControl); w.u16(vdpData); w.u16(vcounter); w.u16(hcounter)
        for v in [vcountPort, lineCounter, statusFlags, secondControlWrite, writesToVram, readbuffer,
                  frameIntPending, lineIntPending, spriteOverflow, spriteCollision, yScroll, frameComplete] {
            w.u8(v)
        }
        w.u16(displayWidth); w.u16(displayHeight)
        w.bytes(priorityBuffer)
        w.u32s(pixels)
        w.u32s(framebuffer)
    }

    func load(_ r: inout StateReader) throws {
        vram = try r.bytes(count: 0x4000); cram = try r.bytes(count: 0x20); registers = try r.bytes(count: 0xB)
        state = VdpDisplayState(rawValue: try r.u8()) ?? .visible
        mode = VdpDisplayMode(rawValue: try r.u8()) ?? .mode4
        cycles = try r.u16(); vdpControl = try r.u16(); vdpData = try r.u16(); vcounter = try r.u16(); hcounter = try r.u16()
        vcountPort = try r.u8(); lineCounter = try r.u8(); statusFlags = try r.u8()
        secondControlWrite = try r.u8(); writesToVram = try r.u8(); readbuffer = try r.u8()
        frameIntPending = try r.u8(); lineIntPending = try r.u8()
        spriteOverflow = try r.u8(); spriteCollision = try r.u8(); yScroll = try r.u8(); frameComplete = try r.u8()
        displayWidth = try r.u16(); displayHeight = try r.u16()
        priorityBuffer = try r.bytes(count: DISPLAY_WIDTH)
        pixels = try r.u32s(count: DISPLAY_WIDTH * DISPLAY_HEIGHT)
        framebuffer = try r.u32s(count: DISPLAY_WIDTH * DISPLAY_HEIGHT)
    }
}

extension Io {
    func save(_ w: inout StateWriter) { w.u8(nationalizationPort) }
    func load(_ r: inout StateReader) throws { nationalizationPort = try r.u8() }
}

extension Joypad {
    func save(_ w: inout StateWriter) { w.u8(joypadPort); w.u8(joypadTemp); w.u8(joypadPort2) }
    func load(_ r: inout StateReader) throws {
        joypadPort = try r.u8(); joypadTemp = try r.u8(); joypadPort2 = try r.u8()
    }
}

extension Psg {
    func save(_ w: inout StateWriter) {
        w.u16(cycles); w.u8(latchedType); w.u8(latchedChannel)
        for v in tones { w.u16(v) }
        w.bytes(volume)
        for v in counters { w.u16(v) }
        for v in polarity { w.u8(v > 0 ? 1 : 0) }
        w.u16(lfsr)
    }

    func load(_ r: inout StateReader) throws {
        cycles = try r.u16(); latchedType = try r.u8(); latchedChannel = try r.u8()
        for i in 0..<4 { tones[i] = try r.u16() }
        volume = try r.bytes(count: 4)
        for i in 0..<4 { counters[i] = try r.u16() }
        for i in 0..<4 { polarity[i] = try r.u8() != 0 ? 1 : -1 }
        lfsr = try r.u16()
        _ = takeSamples() // drop audio generated before the load
    }
}

// MARK: - Binary helpers

struct StateWriter {
    private(set) var data = Data()

    mutating func u8(_ v: UInt8) { data.append(v) }
    mutating func u16(_ v: UInt16) { withUnsafeBytes(of: v.littleEndian) { data.append(contentsOf: $0) } }
    mutating func u32(_ v: UInt32) { withUnsafeBytes(of: v.littleEndian) { data.append(contentsOf: $0) } }
    mutating func u64(_ v: UInt64) { withUnsafeBytes(of: v.littleEndian) { data.append(contentsOf: $0) } }

    /// Byte arrays are stored with a u32 length in front (unless `counted` is false).
    mutating func bytes(_ v: [UInt8], counted: Bool = true) {
        if counted { u32(UInt32(v.count)) }
        data.append(contentsOf: v)
    }

    mutating func u32s(_ v: [UInt32]) {
        u32(UInt32(v.count))
        v.withUnsafeBufferPointer { buf in
            data.append(UnsafeBufferPointer(start: UnsafeRawPointer(buf.baseAddress!)
                .assumingMemoryBound(to: UInt8.self), count: buf.count * 4))
        }
    }
}

struct StateReader {
    private let data: [UInt8]
    private var offset = 0

    init(_ data: Data) { self.data = [UInt8](data) }

    private mutating func take(_ n: Int) throws -> ArraySlice<UInt8> {
        guard offset + n <= data.count else { throw SaveStateError.truncated }
        defer { offset += n }
        return data[offset..<offset + n]
    }

    mutating func u8() throws -> UInt8 { try take(1).first! }

    mutating func u16() throws -> UInt16 {
        let b = try take(2)
        return UInt16(b[b.startIndex]) | UInt16(b[b.startIndex + 1]) << 8
    }

    mutating func u32() throws -> UInt32 {
        let b = try take(4)
        return b.reversed().reduce(UInt32(0)) { $0 << 8 | UInt32($1) }
    }

    mutating func u64() throws -> UInt64 {
        let b = try take(8)
        return b.reversed().reduce(UInt64(0)) { $0 << 8 | UInt64($1) }
    }

    /// Raw bytes without a length prefix.
    mutating func bytes(_ n: Int) throws -> [UInt8] { Array(try take(n)) }

    /// A counted byte array that must have exactly `count` entries.
    mutating func bytes(count: Int) throws -> [UInt8] {
        guard try u32() == count else { throw SaveStateError.truncated }
        return Array(try take(count))
    }

    mutating func u32s(count: Int) throws -> [UInt32] {
        guard try u32() == count else { throw SaveStateError.truncated }
        let raw = try take(count * 4)
        var out = [UInt32](repeating: 0, count: count)
        out.withUnsafeMutableBytes { dst in
            raw.withUnsafeBytes { src in dst.copyMemory(from: src) }
        }
        return out
    }
}
