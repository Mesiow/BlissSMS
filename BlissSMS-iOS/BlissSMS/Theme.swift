import SwiftUI

/// Colours taken from the pixel-art sunset icon.
enum Theme {
    static let night = Color(red: 0.05, green: 0.04, blue: 0.12)
    static let dusk = Color(red: 0.12, green: 0.07, blue: 0.25)
    static let plum = Color(red: 0.34, green: 0.12, blue: 0.43)
    static let ink = Color(red: 0.11, green: 0.08, blue: 0.14)          // main text
    static let panel = Color.black.opacity(0.035)
    static let panelStroke = Color.black.opacity(0.07)

    static let sunYellow = Color(red: 1.00, green: 0.88, blue: 0.10)
    static let sunAmber = Color(red: 1.00, green: 0.75, blue: 0.04)
    static let sunOrange = Color(red: 1.00, green: 0.58, blue: 0.00)
    static let sunRed = Color(red: 0.95, green: 0.13, blue: 0.23)

    static let sunGradient = LinearGradient(
        colors: [sunYellow, sunAmber, sunOrange, sunRed],
        startPoint: .top, endPoint: .bottom
    )

    /// Sun colours without the pale yellow, so titles stay readable on white.
    static let titleGradient = LinearGradient(
        colors: [sunAmber, sunOrange, sunRed],
        startPoint: .top, endPoint: .bottom
    )

    static func mono(_ size: CGFloat, _ weight: Font.Weight = .regular) -> Font {
        .system(size: size, weight: weight, design: .monospaced)
    }
}

/// Plain white background, matching the app icon.
struct AppBackground: View {
    var body: some View {
        Color.white.ignoresSafeArea()
    }
}

/// Rounded translucent panel used across the app.
struct PanelStyle: ViewModifier {
    var padding: CGFloat = 14
    func body(content: Content) -> some View {
        content
            .padding(padding)
            .background(Theme.panel, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 18, style: .continuous).stroke(Theme.panelStroke))
    }
}

extension View {
    func panel(padding: CGFloat = 14) -> some View { modifier(PanelStyle(padding: padding)) }
}

/// Chunky sunset-gradient button.
struct SunButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(.headline, design: .rounded).weight(.bold))
            .foregroundStyle(Theme.night)
            .padding(.horizontal, 22)
            .padding(.vertical, 13)
            .background(Theme.sunGradient, in: Capsule())
            .shadow(color: Theme.sunOrange.opacity(0.45), radius: configuration.isPressed ? 4 : 14, y: 4)
            .scaleEffect(configuration.isPressed ? 0.96 : 1)
            .animation(.spring(duration: 0.2), value: configuration.isPressed)
    }
}
