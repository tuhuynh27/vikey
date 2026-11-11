import XCTest
import UIKit

/// Field-specific layouts like the system keyboard (email, URL, web search, number pads), rendered for a look.
final class KeyboardLayoutTests: XCTestCase {
    private func render(_ type: UIKeyboardType, layer: KeyboardView.Layer = .letters, _ name: String) -> KeyboardView {
        let kb = KeyboardView(frame: CGRect(x: 0, y: 0, width: 402, height: KeyboardView.keyboardHeight(compactHeight: false)))
        kb.fieldType = type
        kb.layer_ = layer
        kb.backgroundColor = UIColor(red: 0.82, green: 0.83, blue: 0.86, alpha: 1)
        kb.layoutIfNeeded()
        let img = UIGraphicsImageRenderer(bounds: kb.bounds).image { ctx in kb.layer.render(in: ctx.cgContext) }
        try? FileManager.default.createDirectory(atPath: "/tmp/claude-501/layouts", withIntermediateDirectories: true)
        try? img.pngData()?.write(to: URL(fileURLWithPath: "/tmp/claude-501/layouts/\(name).png"))
        return kb
    }
    private func titles(_ kb: KeyboardView) -> [String] { kb.subviews.compactMap { ($0 as? KeyCap)?.label.text } }

    func testEmailHasAtAndDot() { let t = titles(render(.emailAddress, "email")); XCTAssertTrue(t.contains("@") && t.contains(".")) }
    func testURLHasSlashAndDotCom() { let t = titles(render(.URL, "url")); XCTAssertTrue(t.contains("/") && t.contains(".com")) }
    func testWebSearchHasDot() { XCTAssertTrue(titles(render(.webSearch, "websearch")).contains(".")) }
    func testNumberPad() {
        let kb = render(.numberPad, layer: .numpad, "numberpad")
        XCTAssertEqual(Set(titles(kb).filter { !$0.isEmpty }), Set("1234567890".map(String.init)))
    }
    func testDecimalAndPhonePads() {
        XCTAssertTrue(titles(render(.decimalPad, layer: .numpad, "decimalpad")).contains(Locale.current.decimalSeparator ?? "."))
        XCTAssertTrue(titles(render(.phonePad, layer: .numpad, "phonepad")).contains("+"))
    }
    func testDefaultUnchanged() { let t = titles(render(.default, "default")); XCTAssertFalse(t.contains("@")) }
}

/// Design proposals: the VI/EN key in the top-left corner of the suggestion strip (Tony). Renders only.
final class LanguageKeyProposalTests: XCTestCase {
    func testRenderProposals() {
        KeyboardView.barOn = true
        for dark in [false, true] {
            for style in 0...3 {
                KeyboardView.langStyle = style
                let kb = KeyboardView(frame: CGRect(x: 0, y: 0, width: 402, height: KeyboardView.keyboardHeight(compactHeight: false)))
                kb.dark = dark
                kb.languageShort = "VI"; kb.languageTitle = "Tiếng Việt"
                kb.backgroundColor = dark ? UIColor(white: 0.17, alpha: 1) : UIColor(red: 0.82, green: 0.83, blue: 0.86, alpha: 1)
                kb.suggestions = [Suggestion(text: "tooi", quoted: true), Suggestion(text: "tôi", highlighted: true), Suggestion(text: "tối")]
                kb.layoutIfNeeded()
                let img = UIGraphicsImageRenderer(bounds: kb.bounds).image { ctx in kb.layer.render(in: ctx.cgContext) }
                try? img.pngData()?.write(to: URL(fileURLWithPath: "/tmp/claude-501/layouts/lang_\(style)_\(dark ? "dark" : "light").png"))
            }
        }
        KeyboardView.langStyle = 1
    }

