import UIKit

/// The Vikey keyboard: keys go through the same engine and driver as the Mac app
/// (Engine/Driver), and its edits are applied to the text field.
/// The keyboard's root view: adopting UIInputViewAudioFeedback is what makes playInputClick() click.
final class KeyboardInputView: UIInputView, UIInputViewAudioFeedback {
    var enableInputClicksWhenVisible: Bool { true }
}

final class KeyboardViewController: UIInputViewController, KeyboardViewDelegate {
    private let driver = VKDriver()
    private var keyboard: KeyboardView!
    var keyboardViewForTests: KeyboardView { keyboard }
    func setLanguageForTests(_ l: Int) { driver.language = l; driver.resetContext() }
    private var heightConstraint: NSLayoutConstraint?
    private var emojiView: EmojiKeyboardView?
    // Apple Intelligence on a selection (AIAssist): one state, replaced whenever the selection changes, so nothing
    // from an earlier selection ("No mistakes", a fix, the preview, Undo) can outlive it. `aiGen` tags each request;
    // an answer for an older state is dropped.
    enum AIState: Equatable {
        case none
        case word(String, Check)                  // one word selected: its fix
        case text(String, Check)                  // several words: ✨ Proofread, then the preview
        case preview(original: String, corrected: String)
        case applied(original: String, inserted: String, after: String)   // Undo, while the fix is still right there
    }
    enum Check: Equatable { case ready, working, fixes([String]), noMistakes, failed, tooLong, onePara }
    private(set) var aiState: AIState = .none
    private var aiGen = 0
    private var aiSelTask: Task<Void, Never>?
    private var aiNext: [String] = [], aiNextFor = ""
    private var aiNextTask: Task<Void, Never>?
    private var selection: String?
    private var selectionAt = ""            // the text just before it: the same word elsewhere is another selection
    private var proofreadPanel: ProofreadPanel?
    /// Longer selections aren't offered: the model's window is small, and the app may hand over only part of them.
    static let proofreadLimit = 1500
    /// Wait before checking a selected word (handles may still be moving); tests set 0.
    static var wordCheckDelay: UInt64 = 300_000_000
    /// The user's Text Replacements (Settings › General › Keyboard), like the system keyboard: "omw" → "On my way!"
    private var textReplacements: [String: String] = [:]
    func setTextReplacementsForTests(_ r: [String: String]) { textReplacements = r }
    var proofreadPanelForTests: ProofreadPanel? { proofreadPanel }
    var aiStateForTests: AIState { aiState }
    private var emojiQuery = ""
    /// Our own copy of the text before the cursor, kept up to date from our edits. Asking the app for it
    /// is a synchronous cross-process call, so it's only read when iOS says the text changed.
    private var lastContext: String?
    /// The text after the cursor when we last looked. Our own typing never changes it, so when it changes
    /// the cursor moved (a tap inside a word leaves the text before the cursor looking like one of our edits).
    private var lastAfter = ""
    private var recentContexts: [String] = []   // states we produced that the app may still be catching up to
    private var lastShiftTap: TimeInterval = 0
    private var lastSpace: TimeInterval = 0
    private var fieldLanguage: Int?           // language to restore after a web/email field
    private let languageKey = "Language"
    /// Letters typed so far in the current word (for the fat-finger model and auto-correct), with
    /// where each was touched and how many were capitals.
    private var wordPrefix = ""
    private var wordXs: [Float] = [], wordYs: [Float] = []
    private var wordCaps = 0
    private var candidates: [VKCandidate] = []     // for the strip; [0] is what auto-correct would use
    private var willCorrect = false
    /// What the end of this word will put in instead of it (auto-correct or a contraction), and its language.
    private var pendingFix: (text: String, vietnamese: Bool)?
    /// The last auto-correct, so the next backspace can put the typed word back.
    private var lastCorrection: (typed: String, inserted: String)?
    private var keepWord: String?                  // a word put back by backspace: don't correct it again
    /// The text field. Tests swap in a counting proxy (performance lock: one text read per key).
    lazy var proxy: UITextDocumentProxy = textDocumentProxy
    // settings, read once when the keyboard appears (reading shared defaults per key is slow)
    private var webFieldsEnglish = true, autoCapitalize = true, doubleSpacePeriod = true, haptics = true
    private var suggestionsOn = true, autoCorrectOn = true

