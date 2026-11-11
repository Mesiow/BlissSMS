// Swift port of BlissSMS/Core/Z80.c + Z80.h
//
// Naming follows the C code: `z80Foo(z80, ...)` becomes `foo(...)` on the Z80
// class, struct fields become camelCase (`process_interrupt_delay` ->
// `processInterruptDelay`), and registers keep their `.hi` / `.lo` / `.value` parts.
//
// Where C passes a register pointer, Swift passes the value and returns the result:
//     incReg8(z80, &z80->bc.hi);   ->   bc.hi = incReg8(bc.hi)
//     addReg8(z80, &z80->af.hi, z80->bc.lo);   ->   addReg8(bc.lo)   (dest is always A)
// The opcode tables were generated from Z80.c with Tools/translate_z80_tables.py.
//
// Behaviour matches the C core (it passes zexdoc), including its quirks:
//  - addReg16 always takes 15 cycles (the C `if (...);` typo adds +4 for HL too)
//  - setMemIy takes 20 cycles, setMemIx 23
// Differences:
//  - Unimplemented opcodes stop the CPU and set `fault` instead of assert(0).
//  - DD/FD + an opcode missing from the IX/IY tables runs the plain opcode +4 cycles
//    (what real hardware does) instead of asserting.
//  - The CP/M test stub lives in Cpm.swift instead of inside the CPU.

public let FLAG_C: UInt8 = 1 << 0
public let FLAG_N: UInt8 = 1 << 1
public let FLAG_PV: UInt8 = 1 << 2
public let FLAG_H: UInt8 = 1 << 4
public let FLAG_Z: UInt8 = 1 << 6
public let FLAG_S: UInt8 = 1 << 7

public let NMI_VECTOR: UInt16 = 0x66
public let INT_VECTOR: UInt16 = 0x38

/// What the Z80 needs from Bus.c (memoryBusReadU8 / memoryBusWriteU8).
public protocol MemoryBus: AnyObject {
    func memoryBusReadU8(_ address: UInt16) -> UInt8
    func memoryBusWriteU8(_ value: UInt8, _ address: UInt16)
}

/// What the Z80 needs from Io.c (ioReadU8 / ioWriteU8).
public protocol IoBus: AnyObject {
    func ioReadU8(_ address: UInt8) -> UInt8
    func ioWriteU8(_ value: UInt8, _ address: UInt8)
}

/// union Register: a 16-bit register with its 8-bit halves.
public struct Register: Equatable, Sendable {
    public var value: UInt16 = 0

    public init(_ value: UInt16 = 0) { self.value = value }

    @inline(__always) public var lo: UInt8 {
        get { UInt8(truncatingIfNeeded: value) }
        set { value = (value & 0xFF00) | UInt16(newValue) }
    }
    @inline(__always) public var hi: UInt8 {
        get { UInt8(truncatingIfNeeded: value >> 8) }
        set { value = (value & 0x00FF) | UInt16(newValue) << 8 }
    }
}

public struct ShadowedRegisters: Equatable, Sendable {
    public var af = Register()
    public var bc = Register()
    public var de = Register()
    public var hl = Register()
}

/// Processing modes for maskable interrupts
public enum IntMode: UInt8, Sendable {
    case zero, one, two
}

/// An opcode the core doesn't implement (the C code printed it and asserted).
public struct Z80Fault: Equatable, Sendable, CustomStringConvertible {
    public let table: String     // "Main", "Bit", "Ix", "Ix Bit", "Extended", "Iy", "Iy Bit"
    public let opcode: UInt8
    public let pc: UInt16
    public var description: String {
        "Unimplemented \(table) Instruction: 0x\(hex2(opcode)) at PC 0x\(hex4(pc))"
    }
}

public final class Z80 {
    public var shadowedRegs = ShadowedRegisters()
    public var af = Register()
    public var bc = Register()
    public var de = Register()
    public var hl = Register()
    public var ix = Register()
    public var iy = Register()
    public var ir = Register()
    public var sp: UInt16 = 0
    public var pc: UInt16 = 0
    public var memPtr: UInt16 = 0

    public var interruptMode: IntMode = .one
    public var cycles: UInt16 = 0
    public var iff1: UInt8 = 0
    public var iff2: UInt8 = 0
    public var processInterruptDelay: UInt8 = 0   // flag used for instruction delay after ei is executed
    public var halted: UInt8 = 0
    public var irqRequested: UInt8 = 0

    public var serviceNmi: UInt8 = 0
    public var lastDaaOperation: UInt8 = 0
    public var opcode: UInt8 = 0
    public var extOpcode: UInt8 = 0

    public private(set) unowned var bus: any MemoryBus
    public private(set) unowned var io: any IoBus

    /// Set when an unimplemented opcode stops the CPU.
    public internal(set) var fault: Z80Fault?

    /// The C core's `cpm_stub_enabled` check in ina(): with the stub on,
    /// IN A,(n) skips the SMS open-bus behaviour. Cpm.swift turns this on.
    public var cpmStubEnabled = false

    /// z80Init
    public init() {
        bus = NoBus.shared
        io = NoBus.shared

        pc = 0x0
        sp = 0xDFF0
        af.value = 0x0

        shadowedRegs = ShadowedRegisters()
        bc.value = 0x0
        de.value = 0x0
        hl.value = 0x0
        ix.value = 0x0
        iy.value = 0x0
        ir.value = 0x0
        memPtr = 0x0

        interruptMode = .one
        cycles = 0
        iff1 = 0
        iff2 = 0
        processInterruptDelay = 0
        halted = 0
        lastDaaOperation = 0
        serviceNmi = 0
    }

    /// z80ConnectBus
    public func connectBus(_ bus: any MemoryBus) {
        self.bus = bus
    }

    /// z80ConnectIo
    public func connectIo(_ io: any IoBus) {
        self.io = io
    }

    public func debugOutput() -> String {
        "PC: \(hex4(pc)), AF: \(hex4(af.value)), BC: \(hex4(bc.value)), DE: \(hex4(de.value)), "
            + "HL: \(hex4(hl.value)), SP: \(hex4(sp)), IX: \(hex4(ix.value)), IY: \(hex4(iy.value)), "
            + "I: \(hex2(ir.hi)), R: \(hex2(ir.lo)) Opcode: \(hex2(opcode)) \(hex2(extOpcode))"
    }

    // MARK: - Flags

    @inline(__always) func affectFlag(_ cond: Bool, _ flags: UInt8) {
        if cond {
            af.lo |= (flags & 0b1101_0111)
        } else {
            af.lo &= ~(flags & 0b1101_0111)
        }
    }

    @inline(__always) func setFlag(_ flags: UInt8) {
        af.lo |= (flags & 0b1101_0111)
    }

    @inline(__always) func clearFlag(_ flags: UInt8) {
        af.lo &= ~(flags & 0b1101_0111)
    }

    @inline(__always) func clearFlagCopyBits() {
        af.lo = clearBit(af.lo, 3)
        af.lo = clearBit(af.lo, 5)
    }

    @inline(__always) public func getFlag(_ flag: UInt8) -> Bool {
        (af.lo & flag) != 0
    }

    // MARK: - Interrupts

    /// z80HandleInterrupts. `vdpPendingInterrupts` is vdpPendingInterrupts(vdp) from the C code.
    public func handleInterrupts(_ vdpPendingInterrupts: Bool) {
        // nmi has priority over maskable irqs
        if serviceNmi != 0 {
            serviceNmi = 0
            halted = 0 // exit halted state

            iff2 = iff1
            iff1 = 0
            rst(NMI_VECTOR)
        }

        if vdpPendingInterrupts || irqRequested != 0 {
            // Maskable interrupts enabled
            if iff1 != 0 {
                if interruptMode == .one {
                    irqRequested = 0
                    // disable interrupts and jump to routine
                    iff1 = 0
                    iff2 = 0
                    halted = 0
                    rst(INT_VECTOR)
                }
            }
        }
    }

    /// z80RequestIrq
    public func requestIrq() {
        irqRequested = 1
    }

    // MARK: - Overflow / carry checks

    @inline(__always) func overflowFromAdd8(_ op1: UInt8, _ op2: UInt8, _ carry: UInt8) -> Bool {
        let total = op1 &+ op2 &+ carry
        // check if sign are same and if they are ~(op1 ^ op2) will evaluate to 1
        // then check results sign and then and them together and determine overflow by the msb
        return ((~(op1 ^ op2) & (op1 ^ total)) >> 7) != 0
    }

    @inline(__always) func overflowFromSub8(_ op1: UInt8, _ op2: UInt8, _ carry: UInt8) -> Bool {
        let total = op1 &- op2 &- carry
        // first check if both operands have a different sign,
        // then check if the result has the same sign as the second operand
        return Int8(bitPattern: op1 ^ op2) < 0 && Int8(bitPattern: op2 ^ total) >= 0
    }

    @inline(__always) func overflowFromAdd16(_ op1: UInt16, _ op2: UInt16, _ carry: UInt8) -> Bool {
        let total = op1 &+ op2 &+ UInt16(carry)
        return ((~(op1 ^ op2) & (op1 ^ total)) >> 15) != 0
    }

    @inline(__always) func overflowFromSub16(_ op1: UInt16, _ op2: UInt16, _ carry: UInt8) -> Bool {
        let total = op1 &- op2 &- UInt16(carry)
        return Int16(bitPattern: op1 ^ op2) < 0 && Int16(bitPattern: op2 ^ total) >= 0
    }

    @inline(__always) func isEvenParity(_ value: UInt8) -> Bool {
        let count = popcount(value)
        return (count % 2) == 0
    }

    @inline(__always) func isSigned16(_ value: UInt16) -> Bool { (value >> 15) & 0x1 == 0x1 }
    @inline(__always) func isSigned8(_ value: UInt8) -> Bool { (value >> 7) & 0x1 == 0x1 }

    // 8 bit carry/borrow check
    @inline(__always) func carryOccured8(_ op1: UInt8, _ op2: UInt8, _ carry: UInt8) -> Bool {
        Int(op1) + Int(op2) + Int(carry) > 0xFF
    }
    @inline(__always) func halfCarryOccured8(_ op1: UInt8, _ op2: UInt8, _ carry: UInt8) -> Bool {
        Int(op1 & 0xF) + Int(op2 & 0xF) + Int(carry) > 0xF
    }
    @inline(__always) func borrowOccured8(_ op1: UInt8, _ op2: UInt8, _ carry: UInt8) -> Bool {
        Int(op2) + Int(carry) > Int(op1)
    }
    @inline(__always) func halfBorrowOccured8(_ op1: UInt8, _ op2: UInt8, _ carry: UInt8) -> Bool {
        Int(op1 & 0xF) - Int(op2 & 0xF) - Int(carry) < 0
    }

    // 16 bit carry/borrow check
    @inline(__always) func carryOccured16(_ op1: UInt16, _ op2: UInt16, _ carry: UInt8) -> Bool {
        Int(op1) + Int(op2) + Int(carry) > 0xFFFF
    }
    @inline(__always) func halfCarryOccured16(_ op1: UInt16, _ op2: UInt16, _ carry: UInt8) -> Bool {
        Int(op1 & 0xFFF) + Int(op2 & 0xFFF) + Int(carry) > 0xFFF
    }
    @inline(__always) func borrowOccured16(_ op1: UInt16, _ op2: UInt16, _ carry: UInt8) -> Bool {
        Int(op2) + Int(carry) > Int(op1)
    }
    @inline(__always) func halfBorrowOccured16(_ op1: UInt16, _ op2: UInt16, _ carry: UInt8) -> Bool {
        Int(op1 & 0xFFF) - Int(op2 & 0xFFF) - Int(carry) < 0
    }

    // MARK: - Memory

    @inline(__always) func writeU8(_ value: UInt8, _ address: UInt16) {
        bus.memoryBusWriteU8(value, address)
    }

    @inline(__always) func readU8(_ address: UInt16) -> UInt8 {
        bus.memoryBusReadU8(address)
    }

    @inline(__always) func fetchU8() -> UInt8 {
        let data = bus.memoryBusReadU8(pc)
        pc &+= 1
        return data
    }

