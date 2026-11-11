import XCTest
import UIKit

/// Next words (the table, Apple Intelligence on top), proofread with a preview, other words for one word.
/// The model is stubbed: these test the keyboard's flow, not Apple's answers.
final class AIAssistFlowTests: XCTestCase {
    private let sender = KeyboardView()

    override func setUp() { super.setUp(); KeyboardViewController.wordCheckDelay = 0 }
    override func tearDown() { AIAssist.stub = nil; KeyboardViewController.wordCheckDelay = 300_000_000; super.tearDown() }

    private func make(_ lang: Int = 0) -> (KeyboardViewController, CursorProxy) {
        let vc = KeyboardViewController()
        let p = CursorProxy()
        vc.proxy = p
        vc.loadViewIfNeeded()
        vc.view.frame = CGRect(x: 0, y: 0, width: 402, height: 300)
        vc.view.layoutIfNeeded()
        vc.viewWillAppear(false)
        vc.setLanguageForTests(lang)
        return (vc, p)
    }
    private func type(_ s: String, _ vc: KeyboardViewController) {
        for ch in s { vc.keyboard(sender, didTap: ch == " " ? .space : .char(String(ch).lowercased())) }
    }
    /// Lets the queued strip update and the model's (stubbed) answer land.
    /// With a condition, waits up to 10 s for it (CI machines are slow: the word check alone waits 0.3 s first);
    /// without one, just lets the queued work run.
    private func settle(_ until: (() -> Bool)? = nil, timeout: TimeInterval? = nil) {
        let end = Date().addingTimeInterval(timeout ?? (until == nil ? 0.5 : 10))
        let until = until ?? { false }
        repeat { RunLoop.main.run(until: Date().addingTimeInterval(0.02)) } while !until() && Date() < end
    }
    private func strip(_ vc: KeyboardViewController) -> [Suggestion] { vc.keyboardViewForTests.suggestions }
    private func select(_ p: CursorProxy, _ vc: KeyboardViewController, _ part: String) {
        let r = p.text.range(of: part)!
        p.cursor = p.text.distance(from: p.text.startIndex, to: r.lowerBound); p.selection = part.count
        vc.selectionDidChange(nil)
    }

    // the table
    func testTableVietnameseAndEnglish() {
        XCTAssertEqual(NextWords.shared.predict(before: "cảm ơn ").first, "bạn")
        XCTAssertTrue(NextWords.shared.predict(before: "thank you ").contains("for"))
        XCTAssertTrue(NextWords.shared.predict(before: "cảm ơn").isEmpty, "only after a space")
    }

    func testNextWordsAfterASpaceAndPick() {
        let (vc, p) = make(1)
        type("camr own ", vc)
        settle { self.strip(vc).first?.kind == .next }
        XCTAssertEqual(strip(vc).first?.text, "bạn")
        XCTAssertTrue(strip(vc).allSatisfy { $0.kind == .next })
        vc.keyboard(sender, pickSuggestion: 0)
        XCTAssertEqual(p.text, "Cảm ơn bạn ")
        settle { self.strip(vc).first?.kind == .next }
        XCTAssertFalse(strip(vc).isEmpty, "and the next ones")
    }

    // proofread: preview, then apply or cancel
    func testProofreadPreviewThenApplyThenUndo() {
        AIAssist.stub = .init(proofread: { _ in "I have gone to the store." })
        let (vc, p) = make()
        p.text = "i has went to the store"; p.cursor = 0
        select(p, vc, p.text)
        settle { self.strip(vc).first?.kind == .proofread }
        XCTAssertEqual(strip(vc).map(\.kind), [.proofread])
        vc.keyboard(sender, pickSuggestion: 0)
        settle { vc.proofreadPanelForTests != nil }
        XCTAssertNotNil(vc.proofreadPanelForTests, "a preview first")
        XCTAssertEqual(p.text, "i has went to the store", "nothing changes before Apply")
        vc.proofreadPanelForTests?.onApply?()
        XCTAssertEqual(p.text, "I have gone to the store.")
        XCTAssertNil(vc.proofreadPanelForTests)
        settle { self.strip(vc).first?.kind == .undo }
        XCTAssertEqual(strip(vc).map(\.kind), [.undo])
        vc.keyboard(sender, pickSuggestion: 0)
        XCTAssertEqual(p.text, "i has went to the store")
    }

