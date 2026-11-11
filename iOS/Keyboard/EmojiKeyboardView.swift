import UIKit

protocol EmojiKeyboardDelegate: AnyObject {
    func emojiKeyboard(_ v: EmojiKeyboardView, insert text: String)
    func emojiKeyboardDelete(_ v: EmojiKeyboardView)
    /// ABC: back to the letters.
    func emojiKeyboardClose(_ v: EmojiKeyboardView)
    /// ‹ left of the search field: out of the search, or back to the letters (Tony, after Grammarly).
    func emojiKeyboardBack(_ v: EmojiKeyboardView)
    /// The search field was tapped: the letter keys come back below the field and a row of results.
    func emojiKeyboardStartSearch(_ v: EmojiKeyboardView)
}

/// The emoji keyboard, laid out like the iOS one: a search field, the category name, a grid that scrolls
/// sideways, and a bar with ABC, the categories and delete. Long-press an emoji for skin tones.
/// In search mode only the field and one row of results show; the controller types into the query.
final class EmojiKeyboardView: UIView, UICollectionViewDataSource, UICollectionViewDelegateFlowLayout {
    weak var delegate: EmojiKeyboardDelegate?
    var haptics = false
    var dark = false { didSet { if dark != oldValue { applyColors() } } }
    private(set) var searching = false
    static let searchHeight: CGFloat = 98     // field + one row of results, above the letter keys
    /// The emoji grid gets taller than the letters, like the system emoji keyboard (Tony: too small)
    static var extraHeight: CGFloat { KeyboardView.tallKeys ? 54 : 53 }   // measured: the system emoji keyboard vs its letters

    private struct Category { let title: String; let symbol: String; var items: [Emoji] }
    private var categories: [Category] = []
    private var results: [Emoji] = []
    var resultsForTests: [Emoji] { results }
    private let vietnameseUI = (Locale.preferredLanguages.first ?? "").hasPrefix("vi")

    private let field = UIView()
    private let fieldIcon = UIImageView(image: UIImage(systemName: "magnifyingglass"))
    private let fieldText = UILabel()
    private let caret = UIView()
    private let titleLabel = UILabel()
    private let grid: UICollectionView
    private let flow = UICollectionViewFlowLayout()        // search: one row of results
    private let browse = SystemEmojiLayout()               // browsing: the system emoji keyboard's grid
    private let catSelection = UIView()                     // the grey circle behind the selected category
    private let barView = UIView()
    private let abc = UIButton(type: .system)
    private let back = UIButton(type: .system)
    private let del = UIButton(type: .system)
    private var catButtons: [UIButton] = []
    var feedback: KeyHaptics?
    private let skinPopup = SkinTonePopup()
    private var deleteTimer: Timer?

    static let maxVersion: Double = {
        if #available(iOS 26.4, *) { return 17 }
        if #available(iOS 18.4, *) { return 16 }
        if #available(iOS 17.4, *) { return 15.1 }
        return 15
    }()
    static let toneKey = "emojiTone"
    private var tone: Int { VKSettings.defaults.integer(forKey: Self.toneKey) }

