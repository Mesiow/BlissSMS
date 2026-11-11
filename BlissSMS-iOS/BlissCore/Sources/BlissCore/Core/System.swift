// Swift port of BlissSMS/Core/System.c + System.h
//
// SFML input/rendering moved to the app: instead of systemHandleInput(sfEvent) there are
// buttonPressed / pausePressed / resetPressed, and the app draws vdp.framebuffer itself.

public let CPU_CLOCK = 3_579_545
public let SCANLINES_PER_FRAME = 262
public let FPS = 60
public let CYCLES_PER_SCANLINE = 228
public let MAX_CYCLES_PER_FRAME = SCANLINES_PER_FRAME * CYCLES_PER_SCANLINE

public final class System {
    public let bus: Bus
    public let io: Io
    public let z80: Z80
    public let vdp: Vdp
    public let psg: Psg
    public let joy: Joypad
    public let cart: Cart

    public var running: UInt8 = 0
    public var runDebugger: UInt8 = 0

    /// systemInit. The C version hardcodes the BIOS and cart paths; here they're passed in.
    /// - Parameters:
    ///   - cartPath: path to a .sms file (save data goes next to it as .sav)
    ///   - biosPath: optional BIOS image. Without one the cart starts directly.
    public init(cartPath: String, biosPath: String? = nil) throws {
        bus = Bus()
        if let biosPath {
            bus.loadBios(biosPath)
        }

        z80 = Z80()
        vdp = Vdp()
        psg = Psg()
        joy = Joypad()
        cart = Cart()
        io = Io(vdp: vdp, psg: psg, joy: joy, bus: bus)

        io.connectBus(bus)

        z80.connectBus(bus)
        z80.connectIo(io)

        vdp.connectIo(io)
        io.connectVdp(vdp)

        io.connectPsg(psg)

        io.connectJoypad(joy)

        try cart.load(cartPath)
        bus.loadCart(cart)

        running = 1
        runDebugger = 0
    }

    /// systemRunEmulation: runs one frame.
    public func runEmulation() {
        guard running != 0 else { return }

        var cyclesThisFrame = 0
        while vdp.isFrameComplete() == 0 {
            let cycles = z80.clock()
            cyclesThisFrame += Int(cycles)

            vdp.update(UInt8(truncatingIfNeeded: cycles))
            psg.update(UInt8(truncatingIfNeeded: cycles))
            if z80.processInterruptDelay != 0 {
                continue
            }

            z80.handleInterrupts(vdp.pendingInterrupts() != 0)
        }
        joy.update()
        vdp.bufferPixels()
    }

    // MARK: - Input (systemHandleInput)

    public func buttonPressed(_ btn: Joypad.Button, _ pressed: Bool) {
        joy.buttonPressed(btn, pressed)
    }

    /// The console's Pause button is wired to the Z80 NMI (Space in the C frontend).
    public func pausePressed() {
        z80.serviceNmi = 1
    }

    /// The console's Reset button (Tab in the C frontend).
    public func resetPressed(_ pressed: Bool) {
        io.resetButtonPressed(pressed)
    }

    /// systemFree: saves cart ram.
    public func free() {
        cart.dumpSram()
    }

    public func tickCpu() {
        _ = z80.clock()
    }
}
