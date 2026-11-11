import UIKit

/// Debug readout (Vikey app › Hiện thông số bàn phím): what one key press costs on a real device.
enum Diag {
    static var on = false
    static var touchStart: CFTimeInterval = 0
    static var touchUp: CFTimeInterval = 0
    static var touches = 0, haptics = 0, edits = 0, late = 0
    static var suggestMs = 0.0, delayMs = 0.0   // delay: finger down → the keyboard sees the touch
    static var readMs = 0.0
    static var lastKind = "letter"
}

/// What a key does.
enum KeyKind: Equatable {
    case char(String)      // inserts this (letters stored lowercase)
    case shift, backspace, space, returnKey, globe, language, emoji
    case layer(KeyboardView.Layer)
}

struct KeyDef {
    var kind: KeyKind
    var frame: CGRect = .zero
}

protocol KeyboardViewDelegate: AnyObject {
    func keyboard(_ view: KeyboardView, didTap kind: KeyKind)
    /// A long-press alternate (… – ’ ă ê …), already in the right case.
    func keyboard(_ view: KeyboardView, insertAlternate text: String)
    /// Delete held down: n = how many repeats so far (the controller speeds up to whole words).
    func keyboardBackspaceRepeat(_ view: KeyboardView, count n: Int)
    /// Space held down and dragged: move the cursor by this many characters.
    func keyboard(_ view: KeyboardView, moveCursorBy offset: Int)
    func keyboardCursorModeEnded(_ view: KeyboardView)
    /// A tap on the suggestion strip (index into `suggestions`).
    func keyboard(_ view: KeyboardView, pickSuggestion index: Int)
}

/// The iOS 26 system keyboard, measured from the simulator (iPhone 17 Pro, 402 pt wide) and copied:
/// key 43 pt tall, rows 54 pt apart, 6 pt gaps, a 52 pt strip on top (where the system shows
/// suggestions; here it gives the top-row key popups room, since iOS clips a keyboard to its view).
/// Touches are handled by this view, not per key, so fast typing between keys never gets lost.
final class KeyboardView: UIView {
    enum Layer { case letters, numbers, symbols, numpad }   // numpad: number, phone and decimal fields
    enum Shift { case off, on, locked }

    weak var delegate: KeyboardViewDelegate?
    var layer_: Layer = .letters { didSet { if layer_ != oldValue { rebuild() } } }
    var shift: Shift = .off { didSet { if shift != oldValue { refreshLabels() } } }
    var dark = false { didSet { if dark != oldValue { refreshColors() } } }
    var showsGlobe = false { didSet { if showsGlobe != oldValue { rebuild() } } }
    /// The text field's kind: email and web fields get @ . / .com keys like the system keyboard.
    var fieldType: UIKeyboardType = .default { didSet { if fieldType != oldValue { rebuild() } } }
    /// Emoji search: Return is a green checkmark ("done searching"), like the system keyboard (Tony).
    var returnIsDone = false { didSet { if returnIsDone != oldValue { refreshLabels() } } }
    // setters restyle keys only when something changed: they run on every key press
    var languageTitle = "Tiếng Việt" { didSet { if languageTitle != oldValue { refreshLabels() } } }
    var languageShort = "VI" {
        didSet {
            guard languageShort != oldValue else { return }
            refreshLabels()
            if Self.markOn { mark.setLanguage(vietnamese: languageShort == "VI", animated: animatesLanguage) }
            else if langTop && animatesLanguage { animateLanguagePill(toVietnamese: languageShort == "VI") }
        }
    }
    /// Off while the keyboard comes up (the saved language is set, not switched to), on once it's shown.
    var animatesLanguage = false
    var returnTitle = "return" { didSet { if returnTitle != oldValue { refreshLabels() } } }
    var returnIsBlue = false { didSet { if returnIsBlue != oldValue { refreshColors() } } }
    var haptics = false
    var globeButton: UIButton?
    /// The letters typed so far in the current word and the mode, for the fat-finger model.
    var wordContext: (() -> (prefix: String, vietnamese: Bool))?
    private var letterRects: [VKKeyRect] = []
    var keyRects: [VKKeyRect] { letterRects }
    private var letterCaps: [Character: KeyCap] = [:]
    private let diagLabel = UILabel()
    var diagnostics: String? {
        didSet { diagLabel.text = diagnostics; diagLabel.isHidden = diagnostics == nil }
    }
    /// Where the last typed key was touched (the auto-correct model weighs letters by it).
    /// Taken (set to nil) by the controller; nil when a key wasn't typed by a touch.
    var lastTapPoint: CGPoint?
    let bar = SuggestionBar()
    /// A nudge's button (true) or its ✕ (false), for Nudges.
    var onNudge: ((NudgeView.Kind, Bool) -> Void)?
    var suggestions: [Suggestion] = [] { didSet { if suggestions != oldValue { bar.show(suggestions) } } }

    private var keys: [(def: KeyDef, view: KeyCap)] = []
    private var touch: UITouch?                 // the touch that owns the current key
    private var touchKey: KeyCap?
    private var touchStart: CGPoint = .zero
    private var slideFromLayer = false         // 123 pressed and slid onto a symbol: back to letters after
    private let popup = KeyPopup()
    private let alternates = AlternatesView()
    private var holdTimer: Timer?
    private var repeatCount = 0
    private var cursorMode = false
    private var cursorAnchor: CGFloat = 0
    let feedback = KeyHaptics()

