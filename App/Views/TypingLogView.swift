import SwiftUI

/// Manual vs automatic switch counts and the latest typing-log events.
struct TypingLogView: View {
    @AppStorage(PrefKey.typingLogKeys) private var recordKeys = 1
    @State private var stats: [String: [String: Int]] = [:]
    @State private var events: [LogEvent] = []
    @State private var confirmClear = false
    @State private var loadGeneration = 0   // the latest reload; a slower earlier one must not overwrite it

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(summary).font(.title3.weight(.semibold))
            StatsGrid(stats: stats)
            VStack(alignment: .leading, spacing: 2) {
                Toggle("Ghi lại phím gõ của từng từ",
                       isOn: Binding(get: { recordKeys != 0 }, set: { recordKeys = $0 ? 1 : 0; Preferences.apply() }))
                Text("Không bao giờ ghi trong ô mật khẩu, trình quản lý mật khẩu và Terminal.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .padding(.leading, 20)
            }
            Text("Sự kiện gần đây").font(.headline)
            Table(events) {
                TableColumn("Thời gian") { Text($0.time).foregroundStyle($0.color) }.width(min: 100, ideal: 110)
                TableColumn("Ứng dụng / trang") { Text($0.place).foregroundStyle($0.color) }.width(min: 120, ideal: 160)
                TableColumn("Sự kiện") { Text($0.title).foregroundStyle($0.color) }.width(min: 110, ideal: 140)
                TableColumn("Chi tiết") { Text($0.detail).foregroundStyle($0.color) }
            }
            .frame(minHeight: 200)
            HStack {
                Button("Làm mới") { reload() }
                Button("Mở file log") { reveal() }
                Spacer()
                Button("Xoá log…") { confirmClear = true }
            }
        }
        .padding(20)
        .frame(minWidth: 720, minHeight: 560)
        .onAppear { reload() }
        .confirmationDialog("Xoá toàn bộ nhật ký gõ?", isPresented: $confirmClear) {
            Button("Xoá", role: .destructive) {
                VikeyCore.clearTypingLog()
                reload()
            }
        } message: {
            Text("Số liệu thống kê và file log sẽ bị xoá.")
        }
    }

    private var summary: String {
        let today = StatsGrid.sum(stats, days: 1)
        let auto = (today["auto_vi_word_to_en"] ?? 0) + (today["auto_en_word_to_vi"] ?? 0) + (today["auto_double_key"] ?? 0)
        return "Hôm nay: \(today["manual"] ?? 0) lần chuyển tay, \(auto) lần tự chuyển"
    }

    private func reload() {
        // the log can be 20 MB: read it off the main thread, which also runs the keyboard tap. A reload started
        // before a clear can finish reading the old file after the reload that follows the clear: only the latest
        // reload's result is shown.
        loadGeneration += 1
        let generation = loadGeneration
        // the stats as of the flush: a key typed while it runs isn't in the rows read, so it isn't counted either
        let snapshot = (VikeyCore.typingLogStats() as? [String: [String: Int]]) ?? [:]
        VikeyCore.flushTypingLogThen {
            guard generation == loadGeneration else { return }
            stats = snapshot
            let path = VikeyCore.typingLogPath()
            DispatchQueue.global(qos: .userInitiated).async {
                let loaded = LogEvent.load(from: path, limit: 300)
                DispatchQueue.main.async { if generation == loadGeneration { events = loaded } }
            }
        }
    }

    private func reveal() {
        VikeyCore.flushTypingLogThen {
            let url = URL(fileURLWithPath: VikeyCore.typingLogPath())
            if FileManager.default.fileExists(atPath: url.path) {
                NSWorkspace.shared.activateFileViewerSelecting([url])
            } else {
                NSWorkspace.shared.open(url.deletingLastPathComponent())
            }
        }
    }
}

private struct StatsGrid: View {
    let stats: [String: [String: Int]]

    static func sum(_ stats: [String: [String: Int]], days: Int) -> [String: Int] {
        let f = DateFormatter()
        f.dateFormat = "yyyy-MM-dd"
        f.locale = Locale(identifier: "en_US_POSIX")
        let from = days > 0 ? f.string(from: Date().addingTimeInterval(-Double(days - 1) * 86400)) : "0000"
        var total: [String: Int] = [:]
        for (day, counters) in stats where day >= from {
            for (k, v) in counters { total[k, default: 0] += v }
        }
        return total
    }