    func testProofreadCancelKeepsTheText() {
        AIAssist.stub = .init(proofread: { _ in "Fixed." })
        let (vc, p) = make()
        p.text = "fixd it"; p.cursor = 0
        select(p, vc, p.text)
        settle { self.strip(vc).first?.kind == .proofread }
        vc.keyboard(sender, pickSuggestion: 0)
        settle { vc.proofreadPanelForTests != nil }
        vc.proofreadPanelForTests?.onCancel?()
        XCTAssertNil(vc.proofreadPanelForTests)
        XCTAssertEqual(p.text, "fixd it")
    }

    func testProofreadWithNoMistakesSaysSo() {
        AIAssist.stub = .init(proofread: { $0 })
        let (vc, p) = make()
        p.text = "All good here."; p.cursor = 0
        select(p, vc, p.text)
        settle { self.strip(vc).first?.kind == .proofread }
        vc.keyboard(sender, pickSuggestion: 0)
        settle { self.strip(vc).first?.kind == .busy && self.strip(vc).first?.text != String(localized: "Đang sửa…") }
        XCTAssertNil(vc.proofreadPanelForTests)
        XCTAssertEqual(strip(vc).first?.text, String(localized: "Không có lỗi ✓"))
    }

    func testApplyNeedsTheSameSelection() {
        AIAssist.stub = .init(proofread: { _ in "Two words." })
        let (vc, p) = make()
        p.text = "two wrds"; p.cursor = 0
        select(p, vc, p.text)
        settle { self.strip(vc).first?.kind == .proofread }
        vc.keyboard(sender, pickSuggestion: 0)
        settle { vc.proofreadPanelForTests != nil }
        p.selection = 0; p.cursor = 3            // the user moved away meanwhile
        vc.proofreadPanelForTests?.onApply?()
        XCTAssertEqual(p.text, "two wrds", "never replaces the wrong text")
    }

    // one word selected: its fix, read off a proofread of its sentence
    func testOneWordGetsItsFix() {
        AIAssist.stub = .init(proofread: { _ in "I went to the store yesterday." })
        let (vc, p) = make()
        p.text = "Hi. I went to the stroe yesterday. Bye"; p.cursor = 0
        select(p, vc, "stroe")
        settle { self.strip(vc).first?.kind == .alternative }
        XCTAssertEqual(strip(vc).map(\.text), ["store"])
        vc.keyboard(sender, pickSuggestion: 0)
        XCTAssertEqual(p.text, "Hi. I went to the store yesterday. Bye")
    }

    func testCorrectWordSaysNoMistakes() {
        AIAssist.stub = .init(proofread: { _ in "I went to the store yesterday." })
        let (vc, p) = make()
        p.text = "i went to the store yesterday"; p.cursor = 0
        select(p, vc, "store")
        settle { self.strip(vc).first?.text == String(localized: "Không có lỗi ✓") }
        XCTAssertEqual(strip(vc).map(\.text), [String(localized: "Không có lỗi ✓")])
    }

    func testWordFixAlignment() {
        func fix(_ w: String, _ i: Int, _ s: String, _ c: String) -> String? { WordFix.replacement(for: w, at: i, in: s, corrected: c) }
        XCTAssertEqual(fix("has", 1, "I has went to the store", "I have gone to the store."), "have")
        XCTAssertEqual(fix("lam", 4, "hôm nay tôi đi lam muộn", "Hôm nay tôi đi làm muộn."), "làm")
        XCTAssertEqual(fix("dont", 1, "she dont like it", "She doesn't like it."), "doesn't")
        XCTAssertEqual(fix("banj", 2, "cảm ơn banj nhiều", "Cảm ơn bạn nhiều."), "bạn")
        XCTAssertEqual(fix("mesage", 3, "I recieve your mesage", "I received your message."), "message", "no added period")
        XCTAssertNil(fix("store", 4, "I went to the store", "I went to the store."), "only punctuation changed")
        XCTAssertNil(fix("hello", 0, "hello there", "Hello there."), "only case changed")
        XCTAssertEqual(fix("alot", 1, "thanks alot", "Thanks a lot."), "a lot", "one word into two")
        let (h, t) = WordFix.sentence(before: "First one. Then the ", after: " ends here. Next")
        XCTAssertEqual(h, "Then the "); XCTAssertEqual(t, " ends here.")
    }

