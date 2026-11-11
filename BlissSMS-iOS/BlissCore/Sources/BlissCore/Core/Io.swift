// Swift port of BlissSMS/Core/Io.c + Io.h

public final class Io: IoBus {
    public var nationalizationPort: UInt8 = 0

    public private(set) unowned var vdp: Vdp
    public private(set) unowned var psg: Psg
    public private(set) unowned var joy: Joypad
    public private(set) unowned var bus: Bus

    /// ioInit. The C version starts with NULL pointers; Swift needs real objects,
    /// so the connected parts are passed in here and can still be swapped with connect*.
    public init(vdp: Vdp, psg: Psg, joy: Joypad, bus: Bus) {
        self.vdp = vdp
        self.psg = psg
        self.joy = joy
        self.bus = bus
    }

    public func connectVdp(_ vdp: Vdp) { self.vdp = vdp }
    public func connectPsg(_ psg: Psg) { self.psg = psg }
    public func connectJoypad(_ joy: Joypad) { self.joy = joy }
    public func connectBus(_ bus: Bus) { self.bus = bus }

    public func ioWriteU8(_ value: UInt8, _ address: UInt8) {
        let evenAddress = (address & 0x1) == 0
        if address <= 0x3F {
            if evenAddress {
                bus.writeMemoryControl(value)
            } else {
                nationalizationPort = value
                // copy bits 5 and 7 to joypad port 2 so
                // they can be read back from
                let b5 = (nationalizationPort >> 5) & 0x1
                let b7 = (nationalizationPort >> 7) & 0x1

                if b5 != 0 { joy.joypadPort2 |= (1 << 6) } else { joy.joypadPort2 &= ~(1 << 6) }

                if b7 != 0 { joy.joypadPort2 |= (1 << 7) } else { joy.joypadPort2 &= ~(1 << 7) }
            }
        } else if address >= 0x40 && address <= 0x7F {
            psg.writePort(value)
        } else if address >= 0x80 && address <= 0xBF {
            if evenAddress {
                vdp.writeDataPort(value)
            } else {
                vdp.writeControlPort(value)
            }
        }
    }

    public func ioReadU8(_ address: UInt8) -> UInt8 {
        let evenAddress = (address & 0x1) == 0
        if address >= 0x40 && address <= 0x7F {
            if evenAddress {
                return vdp.vcountPort
            } else {
                return UInt8(truncatingIfNeeded: vdp.hcounter)
            }
        } else if address >= 0x80 && address <= 0xBF {
            if evenAddress {
                return vdp.readDataPort()
            } else {
                return vdp.readControlPort()
            }
        } else if address >= 0xC0 {
            if evenAddress {
                return joy.joypadPort
            } else {
                return joy.joypadPort2
            }
        }
        // $00-$3F reads are handled as open bus by the Z80 before they get here
        return 0xFF
    }

    /// ioResetButtonPressed
    public func resetButtonPressed(_ pressed: Bool) {
        if pressed {
            joy.joypadPort2 &= ~(1 << 4) // 0 = pressed, 1 = not pressed
        } else {
            joy.joypadPort2 |= (1 << 4)
        }
    }
}
