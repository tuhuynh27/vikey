import SwiftUI

struct MenuBarContent: View {
    @EnvironmentObject var state: AppState
    @AppStorage(PrefKey.autoSwitch) private var autoSwitch = 1
    @AppStorage(PrefKey.hotkey) private var hotkeyRaw = Hotkey.defaultValue

    var body: some View {
        if state.needsOnboarding {
            Button("Cấp quyền cho Vikey…") { OnboardingWindow.show() }
            Divider()
        }
        Toggle(isOn: Binding(get: { state.language == 1 }, set: { state.setLanguage($0 ? 1 : 0) })) {
            Text("Tiếng Việt   \(Hotkey(raw: hotkeyRaw).display)")
        }
        Toggle("Tự chuyển Anh/Việt", isOn: Binding(get: { autoSwitch == 1 }, set: { autoSwitch = $0 ? 1 : 0; Preferences.apply() }))
        // Per app: remembered for the app in front (the tick makes SwiftUI re-read it after an app change).
        if state.frontAppTick >= 0 {
            let app = VikeyCore.frontAppName()
            if VikeyCore.frontAppIsExcluded() {
                Text("\(app): đã loại trừ").foregroundStyle(.secondary)
            } else {
                Toggle("Tự chuyển ở \(app)", isOn: Binding(
                    get: { VikeyCore.autoSwitchEnabledForFrontApp() },
                    set: {
                        VikeyCore.setAutoSwitchForFrontApp($0)
                        state.frontAppChanged()
                    }))
            }
        }
        Divider()
        Button("Mở Vikey…") { Windows.main.show() }
            .keyboardShortcut(",")
        Divider()
        Button("Hướng dẫn…") { OnboardingWindow.show() }
        Button("Gửi góp ý…") { Feedback.open() }
        Button("Giới thiệu Vikey") { AboutPanel.show() }
        Button("Thoát Vikey") { NSApp.terminate(nil) }
            .keyboardShortcut("q")
    }
}

/// Debug items, in the Vikey app menu only (not the menu bar icon).
struct DebugMenu: View {
    @AppStorage(PrefKey.typingLogEnabled) private var logEnabled = 0

    var body: some View {
        Menu("Gỡ lỗi") {
            Toggle("Ghi nhật ký gõ", isOn: Binding(get: { logEnabled == 1 }, set: { logEnabled = $0 ? 1 : 0; Preferences.apply() }))
            Button("Xem nhật ký gõ…") { Windows.history.show() }
                .disabled(logEnabled == 0)
        }
    }
}

/// vikey.app/feedback with the platform and version, so a report says where it came from.
enum Feedback {
    static var url: URL {
        let v = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? ""
        return URL(string: "https://vikey.app/feedback?platform=macos&v=\(v)")!
    }
    static func open() { NSWorkspace.shared.open(url) }
}
