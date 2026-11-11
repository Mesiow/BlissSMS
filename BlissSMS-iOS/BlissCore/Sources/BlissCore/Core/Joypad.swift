// Swift port of BlissSMS/Core/Joypad.c + Joypad.h

public let BIT_UP: UInt8 = 1 << 0
public let BIT_DOWN: UInt8 = 1 << 1
public let BIT_LEFT: UInt8 = 1 << 2
public let BIT_RIGHT: UInt8 = 1 << 3
public let BIT_A: UInt8 = 1 << 4
public let BIT_B: UInt8 = 1 << 5

public final class Joypad {
    /// enum Button. In C these were tied to SFML key codes; here they're just the pad's
    /// buttons (A = button 1, B = button 2). Nested so it doesn't clash with SwiftUI.Button.
    public enum Button: CaseIterable, Sendable {
        case up, down, left, right, a, b
    }

    public var joypadPort: UInt8 = 0xFF
    public var joypadTemp: UInt8 = 0xFF

    public var joypadPort2: UInt8 = 0xFF

    /// joypadInit
    public init() {
        joypadPort = 0xFF
        joypadPort2 = 0xFF
        joypadTemp = 0xFF
    }

    /// joypadUpdate
    public func update() {
        // update the joypad port
        joypadPort = joypadTemp
    }

    /// joypadButtonPressed. 0 = pressed, 1 = released.
    /// (The C version toggles the bit with ^= on release; this sets it, which gives
    /// the same result but can't get stuck if a touch release arrives twice.)
    public func buttonPressed(_ btn: Button, _ pressed: Bool) {
        let bit: UInt8
        switch btn {
        case .up: bit = BIT_UP
        case .down: bit = BIT_DOWN
        case .left: bit = BIT_LEFT
        case .right: bit = BIT_RIGHT
        case .a: bit = BIT_A
        case .b: bit = BIT_B
        }

        if pressed {
            joypadTemp &= ~bit
        } else {
            joypadTemp |= bit
        }
    }
}
