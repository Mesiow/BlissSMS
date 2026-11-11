import Foundation
import QuartzCore
import GameController
import Observation
import BlissCore

/// Runs a `System` once per display refresh (the C main loop) and feeds it input from
/// the touch controls, game controllers and a hardware keyboard.
@MainActor @Observable
final class EmulatorSession {
    private(set) var loadError: String?
    private(set) var fault: String?
    private(set) var isPaused = false
    private(set) var fps = 0.0
    /// Short message shown over the screen ("Saved to Slot 1").
    private(set) var toast: String?
    private(set) var hasQuickSave = false

    var soundEnabled = true {
        didSet { audio.isMuted = !soundEnabled }
    }

    #if DEBUG
    /// UI tests: every joypad bit seen pressed, at the touch layer and at the emulated port.
    private(set) var debugTouchLatch: UInt8 = 0
    private(set) var debugPortLatch: UInt8 = 0
    private(set) var debugTouchEvents = 0
    @ObservationIgnored private(set) var debugFrames = 0
    #endif

    @ObservationIgnored private(set) var system: System?
    @ObservationIgnored weak var screen: PixelScreenUIView?

    @ObservationIgnored private let romPath: String
    @ObservationIgnored private let audio = AudioOutput()
    @ObservationIgnored private let states: SaveStateStore
    @ObservationIgnored private var toastTask: Task<Void, Never>?
    @ObservationIgnored private var link: CADisplayLink?
    @ObservationIgnored private var linkTarget: DisplayLinkTarget?

    // Input
    @ObservationIgnored private var touchButtons: Set<Joypad.Button> = []
    @ObservationIgnored private var heldButtons: Set<Joypad.Button> = []
    @ObservationIgnored private var touchReset = false
    @ObservationIgnored private var resetHeld = false
    @ObservationIgnored private var controllerPauseWasDown = false

    // FPS counter
    @ObservationIgnored private var framesSinceSample = 0
    @ObservationIgnored private var sampleStart = CACurrentMediaTime()

    init(romPath: String) {
        self.romPath = romPath
        states = SaveStateStore(romURL: URL(fileURLWithPath: romPath))
        hasQuickSave = !states.slot(0).isEmpty
        boot()
    }

    private func boot() {
        do {
            system = try System(cartPath: romPath)
            system?.psg.sampleRate = audio.sampleRate
            loadError = nil
            fault = nil
        } catch {
            system = nil
            loadError = String(describing: error)
        }
    }

    // MARK: Running