    // geometry (portrait iPhone; scaled for other widths; landscape rows are shorter)
    /// The suggestion strip is shown when suggestions or auto-correct are on; without it the keys
    /// start right away (Tony: no empty strip). Diagnostics add a readout line above it.
    static var barOn = false
    /// Where the VI/EN key sits: 0 = bottom row (today); 1-3 = design proposals for the top-left corner of the
    /// suggestion strip (Tony): 1 a "VI" pill, 2 a "VI | EN" switch, 3 a "VI ⇄" chip. Only renders use 1-3 for now.
    static var langStyle = 1   // Tony picked the pill (A); the bottom row is the fallback when the strip is off
    private var langTop: Bool { Self.langStyle > 0 && Self.barOn && Self.searchTop == nil && layer_ != .numpad }
    private var langTopWidth: CGFloat { [0, 50, 96, 70][min(Self.langStyle, 3)] }
    /// The corner element: VI/EN switch and Vikey mark in one (LangMark.swift, Tony picked design A). Off = the plain
    /// pill and the strip's brand on the right (kept for the render tests of the old proposals).
    static var markOn = true
    let mark = LangMarkView()
    /// The keyboard is up: the mark plays its opening, at most once every 10 minutes (Tony: opening the keyboard many
    /// times in a row, switching apps, must not replay it). Saved, since iOS ends the keyboard's process often.
    static var markIntroEvery: TimeInterval = 600
    func playMarkIntro() {
        guard Self.markOn && langTop else { return }
        let key = "markIntroLast", now = Date().timeIntervalSince1970
        guard now - UserDefaults.standard.double(forKey: key) >= Self.markIntroEvery else { return }
        UserDefaults.standard.set(now, forKey: key)
        mark.playIntro()
    }
    static let pillH: CGFloat = 38
    static let barH: CGFloat = 44
    /// The corner of the glass iOS puts the keyboard in (measured, iOS 26.5: 80 px at 3x). Anything in a corner
    /// is concentric with it: its radius = this − its inset (Tony).
    static let hostCorner: CGFloat = 26.67
    static func diagH(compactHeight: Bool) -> CGFloat { Diag.on ? (compactHeight ? 30 : 40) : 0 }
    /// Emoji search (EmojiKeyboardView): the search field and a row of results take the top instead of the strip.
    static var searchTop: CGFloat?
    /// Pro Max-size iPhones (430-440 pt wide) have taller keys: 45 pt on a 56 pt pitch, measured on the iOS 26.5
    /// system keyboard (iPhone 17 Pro Max); 43 on 54 below that (iPhone 17 Pro, 402 pt).
    static var portraitWidth: CGFloat { min(UIScreen.main.bounds.width, UIScreen.main.bounds.height) }
    static var tallKeys: Bool { portraitWidth >= 428 }
    static func metrics(compactHeight: Bool) -> (top: CGFloat, keyH: CGFloat, pitch: CGFloat, bottom: CGFloat) {
        let keyH: CGFloat = tallKeys ? 45 : 43, pitch: CGFloat = tallKeys ? 56 : 54
        if let t = searchTop { return compactHeight ? (top: t, keyH: 34, pitch: 40, bottom: 4) : (top: t, keyH: keyH, pitch: pitch, bottom: 3) }
        let extra = diagH(compactHeight: compactHeight) + (barOn ? barH : 0)
        if compactHeight { return (top: 8 + extra, keyH: 34, pitch: 40, bottom: 4) }
        // Pro Max: rows 51.33 below the keyboard's top edge, and 2.67 pt higher than with a 3 pt bottom (measured
        // side by side with the system keyboard in the same app, iOS 26.5)
        if tallKeys { return (top: 11 + extra - (barOn ? 3.67 : 0), keyH: keyH, pitch: pitch, bottom: 5.67) }
        return (top: 11 + extra - (barOn ? 3 : 0), keyH: keyH, pitch: pitch, bottom: 3)
    }
    static func keyboardHeight(compactHeight: Bool) -> CGFloat {
        let m = metrics(compactHeight: compactHeight)
        return m.top + 3 * m.pitch + m.keyH + m.bottom
    }
    private var compactHeight: Bool { traitCollection.verticalSizeClass == .compact }

    override init(frame: CGRect) {
        super.init(frame: frame)
        isMultipleTouchEnabled = true
        // not .clear: iOS doesn't deliver touches on fully transparent parts of a keyboard,
        // so taps in the gaps between keys were lost
        backgroundColor = UIColor.black.withAlphaComponent(0.001)
        rebuild()
    }
    required init?(coder: NSCoder) { fatalError() }

    // MARK: layout

    private func rowKinds() -> [[KeyKind]] {
        func chars(_ s: String) -> [KeyKind] { s.map { .char(String($0)) } }
        // the system's email, URL, web search and Twitter layouts have no emoji key: room for @ . / .com and the space bar
        let special = layer_ == .letters && [.emailAddress, .URL, .webSearch, .twitter].contains(fieldType)
        var bottom: [KeyKind] = [layer_ == .letters ? .layer(.numbers) : .layer(.letters)] + (special ? [] : [.emoji])
        if showsGlobe { bottom.append(.globe) }
        bottom.append(.language)
        switch (layer_, fieldType) {   // like the system keyboard's email, URL, web search and Twitter layouts
        case (.letters, .emailAddress): bottom += [.space, .char("@"), .char(".")]
        case (.letters, .URL): bottom += [.char("."), .char("/"), .char(".com")]
        case (.letters, .webSearch): bottom += [.space, .char(".")]
        case (.letters, .twitter): bottom += [.space, .char("@"), .char("#")]
        default: bottom.append(.space)
        }
        bottom.append(.returnKey)
        switch layer_ {
        case .numpad:
            let left: KeyKind? = fieldType == .decimalPad ? .char(Locale.current.decimalSeparator ?? ".")
                : fieldType == .phonePad ? .char("+") : nil
            return [chars("123"), chars("456"), chars("789"), (left.map { [$0] } ?? []) + [.char("0"), .backspace]]
        case .letters: return [chars("qwertyuiop"), chars("asdfghjkl"), [.shift] + chars("zxcvbnm") + [.backspace], bottom]
        case .numbers: return [chars("1234567890"), chars("-/:;()$&@\""), [.layer(.symbols)] + chars(".,?!'") + [.backspace], bottom]
        case .symbols: return [chars("[]{}#%^*+="), chars("_\\|~<>€£¥•"), [.layer(.numbers)] + chars(".,?!'") + [.backspace], bottom]
        }
    }