    func testNoSelectionFeaturesWithoutAppleIntelligence() {
        guard !AIAssist.available else { return }   // a simulator/phone with the model: nothing to check here
        let (vc, p) = make()
        p.text = "some words here"; p.cursor = 0
        select(p, vc, p.text)
        settle()
        XCTAssertFalse(strip(vc).contains { $0.kind == .proofread || $0.kind == .busy })
    }

    // state: nothing from one selection outlives it (Tony: "fixed" / "no mistake" stuck after selecting elsewhere)
    func testNoMistakesGoesAwayWhenSelectingElsewhere() {
        AIAssist.stub = .init(proofread: { $0 == "I went to the store yesterday." ? $0 : "I went to the store yesterday." })
        let (vc, p) = make()
        p.text = "I went to the store yesterday. All fine"; p.cursor = 0
        select(p, vc, "store")
        settle { if case .word(_, .noMistakes) = vc.aiStateForTests { return true }; return false }
        select(p, vc, "All fine")
        settle()
        XCTAssertEqual(vc.aiStateForTests, .text("All fine", .ready))
        XCTAssertEqual(strip(vc).map(\.kind), [.proofread])
        p.selection = 0; vc.selectionDidChange(nil)
        settle()
        XCTAssertEqual(vc.aiStateForTests, .none)
        XCTAssertFalse(strip(vc).contains { $0.kind == .busy || $0.kind == .proofread || $0.kind == .alternative })
    }

    func testALateAnswerForAnOldSelectionIsDropped() {
        AIAssist.stub = .init(proofread: { _ in "Fixed text here." })
        let (vc, p) = make()
        p.text = "fixd text here and more words"; p.cursor = 0
        select(p, vc, "fixd text here")
        settle { vc.aiStateForTests == .text("fixd text here", .ready) }
        vc.keyboard(sender, pickSuggestion: 0)                  // asked…
        select(p, vc, "more words")                              // …and moved on before the answer
        settle()
        XCTAssertNil(vc.proofreadPanelForTests, "no preview for text that isn't selected any more")
        XCTAssertEqual(vc.aiStateForTests, .text("more words", .ready))
    }

    func testSelectingOtherTextClosesThePreview() {
        AIAssist.stub = .init(proofread: { _ in "Two words." })
        let (vc, p) = make()
        p.text = "two wrds and other text"; p.cursor = 0
        select(p, vc, "two wrds")
        settle { vc.aiStateForTests == .text("two wrds", .ready) }
        vc.keyboard(sender, pickSuggestion: 0)
        settle { vc.proofreadPanelForTests != nil }
        XCTAssertTrue(vc.keyboardViewForTests.isHidden, "the preview takes the whole keyboard")
        select(p, vc, "other text")
        XCTAssertNil(vc.proofreadPanelForTests)
        XCTAssertFalse(vc.keyboardViewForTests.isHidden)
    }

    func testCancelShowsTheKeysAgainAndNothingSticks() {
        AIAssist.stub = .init(proofread: { _ in "Fixed it." })
        let (vc, p) = make()
        p.text = "fixd it"; p.cursor = 0
        select(p, vc, p.text)
        settle { vc.aiStateForTests == .text("fixd it", .ready) }
        vc.keyboard(sender, pickSuggestion: 0)
        settle { vc.proofreadPanelForTests != nil }
        vc.proofreadPanelForTests?.onCancel?()
        XCTAssertFalse(vc.keyboardViewForTests.isHidden)
        XCTAssertEqual(vc.aiStateForTests, .text("fixd it", .ready))
        XCTAssertEqual(p.text, "fixd it")
    }

    func testMovingAwayEndsUndo() {
        AIAssist.stub = .init(proofread: { _ in "Fixed it." })
        let (vc, p) = make()
        p.text = "start fixd it"; p.cursor = 0
        select(p, vc, "fixd it")
        settle { vc.aiStateForTests == .text("fixd it", .ready) }
        vc.keyboard(sender, pickSuggestion: 0)
        settle { vc.proofreadPanelForTests != nil }
        vc.proofreadPanelForTests?.onApply?()
        settle { self.strip(vc).first?.kind == .undo }
        p.cursor = 2; vc.selectionDidChange(nil)                // tapped somewhere else
        settle()
        XCTAssertEqual(vc.aiStateForTests, .none)
    }

