import AppKit
import SwiftUI

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        #if DEBUG
        // before anything else: no event tap, no windows, no focus taken (it runs while others use the Mac)
        if let dir = UserDefaults.standard.string(forKey: "emojiSelfTest") { EmojiSelfTest.run(dir: dir); return }
        #endif
        Migration.fromOpenKeyIfNeeded()
        // people who already use Vikey (permission granted) don't get the first-run guide on update
        if UserDefaults.standard.object(forKey: OnboardingView.doneKey) == nil && VikeyCore.isAccessibilityTrusted() {
            UserDefaults.standard.set(true, forKey: OnboardingView.doneKey)
        }
        AppState.shared.startIfPossible()
        EmojiPicker.shared.install()
        if UserDefaults.standard.bool(forKey: "showOnboarding") { OnboardingWindow.show() }   // -showOnboarding YES (screenshots)
        if UserDefaults.standard.bool(forKey: "showAbout") { AboutPanel.show() }             // -showAbout YES (screenshots)
        if UserDefaults.standard.bool(forKey: "showMain") { Windows.main.show() }           // -showMain YES (screenshots)
        #if DEBUG
        if UserDefaults.standard.bool(forKey: "forceDark") { NSApp.appearance = NSAppearance(named: .darkAqua) }   // screenshots
        #endif
        if UserDefaults.standard.bool(forKey: "showEmoji") {                                 // -showEmoji YES [-emojiQuery horse] (screenshots)
            EmojiPicker.shared.open()
            if let q = UserDefaults.standard.string(forKey: "emojiQuery") { EmojiPicker.shared.model.setRaw(q) }
        }
        // Opened by the user (not at login, minutes after boot): show the main window.
        if AppState.shared.isRunning && ProcessInfo.processInfo.systemUptime > 180 && !UserDefaults.standard.bool(forKey: "noWindowAtLaunch") {
            Windows.main.show()
        }

        let ws = NSWorkspace.shared.notificationCenter
        ws.addObserver(forName: NSWorkspace.didActivateApplicationNotification, object: nil, queue: .main) { _ in
            VikeyCore.frontmostAppChanged()
        }
        ws.addObserver(forName: NSWorkspace.activeSpaceDidChangeNotification, object: nil, queue: .main) { _ in
            VikeyCore.activeSpaceChanged()
        }
        ws.addObserver(forName: NSWorkspace.willSleepNotification, object: nil, queue: .main) { _ in
            VikeyCore.stop()
        }
        ws.addObserver(forName: NSWorkspace.didWakeNotification, object: nil, queue: .main) { _ in
            AppState.shared.startIfPossible()
        }
    }

    /// Opening Vikey again while it runs (Finder, Spotlight, Dock) shows the main window.
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if AppState.shared.needsOnboarding { OnboardingWindow.show() } else { Windows.main.show() }
        return false
    }

    func applicationWillTerminate(_ notification: Notification) {
        VikeyCore.flushTypingLog()
    }
}