    @inline(__always) func readU16(_ address: UInt16) -> UInt16 {
        let lo = UInt16(bus.memoryBusReadU8(address))
        let hi = UInt16(bus.memoryBusReadU8(address &+ 1))
        return (hi << 8) | lo
    }

    @inline(__always) func fetchU16() -> UInt16 {
        let lo = UInt16(fetchU8())
        let hi = UInt16(fetchU8())
        return (hi << 8) | lo
    }

    /// Adds a signed 8-bit offset to an address, like `z80->ix.value + offset` in C.
    @inline(__always) func offsetAddress(_ base: UInt16, _ offset: UInt8) -> UInt16 {
        base &+ UInt16(bitPattern: Int16(Int8(bitPattern: offset)))
    }

    // MARK: - Execution

    /// z80Clock: runs one instruction (or 4 cycles of nops while halted).
    @discardableResult
    public func clock() -> UInt16 {
        if halted == 0 {
            if processInterruptDelay != 0 {
                processInterruptDelay = 0
            }
            let opcode = readU8(pc)
            pc &+= 1

            executeInstruction(opcode)
        } else { // execute nops
            cycles = 4
        }
        return cycles
    }

    func executeInstruction(_ opcode: UInt8) {
        self.opcode = opcode
        switch opcode {
        case 0xCB:
            let bitOpcode = readU8(pc)
            pc &+= 1
            executeBitInstruction(bitOpcode)

        case 0xDD:
            let ixOpcode = readU8(pc)
            pc &+= 1
            if ixOpcode == 0xCB {
                // the opcode is the last byte in the instruction
                // and the immediate byte to fetch is the second to last
                let ixbitOpcode = readU8(pc &+ 1)

                executeIxBitInstruction(ixbitOpcode)
                pc &+= 1
            } else {
                executeIxInstruction(ixOpcode)
            }

        case 0xED:
            let extOpcode = readU8(pc)
            pc &+= 1
            self.extOpcode = extOpcode
            executeExtendedInstruction(extOpcode)

        case 0xFD:
            let iyOpcode = readU8(pc)
            pc &+= 1
            if iyOpcode == 0xCB {
                let iybitOpcode = readU8(pc &+ 1)

                executeIyBitInstruction(iybitOpcode)
                pc &+= 1
            } else {
                executeIyInstruction(iyOpcode)
            }

        default:
            executeMainInstruction(opcode)
        }
        clearFlagCopyBits()
    }

    /// Replaces the C core's printf + assert(0) for missing opcodes.
    func unimplemented(_ table: String, _ opcode: UInt8) {
        fault = Z80Fault(table: table, opcode: opcode, pc: pc)
        halted = 1
    }

    func executeMainInstruction(_ opcode: UInt8) {
        switch opcode {
        case 0x00: cycles = 4

        // Load immediate u16 into register
        case 0x01: bc.value = loadReg16()
        case 0x11: de.value = loadReg16()
        case 0x21: hl.value = loadReg16()
        case 0x31: sp = loadReg16()
        case 0xF9: loadSpReg(hl.value)

        // Misc
        case 0x2F: cpl()
        case 0x76: halt()
        case 0xFE: cp(fetchU8()); cycles += 3
        case 0x27: daa()
        case 0x3F: ccf()
        case 0x37: scf()

        // Shifts
        case 0x07: rlca()
        case 0x0F: rrca()
        case 0x17: rla()
        case 0x1F: rra()

        // Exchanges
        case 0x08: ex(&af.value, &shadowedRegs.af.value)
        case 0xE3: hl.value = exSp(hl.value)
        case 0xEB: ex(&de.value, &hl.value)
        case 0xD9: exx()

        // Loads

        // Register pointer loads
        case 0x0A: af.hi = loadRegMem(bc.value)
        case 0x1A: af.hi = loadRegMem(de.value)
        case 0x02: loadReg8Mem(bc.value, af.hi)
        case 0x12: loadReg8Mem(de.value, af.hi)

        case 0x22: loadMemReg16(hl.value)
        case 0x32: loadMemReg8(af.hi)

        case 0x06: bc.hi = loadReg8()
        case 0x0E: bc.lo = loadReg8()
        case 0x16: de.hi = loadReg8()
        case 0x1E: de.lo = loadReg8()
        case 0x26: hl.hi = loadReg8()
        case 0x2E: hl.lo = loadReg8()
        case 0x3E: af.hi = loadReg8()
        case 0x36: loadHL8()

        case 0x2A: hl.value = load16Reg()
        case 0x3A: load16A()

        // Load src reg into dest reg
        case 0x40: bc.hi = loadReg(bc.hi)
        case 0x41: bc.hi = loadReg(bc.lo)
        case 0x42: bc.hi = loadReg(de.hi)
        case 0x43: bc.hi = loadReg(de.lo)
        case 0x44: bc.hi = loadReg(hl.hi)
        case 0x45: bc.hi = loadReg(hl.lo)
        case 0x47: bc.hi = loadReg(af.hi)
        case 0x48: bc.lo = loadReg(bc.hi)
        case 0x49: bc.lo = loadReg(bc.lo)
        case 0x4A: bc.lo = loadReg(de.hi)
        case 0x4B: bc.lo = loadReg(de.lo)
        case 0x4C: bc.lo = loadReg(hl.hi)
        case 0x4D: bc.lo = loadReg(hl.lo)
        case 0x4F: bc.lo = loadReg(af.hi)
        case 0x50: de.hi = loadReg(bc.hi)
        case 0x51: de.hi = loadReg(bc.lo)
        case 0x52: de.hi = loadReg(de.hi)
        case 0x53: de.hi = loadReg(de.lo)
        case 0x54: de.hi = loadReg(hl.hi)
        case 0x55: de.hi = loadReg(hl.lo)
        case 0x57: de.hi = loadReg(af.hi)
        case 0x58: de.lo = loadReg(bc.hi)
        case 0x59: de.lo = loadReg(bc.lo)
        case 0x5A: de.lo = loadReg(de.hi)
        case 0x5B: de.lo = loadReg(de.lo)
        case 0x5C: de.lo = loadReg(hl.hi)
        case 0x5D: de.lo = loadReg(hl.lo)
        case 0x5F: de.lo = loadReg(af.hi)
        case 0x60: hl.hi = loadReg(bc.hi)
        case 0x61: hl.hi = loadReg(bc.lo)
        case 0x62: hl.hi = loadReg(de.hi)
        case 0x63: hl.hi = loadReg(de.lo)
        case 0x64: hl.hi = loadReg(hl.hi)
        case 0x65: hl.hi = loadReg(hl.lo)
        case 0x67: hl.hi = loadReg(af.hi)
        case 0x68: hl.lo = loadReg(bc.hi)
        case 0x69: hl.lo = loadReg(bc.lo)
        case 0x6A: hl.lo = loadReg(de.hi)
        case 0x6B: hl.lo = loadReg(de.lo)
        case 0x6C: hl.lo = loadReg(hl.hi)
        case 0x6D: hl.lo = loadReg(hl.lo)
        case 0x6F: hl.lo = loadReg(af.hi)

        case 0x78: loadAReg(bc.hi)
        case 0x79: loadAReg(bc.lo)
        case 0x7A: loadAReg(de.hi)
        case 0x7B: loadAReg(de.lo)
        case 0x7C: loadAReg(hl.hi)
        case 0x7D: loadAReg(hl.lo)
        case 0x7F: loadAReg(af.hi)

        case 0x46: bc.hi = loadRegHl()
        case 0x4E: bc.lo = loadRegHl()
        case 0x56: de.hi = loadRegHl()
        case 0x5E: de.lo = loadRegHl()
        case 0x66: hl.hi = loadRegHl()
        case 0x6E: hl.lo = loadRegHl()
        case 0x7E: af.hi = loadRegHl()

        case 0x70: loadHlReg(bc.hi)
        case 0x71: loadHlReg(bc.lo)
        case 0x72: loadHlReg(de.hi)
        case 0x73: loadHlReg(de.lo)
        case 0x74: loadHlReg(hl.hi)
        case 0x75: loadHlReg(hl.lo)
        case 0x77: loadHlReg(af.hi)

        // Arithmetic
        case 0x19: hl.value = addReg16(hl.value, de.value)
        case 0x09: hl.value = addReg16(hl.value, bc.value)
        case 0x29: hl.value = addReg16(hl.value, hl.value)
        case 0x39: hl.value = addReg16(hl.value, sp)

        case 0x03: bc.value = incReg16(bc.value)
        case 0x13: de.value = incReg16(de.value)
        case 0x23: hl.value = incReg16(hl.value)
        case 0x33: sp = incReg16(sp)
        case 0x0B: bc.value = decReg16(bc.value)
        case 0x1B: de.value = decReg16(de.value)
        case 0x2B: hl.value = decReg16(hl.value)
        case 0x3B: sp = decReg16(sp)
        case 0x05: bc.hi = decReg8(bc.hi)
        case 0x15: de.hi = decReg8(de.hi)
        case 0x25: hl.hi = decReg8(hl.hi)
        case 0x0D: bc.lo = decReg8(bc.lo)
        case 0x1D: de.lo = decReg8(de.lo)
        case 0x2D: hl.lo = decReg8(hl.lo)
        case 0x3D: af.hi = decReg8(af.hi)
        case 0x35: decMemHl()

        case 0x04: bc.hi = incReg8(bc.hi)
        case 0x0C: bc.lo = incReg8(bc.lo)
        case 0x14: de.hi = incReg8(de.hi)
        case 0x1C: de.lo = incReg8(de.lo)
        case 0x24: hl.hi = incReg8(hl.hi)
        case 0x2C: hl.lo = incReg8(hl.lo)
        case 0x3C: af.hi = incReg8(af.hi)
        case 0x34: incMemHl()

        // 8 bit reg add
        case 0x80: addReg8(bc.hi)
        case 0x81: addReg8(bc.lo)
        case 0x82: addReg8(de.hi)
        case 0x83: addReg8(de.lo)
        case 0x84: addReg8(hl.hi)
        case 0x85: addReg8(hl.lo)
        case 0x86: addMemHl()
        case 0x87: addReg8(af.hi)
        case 0xC6: addReg8(fetchU8()); cycles += 3

        case 0x88: adcReg8(bc.hi)
        case 0x89: adcReg8(bc.lo)
        case 0x8A: adcReg8(de.hi)
        case 0x8B: adcReg8(de.lo)
        case 0x8C: adcReg8(hl.hi)
        case 0x8D: adcReg8(hl.lo)
        case 0x8E: adcMemHl()
        case 0x8F: adcReg8(af.hi)
        case 0xCE: adcReg8(fetchU8()); cycles += 3

        // 8 bit reg sub
        case 0x90: subReg8(bc.hi)
        case 0x91: subReg8(bc.lo)
        case 0x92: subReg8(de.hi)
        case 0x93: subReg8(de.lo)
        case 0x94: subReg8(hl.hi)
        case 0x95: subReg8(hl.lo)
        case 0x96: subMemHl()
        case 0x97: subReg8(af.hi)
        case 0xD6: subReg8(fetchU8()); cycles += 3

        case 0x98: sbcReg8(bc.hi)
        case 0x99: sbcReg8(bc.lo)
        case 0x9A: sbcReg8(de.hi)
        case 0x9B: sbcReg8(de.lo)
        case 0x9C: sbcReg8(hl.hi)
        case 0x9D: sbcReg8(hl.lo)
        case 0x9E: sbcMemHl()
        case 0x9F: sbcReg8(af.hi)
        case 0xDE: sbcReg8(fetchU8()); cycles += 3

        // Jumps/Branches/Rets
        case 0x10: djnz()
        case 0x18: jrImm()
        case 0x20: jrImmCond(!getFlag(FLAG_Z))
        case 0x28: jrImmCond(getFlag(FLAG_Z))
        case 0x30: jrImmCond(!getFlag(FLAG_C))
        case 0x38: jrImmCond(getFlag(FLAG_C))

        // Logical
        case 0xA0: and(bc.hi)
        case 0xA1: and(bc.lo)
        case 0xA2: and(de.hi)
        case 0xA3: and(de.lo)
        case 0xA4: and(hl.hi)
        case 0xA5: and(hl.lo)
        case 0xA6: andMemHl()
        case 0xA7: and(af.hi)
        case 0xE6: and(fetchU8()); cycles += 3

        case 0xA8: xor(bc.hi)
        case 0xA9: xor(bc.lo)
        case 0xAA: xor(de.hi)
        case 0xAB: xor(de.lo)
        case 0xAC: xor(hl.hi)
        case 0xAD: xor(hl.lo)
        case 0xAE: xorMemHl()
        case 0xAF: xor(af.hi)
        case 0xEE: xor(fetchU8()); cycles += 3

        case 0xB0: or(bc.hi)
        case 0xB1: or(bc.lo)
        case 0xB2: or(de.hi)
        case 0xB3: or(de.lo)
        case 0xB4: or(hl.hi)
        case 0xB5: or(hl.lo)
        case 0xB6: orMemHl()
        case 0xB7: or(af.hi)
        case 0xF6: or(fetchU8()); cycles += 3

        case 0xB8: cp(bc.hi)
        case 0xB9: cp(bc.lo)
        case 0xBA: cp(de.hi)
        case 0xBB: cp(de.lo)
        case 0xBC: cp(hl.hi)
        case 0xBD: cp(hl.lo)
        case 0xBE: cpMemHl()
        case 0xBF: cp(af.hi)

        case 0xCC: callCond(getFlag(FLAG_Z))
        case 0xCD: call()
        case 0xC4: callCond(!getFlag(FLAG_Z))
        case 0xD4: callCond(!getFlag(FLAG_C))
        case 0xDC: callCond(getFlag(FLAG_C))
        case 0xE4: callCond(!getFlag(FLAG_PV))
        case 0xEC: callCond(getFlag(FLAG_PV))
        case 0xF4: callCond(!getFlag(FLAG_S))
        case 0xFC: callCond(getFlag(FLAG_S))

        case 0xC0: retCond(!getFlag(FLAG_Z))
        case 0xC8: retCond(getFlag(FLAG_Z))
        case 0xC9: ret()
        case 0xD0: retCond(!getFlag(FLAG_C))
        case 0xD8: retCond(getFlag(FLAG_C))
        case 0xE0: retCond(!getFlag(FLAG_PV))
        case 0xE8: retCond(getFlag(FLAG_PV))
        case 0xF0: retCond(!getFlag(FLAG_S))
        case 0xF8: retCond(getFlag(FLAG_S))

        // Jumps
        case 0xC2: jpCond(!getFlag(FLAG_Z))
        case 0xC3: jp()
        case 0xCA: jpCond(getFlag(FLAG_Z))
        case 0xD2: jpCond(!getFlag(FLAG_C))
        case 0xDA: jpCond(getFlag(FLAG_C))
        case 0xE2: jpCond(!getFlag(FLAG_PV))
        case 0xE9: jpMemHl()
        case 0xEA: jpCond(getFlag(FLAG_PV))
        case 0xF2: jpCond(!getFlag(FLAG_S))
        case 0xFA: jpCond(getFlag(FLAG_S))

        // Restarts
        case 0xC7: rst(0x00)
        case 0xCF: rst(0x08)
        case 0xD7: rst(0x10)
        case 0xDF: rst(0x18)
        case 0xE7: rst(0x20)
        case 0xEF: rst(0x28)
        case 0xF7: rst(0x30)
        case 0xFF: rst(0x38)

        case 0xD3: outa()
        case 0xDB: ina()

        case 0xC5: push(bc.value)
        case 0xD5: push(de.value)
        case 0xE5: push(hl.value)
        case 0xF5: push(af.value)

        case 0xC1: bc.value = pop()
        case 0xD1: de.value = pop()
        case 0xE1: hl.value = pop()
        case 0xF1: af.value = pop()

        case 0xF3: di()
        case 0xFB: ei()

        default:
            unimplemented("Main", opcode)
        }
    }

