import SwiftUI
import BlissCore

struct LibraryView: View {
    @Environment(RomLibrary.self) private var library
    @State private var showImporter = false
    @State private var search = ""
    @State private var selected: Rom?
    @State private var infoRom: Rom?
    @State private var pendingDelete: Rom?

    private let columns = [GridItem(.adaptive(minimum: 150, maximum: 220), spacing: 16)]

    private var filtered: [Rom] {
        guard !search.isEmpty else { return library.roms }
        return library.roms.filter {
            $0.title.localizedCaseInsensitiveContains(search) || $0.fileName.localizedCaseInsensitiveContains(search)
        }
    }

    var body: some View {
        NavigationStack {
            ZStack {
                AppBackground()

                if library.roms.isEmpty {
                    EmptyLibraryView { showImporter = true }
                } else {
                    ScrollView {
                        VStack(alignment: .leading, spacing: 24) {
                            header

                            if search.isEmpty && !library.recentlyPlayed.isEmpty {
                                recentShelf
                            }

                            sectionTitle(search.isEmpty ? "All Games" : "Results",
                                         count: filtered.count)

                            LazyVGrid(columns: columns, spacing: 16) {
                                ForEach(filtered) { rom in
                                    Button { open(rom) } label: { RomCard(rom: rom) }
                                        .buttonStyle(.plain)
                                        .contextMenu { menu(for: rom) }
                                }
                            }
                        }
                        .padding(.horizontal, 16)
                        .padding(.bottom, 32)
                    }
                    .searchable(text: $search, prompt: "Search games")
                }
            }
            .toolbar {
                ToolbarItemGroup(placement: .topBarTrailing) {
                    if !library.roms.isEmpty {
                        Menu {
                            Picker("Sort", selection: Bindable(library).sort) {
                                ForEach(RomSort.allCases) { Text($0.rawValue).tag($0) }
                            }
                        } label: {
                            Image(systemName: "arrow.up.arrow.down")
                        }
                    }
                    Button { showImporter = true } label: {
                        Image(systemName: "plus")
                    }
                    .accessibilityLabel("Import ROMs")
                }
            }
            .toolbarBackground(.hidden, for: .navigationBar)
            .navigationDestination(item: $selected) { rom in
                GameView(rom: rom)
            }
            .fileImporter(isPresented: $showImporter,
                          allowedContentTypes: RomLibrary.importTypes,
                          allowsMultipleSelection: true) { result in
                if case .success(let urls) = result {
                    Task { await library.importROMs(from: urls) }
                }
            }
            .sheet(item: $infoRom) { rom in
                RomInfoSheet(rom: rom).presentationDetents([.medium])
            }
            .alert("Import", isPresented: Binding(
                get: { library.lastImportMessage != nil },
                set: { if !$0 { library.lastImportMessage = nil } })
            ) {
                Button("OK") { library.lastImportMessage = nil }
            } message: {
                Text(library.lastImportMessage ?? "")
            }
            .confirmationDialog("Delete \(pendingDelete?.title ?? "game")?",
                                isPresented: Binding(get: { pendingDelete != nil },
                                                     set: { if !$0 { pendingDelete = nil } }),
                                titleVisibility: .visible) {
                Button("Delete", role: .destructive) {
                    if let rom = pendingDelete { library.delete(rom) }
                    pendingDelete = nil
                }
            } message: {
                Text("The ROM file is removed from BlissSMS.")
            }
        }
        .tint(Theme.sunOrange)
        #if DEBUG
        // Debug builds: launch with -BlissOpenFirstRom to jump straight into the first game
        .onAppear {
            if ProcessInfo.processInfo.arguments.contains("-BlissOpenFirstRom"), let rom = library.roms.first {
                selected = rom
            }
        }
        #endif
    }

    // MARK: Pieces

    private var header: some View {
        HStack(spacing: 14) {
            Image("Logo")
                .interpolation(.none)
                .resizable()
                .frame(width: 52, height: 52)
                .shadow(color: Theme.sunOrange.opacity(0.3), radius: 10)
            VStack(alignment: .leading, spacing: 2) {
                Text("BlissSMS")
                    .font(.system(size: 32, weight: .black, design: .rounded))
                    .foregroundStyle(Theme.titleGradient)
                Text("Sega Master System")
                    .font(.system(.subheadline, design: .rounded).weight(.medium))
                    .foregroundStyle(Theme.ink.opacity(0.6))
            }
            Spacer()
        }
        .padding(.top, 4)
    }