    override func loadView() {
        let v = KeyboardInputView(frame: .zero, inputViewStyle: .keyboard)
        v.allowsSelfSizing = true
        inputView = v
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        KeyboardView.searchTop = nil   // left over from an emoji search in an earlier keyboard of this process
        keyboard = KeyboardView(frame: view.bounds)
        keyboard.delegate = self
        keyboard.wordContext = { [unowned self] in (self.wordPrefix, self.driver.language == 1) }
        keyboard.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(keyboard)
        NSLayoutConstraint.activate([
            keyboard.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            keyboard.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            keyboard.topAnchor.constraint(equalTo: view.topAnchor),
            keyboard.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])
        // The height must CHANGE in viewWillAppear: when the constant is already right before the keyboard appears,
        // iOS first shows it at an in-between height (a frame of a tall, empty keyboard, Tony, builds 40-49; it
        // happened whenever the keyboard's process was reused, so on most opens and when swiping between apps).
        // So it starts 1 pt off here and after every disappearance, and loadSettings() sets the real height.
        let h = view.heightAnchor.constraint(equalToConstant: KeyboardView.keyboardHeight(compactHeight: traitCollection.verticalSizeClass == .compact) - 1)
        // 999, and the input view sizes itself from it
        h.priority = UILayoutPriority(999)
        h.isActive = true
        heightConstraint = h
        (view as? UIInputView)?.allowsSelfSizing = true
        let saved = UserDefaults.standard.object(forKey: languageKey) as? Int ?? 1
        driver.language = saved
        // the emoji index is built when the emoji keyboard first opens, not here: at launch it pushed the keyboard
        // toward iOS's memory limit (iOS swapped in its own keyboard ~100 ms after Vikey opened, Tony)
        requestSupplementaryLexicon { [weak self] lex in
            var r: [String: String] = [:]
            for e in lex.entries where e.userInput.lowercased() != e.documentText.lowercased()   // (contact names list themselves)
                && e.userInput.allSatisfy({ $0.isLetter && $0.isASCII }) {
                r[e.userInput.lowercased()] = e.documentText
            }
            DispatchQueue.main.async { self?.textReplacements = r }
        }
    }

    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        setAI(.none); selection = nil
        AIAssist.refresh()   // off the main thread: whether the model is there
        keyboard.animatesLanguage = false   // the pill takes the saved language without rolling while the keyboard comes up
        comingUp = true
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in
            guard let self, self.comingUp else { return }
            self.comingUp = false; self.setContentVisible(true)
        }
        keyboard.resetTouches()   // a touch cut off when the keyboard last went away must not hold on to a key
        if let ev = emojiView, !ev.isHidden { emojiKeyboardClose(ev) }   // a new field opens on the letters
        if KeyboardView.searchTop != nil, emojiView?.searching != true {     // static: it outlives a closed keyboard
            KeyboardView.searchTop = nil
            heightConstraint?.constant = keyboardHeight
            keyboard.setNeedsLayout()
        }
        loadSettings()
        startField()
        refresh()
        if keyboard.haptics { keyboard.feedback.start() }
        if Diag.on { startStallWatch() }
        if Diag.on {
            KBLog.add("open: fullAccess=\(hasFullAccess) haptics=\(keyboard.haptics) field=\(proxy.keyboardType?.rawValue ?? -1) return=\(proxy.returnKeyType?.rawValue ?? -1) width=\(Int(view.bounds.width))")
        }
    }

    override func viewDidDisappear(_ animated: Bool) {
        super.viewDidDisappear(animated)
        heightConstraint?.constant -= 1   // the next viewWillAppear must change it (see viewDidLoad)
        hasAppeared = false
    }

    override func viewWillDisappear(_ animated: Bool) {
        stallWatch?.invalidate(); stallWatch = nil
        keyboard.feedback.stop()
        KBLog.flush()
        setAI(.none)                   // a late answer must not open the preview off screen
        super.viewWillDisappear(animated)
    }

    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        if Diag.on { KBLog.add("memory warning") ; KBLog.flush() }
    }

    override func traitCollectionDidChange(_ previous: UITraitCollection?) {
        super.traitCollectionDidChange(previous)
        let browsingEmoji = emojiView.map { !$0.isHidden && !$0.searching } ?? false
        if hasAppeared {   // before it appears, viewWillAppear sets the height (see viewDidLoad)
            heightConstraint?.constant = KeyboardView.keyboardHeight(compactHeight: traitCollection.verticalSizeClass == .compact)
                + (browsingEmoji ? EmojiKeyboardView.extraHeight : 0)
        }
        keyboard?.setNeedsLayout()
        if previous?.userInterfaceStyle != traitCollection.userInterfaceStyle { refresh() }
    }

    /// iOS can show the keyboard for a frame or two at its own, taller height before ours applies (Tony saw the keys
    /// at the top of a tall, half-empty keyboard): until the view has our height, its content stays invisible.
    /// Only while the keyboard comes up, and never longer than 0.3 s (a host that keeps another height still gets keys).
    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        guard comingUp, let h = heightConstraint?.constant else { return }
        setContentVisible(abs(view.bounds.height - h) <= 2)
    }
    private var comingUp = false
    private var hasAppeared = false
    private func setContentVisible(_ on: Bool) {
        let alpha: CGFloat = on ? 1 : 0
        if keyboard.alpha != alpha { keyboard.alpha = alpha }
        if let ev = emojiView, ev.alpha != alpha { ev.alpha = alpha }
    }

    override func viewWillLayoutSubviews() {
        super.viewWillLayoutSubviews()
        keyboard.showsGlobe = needsInputModeSwitchKey
        if let g = keyboard.globeButton {
            g.removeTarget(nil, action: nil, for: .allEvents)
            g.addTarget(self, action: #selector(handleInputModeList(from:with:)), for: .allTouchEvents)
        }
    }

    private func loadSettings() {
        VKSettings.register()
        let d = VKSettings.defaults
        driver.autoSwitch = d.bool(forKey: VKSettings.Key.autoSwitch)
        driver.pauseAfterManual = d.bool(forKey: VKSettings.Key.pauseAfterManual) ? 5 : 0
        driver.modernOrthography = d.bool(forKey: VKSettings.Key.modernOrthography)
        driver.telexStyle = d.integer(forKey: VKSettings.Key.telexStyle)
        driver.spelling = d.bool(forKey: VKSettings.Key.spelling)
        driver.restoreIfInvalid = d.bool(forKey: VKSettings.Key.restoreIfInvalid)
        webFieldsEnglish = d.bool(forKey: VKSettings.Key.englishForWebFields)
        autoCapitalize = d.bool(forKey: VKSettings.Key.autoCapitalize)
        doubleSpacePeriod = d.bool(forKey: VKSettings.Key.doubleSpacePeriod)
        haptics = d.bool(forKey: VKSettings.Key.haptics)
        suggestionsOn = d.bool(forKey: VKSettings.Key.suggestions)
        autoCorrectOn = d.bool(forKey: VKSettings.Key.autoCorrect)
        KeyboardView.barOn = suggestionsOn || autoCorrectOn
        Diag.on = d.bool(forKey: VKSettings.Key.diagnostics)
        keyboard?.diagnostics = Diag.on ? String(localized: "Gõ để xem thông số") : nil
        let browsingEmoji = emojiView.map { !$0.isHidden && !$0.searching } ?? false
        heightConstraint?.constant = KeyboardView.keyboardHeight(compactHeight: traitCollection.verticalSizeClass == .compact)
            + (browsingEmoji ? EmojiKeyboardView.extraHeight : 0)
        keyboard?.setNeedsLayout()
        keyboard?.haptics = haptics && hasFullAccess   // haptics need Full Access; without it keys click
    }

    /// A new text field: start fresh; web addresses and email start in English (and go back after).
    /// The layout for the field, like the system keyboard: a number pad for number/phone/decimal fields,
    /// the 123 page for numbers-and-punctuation, @ . / .com keys for email and web fields.
    private func applyFieldLayout() {
        let t = proxy.keyboardType ?? .default
        keyboard.fieldType = t
        switch t {
        case .numberPad, .asciiCapableNumberPad, .phonePad, .decimalPad: keyboard.layer_ = .numpad
        case .numbersAndPunctuation: keyboard.layer_ = .numbers
        default: if keyboard.layer_ == .numpad { keyboard.layer_ = .letters }
        }
    }

    private func startField() {
        driver.resetContext()
        applyFieldLayout()
        let type = proxy.keyboardType ?? .default
        let webField = type == .URL || type == .emailAddress || type == .webSearch
        if webField && webFieldsEnglish {
            if fieldLanguage == nil { fieldLanguage = driver.language }
            driver.language = 0
        } else if let l = fieldLanguage {
            driver.language = l
            fieldLanguage = nil
        }
        lastContext = proxy.documentContextBeforeInput
        lastAfter = proxy.documentContextAfterInput ?? ""
        recentContexts = []   // the last field's states mean nothing here
        lastCorrection = nil; keepWord = nil   // nor its auto-correct: Backspace here deletes, it doesn't undo an old fix
        clearWord()
        updateAutoShift(lastContext ?? "")
    }

    /// Auto-correct and suggestions only where the system keyboard offers them.
    private var correctsHere: Bool {
        if proxy.autocorrectionType == .no || proxy.spellCheckingType == .no { return false }
        let t = proxy.keyboardType ?? .default
        return !(t == .URL || t == .emailAddress || t == .webSearch || t == .numberPad || t == .decimalPad || t == .asciiCapableNumberPad)
    }

    private var now: Double { ProcessInfo.processInfo.systemUptime }

    private func refresh() {
        keyboard.dark = proxy.keyboardAppearance == .dark || traitCollection.userInterfaceStyle == .dark
        keyboard.languageTitle = driver.language == 1 ? "Tiếng Việt" : "English"
        keyboard.languageShort = driver.language == 1 ? "VI" : "EN"
        let (title, blue) = Self.returnTitle(proxy.returnKeyType ?? .default)
        keyboard.returnTitle = title
        keyboard.returnIsBlue = blue
        emojiView?.dark = keyboard.dark
        proofreadPanel?.dark = keyboard.dark
        if emojiView?.searching == true { updateEmojiQuery() }   // VI/EN changes how the query reads
    }

    private static func returnTitle(_ t: UIReturnKeyType) -> (String, Bool) {
        switch t {
        case .go: return (String(localized: "Đi"), true)
        case .search, .google, .yahoo: return (String(localized: "Tìm"), true)
        case .send: return (String(localized: "Gửi"), true)
        case .done: return (String(localized: "Xong"), true)
        case .next: return (String(localized: "Tiếp"), false)
        case .join: return (String(localized: "Tham gia"), true)
        case .route: return (String(localized: "Chỉ đường"), true)
        case .continue: return (String(localized: "Tiếp tục"), true)
        case .emergencyCall: return ("SOS", true)
        default: return ("return", false)
        }
    }

    // MARK: text changes from outside (cursor moved, field cleared, another keyboard)

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        keyboard.animatesLanguage = true
        hasAppeared = true
        comingUp = false; setContentVisible(true)
        keyboard.playMarkIntro()
        readSelection()   // opened onto text that's already selected
        if !showDebugNudge() { nudgeSoon(after: 2.3) }   // after the mark's intro
    }

    /// When the strip's nudges show (Nudges.swift).
    private let nudges = Nudges()
    private var nudgeWired = false
    /// The nudge due, if any, a moment from now, on an empty strip while no word is being typed.
    private func nudgeSoon(afterUndo: Bool = false, after delay: Double = 0.8) {
        if !nudgeWired {
            nudgeWired = true
            keyboard.onNudge = { [weak self] k, acted in if acted { self?.nudges.acted(k) } else { self?.nudges.closed(k) } }
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
            guard let self, self.keyboard.suggestions.isEmpty, self.wordPrefix.isEmpty, self.keyboard.bar.nudge == nil,
                  let k = self.nudges.due(afterUndo: afterUndo) else { return }
            self.nudges.shown(k)
            self.keyboard.bar.showNudge(k)
        }
    }

    /// The app's debug menu asked for a nudge (VKSettings.Key.debugNudge): shown once, after the mark's intro.
    /// The keyboard only reads the shared defaults, so what it showed is kept in its own.
    @discardableResult private func showDebugNudge() -> Bool {
        guard let v = VKSettings.defaults.string(forKey: VKSettings.Key.debugNudge), v != UserDefaults.standard.string(forKey: "debugNudgeShown"),
              let k = NudgeView.Kind(rawValue: String(v.split(separator: ":").first ?? "")) else { return false }
        UserDefaults.standard.set(v, forKey: "debugNudgeShown")
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.3) { [weak self] in self?.keyboard.bar.showNudge(k) }
        return true
    }

    override func textDidChange(_ textInput: UITextInput?) {
        super.textDidChange(textInput)
        if emojiView?.searching != true, (proxy.keyboardType ?? .default) != keyboard.fieldType { startField() }   // moved to another kind of field
        readSelection()
        externalChange()
        refresh()
    }

    override func selectionDidChange(_ textInput: UITextInput?) {
        super.selectionDidChange(textInput)
        externalChange()
        readSelection()
    }

    /// What's selected in the app (only read when iOS says the selection or text changed, never per key).
    private func readSelection() {
        let raw = proxy.selectedText
        let sel = raw.flatMap { $0.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? nil : $0 }
        let at = sel == nil ? "" : String((proxy.documentContextBeforeInput ?? "").suffix(40))
        guard sel != selection || at != selectionAt else {
            if sel == nil, case .applied = aiState, !undoStillFits() { setAI(.none) }   // the user tapped elsewhere
            return
        }
        selection = sel; selectionAt = at
        selectionChanged()
    }

    /// Typing into a selection replaces it: whatever the engine thought it was typing is gone.
    private func selectionEdited() {
        if selection != nil || proxy.selectedText != nil { driver.resetContext(); clearWord() }
        if aiState != .none { setAI(.none) }
        selection = nil; selectionAt = ""
    }

    private func externalChange() {
        let r0 = CACurrentMediaTime()
        let ctx = proxy.documentContextBeforeInput
        if Diag.on { KBLog.add("text read (iOS said the text changed): \(Self.time((CACurrentMediaTime() - r0) * 1000))") }
        let after = proxy.documentContextAfterInput ?? ""
        let moved = after != lastAfter
        // the app is catching up with our edits. It reads them back in order, so the ones before are done with:
        // the app going back to one of them later (its undo, a field cleared after Send) is a change
        if !moved, selection == nil, let c = ctx, let i = recentContexts.firstIndex(of: c) { recentContexts.removeFirst(i + 1); return }
        if moved || (ctx != lastContext && !Self.sameEnding(ctx ?? "", lastContext ?? "")) {
            if Diag.on { KBLog.add("reset: text changed outside the keyboard (cursor moved, or the app changed it)") }
            driver.resetContext()
            clearWord()
            lastCorrection = nil; keepWord = nil
            recentContexts = []
            lastContext = ctx
            lastAfter = after
            updateAutoShift(ctx ?? "")
            if aiState != .none { scheduleSuggestions() }
        }
    }

    // MARK: keys

    func keyboard(_ view: KeyboardView, didTap kind: KeyKind) {
        if emojiView?.searching == true, emojiSearchKey(kind) { return }
        switch kind {      // keys that edit end whatever was going on with a selection; shift, 123, VI/EN don't
        case .char, .space, .backspace, .returnKey: selectionEdited()
        default: break
        }
        if Diag.on {
            switch kind {
            case .char(let c): Diag.lastKind = c.first?.isLetter == true ? "letter" : "symbol"
            case .space: Diag.lastKind = "space"
            case .backspace: Diag.lastKind = "delete"
            case .returnKey: Diag.lastKind = "return"
            default: Diag.lastKind = "other"
            }
        }
        switch kind {
        case .char(let c) where c.count > 1:   // ".com"
            clearWord()
            for ch in c { type(String(ch)) }
        case .char(let c):
            if snapsBack(c) {   // "this ." → "this." like the system keyboard (Tony); our own delete, so not a Backspace undo of an auto switch
                proxy.deleteBackward(); driver.resetContext(); afterEdit(deleted: 1, inserted: "")
            }
            if c == "-", (lastContext ?? "").hasSuffix("-"), !(lastContext ?? "").hasSuffix("--"),
               (proxy.smartDashesType ?? .default) != .no {   // smart dashes: "--" → "—"
                backspace(); clearWord(); type("—"); break
            }
            lastCorrection = nil
            if [".", ",", "?", "!"].contains(c), !wordPrefix.isEmpty, autoCorrectAtBreak(c) {   // "dont?" → "don't?"
                if c != "," { updateAutoShift(lastContext ?? "") }
                break
            }
            if let ch = c.first, ch.isLetter, ch.isASCII, wordPrefix.count < 30 {
                wordPrefix.append(ch)
                let t = keyboard.lastTapPoint ?? CGPoint(x: CGFloat.nan, y: .nan)
                keyboard.lastTapPoint = nil
                wordXs.append(Float(t.x)); wordYs.append(Float(t.y))
                if keyboard.layer_ == .letters && keyboard.shift != .off { wordCaps += 1 }
            } else { clearWord() }
            var s = c
            if keyboard.layer_ == .letters && keyboard.shift != .off { s = c.uppercased() }
            type(s)
            if keyboard.shift == .on { keyboard.shift = .off }
            if keyboard.layer_ != .letters && (c == "'" ) { keyboard.layer_ = .letters }
        case .space:
            let ended = (lastContext ?? "").last.map { ".!?".contains($0) } ?? false
            if !autoCorrectAtBreak() { spaceKey() }
            clearWord()
            if ended { nudgeSoon() }   // a sentence's end: a quiet moment
        case .returnKey:
            clearWord()
            if keyboard.layer_ == .numbers || keyboard.layer_ == .symbols { keyboard.layer_ = .letters }   // like space: a new line starts on ABC (Tony)
            type("\n")
        case .backspace:
            if undoCorrection() { nudges.undone(); nudgeSoon(afterUndo: true); break }
            backspace()
        case .shift:
            let t = now
            if keyboard.shift == .locked { keyboard.shift = .off }
            else if t - lastShiftTap < 0.35 { keyboard.shift = .locked }
            else { keyboard.shift = keyboard.shift == .on ? .off : .on }
            lastShiftTap = t
        case .layer(let l):
            keyboard.layer_ = l
            if l == .letters { updateAutoShift(lastContext ?? "") }   // back on ABC after a new line or ". ": the capital shows
        case .language:
            nudges.manualSwitch(); nudgeSoon(afterUndo: true)
            lastCorrection = nil
            driver.manualSwitch(at: now)
            fieldLanguage = nil
            saveLanguage()
            refresh()
        case .globe:
            advanceToNextInputMode()
        case .emoji:
            showEmoji()
        }
    }

    func keyboardBackspaceRepeat(_ view: KeyboardView, count n: Int) {
        selectionEdited()
        // like the system keyboard: characters first, whole words after ~2 seconds of holding
        guard n > 20 else { backspace(); return }
        let before = lastContext ?? ""
        guard !before.isEmpty else { return }
        var chars = Array(before), k = 0
        while let last = chars.last, last == " " { chars.removeLast(); k += 1 }
        while let last = chars.last, last != " " && last != "\n" { chars.removeLast(); k += 1 }
        for _ in 0..<max(k, 1) { proxy.deleteBackward() }
        driver.resetContext()
        clearWord()
        afterEdit(deleted: max(k, 1), inserted: "")
    }

    func keyboard(_ view: KeyboardView, insertAlternate text: String) {
        selectionEdited()
        clearWord()
        for ch in text { type(String(ch)) }   // one character at a time (".vn"); unknown ones pass through and start a new word
        if keyboard.shift == .on { keyboard.shift = .off }
    }

    func keyboard(_ view: KeyboardView, moveCursorBy offset: Int) {
        proxy.adjustTextPosition(byCharacterOffset: offset)
    }

    func keyboardCursorModeEnded(_ view: KeyboardView) {
        driver.resetContext()
        clearWord()
        lastCorrection = nil; keepWord = nil
        lastContext = proxy.documentContextBeforeInput
        lastAfter = proxy.documentContextAfterInput ?? ""
        updateAutoShift(lastContext ?? "")
    }

    private func spaceKey() {
        // double space: ". " like the system keyboard
        let before = lastContext ?? ""
        if doubleSpacePeriod, now - lastSpace < 0.6, before.hasSuffix(" "),
           let prev = before.dropLast().last, prev.isLetter || prev.isNumber {
            proxy.deleteBackward()
            proxy.insertText(". ")
            driver.resetContext()
            lastSpace = 0
            afterEdit(deleted: 1, inserted: ". ")
            return
        }
        type(" ")
        lastSpace = now
        if keyboard.layer_ != .letters {   // space on 123 goes back to ABC: with the capital after ". " (Tony: "this. build")
            keyboard.layer_ = .letters
            updateAutoShift(lastContext ?? "")
        }
    }

    private func type(_ s: String) {
        let e = driver.typeCharacter(s, now: now)
        if e.languageChanged { nudges.autoSwitched() }
        apply(e)
    }

    private func backspace() {
        if !wordPrefix.isEmpty {   // every delete shortens the word, a held Delete too ("isnt", held back to "i", + t is "it")
            wordPrefix.removeLast(); wordXs.removeLast(); wordYs.removeLast()
            wordCaps = min(wordCaps, wordPrefix.count)
        }
        let e = driver.backspace(at: now)
        if e.languageChanged { nudges.undone(); nudgeSoon(afterUndo: true) }   // Backspace put back what an auto switch changed
        apply(e)
    }

    private func apply(_ e: VKEditResult) {
        // Send only what changes: "toi" + s comes back as delete "oi", insert "ói"; the "o" is kept.
        // Every call is a message to the app being typed in, which lays its text out again.
        var deletes = e.deleteCount
        var insert = e.text
        if deletes > 0, let before = lastContext, before.count >= deletes {
            let old = Array(before.suffix(deletes)), new = Array(insert)
            var same = 0
            while same < old.count && same < new.count && old[same] == new[same] { same += 1 }
            deletes -= same
            insert = String(new.dropFirst(same))
        }
        for _ in 0..<deletes { proxy.deleteBackward() }
        if !insert.isEmpty { proxy.insertText(insert) }
        Diag.edits += deletes + (insert.isEmpty ? 0 : 1)
        if e.languageChanged { saveLanguage(); refresh() }
        afterEdit(deleted: deletes, inserted: insert)
    }

    /// The app's text before the cursor only shows a window of the document: compare what both have.
    /// Milliseconds as text; under 0.1 ms in nanoseconds (Tony).
    static func time(_ ms: Double) -> String {
        ms < 0.1 ? String(format: "%.0f ns", ms * 1_000_000) : String(format: "%.2f ms", ms)
    }

    private static func sameEnding(_ a: String, _ b: String) -> Bool {
        let n = min(a.count, b.count, 40)
        return n > 0 && a.suffix(n) == b.suffix(n) && abs(a.count - b.count) < 2000 && (a.count == b.count || n == 40)
    }

    private func afterEdit(deleted: Int, inserted: String) {
        var c = lastContext ?? ""
        c = String(c.dropLast(min(deleted, c.count))) + inserted
        if c.count > 4000 { c = String(c.suffix(2000)) }
        lastContext = c
        recentContexts.append(c)
        if recentContexts.count > 12 { recentContexts.removeFirst(recentContexts.count - 12) }
        Diag.readMs = 0
        updateAutoShift(c)
        scheduleSuggestions()
        if Diag.on {
            let end = CACurrentMediaTime()
            let hold = max(0, Diag.touchUp - Diag.touchStart) * 1000, work = max(0, end - Diag.touchUp) * 1000
            keyboard.diagnostics = "finger down \(Self.time(hold)) · lift→text \(Self.time(work)) · read \(Self.time(Diag.readMs))\n"
                + "vibrations \(Diag.haptics) · touches \(Diag.touches) · edits \(Diag.edits)"
            KBLog.add("key=\(Diag.lastKind) hold=\(Self.time(hold)) work=\(Self.time(work)) read=\(Self.time(Diag.readMs)) vib=\(Diag.haptics) late=\(Diag.late) delay=\(Self.time(Diag.delayMs)) touches=\(Diag.touches) edits=\(Diag.edits) suggest=\(Self.time(Diag.suggestMs)) lang=\(driver.language)")
            Diag.haptics = 0; Diag.touches = 0; Diag.edits = 0; Diag.late = 0; Diag.suggestMs = 0; Diag.delayMs = 0
        }
    }

    // MARK: suggestions and auto-correct

    private func clearWord() {
        wordPrefix = ""; wordXs = []; wordYs = []; wordCaps = 0
        candidates = []; willCorrect = false; pendingFix = nil
        keyboard?.suggestions = []
    }

    /// The word before the cursor as it shows in the field (after Telex: "tôi" for "tooi").
    private var shownWord: String {
        let c = lastContext ?? ""
        return String(c.reversed().prefix { $0.isLetter }.reversed())
    }

    private static func text(of c: VKCandidate) -> String {
        var c = c
        let n = Int(c.textLen)
        return withUnsafeBytes(of: &c.text) { raw in
            String(utf16CodeUnits: raw.bindMemory(to: unichar.self).baseAddress!, count: n)
        }
    }

    /// Capitalised like the typed word ("Teh" → "The").
    private func cased(_ s: String) -> String {
        shownWord.first?.isUppercase == true ? s.prefix(1).uppercased() + s.dropFirst() : s
    }

    /// The strip is worked out after the key's text has gone to the app, so it never delays typing;
    /// keys typed faster than that skip the in-between words.
    private var suggestionsDirty = false
    private func scheduleSuggestions() {
        guard !suggestionsDirty else { return }
        suggestionsDirty = true
        DispatchQueue.main.async { [weak self] in self?.updateSuggestionsIfNeeded() }
    }
    private func updateSuggestionsIfNeeded() {
        guard suggestionsDirty else { return }
        let t0 = CACurrentMediaTime()
        updateSuggestions()
        Diag.suggestMs = max(Diag.suggestMs, (CACurrentMediaTime() - t0) * 1000)
    }

    /// Debug log only: any main-thread freeze over 50 ms, and the key it followed. Touches that arrive
    /// during a freeze are what feel like lag (and, before, like double vibrations).
    private var stallWatch: CADisplayLink?
    private var lastFrame: CFTimeInterval = 0
    private func startStallWatch() {
        stallWatch?.invalidate()
        lastFrame = 0
        let link = CADisplayLink(target: self, selector: #selector(frameTick(_:)))
        link.add(to: .main, forMode: .common)
        stallWatch = link
    }
    @objc private func frameTick(_ link: CADisplayLink) {
        let gap = lastFrame == 0 ? 0 : link.timestamp - lastFrame
        lastFrame = link.timestamp
        if gap > 0.05 { KBLog.add("stall \(Self.time(gap * 1000)) after key=\(Diag.lastKind)") }
    }

    func updateSuggestions() {
        suggestionsDirty = false
        if KeyboardView.barOn, correctsHere, showSpecialSuggestions() { return }
        guard KeyboardView.barOn, correctsHere, !wordPrefix.isEmpty, !shownWord.isEmpty else {
            if !candidates.isEmpty || !(keyboard?.suggestions.isEmpty ?? true) { candidates = []; willCorrect = false; pendingFix = nil; keyboard.suggestions = [] }
            return
        }
        let typed = wordPrefix.lowercased()
        let vi: Int32 = driver.language == 1 ? 1 : 0
        let rects = keyboard.keyRects
        guard !rects.isEmpty else { return }
        var out = [VKCandidate](repeating: VKCandidate(), count: 6)
        let touched = !wordXs.contains { $0.isNaN }    // keys typed without a touch (tests): key centres only
        let n = typed.withCString { t in
            touched ? VKCandidates(t, wordXs, wordYs, vi, rects, Int32(rects.count), &out, 6)
                    : VKCandidates(t, nil, nil, vi, rects, Int32(rects.count), &out, 6)
        }
        candidates = Array(out.prefix(Int(n)))
        // never auto-correct acronyms / names typed with capitals inside, or a word put back by backspace
        willCorrect = autoCorrectOn && wordCaps < 2 && keepWord != typed && wordPrefix.count >= 3
            && typed.withCString { t in candidates.withUnsafeBufferPointer { VKShouldCorrect(t, $0.baseAddress!, n) } }
        pendingFix = willCorrect ? candidates.first.map { (cased(Self.text(of: $0)), $0.vietnamese == 1) } : nil
        // "dont" → "don't" from the keys typed (in Vietnamese mode "thats" shows as "thát" until the end of
        // the word); a common Vietnamese word in Vietnamese mode stays ("im")
        let shownNow = shownWord
        let typedCased = wordCaps == wordPrefix.count && wordPrefix.count > 1 ? typed.uppercased()
            : shownNow.first?.isUppercase == true ? typed.prefix(1).uppercased() + typed.dropFirst() : typed
        if autoCorrectOn, keepWord != typed, let c = Contractions.fix(typedCased),
           driver.language == 0 || typed.withCString({ VKWordScore(1, $0) }) < 10 {   // a common Vietnamese word stays ("im"); a rare one ("thát") doesn't block "that's"
            pendingFix = (c, false); willCorrect = true
        }
        if autoCorrectOn, keepWord != typed, let r = textReplacements[typed] {   // the user's own shortcuts win
            pendingFix = (r, driver.language == 1); willCorrect = true
        }
        var comp = [VKCandidate](repeating: VKCandidate(), count: 6)
        let m = suggestionsOn ? typed.withCString { VKCompletions($0, vi, &comp, 6) } : 0
        let shown = shownWord
        var cells: [Suggestion] = [Suggestion(text: shown, quoted: true)]
        var seen: Set<String> = [shown.lowercased()]
        func add(_ c: VKCandidate, highlight: Bool = false) {
            let t = cased(Self.text(of: c))
            guard cells.count < 3, !seen.contains(t.lowercased()) else { return }
            seen.insert(t.lowercased()); cells.append(Suggestion(text: t, highlighted: highlight))
        }
        if willCorrect, let fix = pendingFix, !seen.contains(fix.text.lowercased()) {
            seen.insert(fix.text.lowercased()); cells.append(Suggestion(text: fix.text, highlighted: true))
        }
        if suggestionsOn {
            // a typed word that isn't a word: fixes first; otherwise ways to finish it
            let isWord = candidates.contains { String(cString: withUnsafeBytes(of: $0.letters) { Array($0.bindMemory(to: CChar.self)) + [0] }) == typed }
            let fixes = candidates.filter { $0.score > -9 }
            for c in isWord ? Array(comp.prefix(Int(m))) + fixes : fixes + Array(comp.prefix(Int(m))) { add(c) }
        }
        if cells.count == 1 { cells[0].quoted = false }
        if !suggestionsOn && !willCorrect { cells = [] }
        keyboard.suggestions = cells
    }

    /// Space after a word: put in the correction (like the system keyboard). Returns true when it typed the space itself.
    private func autoCorrectAtBreak(_ end: String = " ") -> Bool {
        updateSuggestionsIfNeeded()       // typed faster than the strip: decide on the whole word now
        guard willCorrect, let fix = pendingFix else { return false }
        replaceWord(with: fix.text, vietnamese: fix.vietnamese, recordUndo: true, then: end)
        return true
    }

    /// Replace the word before the cursor with `word` + a space, in the word's language.
    private func replaceWord(with word: String, vietnamese: Bool, recordUndo: Bool, then end: String = " ") {
        let old = shownWord
        let insert = word + end
        for _ in 0..<old.count { proxy.deleteBackward() }
        proxy.insertText(insert)
        Diag.edits += old.count + 1
        driver.resetContext()
        if (driver.language == 1) != vietnamese && driver.autoSwitch {
            driver.language = vietnamese ? 1 : 0
            nudges.autoSwitched()
            saveLanguage(); refresh()
        }
        lastCorrection = recordUndo ? (old, insert) : nil
        lastSpace = end == " " ? now : 0
        clearWord()
        afterEdit(deleted: old.count, inserted: insert)
    }

    /// Backspace right after an auto-correct puts the typed word back (and keeps it this time).
    private func undoCorrection() -> Bool {
        guard let c = lastCorrection, (lastContext ?? "").hasSuffix(c.inserted) else { lastCorrection = nil; return false }
        lastCorrection = nil
        for _ in 0..<c.inserted.count { proxy.deleteBackward() }
        proxy.insertText(c.typed)
        driver.resetContext()
        keepWord = c.typed.lowercased()
        clearWord()
        afterEdit(deleted: c.inserted.count, inserted: c.typed)
        return true
    }

    func keyboard(_ view: KeyboardView, pickSuggestion index: Int) {
        let cells = keyboard.suggestions
        guard index < cells.count else { return }
        let cell = cells[index]
        switch cell.kind {
        case .next: insertNextWord(cell.text); return
        case .proofread: retryOrProofread(); return
        case .alternative: replaceSelection(with: cell.text); return
        case .undo: undoProofread(); return
        case .busy: return
        case .word: break
        }
        lastCorrection = nil
        if cell.quoted || cell.text == shownWord {
            // keep what was typed: a space, and don't correct it
            keepWord = wordPrefix.lowercased()
            willCorrect = false
            spaceKey()
            clearWord()
            return
        }
        let c = candidates.first { cased(Self.text(of: $0)) == cell.text }
        var vi = driver.language == 1
        if let c { vi = c.vietnamese == 1 } else {
            var comp = [VKCandidate](repeating: VKCandidate(), count: 6)
            let m = wordPrefix.lowercased().withCString { VKCompletions($0, driver.language == 1 ? 1 : 0, &comp, 6) }
            if let hit = comp.prefix(Int(m)).first(where: { cased(Self.text(of: $0)) == cell.text }) { vi = hit.vietnamese == 1 }
        }
        replaceWord(with: cell.text, vietnamese: vi, recordUndo: false)
    }

    private func saveLanguage() { UserDefaults.standard.set(driver.language, forKey: languageKey) }

    /// Capital letter at the start of a sentence, like the system keyboard.
    /// Punctuation typed right after a space goes back onto the word: "this ." → "this.", "hello "" → "hello"".
    /// Quotes and brackets only when they close one that is open in this paragraph. Not : or ; (Tony): they start
    /// emoticons, "haha :)" must not become "haha:)", and "Hello : " stays as typed.
    private func snapsBack(_ c: String) -> Bool {
        guard ".,?!)\"".contains(c), let before = lastContext, before.hasSuffix(" ") else { return false }
        let prev = before.dropLast().last
        guard let p = prev, p != " ", p != "\n" else { return false }
        let para = before.split(separator: "\n", omittingEmptySubsequences: false).last ?? ""
        if c == ")" { return para.filter { $0 == "(" }.count > para.filter { $0 == ")" }.count }
        if c == "\"" { return para.filter { $0 == "\"" }.count % 2 == 1 }
        return true
    }

    private func updateAutoShift(_ before: String) {
        guard keyboard.layer_ == .letters, keyboard.shift != .locked else { return }
        guard autoCapitalize, (proxy.autocapitalizationType ?? .sentences) != .none else {
            if keyboard.shift == .on { keyboard.shift = .off }
            return
        }
        let trimmed = before.replacingOccurrences(of: " ", with: "")
        var start = before.isEmpty || before.hasSuffix("\n")
        // a sentence ends at . ? ! … , also with closing quotes or brackets after it: go." Then, (wow.) Next
        let core = trimmed.reversed().drop { "\"”’')]".contains($0) }
        if !start, before.hasSuffix(" "), let last = core.first { start = ".?!…".contains(last) }
        if proxy.autocapitalizationType == .allCharacters { keyboard.shift = .on; return }
        if proxy.autocapitalizationType == .words && (before.isEmpty || before.hasSuffix(" ")) { start = true }
        keyboard.shift = start ? .on : .off
    }
}

