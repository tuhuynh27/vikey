import SwiftUI

@main
struct VikeyApp: App {
    init() { VKSettings.register() }
    var body: some Scene {
        WindowGroup { RootView() }
    }
}

struct RootView: View {
    @AppStorage("didOnboard") private var didOnboard = false
    @Environment(\.openURL) private var openURL
    var body: some View {
        Group {
            if didOnboard || ProcessInfo.processInfo.arguments.contains("-SkipOnboarding") {
                MainView()
            } else {
                OnboardingView { didOnboard = true }
            }
        }
        // The keyboard's nudges: vikey://review (Apple's review sheet), vikey://feedback (the form).
        .onOpenURL { ReviewPrompt.handle($0, openURL: openURL) }
    }
}
