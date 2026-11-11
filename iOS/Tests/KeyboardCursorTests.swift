import XCTest
import UIKit

/// A text field with a cursor: what the keyboard sees before and after it, and taps that move it.
final class CursorProxy: NSObject, UITextDocumentProxy {
    var text = "", cursor = 0
    var selection = 0   // characters selected after the cursor (proofread / other-words tests)
    @objc var keyboardType: UIKeyboardType = .default
    private var chars: [Character] { Array(text) }
    var documentContextBeforeInput: String? { String(chars[..<cursor]) }
    var documentContextAfterInput: String? { String(chars[(cursor + selection)...]) }
    var selectedText: String? { selection > 0 ? String(chars[cursor..<cursor + selection]) : nil }
    var documentInputMode: UITextInputMode? { nil }
    var documentIdentifier: UUID { UUID() }
    var hasText: Bool { !text.isEmpty }
    func adjustTextPosition(byCharacterOffset offset: Int) { selection = 0; cursor = min(max(0, cursor + offset), chars.count) }
    func setMarkedText(_ markedText: String, selectedRange: NSRange) {}
    func unmarkText() {}
    private func dropSelection() { guard selection > 0 else { return }; var c = chars; c.removeSubrange(cursor..<cursor + selection); text = String(c); selection = 0 }
    func insertText(_ t: String) { dropSelection(); var c = chars; c.insert(contentsOf: Array(t), at: cursor); text = String(c); cursor += t.count }
    func deleteBackward() { if selection > 0 { dropSelection(); return }; guard cursor > 0 else { return }; var c = chars; c.remove(at: cursor - 1); text = String(c); cursor -= 1 }
}

/// Moving the cursor with a tap while typing (Tony): the keyboard must start over where the cursor is,
/// never edit the text it was typing before.
final class KeyboardCursorTests: XCTestCase {
    private let sender = KeyboardView()

    private func make() -> (KeyboardViewController, CursorProxy) {
        let vc = KeyboardViewController()
        let p = CursorProxy()
        vc.proxy = p
        vc.loadViewIfNeeded()
        vc.view.frame = CGRect(x: 0, y: 0, width: 402, height: 300)
        vc.view.layoutIfNeeded()
        vc.viewWillAppear(false)
        vc.setLanguageForTests(1)
        return (vc, p)
    }
    private func type(_ s: String, _ vc: KeyboardViewController) {
        for ch in s { vc.keyboard(sender, didTap: ch == " " ? .space : .char(String(ch))) }
    }
    private func backspace(_ vc: KeyboardViewController, _ n: Int = 1) { for _ in 0..<n { vc.keyboard(sender, didTap: .backspace) } }
    /// A tap in the app: the cursor moves and iOS tells the keyboard.
    private func tap(_ vc: KeyboardViewController, _ p: CursorProxy, at i: Int) {
        vc.textWillChange(nil)
        p.cursor = i
        vc.selectionDidChange(nil)
        vc.textDidChange(nil)
    }

    func testTapInsideTheWordJustTyped() {
        let (vc, p) = make()
        type("tooi", vc)
        XCTAssertEqual(p.text.lowercased(), "tôi")
        tap(vc, p, at: 2)                 // tô|i
        type("s", vc)
        XCTAssertEqual(p.text.lowercased(), "tôsi")    // a new word at the cursor, not a tone on "tôi"
    }

    func testTapBackToAnEarlierWord() {
        let (vc, p) = make()
        type("xin chao", vc)
        tap(vc, p, at: 3)                 // xin| chao
        type("h", vc)
        XCTAssertEqual(p.text.lowercased(), "xinh chao")
    }

    func testTapThenTypeTelexOnTheWordBeforeTheCursor() {
        let (vc, p) = make()
        type("toi di ", vc)
        tap(vc, p, at: 3)                 // toi| di
        type("s", vc)
        XCTAssertEqual(p.text.lowercased(), "tois di ")   // the keyboard didn't type "toi": a lone s stays a letter
    }