// MARK: emoji keyboard

extension KeyboardViewController: EmojiKeyboardDelegate {
    private var keyboardHeight: CGFloat { KeyboardView.keyboardHeight(compactHeight: traitCollection.verticalSizeClass == .compact) }

    func showEmoji() {
        clearWord()
        driver.resetContext()
        let ev = emojiView ?? {
            let v = EmojiKeyboardView(frame: view.bounds)
            v.delegate = self
            v.autoresizingMask = [.flexibleWidth, .flexibleHeight]
            view.addSubview(v)
            emojiView = v
            return v
        }()
        ev.dark = keyboard.dark
        ev.haptics = keyboard.haptics
        ev.feedback = keyboard.feedback
        ev.load()
        browseEmoji()
    }

    private func browseEmoji() {
        guard let ev = emojiView else { return }
        KeyboardView.searchTop = nil
        endEmojiSearchKeys()
        heightConstraint?.constant = keyboardHeight + EmojiKeyboardView.extraHeight
        keyboard.isHidden = true
        ev.isHidden = false
        ev.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        ev.frame = view.bounds
        ev.setSearching(false)
        keyboard.setNeedsLayout()
    }

    func emojiKeyboardClose(_ v: EmojiKeyboardView) {
        KeyboardView.searchTop = nil
        endEmojiSearchKeys()
        heightConstraint?.constant = keyboardHeight
        v.isHidden = true
        keyboard.isHidden = false
        keyboard.layer_ = .letters
        keyboard.setNeedsLayout()
        updateAutoShift(lastContext ?? "")
        keyboard.suggestions = []; scheduleSuggestions()   // the strip as it was (a selection's fix, next words)
    }