    private func rebuild() {
        keys.forEach { $0.view.removeFromSuperview() }
        keys = []
        globeButton?.removeFromSuperview(); globeButton = nil
        for row in rowKinds() {
            for kind in row {
                let cap = KeyCap()
                cap.isUserInteractionEnabled = false
                addSubview(cap)
                keys.append((KeyDef(kind: kind), cap))
                if kind == .globe { let b = UIButton(type: .custom); addSubview(b); globeButton = b }
            }
        }
        diagLabel.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
        diagLabel.textColor = .secondaryLabel
        diagLabel.textAlignment = .center
        diagLabel.numberOfLines = 2
        addSubview(diagLabel)
        diagLabel.isHidden = diagnostics == nil
        addSubview(bar)
        bar.onPick = { [unowned self] i in self.delegate?.keyboard(self, pickSuggestion: i) }
        bar.onBrand = { [unowned self] in self.openVikeyApp() }
        // A nudge's button opens the app where it acts: Apple's review sheet, or the feedback form.
        bar.onNudge = { [unowned self] k in self.onNudge?(k, true); self.bar.hideNudge(); self.openVikeyApp(k == .review ? "review" : "feedback") }
        bar.onNudgeClosed = { [unowned self] k in self.onNudge?(k, false) }
        addSubview(mark)
        addSubview(popup); popup.isHidden = true
        addSubview(alternates); alternates.isHidden = true
        setFonts()
        refreshLabels()
        // frames now, not at the next layout pass: a touch that comes first (a slide off ABC/123, the next tap of
        // fast typing) found every new key at zero size and was dropped
        setNeedsLayout()
        layoutIfNeeded()
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        let m = Self.metrics(compactHeight: compactHeight)
        let W = bounds.width
        // measured on the iOS 26.5 system keyboard at 402 and 440 pt (Tony: same sizes as the system keyboard):
        // the side margin and the gaps don't scale, the letter keys take the rest; function keys grow with the width
        let s = W / 402                       // older proportions still used for the less-measured pieces
        let side: CGFloat = 6.67, gap: CGFloat = 6
        let key = (W - 2 * side - 9 * gap) / 10
        func lerp(_ at402: CGFloat, _ at440: CGFloat) -> CGFloat { at402 + (W - 402) * (at440 - at402) / 38 }
        let rows = rowKinds()
        var frames: [CGRect] = []
        if layer_ == .numpad {   // 3 × 4, the whole height, like the system number pad
            let g = 6 * s, top: CGFloat = 8, bottomPad: CGFloat = 4
            let kw = (W - 2 * side - 2 * g) / 3, kh = (bounds.height - top - bottomPad - 3 * g) / 4
            for (r, row) in rows.enumerated() {
                let cols = row.count == 2 ? [1, 2] : [0, 1, 2]   // number pad: no key bottom left
                for (i, _) in row.enumerated() {
                    frames.append(CGRect(x: side + CGFloat(cols[i]) * (kw + g), y: top + CGFloat(r) * (kh + g), width: kw, height: kh))
                }
            }
        } else {
        for (r, row) in rows.enumerated() {
            let y = m.top + CGFloat(r) * m.pitch
            func rect(_ x: CGFloat, _ w: CGFloat) -> CGRect { CGRect(x: x, y: y, width: w, height: m.keyH) }
            switch r {
            case 0:
                for i in 0..<row.count { frames.append(rect(side + CGFloat(i) * (key + gap), key)) }
            case 1:
                if row.count == 9 {
                    let x0 = side + (key + gap) / 2
                    for i in 0..<9 { frames.append(rect(x0 + CGFloat(i) * (key + gap), key)) }
                } else {
                    for i in 0..<row.count { frames.append(rect(side + CGFloat(i) * (key + gap), key)) }
                }
            case 2:
                let fn = lerp(45.5, 50.17)   // shift and delete: 45.33/45.67 at 402, 50.0/50.33 at 440
                frames.append(rect(side, fn))
                let inner = row.count - 2
                if inner == 7 {
                    let x0 = side + (key + gap) * 1.5
                    for i in 0..<7 { frames.append(rect(x0 + CGFloat(i) * (key + gap), key)) }
                } else {   // 5 wide punctuation keys between the function keys
                    let left = side + fn + gap * 2.5, right = W - side - fn - gap * 2.5
                    let w = (right - left - CGFloat(inner - 1) * gap) / CGFloat(inner)
                    for i in 0..<inner { frames.append(rect(left + CGFloat(i) * (w + gap), w)) }
                }
                frames.append(rect(W - side - fn, fn))
            default:
                let small = lerp(43.33, 48), ret = lerp(93, 102.33)   // 123, emoji; return
                var x = side
                let fixed = row.filter { if case .char = $0 { return false }; return $0 != .space && $0 != .returnKey }.count
                let charKeys = row.filter { if case .char = $0 { return true }; return false }.count
                let hasSpace = row.contains(.space)
                // without a space bar (URL fields) the extra keys share the room; with one they are small
                let charW: CGFloat = hasSpace ? small : (W - 2 * side - ret - CGFloat(fixed) * (small + gap) - CGFloat(charKeys) * gap) / CGFloat(max(1, charKeys))
                for kind in row {
                    switch kind {
                    case .space:
                        let after = CGFloat(charKeys) * (small + gap)
                        let w = W - side - ret - gap - x - after
                        frames.append(rect(x, w)); x += w + gap
                    case .returnKey:
                        frames.append(rect(W - side - ret, ret))
                    case .language where langTop:   // up in the strip's corner: the bottom row gets its room
                        // inset the side margin from the top and the left, so it's concentric with the corner
                        let dh = Self.diagH(compactHeight: compactHeight)
                        frames.append(CGRect(x: side, y: dh + side, width: langTopWidth, height: Self.pillH))
                    case .char:
                        frames.append(rect(x, charW)); x += charW + gap
                    default:
                        frames.append(rect(x, small)); x += small + gap
                    }
                }
            }
        }
        }
        let dh = Self.diagH(compactHeight: compactHeight)
        diagLabel.frame = CGRect(x: 8, y: 2, width: W - 16, height: max(dh - 4, 20))
        bar.isHidden = !Self.barOn || Self.searchTop != nil || layer_ == .numpad
        diagLabel.isHidden = diagnostics == nil || Self.searchTop != nil   // the emoji search field sits there
        let barX = side + (langTop ? langTopWidth + 8 : 0)
        let barY = langTop ? dh + side + Self.pillH / 2 - (Self.barH - 4) / 2 : dh + 2   // centred on the pill
        bar.frame = CGRect(x: barX, y: barY, width: W - side - barX, height: Self.barH - 4)
        bar.showsBrand = !Self.markOn
        mark.isHidden = !(Self.markOn && langTop)
        if !mark.isHidden {
            // the key under the corner element is only a tap area; its colours may date from before the strip was on
            // (a key-coloured square showed behind the pill in dark mode, Tony, build 51)
            if let v = keys.first(where: { $0.def.kind == .language })?.view {
                v.normalColor = .clear; v.pressedColor = .clear; v.layer.shadowOpacity = 0
                if v.backgroundColor != .clear { v.backgroundColor = .clear }
            }
            mark.frame = CGRect(x: side, y: dh + side, width: W - 2 * side, height: Self.pillH)
            mark.pillWidth = langTopWidth
            mark.dark = dark
            mark.setLanguage(vietnamese: languageShort == "VI", animated: false)
        }
        letterRects = []; letterCaps = [:]
        for i in keys.indices where i < frames.count {
            keys[i].def.frame = frames[i]
            keys[i].view.frame = frames[i]
            if keys[i].def.kind == .globe { globeButton?.frame = frames[i].insetBy(dx: -gap / 2, dy: -6) }
            if layer_ == .letters, case .char(let c) = keys[i].def.kind, let ch = c.first, let a = ch.asciiValue, ch.isLetter {
                let f = frames[i]
                letterRects.append(VKKeyRect(letter: CChar(bitPattern: a), x: Float(f.minX), y: Float(f.minY), w: Float(f.width), h: Float(f.height)))
                letterCaps[ch] = keys[i].view
            }
        }
    }

    // MARK: appearance