    private var recentShelf: some View {
        VStack(alignment: .leading, spacing: 12) {
            sectionTitle("Continue Playing", count: nil)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 14) {
                    ForEach(library.recentlyPlayed) { rom in
                        Button { open(rom) } label: {
                            RomCard(rom: rom).frame(width: 150)
                        }
                        .buttonStyle(.plain)
                        .contextMenu { menu(for: rom) }
                    }
                }
                .padding(.vertical, 4)
            }
            .scrollClipDisabled()
        }
    }

    private func sectionTitle(_ text: String, count: Int?) -> some View {
        HStack(alignment: .firstTextBaseline) {
            Text(text)
                .font(.system(.title3, design: .rounded).weight(.bold))
                .foregroundStyle(Theme.ink)
            if let count {
                Text("\(count)")
                    .font(.system(.subheadline, design: .rounded).weight(.semibold))
                    .foregroundStyle(Theme.ink.opacity(0.45))
            }
        }
    }

    @ViewBuilder
    private func menu(for rom: Rom) -> some View {
        Button { open(rom) } label: { Label("Play", systemImage: "play.fill") }
        Button { library.toggleFavorite(rom) } label: {
            Label(rom.favorite ? "Unfavorite" : "Favorite", systemImage: rom.favorite ? "star.slash" : "star")
        }
        Button { infoRom = rom } label: { Label("ROM Info", systemImage: "info.circle") }
        Divider()
        Button(role: .destructive) { pendingDelete = rom } label: { Label("Delete", systemImage: "trash") }
    }

    /// "Last played" is updated when the game closes (GameView.onDisappear), so the
    /// library doesn't re-sort underneath the game screen while it's opening.
    private func open(_ rom: Rom) {
        selected = rom
    }
}

// MARK: - Empty state

private struct EmptyLibraryView: View {
    let importAction: () -> Void
    @State private var glow = false

    var body: some View {
        VStack(spacing: 22) {
            Spacer()
            Image("Logo")
                .interpolation(.none)
                .resizable()
                .frame(width: 148, height: 148)
                .shadow(color: Theme.sunOrange.opacity(glow ? 0.45 : 0.15), radius: glow ? 28 : 14)
                .onAppear {
                    withAnimation(.easeInOut(duration: 2.4).repeatForever()) { glow = true }
                }

            VStack(spacing: 8) {
                Text("BlissSMS")
                    .font(.system(size: 40, weight: .black, design: .rounded))
                    .foregroundStyle(Theme.titleGradient)
                Text("Your Master System library is empty.")
                    .font(.system(.body, design: .rounded))
                    .foregroundStyle(Theme.ink.opacity(0.7))
            }

            Button(action: importAction) {
                Label("Import ROMs", systemImage: "square.and.arrow.down")
            }
            .buttonStyle(SunButtonStyle())
            .padding(.top, 6)

            Text("Pick .sms files from the Files app, or copy them into\nFiles › On My iPhone › BlissSMS › ROMs")
                .font(.system(.footnote, design: .rounded))
                .multilineTextAlignment(.center)
                .foregroundStyle(Theme.ink.opacity(0.45))
            Spacer()
            Spacer()
        }
        .padding(24)
    }
}

// MARK: - ROM info sheet

private struct RomInfoSheet: View {
    let rom: Rom

    var body: some View {
        NavigationStack {
            List {
                Section {
                    row("Title", rom.title)
                    if !rom.tags.isEmpty { row("Tags", rom.tags.joined(separator: " · ")) }
                    row("File", rom.fileName)
                    row("Size", rom.info.sizeDescription)
                }
                Section("Header") {
                    if rom.info.hasHeader {
                        row("Region", rom.info.region.rawValue)
                        if let code = rom.info.productCode { row("Product code", String(code)) }
                        if let v = rom.info.version { row("Version", String(v)) }
                        if let sum = rom.info.checksum { row("Checksum", String(format: "$%04X", sum)) }
                        if let o = rom.info.headerOffset { row("Header offset", String(format: "$%04X", o)) }
                    } else {
                        Text("No \"TMR SEGA\" header found. Some Japanese games don't have one.")
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .navigationTitle("ROM Info")
            .navigationBarTitleDisplayMode(.inline)
        }
    }

    private func row(_ label: String, _ value: String) -> some View {
        LabeledContent(label) {
            Text(value).font(Theme.mono(14)).multilineTextAlignment(.trailing)
        }
    }
}