    func emojiKeyboardBack(_ v: EmojiKeyboardView) {
        if v.searching { browseEmoji() } else { emojiKeyboardClose(v) }
    }

    func emojiKeyboardStartSearch(_ v: EmojiKeyboardView) {
        KeyboardView.searchTop = EmojiKeyboardView.searchHeight
        keyboard.fieldType = .default      // the search uses plain letters, whatever the app's field is (no @ . keys)
        keyboard.returnIsDone = true
        heightConstraint?.constant = keyboardHeight
        keyboard.isHidden = false
        keyboard.layer_ = .letters
        keyboard.shift = .off
        keyboard.setNeedsLayout()
        v.autoresizingMask = [.flexibleWidth]
        v.frame = CGRect(x: 0, y: 0, width: view.bounds.width, height: EmojiKeyboardView.searchHeight)
        v.setSearching(true)
        emojiQuery = ""
        updateEmojiQuery()
    }

    /// Back to the app's own keys: its field layout and Return key.
    private func endEmojiSearchKeys() {
        keyboard.returnIsDone = false
        keyboard.fieldType = proxy.keyboardType ?? .default
    }

    func emojiKeyboard(_ v: EmojiKeyboardView, insert text: String) {
        selectionEdited()
        proxy.insertText(text)
        driver.resetContext()
        clearWord()
        afterEdit(deleted: 0, inserted: text)
    }