    func testBackspaceAfterTap() {
        let (vc, p) = make()
        vc.setLanguageForTests(0)
        type("hello world", vc)
        tap(vc, p, at: 5)                 // hello| world
        backspace(vc)
        XCTAssertEqual(p.text.lowercased(), "hell world")
        type("p", vc)
        XCTAssertEqual(p.text.lowercased(), "hellp world")
    }

    func testBackspaceAfterTapDoesNotUndoAnAutoCorrection() {
        let (vc, p) = make()
        vc.setLanguageForTests(0)
        type("teh ", vc)
        let corrected = p.text
        tap(vc, p, at: 1)
        backspace(vc)
        XCTAssertEqual(p.text, String(corrected.prefix(0)) + String(corrected.dropFirst()))   // just one character, the one before the cursor
    }

    func testBackspaceAfterTheKeyboardReappearsDoesNotUndoAnAutoCorrection() {
        let (vc, p) = make()
        vc.setLanguageForTests(0)
        type("teh ", vc)
        let corrected = p.text
        XCTAssertNotEqual(corrected.lowercased(), "teh ")   // auto-correct ran
        vc.viewWillDisappear(false); vc.viewWillAppear(false)   // keyboard hidden, then back on the same text
        backspace(vc)
        XCTAssertEqual(p.text, String(corrected.dropLast()))   // only the space goes, the correction stays
    }

    func testTapToTheSamePlaceKeepsTheWord() {
        let (vc, p) = make()
        type("tooi", vc)
        tap(vc, p, at: p.text.count)      // tapped where the cursor already was
        type("s", vc)
        XCTAssertEqual(p.text.lowercased(), "tối")
    }

    func testTapToTheEndAfterEditingInTheMiddle() {
        let (vc, p) = make()
        type("ban toi", vc)
        tap(vc, p, at: 3)
        type("j", vc)                     // after a tap, a word already in the field isn't picked up again: "banj"
        tap(vc, p, at: p.text.count)      // back to the end: … toi|
        type("f", vc)
        XCTAssertEqual(p.text.lowercased(), "banj toif")   // neither word is edited behind the cursor
    }

    func testRepeatedDeleteAfterTapDeletesBeforeTheCursor() {
        let (vc, p) = make()
        type("mot hai ba", vc)
        tap(vc, p, at: 7)                 // mot hai| ba
        for i in 1...25 { vc.keyboardBackspaceRepeat(sender, count: i) }
        XCTAssertTrue(p.text.lowercased().hasSuffix(" ba"), p.text)
    }

    func testHeldDeleteShortensTheWord() {
        let (vc, p) = make()
        vc.setLanguageForTests(0)
        type("isnt", vc)
        backspace(vc)                     // Delete held: a tap, then repeats
        for i in 1...2 { vc.keyboardBackspaceRepeat(sender, count: i) }
        XCTAssertEqual(p.text.lowercased(), "i")
        type("t ", vc)
        XCTAssertEqual(p.text.lowercased(), "it ")    // the word is "it", not "isnt" → "isn't"
    }

    func testAppUndoToAnEarlierStateStartsOver() {
        let (vc, p) = make()
        type("toi di", vc)
        vc.textDidChange(nil)             // the app has our last edit
        p.text = String(p.text.prefix(4)); p.cursor = 4   // the app's undo: "toi |", a state we produced
        vc.textDidChange(nil)
        type("s", vc)
        XCTAssertEqual(p.text.lowercased(), "toi s")  // not a tone on "di" deleting the space: "toií"
    }

    func testAppCatchingUpAfterADeleteIsNotAMove() {
        let (vc, p) = make()
        type("tooi", vc); backspace(vc); type("i", vc)   // tôi, tô, tôi
        let full = p.text
        vc.textDidChange(nil)             // the app, lagging, reads back tôi (before the delete), tô, tôi
        p.text = String(full.prefix(2)); p.cursor = 2
        vc.textDidChange(nil)
        p.text = full; p.cursor = full.count
        vc.textDidChange(nil)
        type("s", vc)
        XCTAssertEqual(p.text.lowercased(), "tối")    // still the same word
    }
}
