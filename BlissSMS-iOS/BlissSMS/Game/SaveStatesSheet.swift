import SwiftUI

/// Quick save + 4 slots, each with a screenshot from the moment it was saved.
/// The game is paused while this is open.
struct SaveStatesSheet: View {
    let session: EmulatorSession
    @Environment(\.dismiss) private var dismiss
    @State private var slots: [SaveSlot] = []
    @State private var overwrite: SaveSlot?

    var body: some View {
        NavigationStack {
            List {
                ForEach(slots) { slot in
                    row(slot)
                        .swipeActions {
                            if !slot.isEmpty {
                                Button(role: .destructive) {
                                    session.deleteState(slot.index)
                                    reload()
                                } label: { Label("Delete", systemImage: "trash") }
                            }
                        }
                }
            }
            .navigationTitle("Save States")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } }
            }
            .confirmationDialog("Overwrite \(overwrite?.title ?? "slot")?",
                                isPresented: Binding(get: { overwrite != nil }, set: { if !$0 { overwrite = nil } }),
                                titleVisibility: .visible) {
                Button("Overwrite", role: .destructive) {
                    if let slot = overwrite { save(slot.index) }
                }
            } message: {
                Text("The save in this slot will be replaced.")
            }
        }
        .tint(Theme.sunOrange)
        .onAppear {
            reload()
            session.stop()   // freeze the game while picking
        }
        .onDisappear { session.start() }
    }

    private func row(_ slot: SaveSlot) -> some View {
        HStack(spacing: 14) {
            ZStack {
                Color.black
                if let image = slot.thumbnail {
                    Image(uiImage: image).interpolation(.none).resizable()
                } else {
                    Image(systemName: "square.dashed").foregroundStyle(.white.opacity(0.3))
                }
            }
            .frame(width: 96, height: 72)
            .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))

            VStack(alignment: .leading, spacing: 4) {
                Text(slot.title)
                    .font(.system(.headline, design: .rounded))
                    .foregroundStyle(Theme.ink)
                if let date = slot.date {
                    Text(date, format: .relative(presentation: .named))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                } else {
                    Text("Empty").font(.caption).foregroundStyle(.secondary)
                }
            }

            Spacer()

            VStack(spacing: 6) {
                Button("Save") {
                    if slot.isEmpty { save(slot.index) } else { overwrite = slot }
                }
                .buttonStyle(.bordered)
                Button("Load") {
                    session.loadState(from: slot.index)
                    dismiss()
                }
                .buttonStyle(.borderedProminent)
                .disabled(slot.isEmpty)
            }
            .font(.system(.subheadline, design: .rounded).weight(.semibold))
            .controlSize(.small)
        }
        .padding(.vertical, 4)
    }

    private func save(_ slot: Int) {
        session.saveState(to: slot)
        reload()
    }

    private func reload() { slots = session.saveSlots() }
}
