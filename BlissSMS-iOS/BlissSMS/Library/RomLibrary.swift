import Foundation
import Observation
import UniformTypeIdentifiers
import BlissCore

/// A ROM stored in the app's Documents/ROMs folder.
struct Rom: Identifiable, Hashable {
    var id: String { fileName }
    let fileName: String
    let url: URL
    let title: String
    let tags: [String]        // e.g. ["USA, Europe", "Rev 1"] from the file name
    let info: RomInfo
    let addedAt: Date
    var lastPlayed: Date?
    var favorite: Bool

    static func == (a: Rom, b: Rom) -> Bool {
        a.fileName == b.fileName && a.lastPlayed == b.lastPlayed && a.favorite == b.favorite
    }
    func hash(into h: inout Hasher) { h.combine(fileName) }

    /// "Sonic The Hedgehog (USA, Europe) (Rev 1).sms" -> ("Sonic The Hedgehog", ["USA, Europe", "Rev 1"])
    static func parseName(_ fileName: String) -> (title: String, tags: [String]) {
        let base = (fileName as NSString).deletingPathExtension
        var title = base
        var tags: [String] = []
        if let cut = base.firstIndex(where: { $0 == "(" || $0 == "[" }) {
            title = String(base[..<cut]).trimmingCharacters(in: .whitespaces)
            var current = ""
            var depth = 0
            for ch in base[cut...] {
                if ch == "(" || ch == "[" { depth += 1; if depth == 1 { current = ""; continue } }
                if ch == ")" || ch == "]" {
                    depth -= 1
                    if depth == 0 { if !current.isEmpty && current != "!" { tags.append(current) }; continue }
                }
                if depth > 0 { current.append(ch) }
            }
        }
        return (title.isEmpty ? base : title, tags)
    }
}

enum RomSort: String, CaseIterable, Identifiable {
    case recent = "Recently Played"
    case name = "Name"
    case added = "Date Added"
    var id: String { rawValue }
}

@MainActor @Observable
final class RomLibrary {
    static let allowedExtensions: Set<String> = ["sms", "bin"]
    static let importTypes: [UTType] = [UTType(filenameExtension: "sms") ?? .data, .data]

    private(set) var roms: [Rom] = []
    var sort: RomSort = .recent { didSet { applySort() } }
    var lastImportMessage: String?

    let directory: URL
    private let metaKey = "romMeta.v1"

    private struct Meta: Codable { var lastPlayed: Date?; var favorite = false }
    private var meta: [String: Meta] = [:]

    init() {
        let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        directory = docs.appendingPathComponent("ROMs", isDirectory: true)
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        if let data = UserDefaults.standard.data(forKey: metaKey),
           let saved = try? JSONDecoder().decode([String: Meta].self, from: data) {
            meta = saved
        }
        reload()
    }

    // MARK: Loading

    func reload() {
        let fm = FileManager.default
        let files = (try? fm.contentsOfDirectory(at: directory,
                                                  includingPropertiesForKeys: [.fileSizeKey, .creationDateKey],
                                                  options: [.skipsHiddenFiles])) ?? []
        roms = files
            .filter { Self.allowedExtensions.contains($0.pathExtension.lowercased()) }
            .compactMap(makeRom)
        applySort()
    }

    private func makeRom(_ url: URL) -> Rom? {
        let values = try? url.resourceValues(forKeys: [.fileSizeKey, .creationDateKey])
        guard let handle = try? FileHandle(forReadingFrom: url) else { return nil }
        defer { try? handle.close() }
        // The header sits below $8000, so the first 32 KB is enough
        let head = [UInt8]((try? handle.read(upToCount: 0x8000)) ?? Data())
        let (title, tags) = Rom.parseName(url.lastPathComponent)
        let m = meta[url.lastPathComponent]
        return Rom(fileName: url.lastPathComponent,
                   url: url,
                   title: title,
                   tags: tags,
                   info: RomInfo(rom: head, fileSize: values?.fileSize ?? head.count),
                   addedAt: values?.creationDate ?? .distantPast,
                   lastPlayed: m?.lastPlayed,
                   favorite: m?.favorite ?? false)
    }

    private func applySort() {
        roms.sort { a, b in
            if a.favorite != b.favorite { return a.favorite }
            switch sort {
            case .name:
                return a.title.localizedCaseInsensitiveCompare(b.title) == .orderedAscending
            case .added:
                return a.addedAt > b.addedAt
            case .recent:
                if a.lastPlayed != b.lastPlayed { return (a.lastPlayed ?? .distantPast) > (b.lastPlayed ?? .distantPast) }
                return a.title.localizedCaseInsensitiveCompare(b.title) == .orderedAscending
            }
        }
    }

    var recentlyPlayed: [Rom] {
        roms.filter { $0.lastPlayed != nil }
            .sorted { ($0.lastPlayed ?? .distantPast) > ($1.lastPlayed ?? .distantPast) }
            .prefix(8).map { $0 }
    }

    // MARK: Importing

    /// Copies ROMs picked in the Files app into Documents/ROMs.
    func importROMs(from urls: [URL]) async {
        var imported = 0
        var problems: [String] = []

        for url in urls {
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }

            let name = url.lastPathComponent
            guard Self.allowedExtensions.contains(url.pathExtension.lowercased()) else {
                problems.append("\(name): not a .sms file")
                continue
            }
            do {
                let data = try Data(contentsOf: url)
                guard data.count >= 0x2000, data.count <= 8 * 1024 * 1024 else {
                    problems.append("\(name): unexpected size (\(data.count) bytes)")
                    continue
                }
                let destination = uniqueDestination(for: name, data: data)
                if !FileManager.default.fileExists(atPath: destination.path) {
                    try data.write(to: destination, options: .atomic)
                }
                imported += 1
            } catch {
                problems.append("\(name): \(error.localizedDescription)")
            }
        }

        reload()
        var parts: [String] = []
        if imported > 0 { parts.append("Imported \(imported) game\(imported == 1 ? "" : "s").") }
        parts.append(contentsOf: problems)
        lastImportMessage = parts.joined(separator: "\n")
    }

    /// Same name + same bytes -> reuse the file. Same name, different bytes -> "Name (2).sms".
    private func uniqueDestination(for name: String, data: Data) -> URL {
        let base = (name as NSString).deletingPathExtension
        let ext = (name as NSString).pathExtension
        var candidate = directory.appendingPathComponent(name)
        var n = 2
        while FileManager.default.fileExists(atPath: candidate.path) {
            if (try? Data(contentsOf: candidate)) == data { return candidate }
            candidate = directory.appendingPathComponent("\(base) (\(n)).\(ext)")
            n += 1
        }
        return candidate
    }

    // MARK: Editing

    func delete(_ rom: Rom) {
        try? FileManager.default.removeItem(at: rom.url)
        meta[rom.fileName] = nil
        saveMeta()
        reload()
    }

    func toggleFavorite(_ rom: Rom) {
        meta[rom.fileName, default: Meta()].favorite.toggle()
        saveMeta()
        reload()
    }

    func markPlayed(_ rom: Rom) {
        meta[rom.fileName, default: Meta()].lastPlayed = Date()
        saveMeta()
        reload()
    }

    func data(for rom: Rom) -> [UInt8]? {
        (try? Data(contentsOf: rom.url)).map { [UInt8]($0) }
    }

    private func saveMeta() {
        if let data = try? JSONEncoder().encode(meta) {
            UserDefaults.standard.set(data, forKey: metaKey)
        }
    }
}
