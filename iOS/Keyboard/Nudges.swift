import Foundation

/// When the strip's nudges show (Nudge.swift), kept in the keyboard's own defaults (it only reads the shared ones).
///
/// - The review invite: once Vikey has switched languages on its own 100 times, and the user undid none of the
///   last 20 switches; once a day at most while it's ignored. ✕ asks again at 500; then never. A tap: never again.
/// - The bug report: right after the user's 2nd undo in a day (a Backspace that puts back an auto-correct or an
///   auto switch, or a manual switch just after an auto one): Vikey was just wrong. At most once a day and 3
///   times a week; ✕ pauses it for 2 weeks; a tap: a month.
///
/// Both show only on an empty strip, at a quiet moment (KeyboardViewController: the keyboard opening, a sentence's
/// end, an undo), never while a word is being typed; one at a time, the bug report first.
final class Nudges {
    static let reviewAt = 100, reviewAgainAt = 500, cleanSwitches = 20

    private let d: UserDefaults
    private let clock: () -> Date
    init(defaults: UserDefaults = .standard, clock: @escaping () -> Date = Date.init) { d = defaults; self.clock = clock }

    private enum K {
        static let switches = "nudge.switches", lastUndoAt = "nudge.lastUndoAtSwitch", lastAutoSwitch = "nudge.lastAutoSwitchTime"
        static let reviewDone = "nudge.reviewDone", reviewClosed = "nudge.reviewClosed", reviewShown = "nudge.reviewShownTime"
        static let undos = "nudge.undoTimes", bugShown = "nudge.bugShownTimes", bugPausedUntil = "nudge.bugPausedUntil"
    }
    private var now: Double { clock().timeIntervalSince1970 }

    var switches: Int { d.integer(forKey: K.switches) }

    /// Vikey switched the language on its own.
    func autoSwitched() {
        d.set(switches + 1, forKey: K.switches)
        d.set(now, forKey: K.lastAutoSwitch)
    }

    /// The user undid what Vikey did (an auto-correct, an auto switch).
    func undone() {
        d.set(switches, forKey: K.lastUndoAt)
        let day = now - 86400
        d.set((times(K.undos) + [now]).filter { $0 > day }, forKey: K.undos)
    }

    /// A manual switch: an undo when it comes right after an auto switch (within 5 seconds).
    func manualSwitch() {
        if now - d.double(forKey: K.lastAutoSwitch) < 5 { undone() }
    }

    /// The nudge due now, if any: the bug report (only right after an undo), else the review invite.
    func due(afterUndo: Bool = false) -> NudgeView.Kind? {
        if afterUndo, bugDue() { return .bug }
        return reviewDue() ? .review : nil
    }

    private func reviewDue() -> Bool {
        if d.bool(forKey: K.reviewDone) || now - d.double(forKey: K.reviewShown) < 86400 { return false }   // once a day at most
        let closed = d.integer(forKey: K.reviewClosed)
        guard closed < 2, switches >= (closed == 0 ? Self.reviewAt : Self.reviewAgainAt) else { return false }
        return switches - d.integer(forKey: K.lastUndoAt) >= Self.cleanSwitches || d.object(forKey: K.lastUndoAt) == nil
    }

    private func bugDue() -> Bool {
        guard now >= d.double(forKey: K.bugPausedUntil), times(K.undos).count >= 2 else { return false }
        let shown = times(K.bugShown)
        return !shown.contains { $0 > now - 86400 } && shown.filter { $0 > now - 7 * 86400 }.count < 3
    }

    func shown(_ k: NudgeView.Kind) {
        if k == .review { d.set(now, forKey: K.reviewShown) }
        if k == .bug { d.set((times(K.bugShown) + [now]).filter { $0 > now - 7 * 86400 }, forKey: K.bugShown) }
    }
    func closed(_ k: NudgeView.Kind) {
        switch k {
        case .review: d.set(d.integer(forKey: K.reviewClosed) + 1, forKey: K.reviewClosed)
        case .bug: d.set(now + 14 * 86400, forKey: K.bugPausedUntil)
        }
    }
    func acted(_ k: NudgeView.Kind) {
        switch k {
        case .review: d.set(true, forKey: K.reviewDone)
        case .bug: d.set(now + 30 * 86400, forKey: K.bugPausedUntil)
        }
    }

    private func times(_ key: String) -> [Double] { d.array(forKey: key) as? [Double] ?? [] }
}
