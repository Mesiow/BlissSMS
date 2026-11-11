// Swift port of BlissSMS/Core/Cart.c + Cart.h

import Foundation

public let CART_32K = 0x8000
public let CART_64K = 0x10000
public let CART_128K = 0x20000
public let CART_256K = 0x40000
public let CART_512K = 0x80000

public enum CartError: Error, CustomStringConvertible {
    case fileNotFound(String)
    case sizeNotSupported(Int)

    public var description: String {
        switch self {
        case .fileNotFound(let path): return "Cartridge file: \(path) could not be found"
        case .sizeNotSupported(let size): return String(format: "Cartridge size: 0x%05X not supported", size)
        }
    }
}

public final class Cart {
    public var memory: [UInt8] = []
    public var ramBanks = [UInt8](repeating: 0, count: 0x8000) // on board cartridge ram (sram)
    public var region: UInt8 = 0
    public var romsize = 0

    public var sramPath: String?
    public var usesSram: UInt8 = 0
    public var banksSram: UInt8 = 0

    /// cartInit
    public init() {
        memory = []
        usesSram = 0
        banksSram = 0
        sramPath = nil
    }

    /// cartLoad. `path` is the full path of the .sms file (the C version appends ".sms").
    /// The save file lives next to it with a .sav extension.
    public func load(_ path: String) throws {
        guard let data = readFile(path) else {
            throw CartError.fileNotFound(path)
        }

        // Determine rom size
        let fileSize = data.count
        romsize = fileSize
        // 32kb, 64kb, 128kb, 256kb, 512kb
        guard fileSize == CART_32K || fileSize == CART_64K
                || fileSize == CART_128K || fileSize == CART_256K
                || fileSize == CART_512K else {
            throw CartError.sizeNotSupported(fileSize)
        }

        memory = data

        let headerStartOffset = 0x7FF0
        let regionSize = memory[headerStartOffset + 0xF]
        region = (regionSize >> 4) & 0xF

        // does the cart use sram?
        let savPath = (path as NSString).deletingPathExtension + ".sav"
        // Unlike the C version the path is always kept, so games that have never
        // saved before get a .sav file the first time they use cart ram.
        sramPath = savPath
        if let sav = readFile(savPath) {
            usesSram = 1
            print("cart uses sram")

            // load sram
            for i in 0..<min(sav.count, ramBanks.count) {
                ramBanks[i] = sav[i]
            }
        }
    }

    /// Loads ROM bytes directly (used when there's no file on disk).
    public func load(rom: [UInt8]) throws {
        guard [CART_32K, CART_64K, CART_128K, CART_256K, CART_512K].contains(rom.count) else {
            throw CartError.sizeNotSupported(rom.count)
        }
        romsize = rom.count
        memory = rom
        region = (memory[0x7FF0 + 0xF] >> 4) & 0xF
    }

    @inline(__always) public func writeU8(_ value: UInt8, _ address: UInt32) {
        ramBanks[Int(address & 0x7FFF)] = value
    }

    @inline(__always) public func readU8(_ address: UInt32, _ ram: UInt8) -> UInt8 {
        if ram != 0 {
            return ramBanks[Int(address & 0x7FFF)]
        }
        return memory[Int(address) & (romsize - 1)]
    }

    /// cartDumpSram: writes cart ram to the .sav file if the game used it.
    /// (The C version measured the file size after truncating it, so it wrote 0 bytes;
    /// this writes the full 32 KB.)
    public func dumpSram() {
        guard banksSram != 0, let sramPath else { return }
        // dump sram to disk
        let url = URL(fileURLWithPath: sramPath)
        try? Data(ramBanks).write(to: url, options: .atomic)
        usesSram = 1
    }

    /// cartFree
    public func free() {
        memory = []
    }
}

/// fopen/fread helper shared by Bus and Cart.
func readFile(_ path: String) -> [UInt8]? {
    FileManager.default.contents(atPath: path).map { [UInt8]($0) }
}
