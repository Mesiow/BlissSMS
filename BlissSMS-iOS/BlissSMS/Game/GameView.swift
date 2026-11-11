import SwiftUI
import BlissCore

/// The play screen. Owns the emulator for as long as the screen is on display.
///
/// The session is created in onAppear rather than in init: SwiftUI can rebuild this view
/// many times (e.g. when the library behind it updates), and creating it in init both
/// booted a new System each time and could leave the controls wired to a session that
/// wasn't the one running.
struct GameView: View {
    let rom: Rom
    @Environment(RomLibrary.self) private var library
    @Environment(\.scenePhase) private var scenePhase
    @State private var session: EmulatorSession?

    var body: some View {
        ZStack {
            AppBackground()
            if let session {
                GameScreen(rom: rom, session: session)
            }
        }
        .toolbar(.hidden, for: .navigationBar)
        .statusBarHidden(true)
        .persistentSystemOverlays(.hidden)
        .onAppear {
            if session == nil {
                session = EmulatorSession(romPath: rom.url.path)
            }
            session?.start()
        }
        .onDisappear {
            session?.stop() // also saves cart ram
            library.markPlayed(rom)
        }
        .onChange(of: scenePhase) { _, phase in
            if phase == .active {
                session?.start()
            } else {
                session?.stop()
            }
        }
    }
}

/// Video on top, Master System pad underneath (or either side in landscape).
private struct GameScreen: View {
    let rom: Rom
    let session: EmulatorSession
    @Environment(\.dismiss) private var dismiss
    @AppStorage("showFPS") private var showFPS = false
    @AppStorage("soundOn") private var soundOn = true
    @State private var showStates = false

    var body: some View {
        GeometryReader { geo in
            let landscape = geo.size.width > geo.size.height
            if let error = session.loadError {
                VStack(spacing: 16) {
                    topBar
                    Spacer()
                    ContentUnavailableView("Couldn't start game",
                                           systemImage: "exclamationmark.triangle",
                                           description: Text(error))
                    Spacer()
                }
            } else if landscape {
                landscapeLayout(geo.size)
            } else {
                portraitLayout(geo.size)
            }
        }
        #if DEBUG
        .overlay(alignment: .bottom) {
            if ProcessInfo.processInfo.arguments.contains("-BlissUITest") {
                Text(String(format: "touch=%02X port=%02X", session.debugTouchLatch, session.debugPortLatch))
                    .accessibilityValue("ev=\(session.debugTouchEvents) fr=\(session.debugFrames)")
                    .font(.caption2)
                    .accessibilityIdentifier("inputDebug")
            }
        }
        #endif
        .onAppear { session.soundEnabled = soundOn }
        .onChange(of: soundOn) { _, on in session.soundEnabled = on }
        .sheet(isPresented: $showStates) {
            SaveStatesSheet(session: session)
        }
    }

    // MARK: Layouts

    private func portraitLayout(_ size: CGSize) -> some View {
        VStack(spacing: 0) {
            topBar
            screen
                .padding(.horizontal, 4)
                .padding(.top, 8)
            Spacer(minLength: 16)
            HStack(spacing: 12) {
                pauseButton
                ConsoleButton(title: "Reset") { session.setTouchReset($0) }
            }
            Spacer(minLength: 16)
            HStack(alignment: .center) {
                DPad(session: session, size: min(170, size.width * 0.42))
                Spacer()
                actionButtons(size: min(78, size.width * 0.19))
            }
            .padding(.horizontal, 24)
            .padding(.bottom, 36)
        }
    }

    private func landscapeLayout(_ size: CGSize) -> some View {
        // Fixed-width side columns so the pad never spills onto the screen;
        // the screen takes whatever is left in the middle.
        let side = min(220, max(170, size.width * 0.2))
        let buttonSize = min(72, (side - 16) / 2.3)   // 1 + 2 together are 2.3x a button wide
        return HStack(spacing: 14) {
            VStack {
                HStack { backButton; Spacer() }
                Spacer()
                DPad(session: session, size: min(side - 16, size.height * 0.48))
                Spacer()
                pauseButton
            }
            .frame(width: side)

            screen
                .padding(.vertical, 2)
                .frame(maxWidth: .infinity, maxHeight: .infinity)

            VStack {
                HStack { Spacer(); menu }
                Spacer()
                actionButtons(size: buttonSize)
                Spacer()
                ConsoleButton(title: "Reset") { session.setTouchReset($0) }
            }
            .frame(width: side)
        }
        .padding(8)
    }

    // MARK: Pieces

    private var topBar: some View {
        HStack {
            backButton
            Spacer()
            Text(rom.title)
                .font(.system(.headline, design: .rounded).weight(.bold))
                .foregroundStyle(Theme.ink)
                .lineLimit(1)
            Spacer()
            menu
        }
        .padding(.horizontal, 12)
        .padding(.top, 8)
    }