    func testPillStates() {
        KeyboardView.barOn = true; KeyboardView.langStyle = 1
        for (lang, title) in [("VI", "Tiếng Việt"), ("EN", "English")] {
            let kb = KeyboardView(frame: CGRect(x: 0, y: 0, width: 402, height: KeyboardView.keyboardHeight(compactHeight: false)))
            kb.languageShort = lang; kb.languageTitle = title
            kb.backgroundColor = UIColor(red: 0.82, green: 0.83, blue: 0.86, alpha: 1)
            kb.layoutIfNeeded()
            let img = UIGraphicsImageRenderer(bounds: kb.bounds).image { ctx in kb.layer.render(in: ctx.cgContext) }
            try? img.pngData()?.write(to: URL(fileURLWithPath: "/tmp/claude-501/layouts/pill_\(lang).png"))
            let caps = kb.subviews.compactMap { $0 as? KeyCap }
            // the language key stays in the corner (it takes the taps); the corner element draws VI/EN over it
            XCTAssertTrue(caps.contains { $0.frame.minY < 40 && $0.frame.minX < 20 }, "the pill sits in the strip's corner")
            XCTAssertFalse(kb.mark.isHidden)
            XCTAssertEqual(kb.mark.vietnamese, lang == "VI")
        }
        KeyboardView.barOn = false
        let kb = KeyboardView(frame: CGRect(x: 0, y: 0, width: 402, height: KeyboardView.keyboardHeight(compactHeight: false)))
        kb.layoutIfNeeded()
        XCTAssertTrue(kb.subviews.compactMap { $0 as? KeyCap }.contains { $0.label.text == "VI" && $0.frame.minY > 150 }, "no strip: back in the bottom row")
    }
}


/// Key frames vs the iOS 26.5 system keyboard, measured from simulator screenshots of iPhone 17 Pro Max (440 pt).
final class SystemKeyboardGeometryTests: XCTestCase {
    func testMatchesSystemKeyboardAt440() throws {
        guard KeyboardView.tallKeys else { throw XCTSkip("needs a Pro Max-size simulator") }
        KeyboardView.barOn = true; KeyboardView.langStyle = 1
        let kb = KeyboardView(frame: CGRect(x: 0, y: 0, width: 440, height: KeyboardView.keyboardHeight(compactHeight: false)))
        kb.layoutIfNeeded()
        let caps = kb.subviews.compactMap { $0 as? KeyCap }
        func frame(_ label: String) -> CGRect? { caps.first { $0.label.text == label }?.frame }
        let q = try XCTUnwrap(frame("q")), a = try XCTUnwrap(frame("a")), z = try XCTUnwrap(frame("z")), p = try XCTUnwrap(frame("p"))
        let top = q.minY
        func near(_ v: CGFloat, _ want: CGFloat, _ what: String) { XCTAssertEqual(v, want, accuracy: 0.5, what) }
        near(q.minX, 6.67, "q x"); near(q.width, 37.33, "letter width"); near(q.height, 45, "key height")
        near(p.maxX, 433.67, "p right edge")
        near(a.minX, 28.0, "a x"); near(a.minY - top, 56, "row pitch")
        near(z.minX, 71.33, "z x"); near(z.minY - top, 112, "row 3")
        // row 3 sides: shift 6.67 w 50.0, delete 383.33 w 50.33; bottom row: 123 w 48 at 6.67, return 331.33 w 102.33
        let row3 = caps.filter { abs($0.frame.minY - z.minY) < 1 }.sorted { $0.frame.minX < $1.frame.minX }
        near(row3.first!.frame.width, 50, "shift width"); near(row3.last!.frame.minX, 383.33, "delete x")
        let bottom = caps.filter { abs($0.frame.minY - (top + 168)) < 1 }.sorted { $0.frame.minX < $1.frame.minX }
        near(bottom.first!.frame.width, 48, "123 width"); near(bottom.last!.frame.minX, 331.33, "return x"); near(bottom.last!.frame.width, 102.33, "return width")
    }
}