    func emojiKeyboardDelete(_ v: EmojiKeyboardView) {
        proxy.deleteBackward()
        driver.resetContext()
        clearWord()
        afterEdit(deleted: 1, inserted: "")
    }

    /// Keys while searching go to the query, not the app. Returns false for keys that work as usual.
    private func emojiSearchKey(_ kind: KeyKind) -> Bool {
        switch kind {
        case .char(let c): emojiQuery += c.lowercased()
        case .space: if !emojiQuery.isEmpty && !emojiQuery.hasSuffix(" ") { emojiQuery += " " }
        case .backspace: if !emojiQuery.isEmpty { emojiQuery.removeLast() }
        case .returnKey, .emoji: browseEmoji(); return true
        default: return false
        }
        if keyboard.shift == .on { keyboard.shift = .off }
        updateEmojiQuery()
        return true
    }

    /// The query reads as Telex in Vietnamese mode ("nguwaj" shows "ngựa"); search uses the raw keys too.
    private func updateEmojiQuery() {
        guard let ev = emojiView else { return }
        let vi = driver.language == 1
        let shown = vi ? Telex.convert(emojiQuery) : emojiQuery
        // nothing typed yet: the frequently used ones, never an empty row (Tony)
        let byChar = { (c: String) in EmojiIndex.shared.emoji(c) }
        let found = emojiQuery.trimmingCharacters(in: .whitespaces).isEmpty
            ? EmojiUsage.frequent(in: VKSettings.defaults, count: 24).compactMap(byChar)
            : EmojiIndex.shared.search(emojiQuery, vietnamese: vi, limit: 80).filter { $0.version <= EmojiKeyboardView.maxVersion }
        ev.setQuery(shown, results: found)
    }
}

