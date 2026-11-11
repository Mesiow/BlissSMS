import SwiftUI

@main
struct BlissSMSApp: App {
    @State private var library = RomLibrary()
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            LibraryView()
                .environment(library)
                .preferredColorScheme(.light)
                // "Open in BlissSMS" from Files / AirDrop
                .onOpenURL { url in
                    Task { await library.importROMs(from: [url]) }
                }
                #if DEBUG
                // UI tests: keep refreshing the library underneath the game screen
                .task {
                    guard ProcessInfo.processInfo.arguments.contains("-BlissUITestChurn") else { return }
                    while !Task.isCancelled {
                        try? await Task.sleep(for: .milliseconds(500))
                        library.reload()
                    }
                }
                #endif
                .onChange(of: scenePhase) { _, phase in
                    // Picks up ROMs dropped into the app's folder via the Files app or Finder
                    if phase == .active { library.reload() }
                }
        }
    }
}