    func testWordFixReplacesOnlyThatWordAndOffersUndo() {
        AIAssist.stub = .init(proofread: { _ in "We were late." })
        let (vc, p) = make()
        p.text = "We was late"; p.cursor = 0
        select(p, vc, "was")
        settle { self.strip(vc).first?.kind == .alternative }
        XCTAssertEqual(strip(vc).map(\.text), ["were"])
        vc.keyboard(sender, pickSuggestion: 0)
        XCTAssertEqual(p.text, "We were late")
        settle { self.strip(vc).first?.kind == .undo }
        vc.keyboard(sender, pickSuggestion: 0)
        XCTAssertEqual(p.text, "We was late")
    }

    func testSpacesAroundASelectionStay() {
        AIAssist.stub = .init(proofread: { _ in "Hello world." })
        let (vc, p) = make()
        p.text = "a  helo wrld \nb"; p.cursor = 1; p.selection = 12   // "  helo wrld " (spaces only around it)
        vc.selectionDidChange(nil)
        settle { if case .text(_, .ready) = vc.aiStateForTests { return true }; return false }
        vc.keyboard(sender, pickSuggestion: 0)
        settle { vc.proofreadPanelForTests != nil }
        vc.proofreadPanelForTests?.onApply?()
        XCTAssertEqual(p.text, "a  Hello world. \nb", "the spaces around it stay")
    }

    // what comes back from the model
    func testChatterAndTranslationsAreRejected() {
        XCTAssertNil(ProofreadCheck.clean("Sure, here are the corrected versions of the text:\n\n1. \"this .\" should be \"this.\"", original: "this . is a test , hello"))
        XCTAssertNil(ProofreadCheck.clean("Today I went to work late.", original: "Hom nay toi di lam muon"))
        XCTAssertNil(ProofreadCheck.clean("Sure, I'd be happy to help you write an email! What is the purpose of the email and who is the recipient?", original: "can you help me write an email"))
        XCTAssertEqual(ProofreadCheck.clean("\"Hôm nay tôi đi làm muộn.\"", original: "Hom nay toi di lam muon"), "Hôm nay tôi đi làm muộn.")
        XCTAssertEqual(ProofreadCheck.clean("<<<\nWe were late.\n>>>", original: "we was late"), "We were late.")
        XCTAssertEqual(ProofreadCheck.clean("Okay.", original: "ok"), "Okay.")
    }

    func testFailedAnswerSaysCouldntFix() {
        AIAssist.stub = .init(proofread: { _ in "Sure! Here is a long answer that has nothing at all to do with the text you selected." })
        let (vc, p) = make()
        p.text = "fixd it now"; p.cursor = 0
        select(p, vc, p.text)
        settle { vc.aiStateForTests == .text("fixd it now", .ready) }
        vc.keyboard(sender, pickSuggestion: 0)
        settle { vc.aiStateForTests == .text("fixd it now", .failed) }
        XCTAssertEqual(strip(vc).first?.text, String(localized: "Không sửa được · Thử lại"))
        XCTAssertNil(vc.proofreadPanelForTests)
    }

    // review findings (each was a way to write the wrong text or show a stale state)
    func testTypingOverASelectedJustTypedWordReplacesIt() {
        AIAssist.stub = .init(proofread: { $0 })
        let (vc, p) = make(1)
        type("the toi", vc)
        select(p, vc, "toi")                                   // the word the engine was still typing
        type("s", vc)
        XCTAssertEqual(p.text, "The s")
    }

    func testNoNextWordsWhileTextIsSelected() {
        let (vc, p) = make()                                       // no Apple Intelligence (no stub)
        p.text = "hello there and more"; p.cursor = 6
        select(p, vc, "there and")
        settle()
        XCTAssertFalse(strip(vc).contains { $0.kind == .next }, "a tap would replace the selection")
    }