    func executeBitInstruction(_ opcode: UInt8) {
        switch opcode {
        case 0x00: bc.hi = rlc(bc.hi)
        case 0x01: bc.lo = rlc(bc.lo)
        case 0x02: de.hi = rlc(de.hi)
        case 0x03: de.lo = rlc(de.lo)
        case 0x04: hl.hi = rlc(hl.hi)
        case 0x05: hl.lo = rlc(hl.lo)
        case 0x06: rlcMemHl()
        case 0x07: af.hi = rlc(af.hi)

        case 0x08: bc.hi = rrc(bc.hi)
        case 0x09: bc.lo = rrc(bc.lo)
        case 0x0A: de.hi = rrc(de.hi)
        case 0x0B: de.lo = rrc(de.lo)
        case 0x0C: hl.hi = rrc(hl.hi)
        case 0x0D: hl.lo = rrc(hl.lo)
        case 0x0E: rrcMemHl()
        case 0x0F: af.hi = rrc(af.hi)

        case 0x10: bc.hi = rl(bc.hi)
        case 0x11: bc.lo = rl(bc.lo)
        case 0x12: de.hi = rl(de.hi)
        case 0x13: de.lo = rl(de.lo)
        case 0x14: hl.hi = rl(hl.hi)
        case 0x15: hl.lo = rl(hl.lo)
        case 0x16: rlMemHl()
        case 0x17: af.hi = rl(af.hi)

        case 0x18: bc.hi = rr(bc.hi)
        case 0x19: bc.lo = rr(bc.lo)
        case 0x1A: de.hi = rr(de.hi)
        case 0x1B: de.lo = rr(de.lo)
        case 0x1C: hl.hi = rr(hl.hi)
        case 0x1D: hl.lo = rr(hl.lo)
        case 0x1E: rrMemHl()
        case 0x1F: af.hi = rr(af.hi)

        case 0x20: bc.hi = sla(bc.hi)
        case 0x21: bc.lo = sla(bc.lo)
        case 0x22: de.hi = sla(de.hi)
        case 0x23: de.lo = sla(de.lo)
        case 0x24: hl.hi = sla(hl.hi)
        case 0x25: hl.lo = sla(hl.lo)
        case 0x26: slaMemHl()
        case 0x27: af.hi = sla(af.hi)

        case 0x28: bc.hi = sra(bc.hi)
        case 0x29: bc.lo = sra(bc.lo)
        case 0x2A: de.hi = sra(de.hi)
        case 0x2B: de.lo = sra(de.lo)
        case 0x2C: hl.hi = sra(hl.hi)
        case 0x2D: hl.lo = sra(hl.lo)
        case 0x2E: sraMemHl()
        case 0x2F: af.hi = sra(af.hi)

        case 0x30: bc.hi = sll(bc.hi)
        case 0x31: bc.lo = sll(bc.lo)
        case 0x32: de.hi = sll(de.hi)
        case 0x33: de.lo = sll(de.lo)
        case 0x34: hl.hi = sll(hl.hi)
        case 0x35: hl.lo = sll(hl.lo)
        case 0x36: sllMemHl()
        case 0x37: af.hi = sll(af.hi)

        // srl
        case 0x38: bc.hi = srl(bc.hi)
        case 0x39: bc.lo = srl(bc.lo)
        case 0x3A: de.hi = srl(de.hi)
        case 0x3B: de.lo = srl(de.lo)
        case 0x3C: hl.hi = srl(hl.hi)
        case 0x3D: hl.lo = srl(hl.lo)
        case 0x3E: srlMemHl()
        case 0x3F: af.hi = srl(af.hi)

        // Bit
        case 0x40: bit(bc.hi, 0)
        case 0x41: bit(bc.lo, 0)
        case 0x42: bit(de.hi, 0)
        case 0x43: bit(de.lo, 0)
        case 0x44: bit(hl.hi, 0)
        case 0x45: bit(hl.lo, 0)
        case 0x46: bitMemHl(0)
        case 0x47: bit(af.hi, 0)

        case 0x48: bit(bc.hi, 1)
        case 0x49: bit(bc.lo, 1)
        case 0x4A: bit(de.hi, 1)
        case 0x4B: bit(de.lo, 1)
        case 0x4C: bit(hl.hi, 1)
        case 0x4D: bit(hl.lo, 1)
        case 0x4E: bitMemHl(1)
        case 0x4F: bit(af.hi, 1)

        case 0x50: bit(bc.hi, 2)
        case 0x51: bit(bc.lo, 2)
        case 0x52: bit(de.hi, 2)
        case 0x53: bit(de.lo, 2)
        case 0x54: bit(hl.hi, 2)
        case 0x55: bit(hl.lo, 2)
        case 0x56: bitMemHl(2)
        case 0x57: bit(af.hi, 2)

        case 0x58: bit(bc.hi, 3)
        case 0x59: bit(bc.lo, 3)
        case 0x5A: bit(de.hi, 3)
        case 0x5B: bit(de.lo, 3)
        case 0x5C: bit(hl.hi, 3)
        case 0x5D: bit(hl.lo, 3)
        case 0x5E: bitMemHl(3)
        case 0x5F: bit(af.hi, 3)

        case 0x60: bit(bc.hi, 4)
        case 0x61: bit(bc.lo, 4)
        case 0x62: bit(de.hi, 4)
        case 0x63: bit(de.lo, 4)
        case 0x64: bit(hl.hi, 4)
        case 0x65: bit(hl.lo, 4)
        case 0x66: bitMemHl(4)
        case 0x67: bit(af.hi, 4)

        case 0x68: bit(bc.hi, 5)
        case 0x69: bit(bc.lo, 5)
        case 0x6A: bit(de.hi, 5)
        case 0x6B: bit(de.lo, 5)
        case 0x6C: bit(hl.hi, 5)
        case 0x6D: bit(hl.lo, 5)
        case 0x6E: bitMemHl(5)
        case 0x6F: bit(af.hi, 5)

        case 0x70: bit(bc.hi, 6)
        case 0x71: bit(bc.lo, 6)
        case 0x72: bit(de.hi, 6)
        case 0x73: bit(de.lo, 6)
        case 0x74: bit(hl.hi, 6)
        case 0x75: bit(hl.lo, 6)
        case 0x76: bitMemHl(6)
        case 0x77: bit(af.hi, 6)

        case 0x78: bit(bc.hi, 7)
        case 0x79: bit(bc.lo, 7)
        case 0x7A: bit(de.hi, 7)
        case 0x7B: bit(de.lo, 7)
        case 0x7C: bit(hl.hi, 7)
        case 0x7D: bit(hl.lo, 7)
        case 0x7E: bitMemHl(7)
        case 0x7F: bit(af.hi, 7)

        case 0x80: bc.hi = res(bc.hi, 0)
        case 0x81: bc.lo = res(bc.lo, 0)
        case 0x82: de.hi = res(de.hi, 0)
        case 0x83: de.lo = res(de.lo, 0)
        case 0x84: hl.hi = res(hl.hi, 0)
        case 0x85: hl.lo = res(hl.lo, 0)
        case 0x86: resMemHl(0)
        case 0x87: af.hi = res(af.hi, 0)

        case 0x88: bc.hi = res(bc.hi, 1)
        case 0x89: bc.lo = res(bc.lo, 1)
        case 0x8A: de.hi = res(de.hi, 1)
        case 0x8B: de.lo = res(de.lo, 1)
        case 0x8C: hl.hi = res(hl.hi, 1)
        case 0x8D: hl.lo = res(hl.lo, 1)
        case 0x8E: resMemHl(1)
        case 0x8F: af.hi = res(af.hi, 1)

        case 0x90: bc.hi = res(bc.hi, 2)
        case 0x91: bc.lo = res(bc.lo, 2)
        case 0x92: de.hi = res(de.hi, 2)
        case 0x93: de.lo = res(de.lo, 2)
        case 0x94: hl.hi = res(hl.hi, 2)
        case 0x95: hl.lo = res(hl.lo, 2)
        case 0x96: resMemHl(2)
        case 0x97: af.hi = res(af.hi, 2)

        case 0x98: bc.hi = res(bc.hi, 3)
        case 0x99: bc.lo = res(bc.lo, 3)
        case 0x9A: de.hi = res(de.hi, 3)
        case 0x9B: de.lo = res(de.lo, 3)
        case 0x9C: hl.hi = res(hl.hi, 3)
        case 0x9D: hl.lo = res(hl.lo, 3)
        case 0x9E: resMemHl(3)
        case 0x9F: af.hi = res(af.hi, 3)

        case 0xA0: bc.hi = res(bc.hi, 4)
        case 0xA1: bc.lo = res(bc.lo, 4)
        case 0xA2: de.hi = res(de.hi, 4)
        case 0xA3: de.lo = res(de.lo, 4)
        case 0xA4: hl.hi = res(hl.hi, 4)
        case 0xA5: hl.lo = res(hl.lo, 4)
        case 0xA6: resMemHl(4)
        case 0xA7: af.hi = res(af.hi, 4)

        case 0xA8: bc.hi = res(bc.hi, 5)
        case 0xA9: bc.lo = res(bc.lo, 5)
        case 0xAA: de.hi = res(de.hi, 5)
        case 0xAB: de.lo = res(de.lo, 5)
        case 0xAC: hl.hi = res(hl.hi, 5)
        case 0xAD: hl.lo = res(hl.lo, 5)
        case 0xAE: resMemHl(5)
        case 0xAF: af.hi = res(af.hi, 5)

        case 0xB0: bc.hi = res(bc.hi, 6)
        case 0xB1: bc.lo = res(bc.lo, 6)
        case 0xB2: de.hi = res(de.hi, 6)
        case 0xB3: de.lo = res(de.lo, 6)
        case 0xB4: hl.hi = res(hl.hi, 6)
        case 0xB5: hl.lo = res(hl.lo, 6)
        case 0xB6: resMemHl(6)
        case 0xB7: af.hi = res(af.hi, 6)

        case 0xB8: bc.hi = res(bc.hi, 7)
        case 0xB9: bc.lo = res(bc.lo, 7)
        case 0xBA: de.hi = res(de.hi, 7)
        case 0xBB: de.lo = res(de.lo, 7)
        case 0xBC: hl.hi = res(hl.hi, 7)
        case 0xBD: hl.lo = res(hl.lo, 7)
        case 0xBE: resMemHl(7)
        case 0xBF: af.hi = res(af.hi, 7)

        case 0xC0: bc.hi = set(bc.hi, 0)
        case 0xC1: bc.lo = set(bc.lo, 0)
        case 0xC2: de.hi = set(de.hi, 0)
        case 0xC3: de.lo = set(de.lo, 0)
        case 0xC4: hl.hi = set(hl.hi, 0)
        case 0xC5: hl.lo = set(hl.lo, 0)
        case 0xC6: setMemHl(0)
        case 0xC7: af.hi = set(af.hi, 0)

        case 0xC8: bc.hi = set(bc.hi, 1)
        case 0xC9: bc.lo = set(bc.lo, 1)
        case 0xCA: de.hi = set(de.hi, 1)
        case 0xCB: de.lo = set(de.lo, 1)
        case 0xCC: hl.hi = set(hl.hi, 1)
        case 0xCD: hl.lo = set(hl.lo, 1)
        case 0xCE: setMemHl(1)
        case 0xCF: af.hi = set(af.hi, 1)

        case 0xD0: bc.hi = set(bc.hi, 2)
        case 0xD1: bc.lo = set(bc.lo, 2)
        case 0xD2: de.hi = set(de.hi, 2)
        case 0xD3: de.lo = set(de.lo, 2)
        case 0xD4: hl.hi = set(hl.hi, 2)
        case 0xD5: hl.lo = set(hl.lo, 2)
        case 0xD6: setMemHl(2)
        case 0xD7: af.hi = set(af.hi, 2)

        case 0xD8: bc.hi = set(bc.hi, 3)
        case 0xD9: bc.lo = set(bc.lo, 3)
        case 0xDA: de.hi = set(de.hi, 3)
        case 0xDB: de.lo = set(de.lo, 3)
        case 0xDC: hl.hi = set(hl.hi, 3)
        case 0xDD: hl.lo = set(hl.lo, 3)
        case 0xDE: setMemHl(3)
        case 0xDF: af.hi = set(af.hi, 3)

        case 0xE0: bc.hi = set(bc.hi, 4)
        case 0xE1: bc.lo = set(bc.lo, 4)
        case 0xE2: de.hi = set(de.hi, 4)
        case 0xE3: de.lo = set(de.lo, 4)
        case 0xE4: hl.hi = set(hl.hi, 4)
        case 0xE5: hl.lo = set(hl.lo, 4)
        case 0xE6: setMemHl(4)
        case 0xE7: af.hi = set(af.hi, 4)

        case 0xE8: bc.hi = set(bc.hi, 5)
        case 0xE9: bc.lo = set(bc.lo, 5)
        case 0xEA: de.hi = set(de.hi, 5)
        case 0xEB: de.lo = set(de.lo, 5)
        case 0xEC: hl.hi = set(hl.hi, 5)
        case 0xED: hl.lo = set(hl.lo, 5)
        case 0xEE: setMemHl(5)
        case 0xEF: af.hi = set(af.hi, 5)

        case 0xF0: bc.hi = set(bc.hi, 6)
        case 0xF1: bc.lo = set(bc.lo, 6)
        case 0xF2: de.hi = set(de.hi, 6)
        case 0xF3: de.lo = set(de.lo, 6)
        case 0xF4: hl.hi = set(hl.hi, 6)
        case 0xF5: hl.lo = set(hl.lo, 6)
        case 0xF6: setMemHl(6)
        case 0xF7: af.hi = set(af.hi, 6)

        case 0xF8: bc.hi = set(bc.hi, 7)
        case 0xF9: bc.lo = set(bc.lo, 7)
        case 0xFA: de.hi = set(de.hi, 7)
        case 0xFB: de.lo = set(de.lo, 7)
        case 0xFC: hl.hi = set(hl.hi, 7)
        case 0xFD: hl.lo = set(hl.lo, 7)
        case 0xFE: setMemHl(7)
        case 0xFF: af.hi = set(af.hi, 7)

        default:
            unimplemented("Bit", opcode)
        }
    }