// MARK: Apple Intelligence and next words

extension KeyboardViewController {
    /// Moves to a new state: drops answers still coming for the old one, opens or closes the preview, redraws the strip.
    private func setAI(_ new: AIState) {
        let wasPreview: Bool = { if case .preview = aiState { return true }; return false }()
        if new == aiState && !wasPreview { return }
        aiGen += 1
        aiSelTask?.cancel(); aiSelTask = nil
        aiState = new
        if case .preview(let o, let c) = new { showPreview(original: o, corrected: c) } else if proofreadPanel != nil { closePreview() }
        scheduleSuggestions()
    }

    /// The selection changed (or went away): start over from it.
    private func selectionChanged() {
        guard let sel = selection, AIAssist.available, KeyboardView.barOn, correctsHere else {
            // no selection: only Undo survives, and only while the fix is still right there
            if case .applied = aiState, undoStillFits() { return }
            if aiState != .none { setAI(.none) }
            return
        }
        if case .preview(let o, _) = aiState, o == sel { return }       // the same text, still being looked at
        let core = sel.trimmingCharacters(in: .whitespacesAndNewlines)
        guard core.contains(where: { $0.isLetter }) else { setAI(.none); return }   // only emoji, digits or punctuation
        if !core.contains(where: { $0.isWhitespace }) && core.count <= 40 {
            setAI(.word(sel, .working)); checkWord(sel)
        } else {
            // iOS can hand over a wrong copy of a selection across lines: "line 1  line 3", the middle line gone
            // (seen in the simulator). Replacing it would delete text, so only one paragraph, with no gap in it.
            let risky = core.contains("\n") || core.contains("\r") || core.contains("  ")
            setAI(.text(sel, risky ? .onePara : sel.count > Self.proofreadLimit ? .tooLong : .ready))
        }
    }

