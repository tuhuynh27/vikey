import StoreKit
import SwiftUI
import UIKit

/// What the keyboard's nudges open (vikey://review, vikey://feedback): Apple's review sheet, and the feedback form.
@MainActor
enum ReviewPrompt {
    /// Apple's in-app review sheet, which Apple may decide not to show (3 times a year at most).
    static func request() {
        guard let scene = UIApplication.shared.connectedScenes.first(where: { $0.activationState == .foregroundActive }) as? UIWindowScene else { return }
        AppStore.requestReview(in: scene)
    }
    /// The App Store's "write a review" page: when Apple's sheet doesn't show.
    static let writeReviewURL = URL(string: "https://apps.apple.com/app/id6827958793?action=write-review")!

    static func handle(_ url: URL, openURL: OpenURLAction) {
        guard url.scheme == "vikey" else { return }
        switch url.host {
        // The App Store's review page, not Apple's sheet: the sheet may not show (3 a year, never in TestFlight).
        case "review": openURL(writeReviewURL)
        case "feedback": openURL(MainView.feedbackURL)
        default: break
        }
    }
}