    func executeIxInstruction(_ opcode: UInt8) {
        switch opcode {
        // Loads
        case 0x22: loadMemReg16(ix.value); cycles += 4
        case 0x2A: ix.value = load16Reg(); cycles += 4

        case 0x26: ix.hi = loadReg8(); cycles += 4
        case 0x2E: ix.lo = loadReg8(); cycles += 4

        case 0x40, 0x41, 0x42, 0x43, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5F, 0x78, 0x79, 0x7A, 0x7B, 0x7F: executeMainInstruction(opcode); cycles += 4

        case 0x44: bc.hi = loadReg(ix.hi); cycles += 4
        case 0x45: bc.hi = loadReg(ix.lo); cycles += 4
        case 0x4C: bc.lo = loadReg(ix.hi); cycles += 4
        case 0x4D: bc.lo = loadReg(ix.lo); cycles += 4
        case 0x54: de.hi = loadReg(ix.hi); cycles += 4
        case 0x55: de.hi = loadReg(ix.lo); cycles += 4
        case 0x5C: de.lo = loadReg(ix.hi); cycles += 4
        case 0x5D: de.lo = loadReg(ix.lo); cycles += 4

        case 0x60: ix.hi = loadReg(bc.hi); cycles += 4
        case 0x61: ix.hi = loadReg(bc.lo); cycles += 4
        case 0x62: ix.hi = loadReg(de.hi); cycles += 4
        case 0x63: ix.hi = loadReg(de.lo); cycles += 4
        case 0x64: ix.hi = loadReg(ix.hi); cycles += 4
        case 0x65: ix.hi = loadReg(ix.lo); cycles += 4
        case 0x67: ix.hi = loadReg(af.hi); cycles += 4
        case 0x68: ix.lo = loadReg(bc.hi); cycles += 4
        case 0x69: ix.lo = loadReg(bc.lo); cycles += 4
        case 0x6A: ix.lo = loadReg(de.hi); cycles += 4
        case 0x6B: ix.lo = loadReg(de.lo); cycles += 4
        case 0x6C: ix.lo = loadReg(ix.hi); cycles += 4
        case 0x6D: ix.lo = loadReg(ix.lo); cycles += 4
        case 0x6F: ix.lo = loadReg(af.hi); cycles += 4
        case 0x7C: af.hi = loadReg(ix.hi); cycles += 4
        case 0x7D: af.hi = loadReg(ix.lo); cycles += 4

        // Load value from ix + offset into reg8
        case 0x46: bc.hi = loadRegIx()
        case 0x4E: bc.lo = loadRegIx()
        case 0x56: de.hi = loadRegIx()
        case 0x5E: de.lo = loadRegIx()
        case 0x66: hl.hi = loadRegIx()
        case 0x6E: hl.lo = loadRegIx()
        case 0x7E: af.hi = loadRegIx()

        // Load reg8 into memory location ix + immediate s8
        case 0x36: loadIxImm()
        case 0x70: loadIxReg(bc.hi)
        case 0x71: loadIxReg(bc.lo)
        case 0x72: loadIxReg(de.hi)
        case 0x73: loadIxReg(de.lo)
        case 0x74: loadIxReg(hl.hi)
        case 0x75: loadIxReg(hl.lo)
        case 0x77: loadIxReg(af.hi)

        case 0xF9: loadSpReg(ix.value)

        // Arithmetic
        case 0x09: ix.value = addReg16(ix.value, bc.value)
        case 0x19: ix.value = addReg16(ix.value, de.value)
        case 0x29: ix.value = addReg16(ix.value, ix.value)
        case 0x39: ix.value = addReg16(ix.value, sp)

        case 0x23: ix.value = incReg16(ix.value); cycles += 4
        case 0x24: ix.hi = incReg8(ix.hi); cycles += 4
        case 0x2C: ix.lo = incReg8(ix.lo); cycles += 4

        case 0x25: ix.hi = decReg8(ix.hi); cycles += 4
        case 0x2D: ix.lo = decReg8(ix.lo); cycles += 4

        case 0x2B: ix.value = decReg16(ix.value); cycles += 4

        case 0x34: incMemIx()
        case 0x35: decMemIx()

        case 0x84: addReg8(ix.hi)
        case 0x85: addReg8(ix.lo)
        case 0x86: addMemIx()

        case 0x8C: adcReg8(ix.hi)
        case 0x8D: adcReg8(ix.lo)
        case 0x8E: adcMemIx()

        case 0x94: subReg8(ix.hi)
        case 0x95: subReg8(ix.lo)
        case 0x96: subMemIx()

        case 0x9C: sbcReg8(ix.hi)
        case 0x9D: sbcReg8(ix.lo)
        case 0x9E: sbcMemIx()

        // Logical
        case 0xA4: and(ix.hi)
        case 0xA5: and(ix.lo)
        case 0xA6: andMemIx()

        case 0xAC: xor(ix.hi)
        case 0xAD: xor(ix.lo)
        case 0xAE: xorMemIx()

        case 0xB4: or(ix.hi)
        case 0xB5: or(ix.lo)
        case 0xB6: orMemIx()

        case 0xBC: cp(ix.hi)
        case 0xBD: cp(ix.lo)
        case 0xBE: cpMemIx()

        case 0x21: ix.value = loadReg16(); cycles += 4

        case 0xE9: jpMemIx()

        case 0xE3: ix.value = exSp(ix.value)

        case 0xE1: ix.value = pop(); cycles += 4
        case 0xE5: push(ix.value); cycles += 4

        default:
            // Not in the C table: run the plain opcode, the prefix just costs 4 cycles
            executeInstruction(opcode)
            cycles += 4
        }
    }

    func executeIxBitInstruction(_ opcode: UInt8) {
        switch opcode {
        case 0x06: rlcMemIx()
        case 0x0E: rrcMemIx()
        case 0x16: rlMemIx()
        case 0x1E: rrMemIx()
        case 0x26: slaMemIx()
        case 0x2E: sraMemIx()
        case 0x3E: srlMemIx()
        case 0x36: sllMemIx()

        case 0x46: bitMemIx(0)
        case 0x4E: bitMemIx(1)
        case 0x56: bitMemIx(2)
        case 0x5E: bitMemIx(3)
        case 0x66: bitMemIx(4)
        case 0x6E: bitMemIx(5)
        case 0x76: bitMemIx(6)
        case 0x7E: bitMemIx(7)

        case 0x86: resMemIx(0)
        case 0x8E: resMemIx(1)
        case 0x96: resMemIx(2)
        case 0x9E: resMemIx(3)
        case 0xA6: resMemIx(4)
        case 0xAE: resMemIx(5)
        case 0xB6: resMemIx(6)
        case 0xBE: resMemIx(7)

        case 0xC6: setMemIx(0)
        case 0xCE: setMemIx(1)
        case 0xD6: setMemIx(2)
        case 0xDE: setMemIx(3)
        case 0xE6: setMemIx(4)
        case 0xEE: setMemIx(5)
        case 0xF6: setMemIx(6)
        case 0xFE: setMemIx(7)

        default:
            unimplemented("Ix Bit", opcode)
        }
    }