    private func title(for kind: KeyKind) -> (String?, UIImage?) {
        let sym = UIImage.SymbolConfiguration(pointSize: 19, weight: .regular)
        switch kind {
        case .char(let c): return (shift != .off && layer_ == .letters && c.count == 1 ? c.uppercased() : c, nil)
        case .shift: return (nil, UIImage(systemName: shift == .locked ? "capslock.fill" : (shift == .on ? "shift.fill" : "shift"), withConfiguration: sym))
        case .backspace: return (nil, UIImage(systemName: "delete.left", withConfiguration: sym))
        case .space: return (cursorMode ? "" : languageTitle, nil)
        case .returnKey:
            if returnIsDone { return (nil, UIImage(systemName: "checkmark", withConfiguration: UIImage.SymbolConfiguration(pointSize: 19, weight: .semibold))) }
            return returnTitle == "return" ? (nil, UIImage(systemName: "return", withConfiguration: sym)) : (returnTitle, nil)
        case .globe: return (nil, UIImage(systemName: "globe", withConfiguration: sym))
        case .emoji: return (nil, UIImage(systemName: "face.smiling", withConfiguration: sym))
        case .language: return (languageShort, nil)
        case .layer(let l): return (l == .letters ? "ABC" : (l == .numbers ? "123" : "#+="), nil)
        }
    }

    private func setFonts() {
        for (def, v) in keys {
            switch def.kind {
            case .char(let c): v.label.font = .systemFont(ofSize: c.count > 1 ? 16 : layer_ == .numpad ? 26 : layer_ == .letters ? 23 : 22, weight: .regular)
            case .space: v.label.font = .systemFont(ofSize: 15, weight: .regular); v.label.adjustsFontSizeToFitWidth = true; v.label.minimumScaleFactor = 0.6
            case .language: v.label.font = .systemFont(ofSize: 16, weight: .semibold)
            case .returnKey: v.label.font = .systemFont(ofSize: 16, weight: .medium)
            default: v.label.font = .systemFont(ofSize: 16, weight: .regular)
            }
        }
    }

    private func refreshLabels() {
        for (def, v) in keys {
            var (t, img) = title(for: def.kind)
            if cursorMode, case .char = def.kind { t = "" }     // the system keyboard blanks the keys in cursor mode
            if def.kind == .language && langTop { styleTopLanguage(v); continue }
            if v.label.text != t { v.label.text = t }
            if v.icon.image != img { v.icon.image = img }
        }
        refreshColors()
    }

    /// The switch, seen: the new language rolls in (up into VI, down into EN) and the pill pops.
    private func animateLanguagePill(toVietnamese vi: Bool) {
        guard let v = keys.first(where: { $0.def.kind == .language })?.view else { return }
        let roll = CATransition()
        roll.type = .push
        roll.subtype = vi ? .fromTop : .fromBottom
        roll.duration = 0.22
        roll.timingFunction = CAMediaTimingFunction(name: .easeOut)
        v.label.layer.add(roll, forKey: "roll")
        let tint = CATransition(); tint.type = .fade; tint.duration = 0.22
        v.layer.add(tint, forKey: "tint")
        v.transform = CGAffineTransform(scaleX: 0.86, y: 0.86)
        UIView.animate(withDuration: 0.42, delay: 0, usingSpringWithDamping: 0.45, initialSpringVelocity: 6, options: [.allowUserInteraction]) {
            v.transform = .identity
        }
    }

    /// The proposals for a VI/EN key in the strip's corner.
    private func styleTopLanguage(_ v: KeyCap) {
        if Self.markOn { v.label.attributedText = nil; v.label.text = nil; v.icon.image = nil; v.layer.shadowOpacity = 0; return }
        let brand = dark ? UIColor(red: 0.62, green: 0.60, blue: 1, alpha: 1) : UIColor(red: 0.31, green: 0.27, blue: 0.90, alpha: 1)
        v.layer.cornerRadius = Self.pillH / 2   // = hostCorner − the inset, give or take a point
        v.layer.cornerCurve = .continuous
        v.icon.image = nil
        switch Self.langStyle {
        case 2:   // both, the current one highlighted: what a tap does is obvious
            let other = languageShort == "VI" ? "EN" : "VI"
            let a = NSMutableAttributedString(string: languageShort, attributes: [.foregroundColor: brand, .font: UIFont.systemFont(ofSize: 15, weight: .bold)])
            a.append(NSAttributedString(string: "  |  ", attributes: [.foregroundColor: UIColor.tertiaryLabel, .font: UIFont.systemFont(ofSize: 13)]))
            a.append(NSAttributedString(string: other, attributes: [.foregroundColor: UIColor.secondaryLabel, .font: UIFont.systemFont(ofSize: 15, weight: .medium)]))
            v.label.attributedText = a
        case 3:   // the language and a swap sign
            let a = NSMutableAttributedString(string: languageShort + " ", attributes: [.foregroundColor: brand, .font: UIFont.systemFont(ofSize: 15, weight: .bold)])
            let att = NSTextAttachment(image: UIImage(systemName: "arrow.left.arrow.right", withConfiguration: UIImage.SymbolConfiguration(pointSize: 12, weight: .semibold))!.withTintColor(brand))
            a.append(NSAttributedString(attachment: att))
            v.label.attributedText = a
        default:  // a small pill
            v.label.attributedText = NSAttributedString(string: languageShort, attributes: [.foregroundColor: brand, .font: UIFont.systemFont(ofSize: 15, weight: .bold)])
        }
    }

    private func refreshColors() {
        bar.dark = dark
        mark.dark = dark
        let fg: UIColor = dark ? .white : .black
        // the system keys over the keyboard's glass, measured: light (253,255,255) on (217,221,233) ≈ 95% white,
        // dark (61,61,67) on (23,24,31) ≈ 16.5% white (Tony: ours were too transparent)
        let key: UIColor = dark ? UIColor(white: 1, alpha: 0.165) : UIColor(red: 0.992, green: 1, blue: 1, alpha: 0.99)
        let pressed: UIColor = dark ? UIColor(white: 1, alpha: 0.32) : UIColor(white: 1, alpha: 0.55)
        for (def, v) in keys {
            var bg = key, tint = fg
            if def.kind == .returnKey && returnIsBlue { bg = .systemBlue; tint = .white }
            if def.kind == .returnKey && returnIsDone { bg = .systemGreen; tint = .white }
            if def.kind == .language && langTop && languageShort == "VI" {   // tinted in Vietnamese, neutral in English
                bg = dark ? UIColor(red: 0.42, green: 0.40, blue: 1, alpha: 0.30) : UIColor(red: 0.31, green: 0.27, blue: 0.90, alpha: 0.14)
            }
            if def.kind == .language && langTop && Self.markOn {   // the corner element draws it (LangMark.swift), the key stays a tap area
                v.normalColor = .clear; v.pressedColor = .clear
                if v.backgroundColor != .clear { v.backgroundColor = .clear }
                continue
            }
            if def.kind == .language { tint = dark ? UIColor(red: 0.62, green: 0.60, blue: 1, alpha: 1) : UIColor(red: 0.31, green: 0.27, blue: 0.90, alpha: 1) }
            if cursorMode { bg = dark ? UIColor(white: 0.32, alpha: 1) : UIColor(white: 0.93, alpha: 1) }
            v.normalColor = bg
            v.pressedColor = pressed
            if v !== touchKey && v.backgroundColor != bg { v.backgroundColor = bg }
            if v.label.textColor != tint { v.label.textColor = tint; v.icon.tintColor = tint }
        }
        popup.dark = dark
        alternates.dark = dark
    }

