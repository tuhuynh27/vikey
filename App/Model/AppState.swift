import AppKit
import Combine
import SwiftUI

/// App-wide state the menu bar and windows observe.
@MainActor
final class AppState: ObservableObject {
    static let shared = AppState()

    @Published private(set) var language: Int = Int(VikeyCore.language())
    @Published private(set) var isRunning = false
    @Published var needsOnboarding = false
    /// Bumped when the app in front changes, so the menu bar re-reads the per-app auto switch toggle.
    @Published private(set) var frontAppTick = 0
    func frontAppChanged() { frontAppTick &+= 1 }

    private var permissionTimer: Timer?

    private init() {
        NotificationCenter.default.addObserver(forName: .VikeyLanguageDidChange, object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in self?.language = Int(VikeyCore.language()) }
        }
    }

    /// Starts the keyboard core when Accessibility is granted; otherwise shows onboarding and waits.
    func startIfPossible() {
        if tryStart() { return }
        isRunning = false
        needsOnboarding = true
        OnboardingWindow.show()
        if permissionTimer == nil {
            // retries quietly: if macOS reports the permission but still refuses the tap (a stale entry after an
            // update), showing onboarding again every second would keep stealing focus from the user's app
            permissionTimer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { _ in
                Task { @MainActor in _ = AppState.shared.tryStart() }
            }
        }
    }

    private func tryStart() -> Bool {
        guard VikeyCore.isAccessibilityTrusted(), VikeyCore.start() else { return false }
        isRunning = true
        needsOnboarding = false
        language = Int(VikeyCore.language())
        permissionTimer?.invalidate()
        permissionTimer = nil
        return true
    }

    func setLanguage(_ value: Int) {
        VikeyCore.setLanguageByUser(Int32(value))
        language = Int(VikeyCore.language())
    }

    func toggleLanguage() { setLanguage(language == 1 ? 0 : 1) }
}

extension Notification.Name {
    static let VikeyLanguageDidChange = Notification.Name(rawValue: "VikeyLanguageDidChange")
}