    func executeExtendedInstruction(_ opcode: UInt8) {
        switch opcode {
        // Loads
        case 0x43: loadMemReg16(bc.value)
        case 0x53: loadMemReg16(de.value)
        case 0x73: loadMemReg16(sp)
        case 0x57: loadAWithI()
        case 0x5F: loadAWithR()
        case 0x7B: sp = load16Reg(); cycles += 4
        case 0x4B: bc.value = load16Reg(); cycles += 4
        case 0x5B: de.value = load16Reg(); cycles += 4

        case 0x40: bc.hi = `in`(bc.lo, opcode)
        case 0x48: bc.lo = `in`(bc.lo, opcode)
        case 0x50: de.hi = `in`(bc.lo, opcode)
        case 0x58: de.lo = `in`(bc.lo, opcode)
        case 0x60: hl.hi = `in`(bc.lo, opcode)
        case 0x68: hl.lo = `in`(bc.lo, opcode)
        case 0x70: _ = `in`(bc.lo, opcode)
        case 0x78: af.hi = `in`(bc.lo, opcode)

        case 0x41: out(bc.lo, bc.hi)
        case 0x49: out(bc.lo, bc.lo)
        case 0x51: out(bc.lo, de.hi)
        case 0x59: out(bc.lo, de.lo)
        case 0x61: out(bc.lo, hl.hi)
        case 0x69: out(bc.lo, hl.lo)
        case 0x71: out(bc.lo, 0)
        case 0x79: out(bc.lo, af.hi)

        case 0x56: im(.one)
        case 0x76: im(.one)

        case 0xA1: cpi()
        case 0xB1: cpir()
        case 0xA9: cpd()
        case 0xB9: cpdr()
        case 0xA8: ldd()
        case 0xB8: lddr()
        case 0x67: rrd()
        case 0x6F: rld()

        // Arithmetic
        case 0x42: hl.value = sbcReg16(hl.value, bc.value)
        case 0x52: hl.value = sbcReg16(hl.value, de.value)
        case 0x62: hl.value = sbcReg16(hl.value, hl.value)
        case 0x72: hl.value = sbcReg16(hl.value, sp)

        case 0x4A: hl.value = adcReg16(hl.value, bc.value)
        case 0x5A: hl.value = adcReg16(hl.value, de.value)
        case 0x6A: hl.value = adcReg16(hl.value, hl.value)
        case 0x7A: hl.value = adcReg16(hl.value, sp)

        // Misc
        case 0x44: neg()

        // Returns
        case 0x45: retn()
        case 0x4D: reti()
        case 0x55: retn()
        case 0x5D: retn()
        case 0x65: retn()
        case 0x6D: retn()
        case 0x75: retn()
        case 0x7D: retn()

        // I/O instructions
        case 0xA0: ldi()
        case 0xA2: ini()
        case 0xA3: outi()
        case 0xAB: outd()
        case 0xB0: ldir()
        case 0xB3: otir()
        case 0xBB: otdr()

        default:
            unimplemented("Extended", opcode)
        }
    }

    func executeIyInstruction(_ opcode: UInt8) {
        switch opcode {
        // Loads
        case 0x2A: iy.value = load16Reg(); cycles += 4
        case 0x22: loadMemReg16(iy.value); cycles += 4

        case 0x26: iy.hi = loadReg8(); cycles += 4
        case 0x2E: iy.lo = loadReg8(); cycles += 4

        case 0x40, 0x41, 0x42, 0x43, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5F, 0x78, 0x79, 0x7A, 0x7B, 0x7F: executeMainInstruction(opcode); cycles += 4

        case 0x44: bc.hi = loadReg(iy.hi); cycles += 4
        case 0x45: bc.hi = loadReg(iy.lo); cycles += 4
        case 0x4C: bc.lo = loadReg(iy.hi); cycles += 4
        case 0x4D: bc.lo = loadReg(iy.lo); cycles += 4
        case 0x54: de.hi = loadReg(iy.hi); cycles += 4
        case 0x55: de.hi = loadReg(iy.lo); cycles += 4
        case 0x5C: de.lo = loadReg(iy.hi); cycles += 4
        case 0x5D: de.lo = loadReg(iy.lo); cycles += 4

        case 0x60: iy.hi = loadReg(bc.hi); cycles += 4
        case 0x61: iy.hi = loadReg(bc.lo); cycles += 4
        case 0x62: iy.hi = loadReg(de.hi); cycles += 4
        case 0x63: iy.hi = loadReg(de.lo); cycles += 4
        case 0x64: iy.hi = loadReg(iy.hi); cycles += 4
        case 0x65: iy.hi = loadReg(iy.lo); cycles += 4
        case 0x67: iy.hi = loadReg(af.hi); cycles += 4
        case 0x68: iy.lo = loadReg(bc.hi); cycles += 4
        case 0x69: iy.lo = loadReg(bc.lo); cycles += 4
        case 0x6A: iy.lo = loadReg(de.hi); cycles += 4
        case 0x6B: iy.lo = loadReg(de.lo); cycles += 4
        case 0x6C: iy.lo = loadReg(iy.hi); cycles += 4
        case 0x6D: iy.lo = loadReg(iy.lo); cycles += 4
        case 0x6F: iy.lo = loadReg(af.hi); cycles += 4
        case 0x7C: af.hi = loadReg(iy.hi); cycles += 4
        case 0x7D: af.hi = loadReg(iy.lo); cycles += 4

        // Load value from ix + offset into reg8
        case 0x46: bc.hi = loadRegIy()
        case 0x4E: bc.lo = loadRegIy()
        case 0x56: de.hi = loadRegIy()
        case 0x5E: de.lo = loadRegIy()
        case 0x66: hl.hi = loadRegIy()
        case 0x6E: hl.lo = loadRegIy()
        case 0x7E: af.hi = loadRegIy()

        // Load reg8 into memory location ix + immediate s8
        case 0x36: loadIyImm()
        case 0x70: loadIyReg(bc.hi)
        case 0x71: loadIyReg(bc.lo)
        case 0x72: loadIyReg(de.hi)
        case 0x73: loadIyReg(de.lo)
        case 0x74: loadIyReg(hl.hi)
        case 0x75: loadIyReg(hl.lo)
        case 0x77: loadIyReg(af.hi)

        case 0xF9: loadSpReg(iy.value)

        // Arithmetic
        case 0x09: iy.value = addReg16(iy.value, bc.value)
        case 0x19: iy.value = addReg16(iy.value, de.value)
        case 0x29: iy.value = addReg16(iy.value, iy.value)
        case 0x39: iy.value = addReg16(iy.value, sp)

        case 0x21: iy.value = loadReg16(); cycles += 4
        case 0x23: iy.value = incReg16(iy.value); cycles += 4
        case 0x2B: iy.value = decReg16(iy.value); cycles += 4

        case 0x24: iy.hi = incReg8(iy.hi); cycles += 4
        case 0x2C: iy.lo = incReg8(iy.lo); cycles += 4

        case 0x25: iy.hi = decReg8(iy.hi); cycles += 4
        case 0x2D: iy.lo = decReg8(iy.lo); cycles += 4

        case 0x34: incMemIy()
        case 0x35: decMemIy()

        case 0x84: addReg8(iy.hi)
        case 0x85: addReg8(iy.lo)
        case 0x86: addMemIy()

        case 0x8C: adcReg8(iy.hi)
        case 0x8D: adcReg8(iy.lo)
        case 0x8E: adcMemIy()

        case 0x94: subReg8(iy.hi)
        case 0x95: subReg8(iy.lo)
        case 0x96: subMemIy()

        case 0x9C: sbcReg8(iy.hi)
        case 0x9D: sbcReg8(iy.lo)
        case 0x9E: sbcMemIy()

        // Logical
        case 0xA4: and(iy.hi)
        case 0xA5: and(iy.lo)
        case 0xA6: andMemIy()

        case 0xAC: xor(iy.hi)
        case 0xAD: xor(iy.lo)
        case 0xAE: xorMemIy()

        case 0xB4: or(iy.hi)
        case 0xB5: or(iy.lo)
        case 0xB6: orMemIy()

        case 0xBC: cp(iy.hi)
        case 0xBD: cp(iy.lo)
        case 0xBE: cpMemIy()

        case 0xE9: jpMemIy()

        case 0xE3: iy.value = exSp(iy.value)

        case 0xE1: iy.value = pop(); cycles += 4
        case 0xE5: push(iy.value); cycles += 4

        default:
            // Not in the C table: run the plain opcode, the prefix just costs 4 cycles
            executeInstruction(opcode)
            cycles += 4
        }
    }

    func executeIyBitInstruction(_ opcode: UInt8) {
        switch opcode {
        case 0x06: rlcMemIy()
        case 0x0E: rrcMemIy()
        case 0x16: rlMemIy()
        case 0x1E: rrMemIy()
        case 0x26: slaMemIy()
        case 0x2E: sraMemIy()
        case 0x3E: srlMemIy()
        case 0x36: sllMemIy()

        case 0x46: bitMemIy(0)
        case 0x4E: bitMemIy(1)
        case 0x56: bitMemIy(2)
        case 0x5E: bitMemIy(3)
        case 0x66: bitMemIy(4)
        case 0x6E: bitMemIy(5)
        case 0x76: bitMemIy(6)
        case 0x7E: bitMemIy(7)

        case 0x86: resMemIy(0)
        case 0x8E: resMemIy(1)
        case 0x96: resMemIy(2)
        case 0x9E: resMemIy(3)
        case 0xA6: resMemIy(4)
        case 0xAE: resMemIy(5)
        case 0xB6: resMemIy(6)
        case 0xBE: resMemIy(7)

        case 0xC6: setMemIy(0)
        case 0xCE: setMemIy(1)
        case 0xD6: setMemIy(2)
        case 0xDE: setMemIy(3)
        case 0xE6: setMemIy(4)
        case 0xEE: setMemIy(5)
        case 0xF6: setMemIy(6)
        case 0xFE: setMemIy(7)

        default:
            unimplemented("Iy Bit", opcode)
        }
    }

    // MARK: - Loads

    func loadReg16() -> UInt16 {
        let value = fetchU16()
        cycles = 10
        return value
    }

    func loadReg8() -> UInt8 {
        let value = fetchU8()
        cycles = 7
        return value
    }

    func loadHL8() {
        let value = fetchU8()
        writeU8(value, hl.value)
        cycles = 10
    }

    func loadAReg(_ reg: UInt8) {
        af.hi = reg
        cycles = 4
    }

    func loadRegHl() -> UInt8 {
        let value = readU8(hl.value)
        cycles = 7
        return value
    }

    func loadMemReg16(_ reg: UInt16) {
        let address = fetchU16()
        writeU8(UInt8(truncatingIfNeeded: reg), address)
        writeU8(UInt8(truncatingIfNeeded: reg >> 8), address &+ 1)

        cycles = 16
    }

    func loadMemReg8(_ reg: UInt8) {
        let address = fetchU16()
        writeU8(reg, address)

        cycles = 13
    }

    func loadHlReg(_ reg: UInt8) {
        writeU8(reg, hl.value)
        cycles = 7
    }

    func load16Reg() -> UInt16 {
        let address = fetchU16()

        let lo = UInt16(readU8(address))
        let hi = UInt16(readU8(address &+ 1))
        let value = (hi << 8) | lo

        cycles = 16
        return value
    }

    func load16A() {
        let address = fetchU16()
        let value = readU8(address)

        af.hi = value
        cycles = 13
    }

    func loadReg(_ sourceReg: UInt8) -> UInt8 {
        cycles = 4
        return sourceReg
    }

    func loadRegMem(_ reg: UInt16) -> UInt8 {
        let value = readU8(reg)
        cycles = 7
        return value
    }