    // MARK: touches

    func capForTests(at p: CGPoint) -> KeyCap? { cap(at: p) }
    private func cap(at p: CGPoint) -> KeyCap? {
        for (_, v) in keys where !v.isHidden && v.frame.insetBy(dx: -3, dy: -5.5).contains(p) { return v }
        // the strip is not a key: a tap on a note there ("Fixing…", "No mistakes") must never type the letter below
        if !bar.isHidden, p.y < bar.frame.maxY { return nil }
        var best: KeyCap?; var bestD = CGFloat.greatestFiniteMagnitude
        for (_, v) in keys where !v.isHidden {
            let f = v.frame.insetBy(dx: -3, dy: -5.5)
            let dx = max(f.minX - p.x, 0, p.x - f.maxX), dy = max(f.minY - p.y, 0, p.y - f.maxY)
            let d = dx * dx + dy * dy
            if d < bestD { bestD = d; best = v }
        }
        return bestD < 44 * 44 ? best : nil   // edges and gaps belong to the nearest key, like the system keyboard
    }
    private func kind(of cap: KeyCap) -> KeyKind? { keys.first { $0.view === cap }?.def.kind }

    private var lastHaptic: CFTimeInterval = 0
    /// One vibration per key press at most (overlapping touches in fast typing must not double up).
    private func tick() {
        let t = CACurrentMediaTime()
        guard t - lastHaptic > 0.04 else { return }
        lastHaptic = t
        Diag.haptics += 1
        feedback.tick()
    }

    /// `late`: the touch reached us long after the finger landed (the keyboard was busy). Its vibration is
    /// skipped, or a backlog of touches would buzz two or three times in a row.
    private func press(_ c: KeyCap, late: Bool = false) {
        c.backgroundColor = c.pressedColor
        if Self.markOn && langTop, keys.first(where: { $0.view === c })?.def.kind == .language { mark.pressed() }
        if late { Diag.late += 1; return }
        if haptics { tick() } else { UIDevice.current.playInputClick() }
    }
    private func release(_ c: KeyCap) { c.backgroundColor = c.normalColor }

    /// Taps right after the keys come back (the proofread preview closed) are the second half of a double tap.
    func ignoreTouches(for seconds: Double) { quietUntil = CACurrentMediaTime() + seconds }
    private var quietUntil: CFTimeInterval = 0

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        if CACurrentMediaTime() < quietUntil { return }
        for t in touches {
            let p = t.location(in: self)
            Diag.touches += 1
            Diag.touchStart = CACurrentMediaTime()
            Diag.delayMs = max(Diag.delayMs, (Diag.touchStart - t.timestamp) * 1000)
            // a new touch while another is down: the earlier key is typed now (fast two-thumb typing). First, as it
            // may change the layer (a symbol after a slide off 123 goes back to ABC): the new touch is on those keys
            if let prev = touch, let pk = touchKey { finish(prev, on: pk) }
            guard var c = cap(at: p), var k = kind(of: c), k != .globe else { continue }
            // fat-finger: a touch near a letter's border goes to the letter most likely next in this word
            // only on a letter key: the email/web . @ / .com keys on this page are never swapped for a letter (Tony: . gave n)
            if case .char(let ch) = k, ch.count == 1, ch.first?.isLetter == true, layer_ == .letters, let ctx = wordContext?(), !letterRects.isEmpty {
                let picked = ctx.prefix.lowercased().withCString { VKPickLetter(Float(p.x), Float(p.y), $0, ctx.vietnamese ? 1 : 0, letterRects, Int32(letterRects.count)) }
                if picked != 0, let pc = letterCaps[Character(UnicodeScalar(UInt8(bitPattern: picked)))], let pk = kind(of: pc) { c = pc; k = pk }
            }
            touch = t; touchKey = c; touchStart = p; slideFromLayer = false
            press(c, late: CACurrentMediaTime() - t.timestamp > 0.05)
            switch k {
            case .char:
                if traitCollection.horizontalSizeClass == .compact && layer_ != .numpad { popup.show(over: c, in: self) }
                startHold(after: 0.45) { [weak self] in self?.showAlternates(for: c) }
            case .backspace:
                delegate?.keyboard(self, didTap: .backspace)
                repeatCount = 0
                startHold(after: 0.45) { [weak self] in self?.startRepeat() }
            case .space:
                startHold(after: 0.4) { [weak self] in self?.enterCursorMode(at: p.x) }
            case .shift:
                delegate?.keyboard(self, didTap: .shift)
            case .layer:
                // the system keyboard switches on touch-down and lets you slide onto a symbol
                delegate?.keyboard(self, didTap: k)
                slideFromLayer = true
                if let nc = cap(at: p) { touchKey = nc; nc.backgroundColor = nc.pressedColor }
            default: break
            }
        }
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let t = touch, touches.contains(t), let current = touchKey else { return }
        let p = t.location(in: self)
        if cursorMode {
            let step = 9 * bounds.width / 402
            let n = Int((p.x - cursorAnchor) / step)
            if n != 0 { delegate?.keyboard(self, moveCursorBy: n); cursorAnchor += CGFloat(n) * step }
            return
        }
        if !alternates.isHidden { alternates.select(at: t.location(in: alternates)); return }
        guard let k = kind(of: current), k != .backspace, k != .shift else { return }
        // a tap wobbles: keep the key it started on unless the finger really slid onto another one
        guard slideFromLayer || hypot(p.x - touchStart.x, p.y - touchStart.y) > 14 else { return }
        guard let c = cap(at: p), c !== current, c.frame.insetBy(dx: 2, dy: 2).contains(p),
              let nk = kind(of: c), nk != .globe, nk != .backspace, nk != .shift else { return }
        release(current)
        touchKey = c
        c.backgroundColor = c.pressedColor
        holdTimer?.invalidate()
        if case .char = nk, traitCollection.horizontalSizeClass == .compact { popup.show(over: c, in: self) } else { popup.isHidden = true }
        if case .char = nk { startHold(after: 0.45) { [weak self] in self?.showAlternates(for: c) } }
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        for t in touches where t === touch {
            if let c = touchKey { finish(t, on: c) }
        }
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        for t in touches where t === touch {
            if let c = touchKey { release(c) }
            reset()
        }
    }

    /// The touch that owns a key ended (or a newer touch began): type that key.
    private func finish(_ t: UITouch, on c: KeyCap) {
        Diag.touchUp = CACurrentMediaTime()
        holdTimer?.invalidate(); holdTimer = nil
        release(c)
        popup.isHidden = true
        let wasSlide = slideFromLayer
        if cursorMode {
            cursorMode = false
            refreshLabels()
            delegate?.keyboardCursorModeEnded(self)
        } else if !alternates.isHidden {
            if let s = alternates.selected { delegate?.keyboard(self, insertAlternate: s) }
            alternates.isHidden = true
        } else if let k = kind(of: c) {
            switch k {
            case .backspace, .shift, .layer: break   // handled on touch-down
            default:
                lastTapPoint = touchStart
                delegate?.keyboard(self, didTap: k)
                if wasSlide, case .char = k { delegate?.keyboard(self, didTap: .layer(.letters)) }
            }
        }
        release(c)   // again: the tap may have changed its colour (VI ⇄ EN tints the pill), skipped while it was down
        reset()
    }

    /// A fresh start when the keyboard appears: no key held, no quiet period left over.
    func resetTouches() {
        if let c = touchKey { release(c) }
        quietUntil = 0
        reset()
    }

    private func reset() {
        holdTimer?.invalidate(); holdTimer = nil
        touch = nil; touchKey = nil; slideFromLayer = false
        popup.isHidden = true
        alternates.isHidden = true
        if cursorMode { cursorMode = false; refreshLabels(); delegate?.keyboardCursorModeEnded(self) }
    }

    private func startHold(after s: TimeInterval, _ f: @escaping () -> Void) {
        holdTimer?.invalidate()
        holdTimer = Timer.scheduledTimer(withTimeInterval: s, repeats: false) { _ in f() }
    }

    private func startRepeat() {
        holdTimer = Timer.scheduledTimer(withTimeInterval: 0.09, repeats: true) { [weak self] _ in
            guard let self else { return }
            self.repeatCount += 1
            self.delegate?.keyboardBackspaceRepeat(self, count: self.repeatCount)
        }
    }

    private func enterCursorMode(at x: CGFloat) {
        cursorMode = true
        cursorAnchor = touch?.location(in: self).x ?? x
        popup.isHidden = true
        if haptics { tick() }
        refreshLabels()
    }

    // MARK: long-press alternates

    static let alternateTable: [String: [String]] = [
        "a": ["a", "ă", "â", "á", "à", "ả", "ã", "ạ"], "e": ["e", "ê", "é", "è", "ẻ", "ẽ", "ẹ"],
        "o": ["o", "ô", "ơ", "ó", "ò", "ỏ", "õ", "ọ"], "u": ["u", "ư", "ú", "ù", "ủ", "ũ", "ụ"],
        "i": ["i", "í", "ì", "ỉ", "ĩ", "ị"], "y": ["y", "ý", "ỳ", "ỷ", "ỹ", "ỵ"], "d": ["d", "đ"],
        ".": [".", "…"], "-": ["-", "–", "—", "•"], "'": ["'", "’", "‘", "`"], "\"": ["\"", "“", "”", "„", "«", "»"],
        "!": ["!", "¡"], "?": ["?", "¿"], "$": ["$", "₫", "€", "£", "¥", "₩", "¢"], "0": ["0", "°"], "&": ["&", "§"],
        "/": ["/", "\\"], "%": ["%", "‰"], ".com": [".com", ".vn", ".net", ".org", ".edu", ".io"],
    ]

    private func showAlternates(for c: KeyCap) {
        guard let k = kind(of: c), case .char(let ch) = k, let list = Self.alternateTable[ch] else { return }
        let upper = shift != .off && layer_ == .letters
        popup.isHidden = true
        alternates.show(list.map { upper ? $0.uppercased() : $0 }, over: c, in: self)
        if haptics { tick() }
    }
}

