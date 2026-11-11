import UIKit

/// A nudge in the empty suggestion strip (Tony, 2026-11-07): a few words, then an action button,
/// and an ✕. "Thích Vikey? [★ Gửi đánh giá]" invites a review; "Gặp lỗi? [Gửi góp ý]" a bug report.
/// It opens like the corner mark: the capsule springs out, the button's symbol pops in with a turn,
/// the words rise one after another; ✕ folds it back the way the mark folds into VI / EN.
final class NudgeView: UIView {
    enum Kind: String { case review, bug }

    var onAction: (() -> Void)?
    var onClose: (() -> Void)?
    var dark = false { didSet { if dark != oldValue { restyle() } } }
    private(set) var kind: Kind = .review

    private let capsule = UIView()
    private var words: [UILabel] = []
    private let button = UIControl()
    private let buttonFill = UIView()
    private let symbol = UIImageView()
    private let buttonLabel = UILabel()
    private let close = UIButton(type: .system)
    private var gen = 0

    private var brand: UIColor { dark ? UIColor(red: 0.549, green: 0.529, blue: 1, alpha: 1) : UIColor(red: 0.310, green: 0.275, blue: 0.898, alpha: 1) }
    private var ink: UIColor { dark ? .white : .black }
    private var capsuleFill: UIColor { dark ? UIColor(white: 1, alpha: 0.09) : UIColor(white: 1, alpha: 0.6) }

    /// The words and the action, as Tony wrote them.
    static func copy(_ k: Kind) -> (text: String, action: String, symbol: String) {
        let en = (Bundle.main.preferredLocalizations.first ?? "vi") != "vi"
        switch k {
        case .review: return en ? ("Like Vikey?", "Rate it", "star.fill") : ("Thích Vikey?", "Gửi đánh giá", "star.fill")
        case .bug: return en ? ("Found a bug?", "Send feedback", "ladybug.fill") : ("Gặp lỗi?", "Gửi góp ý", "ladybug.fill")
        }
    }

