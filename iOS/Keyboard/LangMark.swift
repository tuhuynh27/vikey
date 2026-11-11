import UIKit

/// The strip's corner: one element for both the VI/EN switch and the Vikey mark (Tony picked design A, 2026-11-02).
/// When the keyboard comes up it opens as [Vi] Vikey with the letters springing in, then folds its letters down into
/// VI / EN. A switch unfurls the language's name ("Tiếng Việt", "English") for a moment, then folds back.
/// Purely visual: KeyboardView routes taps on the corner to the language key under it.
final class LangMarkView: UIView {
    var dark = false { didSet { if dark != oldValue { restyle() } } }
    /// The width the language key takes in the corner (the element's resting size).
    var pillWidth: CGFloat = 50 { didSet { if pillWidth != oldValue { setNeedsLayout() } } }
    private(set) var vietnamese = true
    private var introRunning = false
    /// A switch animation is playing: a layout pass in the meantime must not snap it to the resting state.
    private var holdUntil: CFTimeInterval = 0
    private var gen = 0

    private var brand: UIColor { dark ? UIColor(red: 0.549, green: 0.529, blue: 1, alpha: 1) : UIColor(red: 0.310, green: 0.275, blue: 0.898, alpha: 1) }
    private let indigo = UIColor(red: 0.310, green: 0.275, blue: 0.898, alpha: 1)
    private var ink: UIColor { dark ? .white : .black }
    private var keyFill: UIColor { dark ? UIColor(white: 1, alpha: 0.165) : UIColor(red: 0.992, green: 1, blue: 1, alpha: 0.99) }
    /// VI and EN look the same (Tony): a key-coloured capsule, the language in brand indigo.
    private var tint: UIColor { keyFill }

    private let capsule = UIView()
    private let langLabel = UILabel()
    private let tile = UIView()
    private let tileLabel = UILabel()
    private let letters: [UILabel] = "Vikey".enumerated().map { i, c in
        let l = UILabel(); l.text = String(c); l.font = .systemFont(ofSize: 16.5, weight: .bold); l.tag = i; l.sizeToFit(); return l
    }

    override init(frame: CGRect) {
        super.init(frame: frame)
        isUserInteractionEnabled = false
        capsule.layer.cornerCurve = .continuous
        langLabel.textAlignment = .center
        langLabel.font = .systemFont(ofSize: 15, weight: .bold)
        tile.layer.cornerCurve = .continuous
        tileLabel.textAlignment = .center
        tileLabel.textColor = .white
        tile.addSubview(tileLabel)
        addSubview(capsule); capsule.addSubview(langLabel); capsule.addSubview(tile)
        letters.forEach { capsule.addSubview($0) }
        restyle()
    }
    required init?(coder: NSCoder) { fatalError() }

    func setLanguage(vietnamese vi: Bool, animated: Bool) {
        let changed = vi != vietnamese
        vietnamese = vi
        guard changed else { return }
        if !animated || introRunning { if !introRunning { settle() }; return }
        holdUntil = CACurrentMediaTime() + 1.5
        morphSwitch()
    }

    /// A finger on the corner: the capsule dims a little, like a key being pressed.
    func pressed() {
        guard !introRunning else { return }
        UIView.animate(withDuration: 0.08) { self.capsule.alpha = 0.6 } completion: { _ in
            UIView.animate(withDuration: 0.25) { self.capsule.alpha = 1 }
        }
    }

    /// The keyboard came up: the mark has its moment, then becomes the language switch.
    func playIntro() {
        guard bounds.width > 0 else { return }
        gen += 1
        introRunning = true
        morphIntro(gen)
    }

    private func restyle() {
        langLabel.textColor = brand
        tile.backgroundColor = indigo
        letters.forEach { $0.textColor = $0.tag >= 2 ? brand : ink }
        if !introRunning { capsule.backgroundColor = vietnamese ? tint : keyFill }
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        guard !introRunning, CACurrentMediaTime() > holdUntil else { return }
        settle()
    }

    /// The resting state: the language switch in the corner.
    private func settle() {
        guard bounds.height > 0 else { return }
        let H = bounds.height
        UIView.performWithoutAnimation {
            capsule.layer.removeAllAnimations()
            capsule.transform = .identity
            capsule.frame = CGRect(x: 0, y: 0, width: pillWidth, height: H)
            capsule.layer.cornerRadius = H / 2
            capsule.alpha = 1
            capsule.backgroundColor = vietnamese ? tint : keyFill
            langLabel.text = vietnamese ? "VI" : "EN"
            langLabel.frame = capsule.bounds.offsetBy(dx: 0, dy: -0.5)
            langLabel.alpha = 1; langLabel.transform = .identity
            tile.alpha = 0
            letters.forEach { $0.alpha = 0 }
        }
    }

    // MARK: animations