/// One key.
final class KeyCap: UIView {
    let label = UILabel()
    let icon = UIImageView()
    var normalColor: UIColor = .white
    var pressedColor: UIColor = .lightGray
    override init(frame: CGRect) {
        super.init(frame: frame)
        layer.cornerRadius = 8.5
        layer.cornerCurve = .continuous
        layer.shadowColor = UIColor.black.cgColor
        layer.shadowOpacity = 0.10   // light: the keys are translucent
        layer.shadowRadius = 0
        layer.shadowOffset = CGSize(width: 0, height: 1)
        label.textAlignment = .center
        icon.contentMode = .center
        addSubview(label); addSubview(icon)
    }
    required init?(coder: NSCoder) { fatalError() }
    override func layoutSubviews() {
        super.layoutSubviews()
        label.frame = bounds.insetBy(dx: 2, dy: 0).offsetBy(dx: 0, dy: -1)
        icon.frame = bounds
        // an explicit shadow path: without it every key's shadow is rendered offscreen on each frame
        layer.shadowPath = UIBezierPath(roundedRect: bounds, cornerRadius: layer.cornerRadius).cgPath
    }
}

/// The balloon shown above a pressed letter: the key itself grows upward into a wider bubble.
final class KeyPopup: UIView {
    let label = UILabel()
    private let shape = CAShapeLayer()
    var dark = false { didSet { shape.fillColor = (dark ? UIColor(white: 0.42, alpha: 1) : .white).cgColor; label.textColor = dark ? .white : .black } }
    override init(frame: CGRect) {
        super.init(frame: frame)
        isUserInteractionEnabled = false
        layer.addSublayer(shape)
        shape.fillColor = UIColor.white.cgColor
        shape.shadowColor = UIColor.black.cgColor
        shape.shadowOpacity = 0.28
        shape.shadowRadius = 3
        shape.shadowOffset = CGSize(width: 0, height: 1)
        label.font = .systemFont(ofSize: 38, weight: .regular)
        label.textAlignment = .center
        addSubview(label)
    }
    required init?(coder: NSCoder) { fatalError() }