/// The brand on the right of an empty suggestion strip (Tony). Renders only.
final class StripBrandTests: XCTestCase {
    func testRenderEmptyAndFullStrip() {
        KeyboardView.barOn = true
        KeyboardView.langStyle = 1
        for dark in [false, true] {
            for (name, s) in [("empty", [Suggestion]()), ("words", [Suggestion(text: "tooi", quoted: true), Suggestion(text: "tôi", highlighted: true), Suggestion(text: "tối")])] {
                let kb = KeyboardView(frame: CGRect(x: 0, y: 0, width: 402, height: KeyboardView.keyboardHeight(compactHeight: false)))
                kb.dark = dark
                kb.languageShort = "VI"; kb.languageTitle = "Tiếng Việt"
                kb.backgroundColor = dark ? UIColor(white: 0.17, alpha: 1) : UIColor(red: 0.82, green: 0.83, blue: 0.86, alpha: 1)
                kb.suggestions = s
                kb.layoutIfNeeded()
                let img = UIGraphicsImageRenderer(bounds: kb.bounds).image { ctx in kb.layer.render(in: ctx.cgContext) }
                try? FileManager.default.createDirectory(atPath: "/tmp/claude-501/layouts", withIntermediateDirectories: true)
                try? img.pngData()?.write(to: URL(fileURLWithPath: "/tmp/claude-501/layouts/brand_\(name)_\(dark ? "dark" : "light").png"))
            }
        }
    }
}

/// The corner mark's opening plays at most once every 10 minutes (Tony).
final class MarkIntroThrottleTests: XCTestCase {
    func testIntroAtMostEveryTenMinutes() {
        KeyboardView.barOn = true; KeyboardView.langStyle = 1
        let key = "markIntroLast"
        UserDefaults.standard.removeObject(forKey: key)
        let kb = KeyboardView(frame: CGRect(x: 0, y: 0, width: 402, height: KeyboardView.keyboardHeight(compactHeight: false)))
        kb.layoutIfNeeded()
        kb.playMarkIntro()
        let first = UserDefaults.standard.double(forKey: key)
        XCTAssertGreaterThan(first, 0, "the first opening plays")
        kb.playMarkIntro()
        XCTAssertEqual(UserDefaults.standard.double(forKey: key), first, "a second opening right after does not")
        UserDefaults.standard.set(first - 601, forKey: key)
        kb.playMarkIntro()
        XCTAssertGreaterThan(UserDefaults.standard.double(forKey: key), first - 601, "after 10 minutes it plays again")
        UserDefaults.standard.removeObject(forKey: key)
    }
}

/// The key under the corner element never shows, even when the strip came on after the keyboard was built
/// (Tony, build 51: a key-coloured square behind the pill in dark mode).
final class CornerKeyInvisibleTests: XCTestCase {
    func testLanguageKeyClearAfterStripTurnsOn() {
        KeyboardView.barOn = false; KeyboardView.langStyle = 1
        let kb = KeyboardView(frame: CGRect(x: 0, y: 0, width: 402, height: KeyboardView.keyboardHeight(compactHeight: false)))
        kb.dark = true
        kb.layoutIfNeeded()
        KeyboardView.barOn = true
        kb.setNeedsLayout(); kb.layoutIfNeeded()
        let cap = kb.subviews.compactMap { $0 as? KeyCap }.first { $0.frame.minY < 40 && $0.frame.minX < 20 }
        XCTAssertNotNil(cap)
        XCTAssertEqual(cap?.backgroundColor, .clear)
        XCTAssertEqual(cap?.normalColor, .clear)
    }
}