    /// Undo stays only while the fix is exactly where it was put: the text before the cursor ends with it and the
    /// text after the cursor is what followed it then.
    private func undoStillFits() -> Bool {
        guard case .applied(_, let ins, let after) = aiState else { return false }
        let before = proxy.documentContextBeforeInput ?? "", now = proxy.documentContextAfterInput ?? ""
        return before.hasSuffix(String(ins.suffix(100))) && now.hasPrefix(String(after.prefix(40)))
    }

    /// The strip for the current state, or next words. True when it filled the strip.
    func showSpecialSuggestions() -> Bool {
        func note(_ t: String) -> [Suggestion] { [Suggestion(text: t, kind: .busy)] }
        switch aiState {
        case .word(_, let c), .text(_, let c):
            let isWord: Bool = { if case .word = aiState { return true }; return false }()
            switch c {
            case .ready: keyboard.suggestions = [Suggestion(text: String(localized: "Sửa lỗi"), kind: .proofread)]
            case .working: keyboard.suggestions = note(isWord ? String(localized: "Đang kiểm tra…") : String(localized: "Đang sửa…"))
            case .fixes(let f): keyboard.suggestions = f.prefix(3).map { Suggestion(text: $0, kind: .alternative) }
            case .noMistakes: keyboard.suggestions = note(String(localized: "Không có lỗi ✓"))
            case .failed: keyboard.suggestions = [Suggestion(text: String(localized: "Không sửa được · Thử lại"), kind: .proofread)]   // tap to retry
            case .tooLong: keyboard.suggestions = note(String(localized: "Đoạn quá dài để sửa"))
            case .onePara: keyboard.suggestions = note(String(localized: "Hãy chọn trong một đoạn"))
            }
            return true
        case .preview:
            keyboard.suggestions = []
            return true
        case .applied(_, let ins, _):
            if (lastContext ?? "").hasSuffix(String(ins.suffix(100))) { keyboard.suggestions = [Suggestion(text: String(localized: "Hoàn tác"), kind: .undo)]; return true }
        case .none: break
        }
        // next words only at a plain cursor: with text selected, a tap would replace the selection
        if selection == nil, wordPrefix.isEmpty, let before = lastContext, before.hasSuffix(" ") {
            keyboard.suggestions = nextWords(before).map { Suggestion(text: $0, kind: .next) }
            return true
        }
        return false
    }