    override init(frame: CGRect) {
        grid = UICollectionView(frame: .zero, collectionViewLayout: flow)
        super.init(frame: frame)
        flow.scrollDirection = .horizontal
        flow.minimumLineSpacing = 0; flow.minimumInteritemSpacing = 0
        grid.dataSource = self; grid.delegate = self
        grid.backgroundColor = .clear
        grid.showsHorizontalScrollIndicator = false
        grid.register(EmojiCell.self, forCellWithReuseIdentifier: "e")
        grid.register(EmojiTitleView.self, forSupplementaryViewOfKind: SystemEmojiLayout.titleKind, withReuseIdentifier: "t")
        grid.delaysContentTouches = false
        let lp = UILongPressGestureRecognizer(target: self, action: #selector(longPress(_:)))
        lp.minimumPressDuration = 0.35
        grid.addGestureRecognizer(lp)

        field.layer.cornerCurve = .continuous   // a capsule, like the system emoji search (radius set in layout)
        fieldIcon.contentMode = .scaleAspectFit
        fieldText.font = .systemFont(ofSize: 17)
        caret.isHidden = true
        field.addSubview(fieldIcon); field.addSubview(fieldText); field.addSubview(caret)
        field.addGestureRecognizer(UITapGestureRecognizer(target: self, action: #selector(tapSearch)))

        titleLabel.font = .systemFont(ofSize: 12, weight: .semibold)

        back.setImage(UIImage(systemName: "chevron.left", withConfiguration: UIImage.SymbolConfiguration(pointSize: 20, weight: .medium)), for: .normal)
        back.accessibilityLabel = vietnameseUI ? "Quay lại" : "Back"
        back.accessibilityIdentifier = "emoji.back"
        back.addTarget(self, action: #selector(tapBack), for: .touchUpInside)
        addSubview(back)
        abc.setTitle("ABC", for: .normal)
        abc.titleLabel?.font = .systemFont(ofSize: 17, weight: .regular)
        abc.addTarget(self, action: #selector(tapABC), for: .touchUpInside)
        let cfg = UIImage.SymbolConfiguration(pointSize: 17, weight: .regular)
        del.setImage(UIImage(systemName: "delete.left", withConfiguration: cfg), for: .normal)
        del.addTarget(self, action: #selector(deleteDown), for: .touchDown)
        del.addTarget(self, action: #selector(deleteUp), for: [.touchUpInside, .touchUpOutside, .touchCancel])
        catSelection.isUserInteractionEnabled = false
        barView.addSubview(catSelection)
        barView.addSubview(abc); barView.addSubview(del)
        for v in [field, titleLabel, grid, barView] as [UIView] { addSubview(v) }
        addSubview(skinPopup); skinPopup.isHidden = true
        applyColors()
    }
    required init?(coder: NSCoder) { fatalError() }

    /// Builds the categories from the light index (characters and groups). The search words (+5 MB) are built only
    /// when the search box opens, in the background while the user starts typing.
    func load() {
        guard categories.isEmpty else { reloadRecents(); return }
        let all = EmojiIndex.shared.all.filter { $0.version <= Self.maxVersion }
        func g(_ ids: Int...) -> [Emoji] { all.filter { ids.contains($0.group) } }
        let t: [(String, String, String, [Emoji])] = [
            ("Dùng thường xuyên", "Frequently Used", "clock", []),
            ("Mặt cười & người", "Smileys & People", "face.smiling", g(0, 1)),
            ("Động vật & thiên nhiên", "Animals & Nature", "pawprint", g(3)),
            ("Đồ ăn & thức uống", "Food & Drink", "fork.knife", g(4)),
            ("Hoạt động", "Activity", "soccerball", g(6)),
            ("Du lịch & địa điểm", "Travel & Places", "car", g(5)),
            ("Đồ vật", "Objects", "lightbulb", g(7)),
            ("Biểu tượng", "Symbols", "heart", g(8)),
            ("Cờ", "Flags", "flag", g(9)),
        ]
        categories = t.map { Category(title: vietnameseUI ? $0.0 : $0.1, symbol: $0.2, items: $0.3) }
        reloadRecents()
        catButtons.forEach { $0.removeFromSuperview() }
        let cfg = UIImage.SymbolConfiguration(pointSize: 15, weight: .regular)
        catButtons = categories.indices.map { i in
            let b = UIButton(type: .system)
            b.setImage(UIImage(systemName: categories[i].symbol, withConfiguration: cfg), for: .normal)
            b.tag = i
            b.addTarget(self, action: #selector(tapCategory(_:)), for: .touchUpInside)
            barView.addSubview(b)
            return b
        }
        applyColors()
        setNeedsLayout()
    }

    private func reloadRecents() {
        guard !categories.isEmpty else { return }
        let list = EmojiUsage.frequent(in: VKSettings.defaults, count: 30)   // 6 columns of 5, like the system
        categories[0].items = list.compactMap { EmojiIndex.shared.emoji($0) }
        grid.reloadData()
    }

    // MARK: modes

    func setSearching(_ on: Bool) {
        if on { DispatchQueue.global(qos: .userInitiated).async { EmojiIndex.shared.prepareSearch() } }
        searching = on
        grid.setCollectionViewLayout(on ? flow : browse, animated: false)
        results = []
        setQuery("")
        grid.reloadData()
        grid.setContentOffset(.zero, animated: false)
        setNeedsLayout()
    }

    /// The query as shown (already read as Telex in Vietnamese mode) and its results.
    func setQuery(_ display: String, results r: [Emoji] = []) {
        fieldText.text = display.isEmpty ? (vietnameseUI ? "Tìm emoji" : "Search Emoji") : display
        fieldText.textColor = display.isEmpty ? .secondaryLabel : (dark ? .white : .black)
        caret.isHidden = !searching
        if searching { results = r; grid.reloadData(); grid.setContentOffset(.zero, animated: false) }
        setNeedsLayout()
    }

    /// One margin everywhere; the magnifier, the section title and the first emoji line up (like iOS).
    private static let margin: CGFloat = 8
    private var glyphPad: CGFloat = 0   // space between a cell's edge and its emoji

    override func layoutSubviews() {
        super.layoutSubviews()
        let W = bounds.width, H = bounds.height, M = Self.margin
        // measured on the system emoji keyboard (iOS 26.5) at 402 and 440 pt; values in between are interpolated
        func lerp(_ at402: CGFloat, _ at440: CGFloat) -> CGFloat { at402 + (W - 402) * (at440 - at402) / 38 }
        let fieldH: CGFloat = 40
        let backW: CGFloat = 44                                              // ‹ in the corner, the field after it
        back.frame = CGRect(x: 2, y: 9.33, width: backW, height: fieldH)
        field.frame = CGRect(x: backW + 2, y: 9.33, width: W - backW - 12, height: fieldH)   // 10 from the right, 9.33 from the top edge
        field.layer.cornerRadius = KeyboardView.hostCorner - 10              // concentric with the keyboard's (right) corner
        field.layer.cornerCurve = .continuous
        let inner: CGFloat = 12                             // magnifier inset inside the field
        fieldIcon.frame = CGRect(x: inner, y: (fieldH - 18) / 2, width: 18, height: 18)
        let textX = inner + 18 + 6
        let tw = min(fieldText.intrinsicContentSize.width, field.bounds.width - textX - 10)
        fieldText.frame = CGRect(x: textX, y: 0, width: tw, height: fieldH)
        caret.frame = CGRect(x: searching && fieldText.textColor == .secondaryLabel ? textX - 1 : textX + tw + 1, y: (fieldH - 20) / 2, width: 2, height: 20)
        let alignX = field.frame.minX + inner              // the magnifier's left edge
        titleLabel.isHidden = true                          // titles are drawn per section in the grid
        if searching {
            barView.isHidden = true
            let cell: CGFloat = 44
            grid.frame = CGRect(x: 0, y: field.frame.maxY + 2, width: W, height: H - field.frame.maxY - 2)
            flow.itemSize = CGSize(width: cell, height: cell)
            glyphPad = (cell - Self.glyphWidth(cell)) / 2
            flow.sectionInset = UIEdgeInsets(top: 0, left: max(0, alignX - glyphPad), bottom: 0, right: M)
            flow.invalidateLayout()
        } else {
            barView.isHidden = false
            let rowPitch = lerp(38.4, 40), colPitch = lerp(46.2, 42.2)
            let glyphTop = lerp(83.2, 79.33), glyphLeft = lerp(10.33, 12.33)
            let titleTop = glyphTop - 21                      // the title's top, 18 pt above the first emoji's top (cap height 8.3)
            browse.rowPitch = rowPitch; browse.colPitch = colPitch
            browse.sectionGap = lerp(13.8, 20)
            browse.band = glyphTop - (rowPitch - Self.glyph) / 2 - titleTop
            browse.firstLeft = glyphLeft - (colPitch - Self.glyph) / 2
            grid.frame = CGRect(x: 0, y: titleTop, width: W, height: browse.band + CGFloat(browse.rows) * rowPitch)
            browse.invalidateLayout()
            // the category bar: centred 26.67 pt above the bottom edge; ABC, 9 icons 35.4 pt apart around the middle, delete
            let barH: CGFloat = 44, mid = H - 26.67
            barView.frame = CGRect(x: 0, y: mid - barH / 2, width: W, height: barH)
            abc.frame = CGRect(x: 31 - 25, y: 0, width: 50, height: barH)
            del.frame = CGRect(x: W - 28.5 - 23, y: 0, width: 46, height: barH)
            let step = 35.4 * W / 440, n = CGFloat(catButtons.count)
            for (i, b) in catButtons.enumerated() {
                b.frame = CGRect(x: W / 2 + (CGFloat(i) - (n - 1) / 2) * step - step / 2, y: 0, width: step, height: barH)
            }
            updateTitle()
        }
    }

    /// The emoji size: glyphs 29.5 pt tall like the system emoji keyboard (Tony: same as the default keyboard).
    static let glyph: CGFloat = 29.5
    static var glyphFont: CGFloat = 31   // draws a 29.5 pt glyph (measured in a render)

    /// How much of a cell the emoji fills (Tony: bigger, not crowded). Renders compare values.
    static var fill: CGFloat = 0.84   // Tony picked 84% (B)
    private static func fontSize(_ cell: CGFloat) -> CGFloat { floor(cell * fill) }
    /// How wide an emoji draws at the grid's font size (cells are wider than the glyph).
    private static func glyphWidth(_ cell: CGFloat) -> CGFloat {
        ceil(("😀" as NSString).size(withAttributes: [.font: UIFont.systemFont(ofSize: fontSize(cell))]).width)
    }

    private func applyColors() {
        backgroundColor = UIColor.black.withAlphaComponent(0.001)
        field.backgroundColor = dark ? UIColor(white: 1, alpha: 0.12) : UIColor(white: 1, alpha: 0.55)   // soft glass, not a grey box
        fieldIcon.tintColor = .secondaryLabel
        back.tintColor = dark ? UIColor(white: 1, alpha: 0.75) : UIColor(white: 0, alpha: 0.6)
        caret.backgroundColor = UIColor(red: 0.31, green: 0.27, blue: 0.90, alpha: 1)
        titleLabel.textColor = .secondaryLabel
        let fg: UIColor = dark ? .white : .black
        abc.tintColor = fg; abc.setTitleColor(fg, for: .normal); del.tintColor = fg
        skinPopup.dark = dark
        updateTitle()
    }

    // MARK: grid

    func numberOfSections(in cv: UICollectionView) -> Int { searching ? 1 : categories.count }
    func collectionView(_ cv: UICollectionView, numberOfItemsInSection s: Int) -> Int { searching ? results.count : categories[s].items.count }
    private func emoji(at ip: IndexPath) -> Emoji { searching ? results[ip.item] : categories[ip.section].items[ip.item] }
    func collectionView(_ cv: UICollectionView, cellForItemAt ip: IndexPath) -> UICollectionViewCell {
        let c = cv.dequeueReusableCell(withReuseIdentifier: "e", for: ip) as! EmojiCell
        c.label.text = emoji(at: ip).withSkin(tone)
        c.label.font = .systemFont(ofSize: searching ? Self.fontSize(flow.itemSize.height) : Self.glyphFont)   // fixed: never sized before layout
        return c
    }
    func collectionView(_ cv: UICollectionView, viewForSupplementaryElementOfKind kind: String, at ip: IndexPath) -> UICollectionReusableView {
        let v = cv.dequeueReusableSupplementaryView(ofKind: kind, withReuseIdentifier: "t", for: ip) as! EmojiTitleView
        v.label.text = searching ? "" : categories[ip.section].title.uppercased()
        return v
    }
    func collectionView(_ cv: UICollectionView, didSelectItemAt ip: IndexPath) {
        cv.deselectItem(at: ip, animated: false)
        choose(emoji(at: ip).withSkin(tone), emoji(at: ip))
    }
    func collectionView(_ cv: UICollectionView, layout l: UICollectionViewLayout, insetForSectionAt s: Int) -> UIEdgeInsets {
        if searching { return flow.sectionInset }
        // the first emoji's left edge on the magnifier's; sections follow each other with one column of air
        let lead = max(0, Self.margin * 2 - glyphPad)   // = alignX - glyphPad
        return UIEdgeInsets(top: 0, left: s == 0 ? lead : 0, bottom: 0, right: s == categories.count - 1 ? lead : 0)
    }

    private func choose(_ text: String, _ e: Emoji) {
        if haptics { feedback?.tick() } else { UIDevice.current.playInputClick() }
        delegate?.emojiKeyboard(self, insert: text)
        EmojiUsage.record(e.char, in: VKSettings.defaults)
    }

    func scrollViewDidScroll(_ s: UIScrollView) { if !searching { updateTitle() } }

    private func currentSection() -> Int {
        let x = grid.contentOffset.x + 30
        let vis = grid.indexPathsForVisibleItems.filter { (grid.layoutAttributesForItem(at: $0)?.frame.maxX ?? 0) > x }
        return vis.map(\.section).min() ?? 0
    }
    private func updateTitle() {
        guard !categories.isEmpty, !searching else { return }
        let s = currentSection()
        let on: UIColor = dark ? .white : .black
        for (i, b) in catButtons.enumerated() { b.tintColor = i == s ? on : .secondaryLabel }
        if s < catButtons.count {   // a grey circle behind the selected category, like the system bar
            let c = catButtons[s].center, d: CGFloat = 30
            catSelection.frame = CGRect(x: c.x - d / 2, y: c.y - d / 2, width: d, height: d)
            catSelection.layer.cornerRadius = d / 2
            catSelection.backgroundColor = dark ? UIColor(white: 1, alpha: 0.16) : UIColor(white: 0, alpha: 0.08)
        }
    }

    @objc private func tapCategory(_ b: UIButton) {
        let s = b.tag
        guard s < categories.count, !categories[s].items.isEmpty,
              let a = grid.layoutAttributesForItem(at: IndexPath(item: 0, section: s)) else { return }
        let x = min(a.frame.minX - browse.firstLeft, max(0, grid.contentSize.width - grid.bounds.width))
        grid.setContentOffset(CGPoint(x: max(0, x), y: 0), animated: false)
        updateTitle()
    }
    @objc private func tapABC() { delegate?.emojiKeyboardClose(self) }
    @objc private func tapBack() { delegate?.emojiKeyboardBack(self) }
    @objc private func tapSearch() { if !searching { delegate?.emojiKeyboardStartSearch(self) } }
    @objc private func deleteDown() {
        delegate?.emojiKeyboardDelete(self)
        if haptics { feedback?.tick() }
        deleteTimer?.invalidate()
        deleteTimer = Timer.scheduledTimer(withTimeInterval: 0.45, repeats: false) { [weak self] _ in
            self?.deleteTimer = Timer.scheduledTimer(withTimeInterval: 0.09, repeats: true) { [weak self] _ in
                guard let self else { return }
                self.delegate?.emojiKeyboardDelete(self)
            }
        }
    }
    @objc private func deleteUp() { deleteTimer?.invalidate(); deleteTimer = nil }

    // MARK: skin tones

    private var skinFor: Emoji?
    @objc private func longPress(_ g: UILongPressGestureRecognizer) {
        let p = g.location(in: self)
        switch g.state {
        case .began:
            guard let ip = grid.indexPathForItem(at: g.location(in: grid)), case let e = emoji(at: ip), e.skins.count == 5,
                  let cell = grid.cellForItem(at: ip) else { return }
            skinFor = e
            skinPopup.show([e.char] + e.skins, selected: tone, over: cell.convert(cell.bounds, to: self), in: self)
            if haptics { feedback?.tick() }
        case .changed:
            if skinFor != nil { skinPopup.track(p) }
        case .ended:
            if let e = skinFor, let i = skinPopup.selectedIndex {
                VKSettings.defaults.set(i, forKey: Self.toneKey)
                choose(i == 0 ? e.char : e.skins[i - 1], e)
                grid.reloadData()
            }
            skinFor = nil; skinPopup.isHidden = true
        default:
            skinFor = nil; skinPopup.isHidden = true
        }
    }
}

final class EmojiCell: UICollectionViewCell {
    let label = UILabel()
    override init(frame: CGRect) {
        super.init(frame: frame)
        label.textAlignment = .center
        contentView.addSubview(label)
    }
    required init?(coder: NSCoder) { fatalError() }
    override func layoutSubviews() { super.layoutSubviews(); label.frame = contentView.bounds }
    override var isHighlighted: Bool {
        didSet { contentView.backgroundColor = isHighlighted ? UIColor.gray.withAlphaComponent(0.25) : .clear; contentView.layer.cornerRadius = 8 }
    }
}

/// The skin-tone choices above a long-pressed emoji: slide to one and lift.
final class SkinTonePopup: UIView {
    var dark = false { didSet { backgroundColor = dark ? UIColor(white: 0.36, alpha: 1) : .white } }
    private var labels: [UILabel] = []
    private(set) var selectedIndex: Int?
    private let hl = UIView()

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .white
        layer.cornerRadius = 12; layer.cornerCurve = .continuous
        layer.shadowColor = UIColor.black.cgColor; layer.shadowOpacity = 0.3; layer.shadowRadius = 6; layer.shadowOffset = CGSize(width: 0, height: 2)
        hl.backgroundColor = UIColor(red: 0.31, green: 0.27, blue: 0.90, alpha: 0.25); hl.layer.cornerRadius = 8
        addSubview(hl)
    }
    required init?(coder: NSCoder) { fatalError() }

    func show(_ items: [String], selected: Int, over r: CGRect, in parent: UIView) {
        labels.forEach { $0.removeFromSuperview() }
        let w: CGFloat = 44
        labels = items.map { s in let l = UILabel(); l.text = s; l.font = .systemFont(ofSize: 30); l.textAlignment = .center; addSubview(l); return l }
        let total = w * CGFloat(items.count) + 8
        var x = r.midX - total / 2
        x = min(max(4, x), parent.bounds.width - total - 4)
        frame = CGRect(x: x, y: max(2, r.minY - 54), width: total, height: 52)
        for (i, l) in labels.enumerated() { l.frame = CGRect(x: 4 + CGFloat(i) * w, y: 4, width: w, height: 44) }
        select(min(max(0, selected), items.count - 1))
        isHidden = false
        parent.bringSubviewToFront(self)
    }
    func track(_ p: CGPoint) {
        let local = convert(p, from: superview)
        let i = Int((local.x - 4) / 44)
        select(min(max(0, i), labels.count - 1))
    }
    private func select(_ i: Int) { selectedIndex = i; hl.frame = labels[i].frame.insetBy(dx: 1, dy: 1) }
}


/// The system emoji keyboard's grid, measured (iOS 26.5, iPhone 17 Pro 402 pt and Pro Max 440 pt): columns of 5,
/// sections side by side with a little more air between them, each section's title above its first column.
final class SystemEmojiLayout: UICollectionViewLayout {
    var rows = 5
    var rowPitch: CGFloat = 40, colPitch: CGFloat = 42.2
    var firstLeft: CGFloat = 6.2        // left edge of the first column's cell
    var sectionGap: CGFloat = 20        // extra space before a new section
    var band: CGFloat = 16              // the title strip above the cells
    var trailing: CGFloat = 8
    private var cells: [IndexPath: UICollectionViewLayoutAttributes] = [:]
    private var titles: [IndexPath: UICollectionViewLayoutAttributes] = [:]
    private var width: CGFloat = 0
    static let titleKind = "title"

    override func prepare() {
        super.prepare()
        cells = [:]; titles = [:]
        guard let cv = collectionView else { return }
        var x = firstLeft
        for s in 0..<cv.numberOfSections {
            let n = cv.numberOfItems(inSection: s)
            guard n > 0 else { continue }
            if s > 0 { x += sectionGap }
            let t = UICollectionViewLayoutAttributes(forSupplementaryViewOfKind: Self.titleKind, with: IndexPath(item: 0, section: s))
            t.frame = CGRect(x: x, y: 0, width: 260, height: band)
            titles[t.indexPath] = t
            for i in 0..<n {
                let ip = IndexPath(item: i, section: s)
                let a = UICollectionViewLayoutAttributes(forCellWith: ip)
                a.frame = CGRect(x: x + CGFloat(i / rows) * colPitch, y: band + CGFloat(i % rows) * rowPitch, width: colPitch, height: rowPitch)
                cells[ip] = a
            }
            x += CGFloat((n + rows - 1) / rows) * colPitch
        }
        width = x + trailing
    }
    override var collectionViewContentSize: CGSize { CGSize(width: width, height: band + CGFloat(rows) * rowPitch) }
    override func layoutAttributesForElements(in rect: CGRect) -> [UICollectionViewLayoutAttributes]? {
        (Array(cells.values) + Array(titles.values)).filter { $0.frame.intersects(rect) }
    }
    override func layoutAttributesForItem(at ip: IndexPath) -> UICollectionViewLayoutAttributes? { cells[ip] }
    override func layoutAttributesForSupplementaryView(ofKind k: String, at ip: IndexPath) -> UICollectionViewLayoutAttributes? { titles[ip] }
    override func shouldInvalidateLayout(forBoundsChange b: CGRect) -> Bool { b.height != collectionView?.bounds.height }
}

final class EmojiTitleView: UICollectionReusableView {
    let label = UILabel()
    override init(frame: CGRect) {
        super.init(frame: frame)
        label.font = .systemFont(ofSize: 12, weight: .semibold)
        label.textColor = .tertiaryLabel   // measured: (165,166,169) on the keyboard's grey, like the system
        addSubview(label)
    }
    required init?(coder: NSCoder) { fatalError() }
    override func layoutSubviews() { super.layoutSubviews(); label.frame = bounds }
}
