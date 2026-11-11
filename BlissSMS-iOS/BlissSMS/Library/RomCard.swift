import SwiftUI
import BlissCore

/// A game tile drawn like a Master System cartridge: black label with the
/// classic white grid, and a colour panel picked from the game's title.
struct RomCard: View {
    let rom: Rom

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            CartridgeLabel(title: rom.title)
                .aspectRatio(1.05, contentMode: .fit)

            VStack(alignment: .leading, spacing: 6) {
                Text(rom.title)
                    .font(.system(.subheadline, design: .rounded).weight(.bold))
                    .foregroundStyle(.white)
                    .lineLimit(2, reservesSpace: true)
                    .multilineTextAlignment(.leading)

                HStack(spacing: 6) {
                    if let region = rom.tags.first {
                        Chip(text: region)
                    }
                    Chip(text: rom.info.sizeDescription)
                    Spacer(minLength: 0)
                    if rom.favorite {
                        Image(systemName: "star.fill")
                            .font(.caption2)
                            .foregroundStyle(Theme.sunYellow)
                    }
                }
            }
            .padding(10)
        }
        .background(Color(white: 0.07), in: RoundedRectangle(cornerRadius: 16, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 16, style: .continuous).stroke(Color.white.opacity(0.08)))
        .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
        .shadow(color: .black.opacity(0.18), radius: 10, y: 5)
    }
}

struct Chip: View {
    let text: String
    var body: some View {
        Text(text)
            .font(.system(size: 10, weight: .semibold, design: .rounded))
            .lineLimit(1)
            .foregroundStyle(.white.opacity(0.75))
            .padding(.horizontal, 7)
            .padding(.vertical, 3)
            .background(Color.white.opacity(0.09), in: Capsule())
    }
}

struct CartridgeLabel: View {
    let title: String

    private var hue: Double {
        // Stable hue per title (String.hashValue changes every launch)
        let sum = title.unicodeScalars.reduce(UInt32(7)) { ($0 &* 31) &+ $1.value }
        return Double(sum % 360) / 360
    }

    /// "Sonic The Hedgehog" -> "SH", "Alex Kidd in Miracle World" -> "AK", "R-Type" -> "RT"
    private var initials: String {
        let filler: Set<String> = ["the", "a", "an", "of", "and", "in", "on", "to", "vs", "vs.", "&"]
        let words = title
            .split(whereSeparator: { $0 == " " || $0 == "-" || $0 == ":" })
            .filter { $0.first?.isLetter == true || $0.first?.isNumber == true }
        let meaningful = words.filter { !filler.contains($0.lowercased()) }
        let picked = meaningful.isEmpty ? words : meaningful   // e.g. a title that's only filler words
        return String(picked.prefix(2).compactMap(\.first)).uppercased()
    }

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width
            ZStack(alignment: .topLeading) {
                Color.black

                // Master System grid
                GridLines(spacing: w / 12)
                    .stroke(Color.white.opacity(0.22), lineWidth: 0.6)

                // Cartridge grip ridges along the top
                HStack(spacing: w * 0.035) {
                    ForEach(0..<7, id: \.self) { _ in
                        Capsule().fill(Color.white.opacity(0.10)).frame(width: w * 0.06, height: 3)
                    }
                }
                .frame(maxWidth: .infinity)
                .padding(.top, 6)

                // "Art" panel
                RoundedRectangle(cornerRadius: 6, style: .continuous)
                    .fill(LinearGradient(
                        colors: [Color(hue: hue, saturation: 0.75, brightness: 0.95),
                                 Color(hue: (hue + 0.12).truncatingRemainder(dividingBy: 1), saturation: 0.85, brightness: 0.55)],
                        startPoint: .topLeading, endPoint: .bottomTrailing))
                    .overlay(
                        Text(initials)
                            .font(.system(size: w * 0.26, weight: .black, design: .rounded))
                            .foregroundStyle(.white.opacity(0.92))
                            .shadow(color: .black.opacity(0.35), radius: 0, x: 2, y: 2)
                    )
                    .overlay(alignment: .bottomTrailing) {
                        Image("Logo")
                            .interpolation(.none)
                            .resizable()
                            .frame(width: w * 0.14, height: w * 0.14)
                            .padding(6)
                            .opacity(0.9)
                    }
                    .frame(width: w * 0.62, height: w * 0.5)
                    .offset(x: w * 0.30, y: w * 0.24)

                // Classic "MASTER SYSTEM" side text
                Text("MASTER SYSTEM")
                    .font(.system(size: max(7, w * 0.055), weight: .heavy, design: .rounded))
                    .tracking(1)
                    .foregroundStyle(.white.opacity(0.85))
                    .rotationEffect(.degrees(-90), anchor: .topLeading)
                    .offset(x: w * 0.1, y: w * 0.88)
            }
        }
        .clipped()
    }
}

private nonisolated struct GridLines: Shape {
    let spacing: CGFloat
    func path(in rect: CGRect) -> Path {
        var p = Path()
        guard spacing > 1 else { return p }
        var x: CGFloat = 0
        while x <= rect.maxX { p.move(to: CGPoint(x: x, y: 0)); p.addLine(to: CGPoint(x: x, y: rect.maxY)); x += spacing }
        var y: CGFloat = 0
        while y <= rect.maxY { p.move(to: CGPoint(x: 0, y: y)); p.addLine(to: CGPoint(x: rect.maxX, y: y)); y += spacing }
        return p
    }
}