    var body: some View {
        let periods = [Self.sum(stats, days: 1), Self.sum(stats, days: 7), Self.sum(stats, days: 0)]
        Grid(alignment: .trailing, horizontalSpacing: 28, verticalSpacing: 6) {
            GridRow {
                Text("")
                Text("Hôm nay").bold(); Text("7 ngày").bold(); Text("Tất cả").bold()
            }
            ForEach(rows, id: \.0) { row in
                GridRow {
                    Text(row.0).gridColumnAlignment(.leading)
                    ForEach(0..<3, id: \.self) { i in
                        Text(value(row.1, periods[i])).monospacedDigit()
                    }
                }
            }
        }
    }

    private let rows: [(String, String)] = [
        ("Từ đã gõ", "words"), ("Chuyển tay", "manual"), ("Tự chuyển", "auto"),
        ("    Tiếng Việt → Anh (cả từ)", "auto_vi_word_to_en"), ("    Anh → Tiếng Việt (cả từ)", "auto_en_word_to_vi"),
        ("    Gõ dấu hai lần → Anh", "auto_double_key"), ("Hoàn tác bằng Delete", "undo"),
        ("Đổi theo ứng dụng / trang web", "context"), ("Chuyển tay mỗi 100 từ", "rate"),
    ]

    private func value(_ key: String, _ p: [String: Int]) -> String {
        let words = (p["words_vi"] ?? 0) + (p["words_en"] ?? 0)
        switch key {
        case "words": return "\(words)"
        case "auto": return "\((p["auto_vi_word_to_en"] ?? 0) + (p["auto_en_word_to_vi"] ?? 0) + (p["auto_double_key"] ?? 0))"
        case "rate": return words > 0 ? String(format: "%.1f", 100.0 * Double(p["manual"] ?? 0) / Double(words)) : "–"
        default: return "\(p[key] ?? 0)"
        }
    }
}

struct LogEvent: Identifiable {
    let id: Int
    let time, place, title, detail: String
    let color: Color

    /// The last `limit` events, reading only the end of the file (lines are a few hundred bytes).
    static func load(from path: String, limit: Int) -> [LogEvent] {
        guard let h = FileHandle(forReadingAtPath: path) else { return [] }
        defer { try? h.close() }
        let tailBytes: UInt64 = UInt64(limit) * 1024
        guard let size = try? h.seekToEnd() else { return [] }
        // from one byte before the tail: everything up to the first newline is a partial line (or, when the tail
        // starts right at a line, just that newline), so a whole record at the boundary is kept
        let start = size > tailBytes ? size - tailBytes - 1 : 0
        guard (try? h.seek(toOffset: start)) != nil, var data = try? h.readToEnd() else { return [] }
        if start > 0 {
            guard let nl = data.firstIndex(of: UInt8(ascii: "\n")) else { return [] }
            data = data[(nl + 1)...]
        }
        let lines = data.split(separator: UInt8(ascii: "\n"))
        var out: [LogEvent] = []
        for line in lines.reversed() {
            if out.count >= limit { break }
            guard let d = try? JSONSerialization.jsonObject(with: Data(line)) as? [String: Any] else { continue }
            out.append(LogEvent(id: out.count, d))
        }
        return out
    }

    init(id: Int, _ d: [String: Any]) {
        self.id = id
        let e = d["e"] as? String ?? ""
        let to = (d["to"] as? String) == "vi" ? "Tiếng Việt" : "Anh"
        let t = d["t"] as? String ?? ""
        time = t.count >= 19 ? String(t.dropFirst(5).prefix(14)).replacingOccurrences(of: "T", with: "  ") : t
        let app = d["app"] as? String ?? ""
        place = (d["site"] as? String).map { "\(app) · \($0)" } ?? app
        switch e {
        case "word":
            title = (d["lang"] as? String) == "vi" ? "Từ (Việt)" : "Từ (Anh)"
            detail = "\(d["keys"] as? String ?? "")   [\(d["end"] as? String ?? "")]"
        case "auto":
            title = "Tự chuyển → \(to)"
            detail = ["vi_word_to_en": "từ tiếng Anh gõ khi đang ở Tiếng Việt",
                      "en_word_to_vi": "từ tiếng Việt gõ khi đang ở Anh",
                      "double_key": "gõ dấu hai lần"][d["rule"] as? String ?? ""] ?? ""
        case "manual":
            title = "Chuyển tay → \(to)"
            detail = ((d["via"] as? String) == "menu" ? "bằng menu" : "bằng phím tắt") + ((d["after_auto"] as? Bool) == true ? ", ngay sau tự chuyển" : "")
        case "undo":
            title = "Hoàn tác → \(to)"
            detail = "bằng Delete"
        case "context":
            title = "Theo app/web → \(to)"
            detail = ""
        default:
            title = e; detail = ""
        }
        color = (e == "manual" || e == "undo") ? .orange : (e == "word" ? .primary : .blue)
    }
}