/// Nudges in the empty suggestion strip (Tony's words): rendered at rest, in light and dark.
final class NudgeRenderTests: XCTestCase {
    func testRenderNudges() {
        KeyboardView.barOn = true
        KeyboardView.langStyle = 1
        for dark in [false, true] {
            for k in [NudgeView.Kind.review, .bug] {
                let kb = KeyboardView(frame: CGRect(x: 0, y: 0, width: 402, height: KeyboardView.keyboardHeight(compactHeight: false)))
                kb.dark = dark
                kb.languageShort = "VI"; kb.languageTitle = "Tiếng Việt"
                kb.backgroundColor = dark ? UIColor(white: 0.17, alpha: 1) : UIColor(red: 0.82, green: 0.83, blue: 0.86, alpha: 1)
                kb.layoutIfNeeded()
                kb.bar.showNudge(k)
                kb.bar.layer.removeAllAnimations()
                kb.bar.subviews.forEach { v in v.layer.removeAllAnimations(); v.subviews.forEach { $0.layer.removeAllAnimations() } }
                RunLoop.main.run(until: Date().addingTimeInterval(2.5))
                kb.layoutIfNeeded()
                let img = UIGraphicsImageRenderer(bounds: kb.bounds).image { ctx in kb.layer.render(in: ctx.cgContext) }
                try? FileManager.default.createDirectory(atPath: "/tmp/claude-501/layouts", withIntermediateDirectories: true)
                try? img.pngData()?.write(to: URL(fileURLWithPath: "/tmp/claude-501/layouts/nudge_\(k.rawValue)_\(dark ? "dark" : "light").png"))
            }
        }
    }
}

/// When the nudges show (Nudges.swift), on a clock the test moves.
final class NudgesTests: XCTestCase {
    private var t = Date(timeIntervalSince1970: 1_800_000_000)
    private func make() -> Nudges {
        let d = UserDefaults(suiteName: "nudges-test-\(UUID().uuidString)")!
        return Nudges(defaults: d, clock: { [unowned self] in self.t })
    }
    func testReviewAfter100CleanSwitches() {
        let n = make()
        for _ in 0..<99 { n.autoSwitched() }
        XCTAssertNil(n.due())
        n.autoSwitched()
        XCTAssertEqual(n.due(), .review)
        n.shown(.review)
        XCTAssertNil(n.due(), "once a day at most")
        t += 86401
        XCTAssertEqual(n.due(), .review)
        n.closed(.review)
        t += 86401
        XCTAssertNil(n.due(), "✕: again at 500")
        for _ in 0..<400 { n.autoSwitched() }
        XCTAssertEqual(n.due(), .review)
        n.acted(.review)
        t += 86401
        XCTAssertNil(n.due(), "rated: never again")
    }
    func testNoReviewRightAfterAnUndo() {
        let n = make()
        for _ in 0..<95 { n.autoSwitched() }
        n.undone()
        for _ in 0..<10 { n.autoSwitched() }
        XCTAssertNil(n.due(), "an undo in the last 20 switches")
        for _ in 0..<10 { n.autoSwitched() }
        XCTAssertEqual(n.due(), .review)
    }
    func testBugAfterTheSecondUndoInADay() {
        let n = make()
        n.undone()
        XCTAssertNil(n.due(afterUndo: true))
        n.undone()
        XCTAssertEqual(n.due(afterUndo: true), .bug)
        XCTAssertNil(n.due(), "only right after an undo")
        n.shown(.bug)
        n.undone()
        XCTAssertNil(n.due(afterUndo: true), "once a day")
        t += 86401; n.undone(); n.undone()
        XCTAssertEqual(n.due(afterUndo: true), .bug)
        n.closed(.bug)
        t += 86401; n.undone(); n.undone()
        XCTAssertNil(n.due(afterUndo: true), "✕: 2 weeks")
        t += 14 * 86400; n.undone(); n.undone()
        XCTAssertEqual(n.due(afterUndo: true), .bug)
    }
    func testManualSwitchRightAfterAnAutoOneIsAnUndo() {
        let n = make()
        n.autoSwitched(); t += 2; n.manualSwitch()
        n.autoSwitched(); t += 2; n.manualSwitch()
        XCTAssertEqual(n.due(afterUndo: true), .bug)
        let m = make()
        m.autoSwitched(); t += 10; m.manualSwitch(); m.manualSwitch()
        XCTAssertNil(m.due(afterUndo: true), "a switch long after isn't an undo")
    }
}
