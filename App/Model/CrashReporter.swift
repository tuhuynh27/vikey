import AppKit

/// After Vikey crashed, macOS leaves a report in ~/Library/Logs/DiagnosticReports. On the next launch Vikey asks
/// whether to send it (or sends it if the user chose "always"), to vikey.app/api/crash. A report has no typed text:
/// the crash location in the code, the version and macOS. Home folder paths are reduced to "~" before sending.
enum CrashReporter {
    static let autoKey = "CrashReportsAuto"          // the user chose "always send, don't ask"
    private static let seenKey = "CrashReportsSeenUntil"
    static var endpoint = URL(string: UserDefaults.standard.string(forKey: "crashEndpoint") ?? "https://vikey.app/api/crash")!

    /// Reports from the last 3 days that weren't handled yet, newest first.
    static func pending() -> [URL] {
        let dir = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/DiagnosticReports")
        let seen = UserDefaults.standard.double(forKey: seenKey)
        let since = max(seen, Date().addingTimeInterval(-3 * 86400).timeIntervalSince1970)
        let files = (try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: [.contentModificationDateKey])) ?? []
        return files.filter { u in
            let n = u.lastPathComponent
            guard n.hasPrefix("Vikey-") || n.hasPrefix("Vikey_"), ["ips", "crash"].contains(u.pathExtension) else { return false }
            let d = (try? u.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate)?.timeIntervalSince1970 ?? 0
            return d > since
        }.sorted { $0.lastPathComponent > $1.lastPathComponent }
    }

    /// Called at launch: one question for the newest report, the others are marked handled with it.
    @MainActor static func checkAtLaunch() {
        let files = pending()
        guard let newest = files.first else { return }
        UserDefaults.standard.set(Date().timeIntervalSince1970, forKey: seenKey)
        if UserDefaults.standard.bool(forKey: autoKey) { send(newest); return }
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { ask(newest) }   // after the menu bar icon is up
    }

    @MainActor static func ask(_ report: URL) {
        let a = NSAlert()
        a.messageText = "Vikey vừa bị thoát đột ngột"
        a.informativeText = "Gửi báo cáo lỗi để Vikey sửa lỗi này? Báo cáo chỉ có thông tin kỹ thuật: vị trí lỗi trong mã, phiên bản Vikey và macOS. Không có chữ bạn đã gõ."
        a.addButton(withTitle: "Gửi báo cáo")
        a.addButton(withTitle: "Không gửi")
        a.showsSuppressionButton = true
        a.suppressionButton?.title = "Luôn gửi, không hỏi lại"
        NSApp.activate(ignoringOtherApps: true)
        let send = a.runModal() == .alertFirstButtonReturn
        if send && a.suppressionButton?.state == .on { UserDefaults.standard.set(true, forKey: autoKey) }
        if send { Self.send(report) }
    }

    static func send(_ report: URL) {
        guard var text = try? String(contentsOf: report, encoding: .utf8) else { return }
        let home = FileManager.default.homeDirectoryForCurrentUser.path
        text = text.replacingOccurrences(of: home, with: "~")
        let v = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? ""
        let b = Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? ""
        let os = ProcessInfo.processInfo.operatingSystemVersionString
        let boundary = "vikey-\(UUID().uuidString)"
        var body = Data()
        func field(_ name: String, _ value: String) {
            body.append("--\(boundary)\r\nContent-Disposition: form-data; name=\"\(name)\"\r\n\r\n\(value)\r\n".data(using: .utf8)!)
        }
        field("platform", "macos"); field("version", "\(v) (\(b))"); field("os", os)
        body.append("--\(boundary)\r\nContent-Disposition: form-data; name=\"report\"; filename=\"\(report.lastPathComponent)\"\r\nContent-Type: text/plain\r\n\r\n".data(using: .utf8)!)
        body.append(Data(text.prefix(1_900_000).utf8))
        body.append("\r\n--\(boundary)--\r\n".data(using: .utf8)!)
        var req = URLRequest(url: endpoint)
        req.httpMethod = "POST"
        req.setValue("multipart/form-data; boundary=\(boundary)", forHTTPHeaderField: "Content-Type")
        URLSession.shared.uploadTask(with: req, from: body) { _, resp, err in
            NSLog("Vikey crash report sent: %d %@", (resp as? HTTPURLResponse)?.statusCode ?? -1, err?.localizedDescription ?? "")
        }.resume()
    }
}