    func loadReg8Mem(_ mem: UInt16, _ reg: UInt8) {
        writeU8(reg, mem)
        cycles = 7
    }

    func loadAWithR() {
        af.hi = ir.lo

        clearFlag(FLAG_N | FLAG_H)
        affectFlag(isSigned8(ir.lo), FLAG_S)
        affectFlag(ir.lo == 0, FLAG_Z)
        affectFlag(iff2 != 0, FLAG_PV)

        cycles = 9
    }

    func loadAWithI() {
        af.hi = ir.hi

        clearFlag(FLAG_N | FLAG_H)
        affectFlag(isSigned8(ir.hi), FLAG_S)
        affectFlag(ir.hi == 0, FLAG_Z)
        affectFlag(iff2 != 0, FLAG_PV)

        cycles = 9
    }

    func loadSpReg(_ reg: UInt16) {
        sp = reg
        cycles = 6
    }

    // MARK: - Arithmetic

    func incReg16(_ reg: UInt16) -> UInt16 {
        cycles = 6
        return reg &+ 1
    }

    func decReg16(_ reg: UInt16) -> UInt16 {
        cycles = 6
        return reg &- 1
    }

    func decReg8(_ reg: UInt8) -> UInt8 {
        let result = reg &- 1

        setFlag(FLAG_N)
        affectFlag(isSigned8(result), FLAG_S)
        affectFlag(result == 0, FLAG_Z)
        affectFlag(halfBorrowOccured8(reg, 1, 0), FLAG_H)
        affectFlag(overflowFromSub8(reg, 1, 0), FLAG_PV)

        cycles = 4
        return result
    }

    func decMemHl() {
        var value = readU8(hl.value)
        value = decReg8(value)

        writeU8(value, hl.value)

        cycles += 7
    }

    func incReg8(_ reg: UInt8) -> UInt8 {
        let result = reg &+ 1

        clearFlag(FLAG_N)
        affectFlag(isSigned8(result), FLAG_S)
        affectFlag(result == 0, FLAG_Z)
        affectFlag(halfCarryOccured8(reg, 1, 0), FLAG_H)
        affectFlag(overflowFromAdd8(reg, 1, 0), FLAG_PV)

        cycles = 4
        return result
    }

    func incMemHl() {
        var value = readU8(hl.value)
        value = incReg8(value)

        writeU8(value, hl.value)

        cycles += 7
    }

    func addReg16(_ destReg: UInt16, _ sourceReg: UInt16) -> UInt16 {
        clearFlag(FLAG_N)
        affectFlag(halfCarryOccured16(destReg, sourceReg, 0), FLAG_H)
        affectFlag(carryOccured16(destReg, sourceReg, 0), FLAG_C)

        // The C version is `cycles = 11; if (ix || iy); cycles += 4;` -- the stray `;`
        // means every add gets the +4. Kept so timing matches.
        cycles = 11
        cycles += 4
        return destReg &+ sourceReg
    }

    /// Destination is always A.
    func addReg8(_ sourceReg: UInt8) {
        let destReg = af.hi
        let result = destReg &+ sourceReg

        clearFlag(FLAG_N)
        affectFlag(isSigned8(result), FLAG_S)
        affectFlag(result == 0, FLAG_Z)
        affectFlag(halfCarryOccured8(destReg, sourceReg, 0), FLAG_H)
        affectFlag(overflowFromAdd8(destReg, sourceReg, 0), FLAG_PV)
        affectFlag(carryOccured8(destReg, sourceReg, 0), FLAG_C)

        af.hi = result
        cycles = 4
    }

    func addMemHl() {
        let value = readU8(hl.value)
        addReg8(value)

        cycles += 3
    }

    func adcReg16(_ destReg: UInt16, _ sourceReg: UInt16) -> UInt16 {
        let carry: UInt8 = getFlag(FLAG_C) ? 1 : 0
        let result = destReg &+ sourceReg &+ UInt16(carry)

        clearFlag(FLAG_N)
        affectFlag(isSigned16(result), FLAG_S)
        affectFlag(result == 0, FLAG_Z)
        affectFlag(halfCarryOccured16(destReg, sourceReg, carry), FLAG_H)
        affectFlag(overflowFromAdd16(destReg, sourceReg, carry), FLAG_PV)
        affectFlag(carryOccured16(destReg, sourceReg, carry), FLAG_C)

        cycles = 15
        return result
    }

    func adcMemHl() {
        let value = readU8(hl.value)
        adcReg8(value)

        cycles += 3
    }

    /// Destination is always A.
    func adcReg8(_ sourceReg: UInt8) {
        let carry: UInt8 = getFlag(FLAG_C) ? 1 : 0
        let destReg = af.hi
        let result = destReg &+ sourceReg &+ carry

        clearFlag(FLAG_N)
        affectFlag(isSigned8(result), FLAG_S)
        affectFlag(result == 0, FLAG_Z)
        affectFlag(halfCarryOccured8(destReg, sourceReg, carry), FLAG_H)
        affectFlag(overflowFromAdd8(destReg, sourceReg, carry), FLAG_PV)
        affectFlag(carryOccured8(destReg, sourceReg, carry), FLAG_C)

        af.hi = result
        cycles = 4
    }

    /// Destination is always A.
    func subReg8(_ sourceReg: UInt8) {
        let destReg = af.hi
        let result = destReg &- sourceReg

        setFlag(FLAG_N)
        affectFlag(isSigned8(result), FLAG_S)
        affectFlag(result == 0, FLAG_Z)
        affectFlag(halfBorrowOccured8(destReg, sourceReg, 0), FLAG_H)
        affectFlag(overflowFromSub8(destReg, sourceReg, 0), FLAG_PV)
        affectFlag(borrowOccured8(destReg, sourceReg, 0), FLAG_C)

        af.hi = result
        cycles = 4
    }

    func subMemHl() {
        let value = readU8(hl.value)
        subReg8(value)

        cycles += 3
    }

    /// Destination is always A.
    func sbcReg8(_ sourceReg: UInt8) {
        let carry: UInt8 = getFlag(FLAG_C) ? 1 : 0
        let destReg = af.hi
        let result = destReg &- sourceReg &- carry

        setFlag(FLAG_N)
        affectFlag(isSigned8(result), FLAG_S)
        affectFlag(result == 0, FLAG_Z)
        affectFlag(halfBorrowOccured8(destReg, sourceReg, carry), FLAG_H)
        affectFlag(overflowFromSub8(destReg, sourceReg, carry), FLAG_PV)
        affectFlag(borrowOccured8(destReg, sourceReg, carry), FLAG_C)

        af.hi = result
        cycles = 4
    }

    func sbcMemHl() {
        let value = readU8(hl.value)
        sbcReg8(value)

        cycles += 3
    }

    func sbcReg16(_ destReg: UInt16, _ sourceReg: UInt16) -> UInt16 {
        let carry: UInt8 = getFlag(FLAG_C) ? 1 : 0
        let result = destReg &- sourceReg &- UInt16(carry)

        setFlag(FLAG_N)
        affectFlag(isSigned16(result), FLAG_S)
        affectFlag(result == 0, FLAG_Z)
        affectFlag(halfBorrowOccured16(destReg, sourceReg, carry), FLAG_H)
        affectFlag(overflowFromSub16(destReg, sourceReg, carry), FLAG_PV)
        affectFlag(borrowOccured16(destReg, sourceReg, carry), FLAG_C)

        cycles = 15
        return result
    }

    // MARK: - Branches / Jumps / Returns

    func jrImm() {
        let imm = fetchU8()
        pc = offsetAddress(pc, imm)
        cycles = 12
    }

    func jrImmCond(_ cond: Bool) {
        if cond {
            jrImm()
        } else {
            pc &+= 1
            cycles = 7
        }
    }

    func djnz() {
        bc.hi &-= 1
        if bc.hi != 0 {
            jrImm()
            cycles += 1
        } else {
            pc &+= 1
            cycles = 8
        }
    }

    func rst(_ vector: UInt16) {
        sp &-= 1
        writeU8(UInt8(truncatingIfNeeded: pc >> 8), sp)
        sp &-= 1
        writeU8(UInt8(truncatingIfNeeded: pc), sp)

        pc = vector
        cycles = 11
    }

    func call() {
        let address = fetchU16()
        sp &-= 1
        writeU8(UInt8(truncatingIfNeeded: pc >> 8), sp)
        sp &-= 1
        writeU8(UInt8(truncatingIfNeeded: pc), sp)

        pc = address
        cycles = 17
    }

    func callCond(_ cond: Bool) {
        if cond {
            call()
        } else {
            pc &+= 2
            cycles = 10
        }
    }

    func ret() {
        let lo = UInt16(readU8(sp))
        sp &+= 1
        let hi = UInt16(readU8(sp))
        sp &+= 1

        let returnAddress = (hi << 8) | lo
        pc = returnAddress
        cycles = 10
    }

    func reti() {
        ret()
        cycles += 4
    }

    func retn() {
        ret()
        iff1 = iff2
        cycles += 4
    }

    func retCond(_ cond: Bool) {
        if cond {
            ret()
            cycles += 1
        } else {
            cycles = 5
        }
    }

    func jp() {
        let address = fetchU16()
        pc = address
        cycles = 10
    }

    func jpCond(_ cond: Bool) {
        if cond {
            jp()
        } else {
            pc &+= 2
            cycles = 10
        }
    }

    func jpMemHl() {
        pc = hl.value
        cycles = 4
    }

    // MARK: - Logical

    func xor(_ reg: UInt8) {
        let result = af.hi ^ reg
        af.hi ^= reg

        affectFlag(result == 0, FLAG_Z)
        affectFlag(isEvenParity(result), FLAG_PV)
        affectFlag(isSigned8(result), FLAG_S)

        clearFlag(FLAG_C | FLAG_N | FLAG_H)
        cycles = 4
    }

    func xorMemHl() {
        let value = readU8(hl.value)
        xor(value)

        cycles += 3
    }

    func or(_ reg: UInt8) {
        let result = af.hi | reg
        af.hi |= reg

        affectFlag(result == 0, FLAG_Z)
        affectFlag(isEvenParity(result), FLAG_PV)
        affectFlag(isSigned8(result), FLAG_S)

        clearFlag(FLAG_C | FLAG_N | FLAG_H)
        cycles = 4
    }

    func orMemHl() {
        let value = readU8(hl.value)
        or(value)

        cycles += 3
    }

    func and(_ reg: UInt8) {
        let result = af.hi & reg
        af.hi &= reg

        affectFlag(result == 0, FLAG_Z)
        affectFlag(isEvenParity(result), FLAG_PV)
        affectFlag(isSigned8(result), FLAG_S)

        setFlag(FLAG_H)
        clearFlag(FLAG_C | FLAG_N)

        cycles = 4
    }

    func andMemHl() {
        let value = readU8(hl.value)
        and(value)

        cycles += 3
    }

    // MARK: - Shifts

    func rlca() {
        let msb = (af.hi >> 7) & 0x1
        clearFlag(FLAG_N | FLAG_H)

        af.hi <<= 1

        // Old bit 7 stored to carry, old bit 7 moved to it 0
        if msb != 0 {
            setFlag(FLAG_C)
            af.hi = setBit(af.hi, 0)
        } else {
            clearFlag(FLAG_C)
            af.hi = clearBit(af.hi, 0)
        }

        cycles = 4
    }

    func rrca() {
        let lsb = af.hi & 0x1
        clearFlag(FLAG_N | FLAG_H)

        af.hi >>= 1

        // Old bit 0 stored to carry, old bit 0 moved to bit 7
        if lsb != 0 {
            setFlag(FLAG_C)
            af.hi = setBit(af.hi, 7)
        } else {
            clearFlag(FLAG_C)
            af.hi = clearBit(af.hi, 7)
        }

        cycles = 4
    }

    func rra() {
        let carry: UInt8 = getFlag(FLAG_C) ? 1 : 0
        let lsb = af.hi & 0x1

        clearFlag(FLAG_N | FLAG_H)

        af.hi >>= 1
        af.hi |= (carry << 7)

        if lsb != 0 {
            setFlag(FLAG_C)
        } else {
            clearFlag(FLAG_C)
        }

        cycles = 4
    }

