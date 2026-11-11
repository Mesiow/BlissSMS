// Swift port of BlissSMS/Core/Bus.c + Bus.h

/*
    Master System/Mark III (assuming Sega mapper)
    Region          Maps to
    $0000-$03ff     ROM (unpaged)
    $0400-$3fff     ROM mapper slot 0
    $4000-$7fff     ROM mapper slot 1
    $8000-$bfff     ROM/RAM mapper slot 2
    $c000-$dfff     System RAM
    $e000-$ffff     System RAM (mirror)
    $fff8           3D glasses control
    $fff9-$fffb     3D glasses control (mirrors)
    $fffc           Cartridge RAM mapper control
    $fffd           Mapper slot 0 control
    $fffe           Mapper slot 1 control
    $ffff           Mapper slot 2 control
*/

public let ROM_SIZE = 0x400
public let ROM_MAPPER_0_SIZE = 0x3C00
public let ROM_MAPPER_1_SIZE = 0x4000
public let RAM_MAPPER_2_SIZE = 0x4000
public let SYSRAM_SIZE = 0x2000
public let BIOS_SIZE = 0x2000

public let ROM_START: UInt16 = 0x0000
public let ROM_END: UInt16 = 0x3FF
public let ROM_SLOT_0_START: UInt16 = 0x400
public let ROM_SLOT_0_END: UInt16 = 0x3FFF
public let ROM_SLOT_1_START: UInt16 = 0x4000
public let ROM_SLOT_1_END: UInt16 = 0x7FFF

public let RAM_SLOT_2_START: UInt16 = 0x8000
public let RAM_SLOT_2_END: UInt16 = 0xBFFF
public let SYSRAM_START: UInt16 = 0xC000
public let SYSRAM_END: UInt16 = 0xDFFF

// Mapping Registers
public let RAM_MAPPING_REGISTER: UInt16 = 0xFFFC
public let ROM_MAPPING_0: UInt16 = 0xFFFD
public let ROM_MAPPING_1: UInt16 = 0xFFFE
public let ROM_MAPPING_2: UInt16 = 0xFFFF

public final class Bus: MemoryBus {
    public var memory = [UInt8](repeating: 0, count: 0xC000)   // 0 - BFFF (rom - rom mapper 2)
    public var systemRam = [UInt8](repeating: 0, count: SYSRAM_SIZE)
    public var bios = [UInt8](repeating: 0, count: SYSRAM_SIZE)

    // Memory control bits
    public var cartSlotEnabled: UInt8 = 1
    public var wramEnabled: UInt8 = 0
    public var biosEnabled: UInt8 = 0
    public var ioEnabled: UInt8 = 0

    // Rom mapping registers
    public var page2Ram: UInt8 = 0
    public var cartRamPage: UInt8 = 0
    public var romBank0Register: UInt8 = 0
    public var romBank1Register: UInt8 = 1
    public var romBank2Register: UInt8 = 2

    public var cartLoaded: UInt8 = 0
    /// Not in the C code: set once a BIOS image is loaded. Games switch the BIOS on and off
    /// through port $3E; without a BIOS image that switch is ignored (otherwise the CPU
    /// would read an empty BIOS and crash, e.g. Ninja Gaiden).
    public var biosLoaded: UInt8 = 0

    public private(set) var cart: Cart?

    /// memoryBusInit
    public init() {
        wramEnabled = 0
        cartSlotEnabled = 1
        biosEnabled = 0
        ioEnabled = 0
        cartLoaded = 0

        systemRam[0x1FFD] = 0x0
        systemRam[0x1FFE] = 0x1
        systemRam[0x1FFF] = 0x2

        page2Ram = 0
        cartRamPage = 0
        romBank0Register = 0
        romBank1Register = 1
        romBank2Register = 2
    }

    /// memoryBusLoadBios. Returns false if the file can't be read.
    @discardableResult
    public func loadBios(_ path: String) -> Bool {
        guard let data = readFile(path) else {
            print("---Bios file could not be found---")
            return false
        }
        biosEnabled = 1
        biosLoaded = 1
        for i in 0..<min(BIOS_SIZE, data.count) {
            bios[i] = data[i]
        }
        return true
    }

    /// memoryBusLoadCart
    public func loadCart(_ cart: Cart) {
        self.cart = cart
        cartLoaded = 1

        let count = cart.romsize == CART_32K ? 0x8000 : 0xC000 // fill the full 48k rom space
        for i in 0..<min(count, cart.memory.count) {
            memory[i] = cart.memory[i]
        }
    }

