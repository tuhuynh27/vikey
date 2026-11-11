import SwiftUI

@main
struct VikeyApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var state = AppState.shared

    var body: some Scene {
        MenuBarExtra {
            MenuBarContent()
                .environmentObject(state)
        } label: {
            Image(nsImage: MenuBarIcon.image(vietnamese: state.language == 1))
        }
        .commands {
            CommandGroup(replacing: .appInfo) {
                Button("Giới thiệu về Vikey") { AboutPanel.show() }   // with the credits
                Button("Ghi nhận…") { Windows.acknowledgements.show() }
                Divider()
                DebugMenu()
            }
            CommandGroup(replacing: .appSettings) {
                Button("Mở Vikey…") { Windows.main.show() }
                    .keyboardShortcut(",")
            }
        }

    }
}