    func show(over key: KeyCap, in parent: UIView) {
        label.text = key.label.text
        let k = key.frame
        let extra: CGFloat = 13, bubbleH: CGFloat = 54
        var bubble = CGRect(x: k.minX - extra, y: k.minY - bubbleH + 6, width: k.width + 2 * extra, height: bubbleH)
        // keep the bubble on screen at the left and right edges
        if bubble.minX < 1 { bubble.origin.x = 1 }
        if bubble.maxX > parent.bounds.width - 1 { bubble.origin.x = parent.bounds.width - 1 - bubble.width }
        if bubble.minY < 0 {
            // no room above (top row, no strip on top): a widened key that grows to the top edge
            // instead of a balloon whose neck would fold back over the key
            let r = CGRect(x: bubble.minX, y: 0, width: bubble.width, height: k.maxY)
            frame = r
            let path = UIBezierPath(roundedRect: CGRect(origin: .zero, size: r.size), cornerRadius: 10)
            shape.path = path.cgPath
            shape.shadowPath = path.cgPath
            label.font = .systemFont(ofSize: 30, weight: .regular)
            label.frame = CGRect(x: 0, y: 2, width: r.width, height: r.height * 0.7)
            isHidden = false
            parent.bringSubviewToFront(self)
            return
        }
        label.font = .systemFont(ofSize: 38, weight: .regular)
        let all = bubble.union(k)
        frame = all
        let b = bubble.offsetBy(dx: -all.minX, dy: -all.minY), kk = k.offsetBy(dx: -all.minX, dy: -all.minY)
        let path = UIBezierPath()
        let r: CGFloat = 10, rk: CGFloat = 8.5, neck = kk.minY + 4
        path.move(to: CGPoint(x: b.minX + r, y: b.minY))
        path.addLine(to: CGPoint(x: b.maxX - r, y: b.minY))
        path.addQuadCurve(to: CGPoint(x: b.maxX, y: b.minY + r), controlPoint: CGPoint(x: b.maxX, y: b.minY))
        path.addLine(to: CGPoint(x: b.maxX, y: b.maxY - 10))
        path.addCurve(to: CGPoint(x: kk.maxX, y: neck + 8), controlPoint1: CGPoint(x: b.maxX, y: b.maxY), controlPoint2: CGPoint(x: kk.maxX, y: neck))
        path.addLine(to: CGPoint(x: kk.maxX, y: kk.maxY - rk))
        path.addQuadCurve(to: CGPoint(x: kk.maxX - rk, y: kk.maxY), controlPoint: CGPoint(x: kk.maxX, y: kk.maxY))
        path.addLine(to: CGPoint(x: kk.minX + rk, y: kk.maxY))
        path.addQuadCurve(to: CGPoint(x: kk.minX, y: kk.maxY - rk), controlPoint: CGPoint(x: kk.minX, y: kk.maxY))
        path.addLine(to: CGPoint(x: kk.minX, y: neck + 8))
        path.addCurve(to: CGPoint(x: b.minX, y: b.maxY - 10), controlPoint1: CGPoint(x: kk.minX, y: neck), controlPoint2: CGPoint(x: b.minX, y: b.maxY))
        path.addLine(to: CGPoint(x: b.minX, y: b.minY + r))
        path.addQuadCurve(to: CGPoint(x: b.minX + r, y: b.minY), controlPoint: CGPoint(x: b.minX, y: b.minY))
        path.close()
        shape.path = path.cgPath
        shape.shadowPath = path.cgPath
        label.frame = CGRect(x: b.minX, y: b.minY + 2, width: b.width, height: b.height - 8)
        isHidden = false
        parent.bringSubviewToFront(self)
    }
}

/// The strip of alternates shown when a key is held; the finger slides to pick one.
final class AlternatesView: UIView {
    private var labels: [UILabel] = []
    private(set) var selected: String?
    private let highlight = UIView()
    var dark = false { didSet { backgroundColor = dark ? UIColor(white: 0.42, alpha: 1) : .white; labels.forEach { $0.textColor = dark ? .white : .black } } }
    override init(frame: CGRect) {
        super.init(frame: frame)
        isUserInteractionEnabled = false
        backgroundColor = .white
        layer.cornerRadius = 10
        layer.cornerCurve = .continuous
        layer.shadowColor = UIColor.black.cgColor
        layer.shadowOpacity = 0.28
        layer.shadowRadius = 4
        layer.shadowOffset = CGSize(width: 0, height: 1)
        highlight.backgroundColor = .systemBlue
        highlight.layer.cornerRadius = 7
        addSubview(highlight)
    }
    required init?(coder: NSCoder) { fatalError() }

    func show(_ items: [String], over key: KeyCap, in parent: UIView) {
        labels.forEach { $0.removeFromSuperview() }
        labels = items.map { s in
            let l = UILabel(); l.text = s; l.font = .systemFont(ofSize: 24); l.textAlignment = .center
            l.textColor = dark ? .white : .black; addSubview(l); return l
        }
        let w: CGFloat = 34, h: CGFloat = 46
        let total = CGFloat(items.count) * w + 8
        var x = key.frame.minX - 4
        if x + total > parent.bounds.width - 2 { x = parent.bounds.width - 2 - total }
        x = max(2, x)
        frame = CGRect(x: x, y: max(0, key.frame.minY - h - 6), width: total, height: h)
        for (i, l) in labels.enumerated() { l.frame = CGRect(x: 4 + CGFloat(i) * w, y: 0, width: w, height: h) }
        layer.shadowPath = UIBezierPath(roundedRect: bounds, cornerRadius: 10).cgPath
        selectIndex(0)
        isHidden = false
        parent.bringSubviewToFront(self)
    }

    func select(at p: CGPoint) {
        guard !labels.isEmpty else { return }
        let i = min(max(Int((p.x - 4) / 34), 0), labels.count - 1)
        selectIndex(i)
    }

    private func selectIndex(_ i: Int) {
        selected = labels[i].text
        highlight.frame = labels[i].frame.insetBy(dx: 1, dy: 5)
        for (j, l) in labels.enumerated() { l.textColor = j == i ? .white : (dark ? .white : .black) }
    }
}

/// One cell of the suggestion strip.
struct Suggestion: Equatable {
    enum Kind: Equatable { case word, next, proofread, alternative, undo, busy }
    var text: String
    var quoted = false        // the word as typed, when something else is offered
    var highlighted = false   // what the space bar will put in (auto-correct)
    var kind: Kind = .word    // next word, ✨ proofread, another word for a selection, undo, or a "working…" note
}

/// The strip above the keys: up to three words, like the system keyboard's.
final class SuggestionBar: UIView {
    var onPick: ((Int) -> Void)?
    var onBrand: (() -> Void)?
    /// A nudge (Nudge.swift): its button, and its ✕.
    var onNudge: ((NudgeView.Kind) -> Void)?
    var onNudgeClosed: ((NudgeView.Kind) -> Void)?
    private let nudgeView = NudgeView()
    /// The nudge shown, on an empty strip only: words coming in fold it away.
    private(set) var nudge: NudgeView.Kind?
    var dark = false { didSet { if dark != oldValue { restyle() } } }
    private var cells: [UIButton] = []
    private var lines: [UIView] = []
    private var items: [Suggestion] = []
    /// The brand on the right of an empty strip (Tony): the icon and the wordmark, as in the Mac emoji panel.
    private let mark = UIView()
    var showsBrand = true { didSet { if showsBrand != oldValue { restyle() } } }
    private let markIcon = UILabel()
    private let markText = UILabel()