    public func memoryBusWriteU8(_ value: UInt8, _ address: UInt16) {
        if address < 0x8000 { return }

        if address >= RAM_SLOT_2_START && address <= RAM_SLOT_2_END {
            if page2Ram == 1 { // page 2 mapped as cart ram
                var ramBankAddr = UInt32(address) + 0x4000 * UInt32(cartRamPage)
                ramBankAddr -= 0x8000
                cart?.writeU8(value, ramBankAddr)
            }
        }

        if address >= SYSRAM_START && address <= SYSRAM_END {
            systemRam[Int(address) & (SYSRAM_SIZE - 1)] = value
        }
        // Mirrored system ram
        else if address >= 0xE000 {
            systemRam[Int(address) & (SYSRAM_SIZE - 1)] = value
        }

        // Rom mapping registers
        if address == RAM_MAPPING_REGISTER { // ram/rom control
            let b3 = testBit(value, 3)
            if b3 != 0 { // page 2 mapped as on board cart ram
                cart?.banksSram = 1
                page2Ram = 1
                if testBit(value, 2) != 0 {
                    cartRamPage = 1 // use second page of cart ram
                } else {
                    cartRamPage = 0 // use first page of cart ram
                }
            } else { // page 2 mapped as rom
                page2Ram = 0
            }
        } else if address == ROM_MAPPING_0 {
            romBank0Register = value
        } else if address == ROM_MAPPING_1 {
            romBank1Register = value
        } else if address == ROM_MAPPING_2 {
            if page2Ram == 0 {
                // mapped as rom
                romBank2Register = value
            }
        }
    }

    /// Port $3E (written through Io)
    public func writeMemoryControl(_ value: UInt8) {
        cartSlotEnabled = ((value >> 6) & 0x1) == 0 ? 1 : 0
        wramEnabled = ((value >> 4) & 0x1) == 0 ? 1 : 0
        biosEnabled = ((value >> 3) & 0x1) == 0 ? 1 : 0
        ioEnabled = ((value >> 2) & 0x1) == 0 ? 1 : 0
    }

    public func memoryBusReadU8(_ address: UInt16) -> UInt8 {
        if Int(address) < BIOS_SIZE && biosEnabled != 0 && biosLoaded != 0 {
            return bios[Int(address) & (BIOS_SIZE - 1)]
        }

        if cartSlotEnabled != 0 {
            guard cartLoaded != 0, let cart else { return 0 }

            if address >= ROM_START && address <= ROM_END {
                return memory[Int(address)]
            }

            if cart.romsize == CART_32K {
                if address < 0x8000 {
                    return memory[Int(address) & 0x7FFF]
                }
            } else {
                if address >= ROM_SLOT_0_START && address <= ROM_SLOT_0_END {
                    return handleRomMappingRead(address, 0)
                } else if address >= ROM_SLOT_1_START && address <= ROM_SLOT_1_END {
                    return handleRomMappingRead(address, 1)
                } else if address >= RAM_SLOT_2_START && address <= RAM_SLOT_2_END {
                    return handleRomMappingRead(address, 2)
                }
            }
        }

        if address >= SYSRAM_START && address <= SYSRAM_END {
            return systemRam[Int(address) & (SYSRAM_SIZE - 1)]
        }
        // Mirrored system ram
        else if address >= 0xE000 {
            return systemRam[Int(address) & (SYSRAM_SIZE - 1)]
        }

        // The C version falls off the end of the function here (e.g. $8000-$BFFF on a
        // 32K cart), which is undefined. Return open-bus $FF instead.
        return 0xFF
    }

    /// memoryBusHandleRomMappingRead
    public func handleRomMappingRead(_ address: UInt16, _ romBank: UInt8) -> UInt8 {
        guard let cart else { return 0xFF }
        switch romBank {
        case 0:
            let bankAddress = UInt32(address) + 0x4000 * UInt32(romBank0Register)
            return cart.readU8(bankAddress, 0)

        case 1:
            var bankAddress = UInt32(address) + 0x4000 * UInt32(romBank1Register)
            bankAddress -= 0x4000 // get actual bank offset
            return cart.readU8(bankAddress, 0)

        case 2:
            if page2Ram == 1 { // cart ram mapped here
                var ramBankAddr = UInt32(address) + 0x4000 * UInt32(cartRamPage)
                ramBankAddr -= 0x8000
                return cart.readU8(ramBankAddr, 1)
            }

            var bankAddress = UInt32(address) + 0x4000 * UInt32(romBank2Register)
            bankAddress -= 0x8000
            return cart.readU8(bankAddress, 0)

        default:
            print("Rom bank \(romBank) not yet implemented")
            return 0xFF
        }
    }
}