    override init(frame: CGRect) {
        super.init(frame: frame)
        addSubview(capsule)
        capsule.layer.cornerCurve = .continuous
        buttonFill.isUserInteractionEnabled = false
        buttonFill.layer.cornerCurve = .continuous
        button.addSubview(buttonFill)
        symbol.contentMode = .scaleAspectFit
        symbol.isUserInteractionEnabled = false
        button.addSubview(symbol)
        buttonLabel.font = .systemFont(ofSize: 14, weight: .semibold)
        buttonLabel.isUserInteractionEnabled = false
        button.addSubview(buttonLabel)
        button.addTarget(self, action: #selector(tapAction), for: .touchUpInside)
        button.addTarget(self, action: #selector(down), for: [.touchDown, .touchDragEnter])
        button.addTarget(self, action: #selector(up), for: [.touchUpInside, .touchUpOutside, .touchCancel, .touchDragExit])
        button.isAccessibilityElement = true
        button.accessibilityTraits = .button
        addSubview(button)
        close.setImage(UIImage(systemName: "xmark", withConfiguration: UIImage.SymbolConfiguration(pointSize: 12, weight: .semibold)), for: .normal)
        close.accessibilityLabel = "Để sau"
        close.addTarget(self, action: #selector(tapClose), for: .touchUpInside)
        addSubview(close)
        restyle()
    }
    required init?(coder: NSCoder) { fatalError() }

    @objc private func tapAction() { onAction?() }
    @objc private func tapClose() { fold { self.onClose?() } }
    @objc private func down() { UIView.animate(withDuration: 0.08) { self.button.transform = CGAffineTransform(scaleX: 0.94, y: 0.94); self.button.alpha = 0.8 } }
    @objc private func up() { UIView.animate(withDuration: 0.3, delay: 0, usingSpringWithDamping: 0.6, initialSpringVelocity: 0) { self.button.transform = .identity; self.button.alpha = 1 } }

    func set(_ k: Kind) {
        kind = k
        let c = Self.copy(k)
        words.forEach { $0.removeFromSuperview() }
        // Each word its own label, so that they rise one after another (the mark's letters do).
        words = c.text.split(separator: " ").map { w in
            let l = UILabel(); l.text = String(w); l.font = .systemFont(ofSize: 15, weight: .medium); l.sizeToFit()
            l.isUserInteractionEnabled = false
            addSubview(l); return l
        }
        bringSubviewToFront(button); bringSubviewToFront(close)
        symbol.image = UIImage(systemName: c.symbol, withConfiguration: UIImage.SymbolConfiguration(pointSize: 12, weight: .bold))
        buttonLabel.text = c.action; buttonLabel.sizeToFit()
        button.accessibilityLabel = "\(c.text) \(c.action)"
        restyle(); setNeedsLayout(); layoutIfNeeded()
    }

    private func restyle() {
        capsule.backgroundColor = capsuleFill
        words.forEach { $0.textColor = ink.withAlphaComponent(0.75) }
        buttonFill.backgroundColor = brand
        symbol.tintColor = .white
        buttonLabel.textColor = .white
        close.tintColor = ink.withAlphaComponent(0.4)
    }

    // The resting layout: the words from the left, the button after them, the ✕ on the right.
    private var buttonW: CGFloat { 10 + 14 + 5 + buttonLabel.bounds.width + 12 }
    override func layoutSubviews() {
        super.layoutSubviews()
        guard !animating else { return }
        place()
    }
    private var animating = false
    private func place() {
        let H = bounds.height, cw: CGFloat = 34
        close.frame = CGRect(x: bounds.width - cw, y: 0, width: cw, height: H)
        var x: CGFloat = 14
        for w in words { w.frame.origin = CGPoint(x: x, y: (H - w.bounds.height) / 2); x += w.bounds.width + 4 }
        let bh = H - 10
        button.frame = CGRect(x: x + 6, y: 5, width: buttonW, height: bh)
        buttonFill.frame = button.bounds; buttonFill.layer.cornerRadius = bh / 2
        symbol.frame = CGRect(x: 10, y: (bh - 14) / 2, width: 14, height: 14)
        buttonLabel.frame.origin = CGPoint(x: 29, y: (bh - buttonLabel.bounds.height) / 2)
        capsule.frame = CGRect(x: 0, y: 1, width: button.frame.maxX + 5, height: H - 2)
        capsule.layer.cornerRadius = capsule.bounds.height / 2
    }

    /// Opens: the capsule springs out from the left, the words rise one after another, the button
    /// grows in with its symbol turning into place, then the ✕ appears.
    func play() {
        gen += 1; let g = gen
        animating = true
        place()
        let full = capsule.frame.width
        UIView.performWithoutAnimation {
            capsule.alpha = 0
            capsule.frame.size.width = bounds.height
            capsule.transform = CGAffineTransform(scaleX: 0.6, y: 0.6)
            for w in words { w.alpha = 0; w.transform = CGAffineTransform(translationX: 0, y: 10) }
            button.alpha = 0; button.transform = CGAffineTransform(scaleX: 0.5, y: 0.5)
            symbol.transform = CGAffineTransform(scaleX: 0.01, y: 0.01).rotated(by: -.pi / 3)
            close.alpha = 0
        }
        UIView.animate(withDuration: 0.6, delay: 0, usingSpringWithDamping: 0.72, initialSpringVelocity: 0) {
            self.capsule.alpha = 1; self.capsule.transform = .identity; self.capsule.frame.size.width = full
        }
        for (i, w) in words.enumerated() {
            UIView.animate(withDuration: 0.5, delay: 0.1 + Double(i) * 0.07, usingSpringWithDamping: 0.6, initialSpringVelocity: 0) {
                w.alpha = 1; w.transform = .identity
            }
        }
        let after = 0.1 + Double(words.count) * 0.07
        UIView.animate(withDuration: 0.55, delay: after, usingSpringWithDamping: 0.6, initialSpringVelocity: 0) {
            self.button.alpha = 1; self.button.transform = .identity
        }
        UIView.animate(withDuration: 0.55, delay: after + 0.08, usingSpringWithDamping: 0.55, initialSpringVelocity: 0) { self.symbol.transform = .identity }
        UIView.animate(withDuration: 0.3, delay: after + 0.25) { self.close.alpha = 1 }
        // A star's twinkle (the review's), a moment after it settles.
        DispatchQueue.main.asyncAfter(deadline: .now() + after + 1.1) { [weak self] in
            guard let self, self.gen == g else { return }
            self.animating = false
            guard self.kind == .review else { return }
            UIView.animateKeyframes(withDuration: 0.6, delay: 0) {
                UIView.addKeyframe(withRelativeStartTime: 0, relativeDuration: 0.4) { self.symbol.transform = CGAffineTransform(scaleX: 1.35, y: 1.35).rotated(by: .pi / 5) }
                UIView.addKeyframe(withRelativeStartTime: 0.4, relativeDuration: 0.6) { self.symbol.transform = .identity }
            }
        }
    }

    /// Folds away: the words drop right to left, the button spins out, the capsule closes.
    func fold(_ done: @escaping () -> Void) {
        gen += 1
        animating = true
        UIView.animate(withDuration: 0.15) { self.close.alpha = 0 }
        UIView.animate(withDuration: 0.28, delay: 0, options: .curveEaseIn) {
            self.button.alpha = 0; self.button.transform = CGAffineTransform(scaleX: 0.4, y: 0.4)
            self.symbol.transform = CGAffineTransform(scaleX: 0.2, y: 0.2).rotated(by: .pi / 2)
        }
        for (n, w) in words.reversed().enumerated() {
            UIView.animate(withDuration: 0.28, delay: 0.05 + Double(n) * 0.04, options: .curveEaseIn) {
                w.alpha = 0; w.transform = CGAffineTransform(translationX: 0, y: 9).scaledBy(x: 0.7, y: 0.7)
            }
        }
        UIView.animate(withDuration: 0.4, delay: 0.12, usingSpringWithDamping: 0.85, initialSpringVelocity: 0) {
            self.capsule.frame.size.width = self.bounds.height; self.capsule.alpha = 0
        } completion: { _ in self.animating = false; done() }
    }
}