    func rla() {
        let carry: UInt8 = getFlag(FLAG_C) ? 1 : 0
        let msb = (af.hi >> 7) & 0x1
        clearFlag(FLAG_N | FLAG_H)

        af.hi <<= 1
        af.hi |= carry
        if msb != 0 {
            setFlag(FLAG_C)
        } else {
            clearFlag(FLAG_C)
        }

        cycles = 4
    }

    // MARK: - Exchanges

    func ex(_ reg1: inout UInt16, _ reg2: inout UInt16) {
        cycles = 4
        let tempReg1 = reg1
        reg1 = reg2
        reg2 = tempReg1
    }

    /// The `reg1 == &z80->sp` branch of ex(): ex (sp),hl / ex (sp),ix / ex (sp),iy
    func exSp(_ reg2: UInt16) -> UInt16 {
        cycles = 4
        let lo = UInt16(readU8(sp))
        let hi = UInt16(readU8(sp &+ 1))

        let reg2Lo = UInt8(truncatingIfNeeded: reg2)
        let reg2Hi = UInt8(truncatingIfNeeded: reg2 >> 8)

        writeU8(reg2Lo, sp)
        writeU8(reg2Hi, sp &+ 1)

        cycles += 15
        return (hi << 8) | lo
    }

    func exx() {
        let tempBc = bc.value
        bc.value = shadowedRegs.bc.value
        shadowedRegs.bc.value = tempBc

        let tempDe = de.value
        de.value = shadowedRegs.de.value
        shadowedRegs.de.value = tempDe

        let tempHl = hl.value
        hl.value = shadowedRegs.hl.value
        shadowedRegs.hl.value = tempHl

        cycles = 4
    }

    // MARK: - Stack
    // (The C versions add 4 cycles for IX/IY; the IX/IY tables do that here.)

    func push(_ reg: UInt16) {
        sp &-= 1
        writeU8(UInt8(truncatingIfNeeded: reg >> 8), sp)
        sp &-= 1
        writeU8(UInt8(truncatingIfNeeded: reg), sp)

        cycles = 11
    }

    func pop() -> UInt16 {
        let lo = UInt16(readU8(sp))
        sp &+= 1
        let hi = UInt16(readU8(sp))
        sp &+= 1

        cycles = 10
        return (hi << 8) | lo
    }

    // MARK: - Io / Ports

    func outa() {
        let ioPort = fetchU8()
        io.ioWriteU8(af.hi, ioPort)

        cycles = 11
    }

    func outi() {
        let value = readU8(hl.value)
        let port = bc.lo

        io.ioWriteU8(value, port)

        hl.value &+= 1
        bc.hi &-= 1

        setFlag(FLAG_N)
        affectFlag(bc.hi == 0, FLAG_Z)

        cycles = 16
    }

    func outd() {
        let value = readU8(hl.value)
        let port = bc.lo

        io.ioWriteU8(value, port)

        hl.value &-= 1
        bc.hi &-= 1

        setFlag(FLAG_N)
        affectFlag(bc.hi == 0, FLAG_Z)

        cycles = 16
    }

    func ina() {
        let ioPort = fetchU8()

        if cpmStubEnabled {
            af.hi = 0xFF
        } else {
            let openBus = ioPort <= 0x3F
            if openBus {
                af.hi = ioPort
            } else {
                let ioValue = io.ioReadU8(ioPort)
                af.hi = ioValue
            }
        }

        cycles = 11
    }

    func out(_ destPort: UInt8, _ sourceReg: UInt8) {
        io.ioWriteU8(sourceReg, destPort)
        cycles = 12
    }

    /// Returns the value read (the C version writes it through destReg, which can be NULL).
    func `in`(_ sourcePort: UInt8, _ opcode: UInt8) -> UInt8 {
        var ioValue: UInt8 = 0x0
        let openBus = sourcePort <= 0x3F
        if openBus {
            ioValue = opcode
        } else {
            ioValue = io.ioReadU8(sourcePort)
        }

        affectFlag(ioValue == 0, FLAG_Z)
        affectFlag(isEvenParity(ioValue), FLAG_PV)
        affectFlag(isSigned8(ioValue), FLAG_S)

        clearFlag(FLAG_H | FLAG_N)
        cycles = 12
        return ioValue
    }

    func otir() {
        cycles = bc.hi != 0 ? 21 : 16

        // Byte from address hl written to port c
        let value = readU8(hl.value)
        let ioPort = bc.lo
        io.ioWriteU8(value, ioPort)

        hl.value &+= 1
        bc.hi &-= 1

        // If byte counter (B) is not 0, pc is decremented by 2 and the instruction
        // will be repeated (also interrupts will be checked and possibly triggered
        // until the instructions terminates once B becomes 0
        if bc.hi != 0 {
            pc &-= 2
        }

        affectFlag(bc.hi == 0, FLAG_Z)
        setFlag(FLAG_N)
    }

    func otdr() {
        cycles = bc.hi != 0 ? 21 : 16

        // Byte from address hl written to port c
        let value = readU8(hl.value)
        let ioPort = bc.lo
        io.ioWriteU8(value, ioPort)

        hl.value &-= 1
        bc.hi &-= 1

        if bc.hi != 0 {
            pc &-= 2
        }

        affectFlag(bc.hi == 0, FLAG_Z)
        setFlag(FLAG_N)
    }

    func ini() {
        let ioPort = bc.lo
        let value = io.ioReadU8(ioPort)

        writeU8(value, hl.value)

        hl.value &+= 1
        bc.hi &-= 1

        setFlag(FLAG_N)
        affectFlag(bc.hi == 0, FLAG_Z)

        cycles = 16
    }

    func ldir() {
        cycles = bc.value != 0 ? 21 : 16

        // Transfers bytes from source address hl to destination
        // address de, bc times
        let value = readU8(hl.value)
        writeU8(value, de.value)

        hl.value &+= 1
        de.value &+= 1
        bc.value &-= 1

        if bc.value != 0 {
            pc &-= 2
        }

        affectFlag(bc.value != 0, FLAG_PV)
        clearFlag(FLAG_N | FLAG_H)
    }

    func ldi() {
        let value = readU8(hl.value)
        writeU8(value, de.value)

        hl.value &+= 1
        de.value &+= 1
        bc.value &-= 1

        affectFlag(bc.value != 0, FLAG_PV)
        clearFlag(FLAG_N | FLAG_H)

        cycles = 16
    }

    func ldd() {
        let value = readU8(hl.value)
        writeU8(value, de.value)

        hl.value &-= 1
        de.value &-= 1
        bc.value &-= 1

        clearFlag(FLAG_H | FLAG_N)
        affectFlag(bc.value != 0, FLAG_PV)

        cycles = 16
    }

    func lddr() {
        ldd()
        if bc.value != 0 {
            pc &-= 2
            cycles += 5
        }
    }

    func rrd() {
        let value = readU8(hl.value)

        let a = af.hi
        af.hi = (a & 0xF0) | (value & 0xF)
        let result = (value >> 4) | (a << 4)

        writeU8(result, hl.value)

        clearFlag(FLAG_N | FLAG_H)
        affectFlag(isSigned8(af.hi), FLAG_S)
        affectFlag(af.hi == 0, FLAG_Z)
        affectFlag(isEvenParity(af.hi), FLAG_PV)

        cycles = 18
    }

    func rld() {
        let value = readU8(hl.value)

        let a = af.hi
        af.hi = (a & 0xF0) | (value >> 4)
        let result = (value << 4) | (a & 0xF)

        writeU8(result, hl.value)

        clearFlag(FLAG_N | FLAG_H)
        affectFlag(isSigned8(af.hi), FLAG_S)
        affectFlag(af.hi == 0, FLAG_Z)
        affectFlag(isEvenParity(af.hi), FLAG_PV)

        cycles = 18
    }

    // MARK: - Misc

    func cpl() {
        af.hi = ~af.hi
        setFlag(FLAG_N | FLAG_H)

        cycles = 4
    }

    func halt() {
        halted = 1
        cycles = 4
    }

    func cp(_ reg: UInt8) {
        let result = af.hi &- reg

        setFlag(FLAG_N)
        affectFlag(result == 0, FLAG_Z)
        affectFlag(overflowFromSub8(af.hi, reg, 0), FLAG_PV)
        affectFlag(isSigned8(result), FLAG_S)
        affectFlag(borrowOccured8(af.hi, reg, 0), FLAG_C)
        affectFlag(halfBorrowOccured8(af.hi, reg, 0), FLAG_H)

        cycles = 4
    }

    func cpMemHl() {
        let value = readU8(hl.value)
        cp(value)

        cycles += 3
    }

    func daa() {
        // The DA instruction adjusts the 8-bit value in the accumulator to correspond
        // to binary-coded decimal (BCD) format: +06h if the low nibble is > 9 or H is set,
        // +60h if A > 99h or C is set (subtracted instead after a subtraction).
        var correction: UInt8 = 0
        if (af.hi & 0xF) > 0x9 || getFlag(FLAG_H) {
            correction &+= 0x6
        }

        if af.hi > 0x99 || getFlag(FLAG_C) {
            correction &+= 0x60
            setFlag(FLAG_C)
        }

        let sub = getFlag(FLAG_N)
        if sub {
            let halfCarry = getFlag(FLAG_H)
            affectFlag(halfCarry && (af.hi & 0xF) < 0x06, FLAG_H)
            af.hi &-= correction
        } else {
            affectFlag((af.hi & 0xF) > 0x09, FLAG_H)
            af.hi &+= correction
        }

        affectFlag(isSigned8(af.hi), FLAG_S)
        affectFlag(af.hi == 0, FLAG_Z)
        affectFlag(isEvenParity(af.hi), FLAG_PV)

        cycles = 4
    }

    func ccf() {
        let carry = getFlag(FLAG_C)

        clearFlag(FLAG_N)
        affectFlag(carry, FLAG_H)
        affectFlag(!carry, FLAG_C)

        cycles = 4
    }

    func neg() {
        let result = 0 &- af.hi

        setFlag(FLAG_N)
        affectFlag(isSigned8(result), FLAG_S)
        affectFlag(result == 0, FLAG_Z)
        affectFlag(halfBorrowOccured8(0, af.hi, 0), FLAG_H)
        affectFlag(overflowFromSub8(0, af.hi, 0), FLAG_PV)
        affectFlag(borrowOccured8(0, af.hi, 0), FLAG_C)

        af.hi = result
        cycles = 8
    }

    func scf() {
        clearFlag(FLAG_N | FLAG_H)
        setFlag(FLAG_C)

        cycles = 4
    }

    func cpd() {
        let value = readU8(hl.value)
        let result = af.hi &- value

        hl.value &-= 1
        bc.value &-= 1

        setFlag(FLAG_N)
        affectFlag(isSigned8(result), FLAG_S)
        affectFlag(af.hi == value, FLAG_Z)
        affectFlag(halfBorrowOccured8(af.hi, value, 0), FLAG_H)
        affectFlag(bc.value != 0, FLAG_PV)

        cycles = 16
    }

    func cpdr() {
        cpd()
        if bc.value != 0 && !getFlag(FLAG_Z) {
            pc &-= 2
            cycles += 5
        }
    }

    func cpi() {
        let value = readU8(hl.value)
        let result = af.hi &- value

        hl.value &+= 1
        bc.value &-= 1

        setFlag(FLAG_N)
        affectFlag(isSigned8(result), FLAG_S)
        affectFlag(af.hi == value, FLAG_Z)
        affectFlag(halfBorrowOccured8(af.hi, value, 0), FLAG_H)
        affectFlag(bc.value != 0, FLAG_PV)

        cycles = 16
    }

    func cpir() {
        cpi()
        if bc.value != 0 && !getFlag(FLAG_Z) {
            pc &-= 2
            cycles += 5
        }
    }

    // MARK: - Bit instructions

    /// Shared tail of rlc/rl/rrc/rr/sla/sra/srl/sll: S, Z, P/V from the result.
    @inline(__always) private func shiftFlags(_ regValue: UInt8, carryOut: Bool) {
        clearFlag(FLAG_N | FLAG_H)
        if carryOut { setFlag(FLAG_C) } else { clearFlag(FLAG_C) }

        affectFlag(isSigned8(regValue), FLAG_S)
        affectFlag(regValue == 0, FLAG_Z)
        affectFlag(isEvenParity(regValue), FLAG_PV)

        cycles = 8
    }

