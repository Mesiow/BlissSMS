import UIKit

/// One save state slot on disk.
struct SaveSlot: Identifiable, Equatable {
    let index: Int            // 0 = quick save, 1...4 = slots
    var date: Date?
    var thumbnail: UIImage?

    var id: Int { index }
    var isEmpty: Bool { date == nil }
    var title: String { index == 0 ? "Quick Save" : "Slot \(index)" }
}

/// Save states live in Documents/States/<game>/slotN.state with a slotN.png screenshot,
/// so they show up in the Files app and get backed up with the device.
struct SaveStateStore {
    static let slotCount = 5 // quick + 4

    let directory: URL

    init(romURL: URL) {
        let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let game = romURL.deletingPathExtension().lastPathComponent
        directory = docs.appendingPathComponent("States", isDirectory: true)
            .appendingPathComponent(game, isDirectory: true)
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    }

    private func stateURL(_ slot: Int) -> URL { directory.appendingPathComponent("slot\(slot).state") }
    private func imageURL(_ slot: Int) -> URL { directory.appendingPathComponent("slot\(slot).png") }

    func slots() -> [SaveSlot] {
        (0..<Self.slotCount).map { slot($0) }
    }

    func slot(_ index: Int) -> SaveSlot {
        let values = try? stateURL(index).resourceValues(forKeys: [.contentModificationDateKey])
        let image = (try? Data(contentsOf: imageURL(index))).flatMap(UIImage.init(data:))
        return SaveSlot(index: index, date: values?.contentModificationDate, thumbnail: image)
    }

    func write(_ state: Data, thumbnail: CGImage?, to slot: Int) throws {
        try state.write(to: stateURL(slot), options: .atomic)
        if let thumbnail, let png = UIImage(cgImage: thumbnail).pngData() {
            try? png.write(to: imageURL(slot), options: .atomic)
        }
    }

    func read(_ slot: Int) -> Data? {
        try? Data(contentsOf: stateURL(slot))
    }

    func delete(_ slot: Int) {
        try? FileManager.default.removeItem(at: stateURL(slot))
        try? FileManager.default.removeItem(at: imageURL(slot))
    }
}
