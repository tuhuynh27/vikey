import AppKit

/// Apps Vikey stays out of: while one is in front every key passes through untouched (no Vietnamese typing, no
/// auto switch). Meant for remote-control apps, where the keys go to another machine with its own keyboard.
/// The list is a UserDefaults array of bundle ids (PrefKey.excludedApps), read by VikeyCore.mm in VKLoadSettings.
enum ExcludedApps {
    /// Remote desktop / remote control clients. Edit freely in the main window: the user's list replaces this one.
    static let defaults: [String] = [
        "com.microsoft.rdc.macos",        // Windows App
        "com.microsoft.rdc.mac",          // Microsoft Remote Desktop (older)
        "com.apple.ScreenSharing",
        "com.apple.RemoteDesktop",
        "com.teamviewer.TeamViewer",
        "com.anydesk.anydesk",
        "com.carriez.RustDesk",
        "tv.parsec.www",
        "com.realvnc.vncviewer",
        "com.citrix.receiver.nomas",      // Citrix Workspace
        "com.vmware.horizon",
        "com.p5sys.jump.mac.viewer",      // Jump Desktop
        "com.moonlight-stream.Moonlight",
    ]

    static var all: [String] {
        UserDefaults.standard.stringArray(forKey: PrefKey.excludedApps) ?? defaults
    }

    static func set(_ ids: [String]) {
        UserDefaults.standard.set(ids, forKey: PrefKey.excludedApps)
        Preferences.apply()
    }

    static func add(_ id: String) {
        let ids = all
        guard !id.isEmpty, !ids.contains(id) else { return }
        set(ids + [id])
    }

    static func remove(_ id: String) { set(all.filter { $0 != id }) }

    static func resetToDefaults() { set(defaults) }

    /// Installed on this Mac. The settings list only shows these; the others stay excluded in case they get installed.
    static func isInstalled(_ id: String) -> Bool { NSWorkspace.shared.urlForApplication(withBundleIdentifier: id) != nil }

    /// The name and icon of an installed app, falling back to the bundle id when it isn't installed.
    static func info(_ id: String) -> (name: String, icon: NSImage?) {
        guard let url = NSWorkspace.shared.urlForApplication(withBundleIdentifier: id) else { return (id, nil) }
        let name = FileManager.default.displayName(atPath: url.path).replacingOccurrences(of: ".app", with: "")
        return (name, NSWorkspace.shared.icon(forFile: url.path))
    }

    /// Lets the user pick an .app and adds its bundle id.
    @MainActor static func pickAndAdd() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.application]
        panel.allowsMultipleSelection = true
        panel.directoryURL = URL(fileURLWithPath: "/Applications")
        panel.message = "Chọn ứng dụng mà Vikey không gõ tiếng Việt"
        panel.prompt = "Thêm"
        guard panel.runModal() == .OK else { return }
        for url in panel.urls {
            if let id = Bundle(url: url)?.bundleIdentifier { add(id) }
        }
    }

    /// Adds the app that was in front before Vikey's window (by its bundle id), for "Thêm ứng dụng đang mở".
    static func runningApps() -> [(id: String, name: String)] {
        NSWorkspace.shared.runningApplications
            .filter { $0.activationPolicy == .regular && $0.bundleIdentifier != nil && $0.bundleIdentifier != Bundle.main.bundleIdentifier }
            .compactMap { app in app.bundleIdentifier.map { ($0, app.localizedName ?? $0) } }
            .sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }
}
