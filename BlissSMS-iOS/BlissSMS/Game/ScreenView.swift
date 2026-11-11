import SwiftUI
import UIKit
import BlissCore

/// Shows the VDP framebuffer (256x192) scaled up with sharp pixels.
/// This replaces vdpDisplayGraphics / the SFML sprite in the C frontend.
final class PixelScreenUIView: UIView {
    private let colorSpace = CGColorSpaceCreateDeviceRGB()

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .black
        layer.magnificationFilter = .nearest
        layer.contentsGravity = .resize
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    func display(_ framebuffer: [UInt32]) {
        guard let image = makeFrameImage(framebuffer, colorSpace: colorSpace) else { return }

        CATransaction.begin()
        CATransaction.setDisableActions(true)
        layer.contents = image
        CATransaction.commit()
    }
}

/// Turns a VDP framebuffer into a 256x192 image (also used for save state thumbnails).
func makeFrameImage(_ framebuffer: [UInt32], colorSpace: CGColorSpace = CGColorSpaceCreateDeviceRGB()) -> CGImage? {
    let data = framebuffer.withUnsafeBufferPointer { Data(buffer: $0) }
    guard let provider = CGDataProvider(data: data as CFData) else { return nil }
    return CGImage(
        width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT,
        bitsPerComponent: 8, bitsPerPixel: 32,
        bytesPerRow: DISPLAY_WIDTH * 4,
        space: colorSpace,
        // Pixels are stored as bytes R, G, B, A
        bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
        provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
}

struct ScreenView: UIViewRepresentable {
    let session: EmulatorSession

    func makeUIView(context: Context) -> PixelScreenUIView {
        let view = PixelScreenUIView()
        session.screen = view
        if let system = session.system { view.display(system.vdp.framebuffer) }
        return view
    }

    func updateUIView(_ view: PixelScreenUIView, context: Context) {
        session.screen = view
    }
}