    /// The table's next words at once; Apple Intelligence's best guess joins after a short pause (when it differs).
    private func nextWords(_ before: String) -> [String] {
        var out = NextWords.shared.predict(before: before)
        let key = String(before.suffix(80))
        if AIAssist.available {
            if aiNextFor == key {
                if let ai = aiNext.first(where: { a in !out.contains { $0.lowercased() == a.lowercased() } }) {
                    if out.count >= 3 { out[2] = ai } else { out.append(ai) }
                }
            } else {
                aiNextFor = key; aiNext = []
                aiNextTask?.cancel()
                aiNextTask = Task { [weak self] in
                    try? await Task.sleep(nanoseconds: 450_000_000)        // only after a short pause in typing
                    guard !Task.isCancelled else { return }
                    let r = await AIAssist.nextWords(after: before)
                    await MainActor.run {
                        guard let self, self.aiNextFor == key, !r.isEmpty else { return }
                        self.aiNext = r.map { $0.lowercased() }
                        self.scheduleSuggestions()
                    }
                }
            }
        }
        if keyboard.shift == .on { out = out.map { $0.prefix(1).uppercased() + $0.dropFirst() } }   // a sentence start
        return Array(out.prefix(3))
    }

    private func insertNextWord(_ w: String) {
        let text = w + " "
        proxy.insertText(text)
        driver.resetContext()
        clearWord()
        lastSpace = now
        afterEdit(deleted: 0, inserted: text)
    }

    /// Runs `work` for the current state; its answer is used only if the state hasn't moved on.
    private func ask<T>(_ work: @escaping () async -> T?, then use: @escaping (T?) -> Void) {
        let gen = aiGen
        aiSelTask?.cancel()
        aiSelTask = Task { [weak self] in
            let r = await AIAssist.withTimeout(15) { await work() } ?? nil
            await MainActor.run {
                guard let self, self.aiGen == gen, !Task.isCancelled else { return }
                use(r)
            }
        }
    }

    private func checkWord(_ word: String) {
        let before = proxy.documentContextBeforeInput ?? "", after = proxy.documentContextAfterInput ?? ""
        ask({
            try? await Task.sleep(nanoseconds: Self.wordCheckDelay)      // the selection handles may still be moving
            if Task.isCancelled { return nil }
            return await AIAssist.fixWord(word, before: before, after: after)
        }) { [weak self] (fixes: [String]?) in
            guard let fixes else { self?.update(.word(word, .failed)); return }
            self?.update(.word(word, fixes.isEmpty ? .noMistakes : .fixes(fixes)))
        }
    }

    /// ✨ Proofread, or "Couldn't fix · Retry".
    private func retryOrProofread() {
        switch aiState {
        case .word(let w, .failed): update(.word(w, .working)); aiGen += 1; checkWord(w)
        case .text(let t, .failed): update(.text(t, .ready)); startProofread()
        default: startProofread()
        }
    }

    /// The same state with a new answer, without starting over (no new generation: nothing else is in flight).
    private func update(_ s: AIState) { aiState = s; scheduleSuggestions() }

    private func startProofread() {
        guard case .text(let original, .ready) = aiState else { return }
        update(.text(original, .working))
        ask({ await AIAssist.proofread(original) }) { [weak self] (fixed: String?) in
            guard let self else { return }
            guard let fixed else { self.update(.text(original, .failed)); return }
            if fixed == original { self.update(.text(original, .noMistakes)); return }
            self.setAI(.preview(original: original, corrected: fixed))
        }
    }

    /// The fixed word replaces the selected one, if that word is still what's selected.
    private func replaceSelection(with text: String) {
        guard case .word(let word, _) = aiState, proxy.selectedText == word else { setAI(.none); return }
        let before = proxy.documentContextBeforeInput ?? "", after = proxy.documentContextAfterInput ?? ""
        proxy.insertText(text)
        afterReplace(original: word, inserted: text, before: before, after: after)
    }

    private func applyProofread() {
        guard case .preview(let original, let corrected) = aiState else { return }
        // the selection must still be the text that was proofread, or the fix would land somewhere else
        guard proxy.selectedText == original else { setAI(.none); selection = nil; readSelection(); return }
        let before = proxy.documentContextBeforeInput ?? "", after = proxy.documentContextAfterInput ?? ""
        proxy.insertText(corrected)
        afterReplace(original: original, inserted: corrected, before: before, after: after)
    }

    /// Our own idea of the text around the cursor after a replace: the app may report the old text for a moment.
    private func afterReplace(original: String, inserted: String, before: String, after: String) {
        selection = nil; selectionAt = ""
        driver.resetContext()
        clearWord()
        lastContext = before + inserted
        lastAfter = after
        recentContexts = [lastContext ?? ""]
        updateAutoShift(lastContext ?? "")
        setAI(.applied(original: original, inserted: inserted, after: after))
    }

    private func undoProofread() {
        guard case .applied(let original, let inserted, let after) = aiState, undoStillFits() else { setAI(.none); return }
        let before = String((lastContext ?? "").dropLast(inserted.count))
        for _ in 0..<inserted.count { proxy.deleteBackward() }
        proxy.insertText(original)
        driver.resetContext()
        clearWord()
        lastContext = before + original
        lastAfter = after
        recentContexts = [lastContext ?? ""]
        updateAutoShift(lastContext ?? "")
        setAI(.none)
    }

    /// The preview takes the whole keyboard (Tony): the keys hide under it until Apply or Cancel.
    private func showPreview(original: String, corrected: String) {
        let panel = proofreadPanel ?? ProofreadPanel()
        panel.dark = keyboard.dark
        panel.show(original: original, corrected: corrected)
        panel.onCancel = { [weak self] in      // back to ✨ Proofread for the text that's still selected
            guard let self else { return }
            if case .preview(let o, _) = self.aiState, self.selection == o { self.setAI(.text(o, .ready)) } else { self.setAI(.none) }
        }
        panel.onApply = { [weak self] in self?.applyProofread() }
        if proofreadPanel == nil {
            panel.frame = view.bounds
            panel.autoresizingMask = [.flexibleWidth, .flexibleHeight]
            view.addSubview(panel); proofreadPanel = panel
        }
        keyboard.isHidden = true
    }

    private func closePreview() {
        proofreadPanel?.removeFromSuperview(); proofreadPanel = nil
        keyboard.isHidden = false
        keyboard.ignoreTouches(for: 0.8)      // a double tap on Apply must not land on Return
    }
}