    /// Pauses the emulator (not the console's NMI button, whose effect the app can't see),
    /// so the button can show RESUME and the screen can say "Paused".
    private var pauseButton: some View {
        ConsoleButton(title: session.isPaused ? "Resume" : "Pause", active: session.isPaused) { down in
            if down { session.togglePause() }
        }
    }

    private var backButton: some View {
        Button {
            session.stop()
            dismiss()
        } label: {
            Image(systemName: "chevron.left")
                .font(.system(size: 17, weight: .bold))
                .frame(width: 40, height: 40)
                .background(Color.black.opacity(0.05), in: Circle())
        }
        .foregroundStyle(Theme.ink)
        .accessibilityLabel("Back to library")
    }

    private var menu: some View {
        Menu {
            Section {
                Button {
                    session.saveState(to: 0)
                } label: {
                    Label("Quick Save", systemImage: "square.and.arrow.down")
                }
                Button {
                    session.loadState(from: 0)
                } label: {
                    Label("Quick Load", systemImage: "square.and.arrow.up")
                }
                .disabled(!session.hasQuickSave)
                Button {
                    showStates = true
                } label: {
                    Label("Save States…", systemImage: "square.stack.3d.up")
                }
            }
            Button {
                session.togglePause()
            } label: {
                Label(session.isPaused ? "Resume" : "Pause Emulation",
                      systemImage: session.isPaused ? "play.fill" : "pause.fill")
            }
            Button {
                session.restart()
            } label: {
                Label("Restart Game", systemImage: "arrow.counterclockwise")
            }
            Toggle(isOn: $soundOn) { Label("Sound", systemImage: soundOn ? "speaker.wave.2.fill" : "speaker.slash.fill") }
            Toggle(isOn: $showFPS) { Label("Show FPS", systemImage: "speedometer") }
        } label: {
            Image(systemName: "ellipsis")
                .font(.system(size: 17, weight: .bold))
                .frame(width: 40, height: 40)
                .background(Color.black.opacity(0.05), in: Circle())
        }
        .foregroundStyle(Theme.ink)
        .accessibilityLabel("Game menu")
        .accessibilityIdentifier("gameMenu")
    }

    private var screen: some View {
        ScreenView(session: session)
            .aspectRatio(CGFloat(DISPLAY_WIDTH) / CGFloat(DISPLAY_HEIGHT), contentMode: .fit)
            .clipShape(RoundedRectangle(cornerRadius: 6, style: .continuous))
            .padding(5)
            .background(Color.black, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
            .overlay(alignment: .topTrailing) {
                if showFPS {
                    Text(String(format: "%.0f FPS", session.fps))
                        .font(Theme.mono(11, .bold))
                        .foregroundStyle(.green)
                        .padding(6)
                        .background(.black.opacity(0.6), in: RoundedRectangle(cornerRadius: 6))
                        .padding(16)
                }
            }
            .overlay(alignment: .top) {
                if let toast = session.toast {
                    Text(toast)
                        .font(.system(.subheadline, design: .rounded).weight(.semibold))
                        .foregroundStyle(.white)
                        .padding(.horizontal, 14)
                        .padding(.vertical, 8)
                        .background(.black.opacity(0.75), in: Capsule())
                        .padding(.top, 14)
                        .transition(.move(edge: .top).combined(with: .opacity))
                }
            }
            .animation(.spring(duration: 0.3), value: session.toast)
            .overlay {
                if let fault = session.fault {
                    overlayMessage(icon: "exclamationmark.octagon.fill", title: "CPU stopped", detail: fault)
                } else if session.isPaused {
                    overlayMessage(icon: "pause.fill", title: "Paused", detail: nil)
                }
            }
            .shadow(color: .black.opacity(0.2), radius: 16, y: 8)
    }

    private func overlayMessage(icon: String, title: String, detail: String?) -> some View {
        VStack(spacing: 6) {
            Image(systemName: icon).font(.title2)
            Text(title).font(.system(.headline, design: .rounded))
            if let detail {
                Text(detail).font(Theme.mono(11)).multilineTextAlignment(.center).opacity(0.8)
            }
        }
        .foregroundStyle(.white)
        .padding(16)
        .background(.black.opacity(0.7), in: RoundedRectangle(cornerRadius: 12))
    }

    private func actionButtons(size: CGFloat) -> some View {
        // Laid out like the real pad: 1 lower-left, 2 upper-right
        HStack(alignment: .center, spacing: size * 0.3) {
            PadButton(label: "1", button: .a, session: session, size: size)
                .offset(y: size * 0.25)
            PadButton(label: "2", button: .b, session: session, size: size)
                .offset(y: -size * 0.25)
        }
    }
}