    override init(frame: CGRect) {
        super.init(frame: frame)
        markIcon.text = "Vi"
        markIcon.font = .systemFont(ofSize: 10, weight: .bold)
        markIcon.textColor = .white
        markIcon.textAlignment = .center
        markIcon.layer.cornerRadius = 5
        markIcon.layer.cornerCurve = .continuous
        markIcon.layer.masksToBounds = true
        mark.addSubview(markIcon); mark.addSubview(markText)
        mark.addGestureRecognizer(UITapGestureRecognizer(target: self, action: #selector(tapBrand)))   // opens the Vikey app (Tony)
        mark.isAccessibilityElement = true
        mark.accessibilityLabel = "Vikey"
        mark.accessibilityTraits = .button
        addSubview(mark)
        for i in 0..<3 {
            let b = UIButton(type: .custom)
            b.tag = i
            b.titleLabel?.font = .systemFont(ofSize: 17)
            b.titleLabel?.lineBreakMode = .byTruncatingMiddle
            b.layer.cornerRadius = 8
            b.layer.cornerCurve = .continuous
            b.addTarget(self, action: #selector(tap(_:)), for: .touchUpInside)
            addSubview(b); cells.append(b)
        }
        for _ in 0..<2 { let l = UIView(); addSubview(l); lines.append(l) }
        nudgeView.isHidden = true
        nudgeView.onAction = { [unowned self] in if let k = nudge { onNudge?(k) } }
        nudgeView.onClose = { [unowned self] in if let k = nudge { nudge = nil; nudgeView.isHidden = true; restyle(); onNudgeClosed?(k) } }
        addSubview(nudgeView)
        restyle()
    }
    required init?(coder: NSCoder) { fatalError() }

    @objc private func tapBrand() { onBrand?() }

    @objc private func tap(_ b: UIButton) { if b.tag < items.count { onPick?(b.tag) } }

    func show(_ s: [Suggestion]) {
        items = s
        if !s.isEmpty, nudge != nil { hideNudge() }
        restyle(); setNeedsLayout()
    }

    /// Shows a nudge on the empty strip, opening as the corner mark does.
    func showNudge(_ k: NudgeView.Kind) {
        guard items.isEmpty else { return }
        nudge = k
        nudgeView.dark = dark
        nudgeView.isHidden = false
        nudgeView.frame = bounds.insetBy(dx: 2, dy: 2)
        nudgeView.set(k)
        restyle()
        nudgeView.play()
    }
    func hideNudge() {
        guard nudge != nil else { return }
        nudge = nil
        nudgeView.fold { [weak self] in self?.nudgeView.isHidden = true; self?.restyle() }
    }

    private func restyle() {
        let fg: UIColor = dark ? .white : .black
        let indigo: UIColor = dark ? UIColor(red: 0.549, green: 0.529, blue: 1, alpha: 1) : UIColor(red: 0.310, green: 0.275, blue: 0.898, alpha: 1)
        markIcon.backgroundColor = UIColor(red: 0.310, green: 0.275, blue: 0.898, alpha: 1)   // the app icon's indigo in both modes
        let word = NSMutableAttributedString(string: "Vi", attributes: [.font: UIFont.systemFont(ofSize: 15, weight: .bold), .foregroundColor: fg])
        word.append(NSAttributedString(string: "key", attributes: [.font: UIFont.systemFont(ofSize: 15, weight: .bold), .foregroundColor: indigo]))
        markText.attributedText = word
        mark.isHidden = !items.isEmpty || !showsBrand || nudge != nil   // only on an empty strip
        nudgeView.dark = dark
        for (i, b) in cells.enumerated() {
            let it = i < items.count ? items[i] : nil
            let title: String? = it.map {
                switch $0.kind {
                case .proofread: return "✨ " + $0.text
                case .undo: return "↩︎ " + $0.text
                default: return $0.quoted ? "\u{201C}\($0.text)\u{201D}" : $0.text
                }
            }
            b.setTitle(title, for: .normal)
            let brand: UIColor = dark ? UIColor(red: 0.62, green: 0.60, blue: 1, alpha: 1) : UIColor(red: 0.31, green: 0.27, blue: 0.90, alpha: 1)
            b.setTitleColor(it?.kind == .busy ? fg.withAlphaComponent(0.45) : it?.kind == .proofread || it?.kind == .undo ? brand : fg, for: .normal)
            b.titleLabel?.font = .systemFont(ofSize: 17, weight: it?.kind == .proofread ? .semibold : .regular)
            b.backgroundColor = it?.highlighted == true ? (dark ? UIColor(white: 0.42, alpha: 1) : .white) : .clear
            b.isEnabled = it != nil && it?.kind != .busy
        }
        for (i, l) in lines.enumerated() {
            l.backgroundColor = fg.withAlphaComponent(0.18)
            // no line next to a highlighted cell, like the system strip
            l.isHidden = items.count < 2 || items.indices.contains(i) && items[i].highlighted || items.indices.contains(i + 1) && items[i + 1].highlighted
        }
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        markText.sizeToFit()
        if !nudgeView.isHidden { nudgeView.frame = bounds.insetBy(dx: 2, dy: 2) }
        let iconSide: CGFloat = 18, gap: CGFloat = 6
        let mw = iconSide + gap + markText.bounds.width
        mark.frame = CGRect(x: bounds.width - 14 - mw, y: (bounds.height - iconSide) / 2, width: mw, height: iconSide)
        markIcon.frame = CGRect(x: 0, y: 0, width: iconSide, height: iconSide)
        markText.frame = CGRect(x: iconSide + gap, y: (iconSide - markText.bounds.height) / 2, width: markText.bounds.width, height: markText.bounds.height)
        // one wide cell for a single action (✨ Proofread, ↩︎ Undo, "working…"); three otherwise
        if items.count == 1, items[0].kind != .word, items[0].kind != .next {
            cells[0].frame = bounds.insetBy(dx: 2, dy: 2)
            for b in cells.dropFirst() { b.frame = .zero }
            lines.forEach { $0.isHidden = true }
            return
        }
        let w = bounds.width / 3
        for (i, b) in cells.enumerated() { b.frame = CGRect(x: CGFloat(i) * w + 2, y: 2, width: w - 4, height: bounds.height - 4) }
        for (i, l) in lines.enumerated() { l.frame = CGRect(x: CGFloat(i + 1) * w - 0.5, y: bounds.height * 0.28, width: 1, height: bounds.height * 0.44) }
    }
}

extension KeyboardView {
    /// Opens the Vikey app (vikey://). A keyboard can't call UIApplication.shared, so this walks the responder chain
    /// to the host's UIApplication and calls open(_:options:completionHandler:) on it by selector.
    func openVikeyApp(_ path: String = "") {
        guard let url = URL(string: "vikey://\(path)") else { return }
        let sel = NSSelectorFromString("openURL:options:completionHandler:")
        var r: UIResponder? = self
        while let next = r {
            if next is UIApplication, next.responds(to: sel), let imp = next.method(for: sel) {
                typealias Open = @convention(c) (AnyObject, Selector, NSURL, NSDictionary, AnyObject?) -> Void
                unsafeBitCast(imp, to: Open.self)(next, sel, url as NSURL, NSDictionary(), nil)
                return
            }
            r = next.next
        }
    }
}
