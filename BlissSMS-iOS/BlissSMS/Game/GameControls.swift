import SwiftUI
import UIKit
import BlissCore

/// On-screen Master System pad: D-pad, buttons 1 and 2, plus the console's Pause / Reset.

// MARK: - D-pad

/// One touch area for all four directions so you can slide between them (and press diagonals).
struct DPad: View {
    let session: EmulatorSession
    var size: CGFloat = 150

    @State private var active: Set<Joypad.Button> = []

    var body: some View {
        ZStack {
            Circle()
                .fill(Color.black.opacity(0.05))
            // Cross
            ForEach(Array(arms.enumerated()), id: \.offset) { _, arm in
                RoundedRectangle(cornerRadius: size * 0.06, style: .continuous)
                    .fill(active.contains(arm.button) ? AnyShapeStyle(Theme.sunGradient) : AnyShapeStyle(Theme.ink))
                    .frame(width: size * 0.32, height: size * 0.36)
                    .overlay(
                        Image(systemName: "arrowtriangle.up.fill")
                            .font(.system(size: size * 0.09))
                            .foregroundStyle(.white.opacity(0.35))
                            .offset(y: -size * 0.04)
                    )
                    .rotationEffect(arm.rotation)
                    .offset(arm.offset(size * 0.2))
            }
            RoundedRectangle(cornerRadius: 4)
                .fill(Theme.ink)
                .frame(width: size * 0.32, height: size * 0.32)
            Circle()
                .fill(Color.white.opacity(0.08))
                .frame(width: size * 0.16)
        }
        .frame(width: size, height: size)
        .overlay(TouchArea { point in update(directionsAt: point) })
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("Direction pad")
        .accessibilityIdentifier("dpad")
    }

    private struct Arm {
        let button: Joypad.Button
        let rotation: Angle
        let dx: CGFloat
        let dy: CGFloat
        func offset(_ d: CGFloat) -> CGSize { CGSize(width: dx * d, height: dy * d) }
    }

    private var arms: [Arm] {
        [Arm(button: .up, rotation: .degrees(0), dx: 0, dy: -1),
         Arm(button: .right, rotation: .degrees(90), dx: 1, dy: 0),
         Arm(button: .down, rotation: .degrees(180), dx: 0, dy: 1),
         Arm(button: .left, rotation: .degrees(270), dx: -1, dy: 0)]
    }

    private func update(directionsAt point: CGPoint?) {
        var next: Set<Joypad.Button> = []
        if let point {
            let dx = point.x - size / 2
            let dy = point.y - size / 2
            let deadZone = size * 0.1
            if hypot(dx, dy) > deadZone {
                // 8-way: a direction counts if the touch is within 67.5° of it
                let angle = atan2(-dy, dx) // 0 = right, counter-clockwise
                let deg = angle * 180 / .pi
                if deg > 22.5 && deg < 157.5 { next.insert(.up) }
                if deg < -22.5 && deg > -157.5 { next.insert(.down) }
                if abs(deg) < 67.5 { next.insert(.right) }
                if abs(deg) > 112.5 { next.insert(.left) }
            }
        }
        guard next != active else { return }
        if next.count > active.count { Haptics.tap() }
        active = next
        session.setTouchDirections(next)
    }
}

// MARK: - Buttons 1 / 2

struct PadButton: View {
    let label: String
    let button: Joypad.Button
    let session: EmulatorSession
    var size: CGFloat = 70

    @State private var pressed = false

    var body: some View {
        Circle()
            .fill(pressed ? AnyShapeStyle(Theme.sunOrange) : AnyShapeStyle(Theme.sunGradient))
            .overlay(
                Text(label)
                    .font(.system(size: size * 0.36, weight: .black, design: .rounded))
                    .foregroundStyle(Theme.night.opacity(0.85))
            )
            .frame(width: size, height: size)
            .shadow(color: Theme.sunOrange.opacity(pressed ? 0.2 : 0.4), radius: pressed ? 3 : 10, y: pressed ? 1 : 4)
            .scaleEffect(pressed ? 0.93 : 1)
            .animation(.spring(duration: 0.12), value: pressed)
            // Touch area stays full size (outside the scale effect) and a bit larger than the circle
            .overlay(TouchArea { point in setPressed(point != nil) }.padding(-size * 0.12))
            .accessibilityElement(children: .ignore)
            .accessibilityLabel("Button \(label)")
            .accessibilityIdentifier("button\(label)")
    }

    private func setPressed(_ value: Bool) {
        guard value != pressed else { return }
        pressed = value
        if value { Haptics.tap() }
        session.setTouch(button, value)
    }
}

// MARK: - Console buttons (Pause / Reset)

struct ConsoleButton: View {
    let title: String
    var active = false           // lit up, e.g. while paused
    let action: (Bool) -> Void   // true on press, false on release

    @State private var pressed = false

    var body: some View {
        Text(title.uppercased())
            .font(.system(size: 11, weight: .heavy, design: .rounded))
            .tracking(1)
            .foregroundStyle(active ? Theme.night : Theme.ink.opacity(pressed ? 1 : 0.6))
            .padding(.horizontal, 14)
            .padding(.vertical, 8)
            .background {
                if active {
                    Capsule().fill(Theme.sunGradient)
                } else {
                    Capsule().fill(Color.black.opacity(pressed ? 0.12 : 0.06))
                }
            }
            .animation(.easeOut(duration: 0.15), value: active)
            .overlay(TouchArea { point in
                let down = point != nil
                guard down != pressed else { return }
                pressed = down
                if down { Haptics.tap() }
                action(down)
            })
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(title)
            .accessibilityAddTraits(.isButton)
    }
}

// MARK: - Touch handling

/// Raw UIKit touches for the pad. SwiftUI's DragGesture is built for dragging and on a real
/// device didn't always report a press until the finger moved; UIKit reports touch-down
/// immediately and handles several fingers at once (e.g. D-pad + button).
///
/// `onTouch` gets the touch location while a finger is down (it follows the finger),
/// and nil once every finger has lifted.
struct TouchArea: UIViewRepresentable {
    let onTouch: (CGPoint?) -> Void

    func makeUIView(context: Context) -> TouchAreaView {
        let view = TouchAreaView()
        view.onTouch = onTouch
        return view
    }

    func updateUIView(_ view: TouchAreaView, context: Context) {
        view.onTouch = onTouch
    }
}

final class TouchAreaView: UIView {
    var onTouch: ((CGPoint?) -> Void)?
    private var touches: [UITouch] = []

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear
        isMultipleTouchEnabled = true
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        self.touches.append(contentsOf: touches)
        report()
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        report()
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        self.touches.removeAll { touches.contains($0) }
        report()
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        self.touches.removeAll { touches.contains($0) }
        report()
    }

    /// Uses the most recent finger still down.
    private func report() {
        onTouch?(touches.last?.location(in: self))
    }
}

enum Haptics {
    private static let generator = UIImpactFeedbackGenerator(style: .light)
    static func tap() { generator.impactOccurred(intensity: 0.7) }
}