    func testFailureIsNotNoMistakesAndCanRetry() {
        var answer: String? = nil
        AIAssist.stub = .init(proofread: { _ in answer })
        let (vc, p) = make()
        p.text = "We was late"; p.cursor = 0
        select(p, vc, "was")
        settle { vc.aiStateForTests == .word("was", .failed) }
        XCTAssertEqual(strip(vc).first?.text, String(localized: "Không sửa được · Thử lại"))
        answer = "We were late."
        vc.keyboard(sender, pickSuggestion: 0)                    // retry
        settle { self.strip(vc).first?.kind == .alternative }
        XCTAssertEqual(strip(vc).map(\.text), ["were"])
    }

    func testShiftAndLanguageKeysKeepTheSelectionState() {
        AIAssist.stub = .init(proofread: { _ in "Fixed it now." })
        let (vc, p) = make()
        p.text = "fixd it now"; p.cursor = 0
        select(p, vc, p.text)
        settle { vc.aiStateForTests == .text("fixd it now", .ready) }
        vc.keyboard(sender, didTap: .shift)
        vc.keyboard(sender, didTap: .language)
        settle()
        XCTAssertEqual(vc.aiStateForTests, .text("fixd it now", .ready))
        XCTAssertEqual(strip(vc).map(\.kind), [.proofread])
    }

    func testCancelGoesBackToProofread() {
        AIAssist.stub = .init(proofread: { _ in "Fixed it now." })
        let (vc, p) = make()
        p.text = "fixd it now"; p.cursor = 0
        select(p, vc, p.text)
        settle { vc.aiStateForTests == .text("fixd it now", .ready) }
        vc.keyboard(sender, pickSuggestion: 0)
        settle { vc.proofreadPanelForTests != nil }
        vc.proofreadPanelForTests?.onCancel?()
        settle()
        XCTAssertEqual(vc.aiStateForTests, .text("fixd it now", .ready), "still selected: offer it again")
        XCTAssertEqual(p.text, "fixd it now")
    }

    func testOnlyEmojiOrPunctuationIsIgnored() {
        AIAssist.stub = .init(proofread: { $0 })
        let (vc, p) = make()
        p.text = "ok 😀 !! fine"; p.cursor = 3
        select(p, vc, "😀 !!")
        settle()
        XCTAssertEqual(vc.aiStateForTests, .none)
    }

    func testShortAnswersAndLeadInsAreChecked() {
        XCTAssertNil(ProofreadCheck.clean("Thank you.", original: "cam on"))
        XCTAssertNil(ProofreadCheck.clean("It's 3 PM.", original: "what time"))
        XCTAssertEqual(ProofreadCheck.clean("Cảm ơn.", original: "cam on"), "Cảm ơn.")
        XCTAssertEqual(ProofreadCheck.clean("Here is the corrected text: I went to the store yesterday and bought some apples for my mom.",
                                            original: "i has went to the store yesterday and buyed some apple for my mom"),
                       "I went to the store yesterday and bought some apples for my mom.")
    }

    func testMultiLineOrGappedSelectionsAreNotProofread() {
        AIAssist.stub = .init(proofread: { $0 })
        let (vc, p) = make()
        p.text = "She don't know it  The weather were bad"; p.cursor = 0   // what iOS handed over for 3 lines
        select(p, vc, p.text)
        settle { if case .text(_, .onePara) = vc.aiStateForTests { return true }; return false }
        XCTAssertEqual(strip(vc).first?.text, String(localized: "Hãy chọn trong một đoạn"))
        p.text = "line one\nline two"; p.cursor = 0
        select(p, vc, p.text)
        settle { if case .text(_, .onePara) = vc.aiStateForTests { return true }; return false }
        XCTAssertEqual(strip(vc).first?.kind, .busy)
    }

    func testTappingAStripNoteNeverTypesAKey() {
        AIAssist.stub = .init(proofread: { $0 })
        let (vc, p) = make()
        p.text = "All good here."; p.cursor = 0
        select(p, vc, p.text)
        settle { vc.aiStateForTests == .text("All good here.", .ready) }
        let kv = vc.keyboardViewForTests
        XCTAssertNil(kv.capForTests(at: CGPoint(x: 100, y: 20)), "the strip is not the Q-row")
        XCTAssertNotNil(kv.capForTests(at: CGPoint(x: 100, y: kv.bounds.height - 30)))
    }
}