    func rlc(_ reg: UInt8) -> UInt8 {
        let msb = (reg >> 7) & 0x1
        let regValue = (reg << 1) | msb
        shiftFlags(regValue, carryOut: msb != 0)
        return regValue
    }

    func rlcMemHl() {
        let value = rlc(readU8(hl.value))
        writeU8(value, hl.value)
        cycles = 15
    }

    func rl(_ reg: UInt8) -> UInt8 {
        let carry: UInt8 = getFlag(FLAG_C) ? 1 : 0
        let msb = (reg >> 7) & 0x1
        let regValue = (reg << 1) | carry
        shiftFlags(regValue, carryOut: msb != 0)
        return regValue
    }

    func rlMemHl() {
        let value = rl(readU8(hl.value))
        writeU8(value, hl.value)
        cycles = 15
    }

    func rrc(_ reg: UInt8) -> UInt8 {
        let lsb = reg & 0x1
        let regValue = (reg >> 1) | (lsb << 7)
        shiftFlags(regValue, carryOut: lsb != 0)
        return regValue
    }

    func rr(_ reg: UInt8) -> UInt8 {
        let carry: UInt8 = getFlag(FLAG_C) ? 1 : 0
        let lsb = reg & 0x1
        let regValue = (reg >> 1) | (carry << 7)
        shiftFlags(regValue, carryOut: lsb != 0)
        return regValue
    }

    func rrMemHl() {
        let value = rr(readU8(hl.value))
        writeU8(value, hl.value)
        cycles = 15
    }

    func rrcMemHl() {
        let value = rrc(readU8(hl.value))
        writeU8(value, hl.value)
        cycles = 15
    }

    func sla(_ reg: UInt8) -> UInt8 {
        let msb = (reg >> 7) & 0x1
        let regValue = reg << 1
        shiftFlags(regValue, carryOut: msb != 0)
        return regValue
    }

    func slaMemHl() {
        let value = sla(readU8(hl.value))
        writeU8(value, hl.value)
        cycles = 15
    }

    func sra(_ reg: UInt8) -> UInt8 {
        let lsb = reg & 0x1
        let sign = reg & 0x80
        let regValue = (reg >> 1) | sign
        shiftFlags(regValue, carryOut: lsb != 0)
        return regValue
    }

    func sraMemHl() {
        let value = sra(readU8(hl.value))
        writeU8(value, hl.value)
        cycles = 15
    }

    func srl(_ reg: UInt8) -> UInt8 {
        let lsb = reg & 0x1
        let regValue = reg >> 1
        shiftFlags(regValue, carryOut: lsb != 0)
        return regValue
    }

    func srlMemHl() {
        let value = srl(readU8(hl.value))
        writeU8(value, hl.value)
        cycles = 15
    }

    func sll(_ reg: UInt8) -> UInt8 {
        let msb = (reg >> 7) & 0x1
        let regValue = (reg << 1) | 0x1
        shiftFlags(regValue, carryOut: msb != 0)
        return regValue
    }

    func sllMemHl() {
        let value = sll(readU8(hl.value))
        writeU8(value, hl.value)
        cycles = 15
    }

    func bit(_ reg: UInt8, _ bit: UInt8) {
        let test = testBit(reg, bit)

        affectFlag(test == 0, FLAG_Z)
        clearFlag(FLAG_N)
        setFlag(FLAG_H)

        cycles = 8
    }

    func bitMemHl(_ bitToTest: UInt8) {
        let value = readU8(hl.value)
        bit(value, bitToTest)

        cycles += 4
    }

    func res(_ reg: UInt8, _ bit: UInt8) -> UInt8 {
        cycles = 8
        return clearBit(reg, bit)
    }

    func resMemHl(_ bit: UInt8) {
        let value = res(readU8(hl.value), bit)
        writeU8(value, hl.value)

        cycles += 7
    }

    func set(_ reg: UInt8, _ bit: UInt8) -> UInt8 {
        cycles = 8
        return setBit(reg, bit)
    }

    func setMemHl(_ bit: UInt8) {
        let value = set(readU8(hl.value), bit)
        writeU8(value, hl.value)

        cycles = 15
    }

    // MARK: - Ix instructions

    func loadRegIx() -> UInt8 {
        let offset = fetchU8()
        let value = readU8(offsetAddress(ix.value, offset))
        cycles = 19
        return value
    }

    func loadIxReg(_ reg: UInt8) {
        let offset = fetchU8()
        writeU8(reg, offsetAddress(ix.value, offset))
        cycles = 19
    }

    func loadIxImm() {
        // ix offset byte is first in memory
        let offset = fetchU8()
        let address = offsetAddress(ix.value, offset)

        let immValue = fetchU8()
        writeU8(immValue, address)

        cycles = 19
    }

    func incMemIx() {
        let address = offsetAddress(ix.value, fetchU8())
        writeU8(incReg8(readU8(address)), address)
        cycles += 19
    }

    func addMemIx() {
        addReg8(readU8(offsetAddress(ix.value, fetchU8())))
        cycles += 15
    }

    func adcMemIx() {
        adcReg8(readU8(offsetAddress(ix.value, fetchU8())))
        cycles += 15
    }

    func decMemIx() {
        let address = offsetAddress(ix.value, fetchU8())
        writeU8(decReg8(readU8(address)), address)
        cycles += 19
    }

    func subMemIx() {
        subReg8(readU8(offsetAddress(ix.value, fetchU8())))
        cycles += 15
    }

    func sbcMemIx() {
        sbcReg8(readU8(offsetAddress(ix.value, fetchU8())))
        cycles += 15
    }

    func andMemIx() {
        and(readU8(offsetAddress(ix.value, fetchU8())))
        cycles += 15
    }

    func xorMemIx() {
        xor(readU8(offsetAddress(ix.value, fetchU8())))
        cycles += 15
    }

    func orMemIx() {
        or(readU8(offsetAddress(ix.value, fetchU8())))
        cycles += 15
    }

    func cpMemIx() {
        cp(readU8(offsetAddress(ix.value, fetchU8())))
        cycles += 15
    }

    func jpMemIx() {
        pc = ix.value
        cycles = 8
    }

    func bitMemIx(_ bitToTest: UInt8) {
        let value = readU8(offsetAddress(ix.value, fetchU8()))
        bit(value, bitToTest)
        cycles += 12
    }

    func resMemIx(_ bit: UInt8) {
        let address = offsetAddress(ix.value, fetchU8())
        writeU8(res(readU8(address), bit), address)
        cycles += 15
    }

    func setMemIx(_ bit: UInt8) {
        let address = offsetAddress(ix.value, fetchU8())
        writeU8(set(readU8(address), bit), address)
        cycles += 15
    }

    func rlcMemIx() { shiftMemIndexed(ix.value, rlc) }
    func rrcMemIx() { shiftMemIndexed(ix.value, rrc) }
    func rlMemIx() { shiftMemIndexed(ix.value, rl) }
    func rrMemIx() { shiftMemIndexed(ix.value, rr) }
    func slaMemIx() { shiftMemIndexed(ix.value, sla) }
    func sraMemIx() { shiftMemIndexed(ix.value, sra) }
    func srlMemIx() { shiftMemIndexed(ix.value, srl) }
    func sllMemIx() { shiftMemIndexed(ix.value, sll) }

    /// Body shared by rlcMemIx ... sllMemIy: shift the byte at (index + d), 23 cycles.
    @inline(__always) private func shiftMemIndexed(_ index: UInt16, _ shift: (UInt8) -> UInt8) {
        let address = offsetAddress(index, fetchU8())
        writeU8(shift(readU8(address)), address)
        cycles = 23
    }

    // MARK: - Iy instructions

    func loadIyImm() {
        // iy offset byte is first in memory
        let offset = fetchU8()
        let address = offsetAddress(iy.value, offset)

        let immValue = fetchU8()
        writeU8(immValue, address)

        cycles = 19
    }

    func loadRegIy() -> UInt8 {
        let offset = fetchU8()
        let value = readU8(offsetAddress(iy.value, offset))
        cycles = 19
        return value
    }

    func loadIyReg(_ reg: UInt8) {
        let offset = fetchU8()
        writeU8(reg, offsetAddress(iy.value, offset))
        cycles = 19
    }

    func incMemIy() {
        let address = offsetAddress(iy.value, fetchU8())
        writeU8(incReg8(readU8(address)), address)
        cycles += 19
    }

    func decMemIy() {
        let address = offsetAddress(iy.value, fetchU8())
        writeU8(decReg8(readU8(address)), address)
        cycles += 19
    }

    func addMemIy() {
        addReg8(readU8(offsetAddress(iy.value, fetchU8())))
        cycles += 15
    }

    func adcMemIy() {
        adcReg8(readU8(offsetAddress(iy.value, fetchU8())))
        cycles += 15
    }

    func subMemIy() {
        subReg8(readU8(offsetAddress(iy.value, fetchU8())))
        cycles += 15
    }

    func sbcMemIy() {
        sbcReg8(readU8(offsetAddress(iy.value, fetchU8())))
        cycles += 15
    }

    func andMemIy() {
        and(readU8(offsetAddress(iy.value, fetchU8())))
        cycles += 15
    }

    func xorMemIy() {
        xor(readU8(offsetAddress(iy.value, fetchU8())))
        cycles += 15
    }

    func orMemIy() {
        or(readU8(offsetAddress(iy.value, fetchU8())))
        cycles += 15
    }

    func cpMemIy() {
        cp(readU8(offsetAddress(iy.value, fetchU8())))
        cycles += 15
    }

    func jpMemIy() {
        pc = iy.value
        cycles = 8
    }

    func bitMemIy(_ bitToTest: UInt8) {
        let value = readU8(offsetAddress(iy.value, fetchU8()))
        bit(value, bitToTest)
        cycles += 12
    }

    func resMemIy(_ bit: UInt8) {
        let address = offsetAddress(iy.value, fetchU8())
        writeU8(res(readU8(address), bit), address)
        cycles += 15
    }

    func setMemIy(_ bit: UInt8) {
        let address = offsetAddress(iy.value, fetchU8())
        writeU8(set(readU8(address), bit), address)
        cycles += 12   // C uses 12 here (setMemIx uses 15)
    }

    func rlcMemIy() { shiftMemIndexed(iy.value, rlc) }
    func rrcMemIy() { shiftMemIndexed(iy.value, rrc) }
    func rlMemIy() { shiftMemIndexed(iy.value, rl) }
    func rrMemIy() { shiftMemIndexed(iy.value, rr) }
    func slaMemIy() { shiftMemIndexed(iy.value, sla) }
    func sraMemIy() { shiftMemIndexed(iy.value, sra) }
    func srlMemIy() { shiftMemIndexed(iy.value, srl) }
    func sllMemIy() { shiftMemIndexed(iy.value, sll) }

    // MARK: - Interrupt related instructions

    func di() {
        iff1 = 0
        iff2 = 0
        processInterruptDelay = 0
        cycles = 4
    }

    func ei() {
        iff1 = 1
        iff2 = 1
        processInterruptDelay = 1
        cycles = 4
    }

    func im(_ interruptMode: IntMode) {
        self.interruptMode = interruptMode
        cycles = 8
    }
}

/// Placeholder until connectBus / connectIo are called.
final class NoBus: MemoryBus, IoBus, Sendable {
    static let shared = NoBus()
    func memoryBusReadU8(_ address: UInt16) -> UInt8 { 0xFF }
    func memoryBusWriteU8(_ value: UInt8, _ address: UInt16) {}
    func ioReadU8(_ address: UInt8) -> UInt8 { 0xFF }
    func ioWriteU8(_ value: UInt8, _ address: UInt8) {}
}

@inline(__always) func hex2(_ v: UInt8) -> String {
    let s = String(v, radix: 16, uppercase: true)
    return s.count < 2 ? "0" + s : s
}

@inline(__always) func hex4(_ v: UInt16) -> String {
    let s = String(v, radix: 16, uppercase: true)
    return String(repeating: "0", count: max(0, 4 - s.count)) + s
}