    private func morphIntro(_ g: Int) {
        let H = bounds.height
        let tileS: CGFloat = 21
        let wordW = letters.reduce(0) { $0 + $1.bounds.width }
        let wide = 12 + tileS + 7 + wordW + 14
        UIView.performWithoutAnimation {
            capsule.frame = CGRect(x: 0, y: 0, width: pillWidth, height: H)
            capsule.layer.cornerRadius = H / 2
            capsule.backgroundColor = keyFill
            capsule.alpha = 0
            capsule.transform = CGAffineTransform(scaleX: 0.6, y: 0.6)
            langLabel.alpha = 0
            tile.frame = CGRect(x: 12, y: (H - tileS) / 2, width: tileS, height: tileS)
            tile.layer.cornerRadius = 6
            tileLabel.frame = tile.bounds; tileLabel.text = "Vi"; tileLabel.font = .systemFont(ofSize: 10.5, weight: .heavy)
            tile.alpha = 1; tile.transform = CGAffineTransform(scaleX: 0.01, y: 0.01).rotated(by: -.pi / 3)
            var x = 12 + tileS + 7
            for l in letters {
                l.frame.origin = CGPoint(x: x, y: (H - l.bounds.height) / 2)
                x += l.bounds.width
                l.alpha = 0; l.transform = CGAffineTransform(translationX: 0, y: 10)
            }
        }
        UIView.animate(withDuration: 0.6, delay: 0, usingSpringWithDamping: 0.72, initialSpringVelocity: 0) {
            self.capsule.alpha = 1; self.capsule.transform = .identity
            self.capsule.frame.size.width = wide
        }
        UIView.animate(withDuration: 0.55, delay: 0.08, usingSpringWithDamping: 0.55, initialSpringVelocity: 0) { self.tile.transform = .identity }
        for (i, l) in letters.enumerated() {
            UIView.animate(withDuration: 0.5, delay: 0.16 + Double(i) * 0.045, usingSpringWithDamping: 0.6, initialSpringVelocity: 0) {
                l.alpha = 1; l.transform = .identity
            }
        }
        after(1.45, g) { self.morphCollapse(g) }
    }

    private func morphCollapse(_ g: Int) {
        // "key" drops away right to left, the tile spins out, "Vi" settles into the pill as VI
        for (n, l) in letters.reversed().enumerated() where l.tag >= 2 {
            UIView.animate(withDuration: 0.28, delay: Double(n) * 0.04, options: .curveEaseIn) {
                l.alpha = 0; l.transform = CGAffineTransform(translationX: 0, y: 9).scaledBy(x: 0.7, y: 0.7)
            }
        }
        UIView.animate(withDuration: 0.3, delay: 0.05, options: .curveEaseIn) {
            self.tile.alpha = 0; self.tile.transform = CGAffineTransform(scaleX: 0.2, y: 0.2).rotated(by: .pi / 2)
        }
        let vi = letters.filter { $0.tag < 2 }
        let target = CGPoint(x: pillWidth / 2, y: bounds.height / 2)
        UIView.animate(withDuration: 0.5, delay: 0.12, usingSpringWithDamping: 0.75, initialSpringVelocity: 0) {
            self.capsule.frame.size.width = self.pillWidth
            self.capsule.backgroundColor = self.vietnamese ? self.tint : self.keyFill
            let w = vi.reduce(0) { $0 + $1.bounds.width }
            var x = target.x - w / 2
            for l in vi { l.center = CGPoint(x: x + l.bounds.width / 2, y: target.y); x += l.bounds.width; l.textColor = self.brand }
        }
        after(0.42, g) {
            self.langLabel.text = self.vietnamese ? "VI" : "EN"
            self.langLabel.frame = CGRect(x: 0, y: -0.5, width: self.pillWidth, height: self.bounds.height)
            UIView.animate(withDuration: 0.22) { self.langLabel.alpha = 1; vi.forEach { $0.alpha = 0 } }
        }
        after(0.75, g) { self.introRunning = false; self.restyle(); self.settle() }
    }

    private func morphSwitch() {
        // squash, unfurl the language's name, fold back
        let H = bounds.height
        let name = vietnamese ? "Tiếng Việt" : "English"
        let w = (name as NSString).size(withAttributes: [.font: UIFont.systemFont(ofSize: 15, weight: .bold)]).width + 28
        let roll = CATransition(); roll.type = .push; roll.subtype = vietnamese ? .fromTop : .fromBottom; roll.duration = 0.25
        langLabel.layer.add(roll, forKey: "roll")
        langLabel.text = name
        UIView.animateKeyframes(withDuration: 0.25, delay: 0) {
            UIView.addKeyframe(withRelativeStartTime: 0, relativeDuration: 0.5) { self.capsule.transform = CGAffineTransform(scaleX: 0.9, y: 1.08) }
            UIView.addKeyframe(withRelativeStartTime: 0.5, relativeDuration: 0.5) { self.capsule.transform = .identity }
        }
        UIView.animate(withDuration: 0.5, delay: 0.05, usingSpringWithDamping: 0.7, initialSpringVelocity: 0) {
            self.capsule.frame = CGRect(x: 0, y: 0, width: w, height: H)
            self.langLabel.frame = CGRect(x: 0, y: -0.5, width: w, height: H)
            self.capsule.backgroundColor = self.vietnamese ? self.tint : self.keyFill
        }
        gen += 1
        let g = gen
        after(0.95, g) {
            // the name fades out as the pill folds; the short label fades in once it has room
            UIView.animate(withDuration: 0.14) { self.langLabel.alpha = 0 }
            UIView.animate(withDuration: 0.45, delay: 0.05, usingSpringWithDamping: 0.8, initialSpringVelocity: 0) {
                self.capsule.frame = CGRect(x: 0, y: 0, width: self.pillWidth, height: H)
            }
            self.after(0.2, g) {
                self.langLabel.text = self.vietnamese ? "VI" : "EN"
                self.langLabel.frame = CGRect(x: 0, y: -0.5, width: self.pillWidth, height: H)
                self.langLabel.transform = CGAffineTransform(scaleX: 0.7, y: 0.7)
                UIView.animate(withDuration: 0.35, delay: 0, usingSpringWithDamping: 0.6, initialSpringVelocity: 0) {
                    self.langLabel.alpha = 1; self.langLabel.transform = .identity
                }
            }
        }
    }

    // MARK: util

    private func after(_ s: Double, _ g: Int, _ f: @escaping () -> Void) {
        DispatchQueue.main.asyncAfter(deadline: .now() + s) { [weak self] in
            guard let self, self.gen == g else { return }
            f()
        }
    }
}
