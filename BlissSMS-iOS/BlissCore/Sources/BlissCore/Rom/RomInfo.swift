/// Information read from a Master System ROM's "TMR SEGA" header.
public struct RomInfo: Equatable, Sendable {
    public enum Region: String, Sendable {
        case smsJapan = "SMS Japan"
        case smsExport = "SMS Export"
        case ggJapan = "Game Gear Japan"
        case ggExport = "Game Gear Export"
        case ggInternational = "Game Gear Intl"
        case unknown = "Unknown"
    }

    public let size: Int
    public let hasHeader: Bool
    public let headerOffset: Int?
    public let checksum: UInt16?
    public let productCode: Int?
    public let version: Int?
    public let region: Region

    public var sizeDescription: String {
        size >= 1024 * 1024 ? "\(size / (1024 * 1024)) MB" : "\(size / 1024) KB"
    }

    /// `rom` can be just the first 32 KB of the file (the header lives below $8000);
    /// pass the real file length as `fileSize` in that case.
    public init(rom: [UInt8], fileSize: Int? = nil) {
        size = fileSize ?? rom.count
        let signature = Array("TMR SEGA".utf8)
        let offset = [0x7FF0, 0x3FF0, 0x1FF0].first { base in
            base + 16 <= rom.count && Array(rom[base..<base + 8]) == signature
        }
        headerOffset = offset
        hasHeader = offset != nil

        guard let o = offset else {
            checksum = nil; productCode = nil; version = nil; region = .unknown
            return
        }
        checksum = UInt16(rom[o + 0xA]) | UInt16(rom[o + 0xB]) << 8
        // Product code is BCD in bytes C-D plus the high nibble of E
        let bcd = { (b: UInt8) in Int(b >> 4) * 10 + Int(b & 0xF) }
        productCode = bcd(rom[o + 0xC]) + bcd(rom[o + 0xD]) * 100 + Int(rom[o + 0xE] >> 4) * 10000
        version = Int(rom[o + 0xE] & 0xF)
        switch rom[o + 0xF] >> 4 {
        case 3: region = .smsJapan
        case 4: region = .smsExport
        case 5: region = .ggJapan
        case 6: region = .ggExport
        case 7: region = .ggInternational
        default: region = .unknown
        }
    }
}