    func start() {
        guard link == nil, system != nil, !isPaused else { return }
        let target = DisplayLinkTarget { [weak self] in self?.frame() }
        let link = CADisplayLink(target: target, selector: #selector(DisplayLinkTarget.tick))
        link.preferredFrameRateRange = CAFrameRateRange(minimum: 60, maximum: 60, preferred: 60)
        link.add(to: .main, forMode: .common)
        self.link = link
        linkTarget = target
        audio.start()
        sampleStart = CACurrentMediaTime()
        framesSinceSample = 0
    }

    func stop() {
        link?.invalidate()
        link = nil
        linkTarget = nil
        audio.stop()
        save()
    }

    func togglePause() {
        isPaused.toggle()
        isPaused ? stop() : start()
    }

    /// Saves cart ram to the .sav file (systemFree in C).
    func save() {
        system?.free()
    }

    /// Power-cycles the console: saves, then reloads the cart.
    func restart() {
        let wasRunning = link != nil
        stop()
        boot()
        heldButtons = []
        if let system { screen?.display(system.vdp.framebuffer) }
        if wasRunning || !isPaused { start() }
    }

    private func frame() {
        guard let system else { return }

        syncInput(system)
        system.runEmulation()
        audio.push(system.psg.takeSamples())
        #if DEBUG
        debugFrames += 1
        #endif
        screen?.display(system.vdp.framebuffer)

        if let f = system.z80.fault {
            fault = f.description
            stop()
            return
        }

        framesSinceSample += 1
        let now = CACurrentMediaTime()
        if now - sampleStart >= 0.5 {
            fps = Double(framesSinceSample) / (now - sampleStart)
            framesSinceSample = 0
            sampleStart = now
        }
    }

    // MARK: Save states

    func saveSlots() -> [SaveSlot] { states.slots() }

    func saveState(to slot: Int) {
        guard let system else { return }
        do {
            let image = makeFrameImage(system.vdp.framebuffer)
            try states.write(system.saveState(), thumbnail: image, to: slot)
            if slot == 0 { hasQuickSave = true }
            showToast("Saved to \(SaveSlot(index: slot).title)")
        } catch {
            showToast("Couldn't save: \(error.localizedDescription)")
        }
    }

    func loadState(from slot: Int) {
        guard let system, let data = states.read(slot) else { return }
        do {
            try system.loadState(data)
            heldButtons = []
            fault = nil
            screen?.display(system.vdp.framebuffer)
            showToast("Loaded \(SaveSlot(index: slot).title)")
        } catch {
            showToast(String(describing: error))
        }
    }

    func deleteState(_ slot: Int) {
        states.delete(slot)
        if slot == 0 { hasQuickSave = false }
    }

    private func showToast(_ text: String) {
        toast = text
        toastTask?.cancel()
        toastTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(2.5))
            guard !Task.isCancelled else { return }
            self?.toast = nil
        }
    }

    // MARK: Input

    /// Touch controls call this.
    func setTouch(_ button: Joypad.Button, _ pressed: Bool) {
        #if DEBUG
        debugTouchEvents += 1
        #endif
        if pressed { touchButtons.insert(button) } else { touchButtons.remove(button) }
    }

    func setTouchDirections(_ directions: Set<Joypad.Button>) {
        #if DEBUG
        debugTouchEvents += 1
        #endif
        touchButtons.subtract([.up, .down, .left, .right])
        touchButtons.formUnion(directions)
    }

    func setTouchReset(_ pressed: Bool) { touchReset = pressed }


    /// Merges touch, controller and keyboard state and sends changes to the joypad.
    private func syncInput(_ system: System) {
        var pressed = touchButtons
        var reset = touchReset
        var pauseDown = false

        if let pad = GCController.current?.extendedGamepad {
            if pad.dpad.up.isPressed || pad.leftThumbstick.up.isPressed { pressed.insert(.up) }
            if pad.dpad.down.isPressed || pad.leftThumbstick.down.isPressed { pressed.insert(.down) }
            if pad.dpad.left.isPressed || pad.leftThumbstick.left.isPressed { pressed.insert(.left) }
            if pad.dpad.right.isPressed || pad.leftThumbstick.right.isPressed { pressed.insert(.right) }
            if pad.buttonA.isPressed { pressed.insert(.a) }   // button 1
            if pad.buttonB.isPressed { pressed.insert(.b) }   // button 2
            if pad.buttonMenu.isPressed { pauseDown = true }
        }

        // Same keys as the SFML frontend: arrows, A = 1, S = 2, Space = Pause, Tab = Reset
        if let keys = GCKeyboard.coalesced?.keyboardInput {
            if keys.button(forKeyCode: .upArrow)?.isPressed == true { pressed.insert(.up) }
            if keys.button(forKeyCode: .downArrow)?.isPressed == true { pressed.insert(.down) }
            if keys.button(forKeyCode: .leftArrow)?.isPressed == true { pressed.insert(.left) }
            if keys.button(forKeyCode: .rightArrow)?.isPressed == true { pressed.insert(.right) }
            if keys.button(forKeyCode: .keyA)?.isPressed == true { pressed.insert(.a) }
            if keys.button(forKeyCode: .keyS)?.isPressed == true { pressed.insert(.b) }
            if keys.button(forKeyCode: .spacebar)?.isPressed == true { pauseDown = true }
            if keys.button(forKeyCode: .tab)?.isPressed == true { reset = true }
        }

        for button in Joypad.Button.allCases where pressed.contains(button) != heldButtons.contains(button) {
            system.buttonPressed(button, pressed.contains(button))
        }
        heldButtons = pressed

        #if DEBUG
        let touchBits = pressed.reduce(UInt8(0)) { $0 | Self.bit(for: $1) }
        if touchBits & ~debugTouchLatch != 0 { debugTouchLatch |= touchBits }
        let portBits = ~system.joy.joypadTemp & 0x3F
        if portBits & ~debugPortLatch != 0 { debugPortLatch |= portBits }
        #endif

        if reset != resetHeld {
            system.resetPressed(reset)
            resetHeld = reset
        }

        // Pause fires once per press
        if pauseDown && !controllerPauseWasDown { system.pausePressed() }
        controllerPauseWasDown = pauseDown
    }
}

#if DEBUG
extension EmulatorSession {
    static func bit(for button: Joypad.Button) -> UInt8 {
        switch button {
        case .up: BIT_UP
        case .down: BIT_DOWN
        case .left: BIT_LEFT
        case .right: BIT_RIGHT
        case .a: BIT_A
        case .b: BIT_B
        }
    }
}
#endif

/// CADisplayLink needs an NSObject target.
private final class DisplayLinkTarget: NSObject {
    let callback: () -> Void
    init(_ callback: @escaping () -> Void) { self.callback = callback }
    @objc func tick() { callback() }
}
