import AppKit
import SwiftUI

/// The emoji picker: a hotkey (off by default, set in the main window) opens a panel in the middle of the
/// screen. Typing searches in English and Vietnamese (EmojiSearch.swift), arrows move, Return types the emoji
/// into the app you were in, Esc closes. The panel never activates Vikey, so that app keeps its cursor.
enum PrefEmoji {
    static let enabled = "EmojiPicker"      // 0/1, read by the core
    static let hotkey = "EmojiHotkey"       // Hotkey raw, 0 = not set
    static let tone = "EmojiSkinTone"       // 0 = default yellow, 1-5 light … dark
    static let recents = "EmojiRecents"
}

final class EmojiPicker {
    static let shared = EmojiPicker()
    private var panel: EmojiPanel?
    let model = EmojiPickerModel()

    /// At launch: the hotkey handler, and the panel built ahead of time so it opens instantly.
    func install() {
        VikeyCore.setEmojiHotkeyHandler { [weak self] in self?.toggle() }
        DispatchQueue.global(qos: .userInitiated).async { EmojiIndex.shared.prepareSearch() }   // the Mac panel opens on a search box
        DispatchQueue.main.async { if UserDefaults.standard.integer(forKey: PrefEmoji.enabled) != 0 { self.prepare() } }
    }

    private func prepare() {
        guard panel == nil else { return }
        let p = EmojiPanel(model: model)
        p.onClose = { [weak self] in self?.close() }
        panel = p
    }

    func toggle() { panel?.isVisible == true ? close() : open() }

    func open() {
        prepare()
        guard let p = panel else { return }
        model.reset(vietnamese: VikeyCore.language() == 1)
        let screen = NSScreen.screens.first { NSMouseInRect(NSEvent.mouseLocation, $0.frame, false) } ?? NSScreen.main
        if let f = screen?.visibleFrame {
            p.setFrameOrigin(NSPoint(x: f.midX - p.frame.width / 2, y: f.midY - p.frame.height / 2 + f.height * 0.08))
        }
        VikeyCore.setEmojiPickerOpen(true)
        p.makeKeyAndOrderFront(nil)
    }

    func close() {
        VikeyCore.setEmojiPickerOpen(false)
        panel?.orderOut(nil)
    }

    /// Types the emoji into the app that was in front, after the panel is gone.
    func choose(_ e: Emoji) {
        let text = e.withSkin(model.tone)
        model.remember(e)
        close()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.03) { VikeyCore.insertText(text) }
    }
}

/// Borderless, non-activating, but able to take keys.
final class EmojiPanel: NSPanel {
    var onClose: (() -> Void)?
    private let model: EmojiPickerModel

    init(model: EmojiPickerModel) {
        self.model = model
        super.init(contentRect: NSRect(x: 0, y: 0, width: EmojiGridView.width, height: 404),
                   styleMask: [.borderless, .nonactivatingPanel, .fullSizeContentView], backing: .buffered, defer: false)
        isFloatingPanel = true
        level = .popUpMenu
        collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .transient]
        hidesOnDeactivate = false
        isMovableByWindowBackground = true
        backgroundColor = .clear
        isOpaque = false   // else the square window shows white corners outside the rounded panel (Tony)
        hasShadow = true
        let fx = NSVisualEffectView()
        fx.material = .popover; fx.state = .active; fx.blendingMode = .behindWindow
        fx.maskImage = Self.roundedMask(radius: 14)   // a mask, not a layer radius: the window shadow follows it
        let host = NSHostingView(rootView: EmojiGridView(model: model))
        host.translatesAutoresizingMaskIntoConstraints = false
        fx.addSubview(host)
        NSLayoutConstraint.activate([host.leadingAnchor.constraint(equalTo: fx.leadingAnchor), host.trailingAnchor.constraint(equalTo: fx.trailingAnchor),
                                     host.topAnchor.constraint(equalTo: fx.topAnchor), host.bottomAnchor.constraint(equalTo: fx.bottomAnchor)])
        contentView = fx
    }

    /// A stretchable rounded-rect mask for the blur view.
    private static func roundedMask(radius r: CGFloat) -> NSImage {
        let size = NSSize(width: r * 2 + 1, height: r * 2 + 1)
        let img = NSImage(size: size, flipped: false) { rect in
            NSColor.black.setFill()
            NSBezierPath(roundedRect: rect, xRadius: r, yRadius: r).fill()
            return true
        }
        img.capInsets = NSEdgeInsets(top: r, left: r, bottom: r, right: r)
        img.resizingMode = .stretch
        return img
    }

    override func makeKeyAndOrderFront(_ sender: Any?) {
        super.makeKeyAndOrderFront(sender)
        invalidateShadow()   // shadow from the rounded shape, not the square frame
    }

    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { false }
    override func resignKey() { super.resignKey(); if isVisible { onClose?() } }   // clicked elsewhere

    override func keyDown(with e: NSEvent) {
        let cmd = e.modifierFlags.contains(.command), opt = e.modifierFlags.contains(.option)
        switch e.keyCode {
        case 53: model.raw.isEmpty ? onClose?() : model.setRaw("")             // Esc: clears the search, then closes
        case 36, 76: if let x = model.current { EmojiPicker.shared.choose(x) }  // Return
        // the first emoji is selected from the start (Tony); the arrows move from there
        case 123: model.allSelected = false; model.move(-1)
        case 124: model.allSelected = false; model.move(1)
        case 125: model.allSelected = false; model.moveRow(1)
        case 126: model.allSelected = false; model.moveRow(-1)
        case 51: (cmd || model.allSelected) ? model.setRaw("") : opt ? model.deleteWord() : model.deleteBack()   // ⌫
        default:
            if cmd {
                switch e.charactersIgnoringModifiers {
                case "v": if let s = NSPasteboard.general.string(forType: .string) { model.type(s) }
                case "a": if !model.raw.isEmpty { model.allSelected = true }
                case "c", "x":
                    if model.allSelected && !model.raw.isEmpty {
                        NSPasteboard.general.clearContents(); NSPasteboard.general.setString(model.display, forType: .string)
                        if e.charactersIgnoringModifiers == "x" { model.setRaw("") }
                    }
                case "w": onClose?()
                default: break
                }
                return
            }
            if let c = e.characters, !c.isEmpty, c.unicodeScalars.allSatisfy({ !CharacterSet.controlCharacters.contains($0) }) {
                model.type(c)
            }
        }
    }
}

final class EmojiPickerModel: ObservableObject {
    @Published private(set) var raw = ""
    @Published var allSelected = false   // ⌘A: the next key replaces the query
    @Published private(set) var opened = 0   // each open starts at the top of the list (Tony)
    @Published private(set) var vietnamese = true
    @Published private(set) var results: [Emoji] = []          // what the grid shows when searching
    @Published var selected = 0
    var selectedByKeys = false   // only arrow keys scroll the grid: hover scrolling moved it under the mouse (Tony)
    @Published var tone = UserDefaults.standard.integer(forKey: PrefEmoji.tone) {
        didSet { UserDefaults.standard.set(tone, forKey: PrefEmoji.tone) }
    }
    @Published private(set) var recents: [Emoji] = []
    private var observer: Any?
    var lastMouse = NSPoint.zero   // hover selects only when the mouse really moved (not when the grid scrolled under it)

    /// The emoji this Mac can draw (newer Unicode versions show as boxes on older macOS).
    static let maxVersion: Double = {
        if #available(macOS 26.4, *) { return 17 }
        if #available(macOS 15.4, *) { return 16 }
        if #available(macOS 14.4, *) { return 15.1 }
        if #available(macOS 13.3, *) { return 15 }
        return 14
    }()
    lazy var all: [Emoji] = EmojiIndex.shared.all.filter { $0.version <= Self.maxVersion }
    lazy var groups: [(Int, [Emoji])] = Dictionary(grouping: all, by: \.group).sorted { $0.key < $1.key }.map { ($0.key, $0.value) }

    init() {
        observer = NotificationCenter.default.addObserver(forName: .VikeyLanguageDidChange, object: nil, queue: .main) { [weak self] _ in
            guard let self else { return }
            self.vietnamese = VikeyCore.language() == 1; self.search()
        }
    }

    var display: String { vietnamese ? Telex.convert(raw) : raw }
    var searching: Bool { !raw.trimmingCharacters(in: .whitespaces).isEmpty }
    var current: Emoji? { let v = visible; return v.indices.contains(selected) ? v[selected] : nil }

    func reset(vietnamese: Bool) {
        self.vietnamese = vietnamese
        raw = ""; allSelected = false; results = []; selected = 0; lastMouse = NSEvent.mouseLocation; opened += 1
        let byChar = Dictionary(all.map { ($0.char, $0) }, uniquingKeysWith: { a, _ in a })
        recents = EmojiUsage.frequent(in: .standard, count: EmojiGridView.columns * 3).compactMap { byChar[$0] }
        relayout()
    }

    func setRaw(_ s: String) { raw = s; allSelected = false; search() }
    func type(_ s: String) { setRaw(allSelected ? s : raw + s) }
    func deleteBack() { if !raw.isEmpty { raw.removeLast(); allSelected = false; search() } }
    func deleteWord() {
        var r = raw
        while r.last == " " { r.removeLast() }
        while let l = r.last, l != " " { r.removeLast() }
        setRaw(r)
    }

    private func search() {
        results = searching ? EmojiIndex.shared.search(raw, vietnamese: vietnamese, limit: 400, among: all) : []
        selected = 0
        relayout()
    }

    /// The grid as rows of up to 9, per section, so the arrows move by what you see and scrolling lands
    /// on exact rows (nested lazy grids scrolled to wrong places: Tony).
    struct Row: Identifiable { let id: Int; let section: Int; let start: Int; let count: Int }
    private(set) var sections: [(title: String, rows: [Row])] = []
    private(set) var allRows: [Row] = []
    private(set) var visible: [Emoji] = []          // everything the arrows walk through, in grid order
    /// Rebuilt when the content changes, never per row while drawing.
    private var generation = 0
    /// A new list for each search and each open: it starts at the top with no old scroll offset under new
    /// content (a search after scrolling showed its results cut off: Tony)
    var listKey: String { searching ? "q\(generation)" : "b\(opened)" }
    private func relayout() {
        generation += 1   // new row identities: rows keyed by position alone were drawn stale after a search (Tony)
        visible = searching ? results : recents + all
        sections = makeSections()
        allRows = sections.flatMap(\.rows)
    }
    private func makeSections() -> [(title: String, rows: [Row])] {
        let cols = EmojiGridView.columns
        var out: [(String, [Row])] = [], start = 0, rid = 0
        func add(_ title: String, _ n: Int) {
            var rows: [Row] = []
            var i = 0
            while i < n { rows.append(Row(id: generation * 100_000 + rid, section: out.count, start: start + i, count: min(cols, n - i))); rid += 1; i += cols }
            out.append((title, rows)); start += n
        }
        if searching { add("", results.count) }
        else {
            if !recents.isEmpty { add(vietnamese ? "Dùng thường xuyên" : "Frequently Used", recents.count) }
            for (g, list) in groups { add(vietnamese ? EmojiGridView.groupNames[g] : EmojiGridView.groupNamesEN[g], list.count) }
        }
        return out
    }
    func row(of i: Int) -> Row? { allRows.first { i >= $0.start && i < $0.start + $0.count } }

    /// ↑/↓: the row above/below, same column (or its last emoji).
    func moveRow(_ d: Int) {
        let rows = allRows
        guard let r = rows.firstIndex(where: { selected >= $0.start && selected < $0.start + $0.count }) else { return }
        let t = r + d
        guard rows.indices.contains(t) else { return }
        selectedByKeys = true
        selected = rows[t].start + min(selected - rows[r].start, rows[t].count - 1)
    }

    func move(_ d: Int) {
        let n = visible.count
        guard n > 0 else { return }
        selectedByKeys = true
        selected = min(max(0, selected + d), n - 1)
    }

    func remember(_ e: Emoji) { EmojiUsage.record(e.char, in: .standard) }
}

struct EmojiGridView: View {
    static let columns = 9
    static let cell: CGFloat = 44
    static let width = CGFloat(columns) * cell + 24
    @ObservedObject var model: EmojiPickerModel

    static let groupNames = ["Mặt cười & cảm xúc", "Người & cơ thể", "", "Động vật & thiên nhiên", "Đồ ăn & thức uống",
                                     "Du lịch & địa điểm", "Hoạt động", "Đồ vật", "Biểu tượng", "Cờ"]
    static let groupNamesEN = ["Smileys & Emotion", "People & Body", "", "Animals & Nature", "Food & Drink",
                                       "Travel & Places", "Activities", "Objects", "Symbols", "Flags"]

    var body: some View {
        VStack(spacing: 0) {
            searchField.padding(.horizontal, 14).padding(.top, 14).padding(.bottom, 8)
            Divider().opacity(0.5)
            grid.padding(.vertical, 8)   // the scrolling area itself is inset: a gap under the divider at any scroll (Tony)
            Divider().opacity(0.5)
            footer.padding(.horizontal, 14).frame(height: 44)
        }
        .frame(width: Self.width, height: 404)
    }

    private var searchField: some View {
        HStack(spacing: 8) {
            Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
            HStack(spacing: 1) {
                Text(model.display).lineLimit(1)
                    .background(model.allSelected ? Color.accentColor.opacity(0.3) : .clear, in: RoundedRectangle(cornerRadius: 3))
                BlinkingCaret().opacity(model.allSelected ? 0 : 1)
                if model.raw.isEmpty {
                    Text(model.vietnamese ? "Tìm emoji: ngựa, horse…" : "Search emoji: horse, ngựa…").foregroundStyle(.tertiary).padding(.leading, 2)
                }
            }
            .font(.system(size: 17))
            Spacer(minLength: 0)
            Text(model.vietnamese ? "VI" : "EN").font(.caption.weight(.semibold)).foregroundStyle(.secondary)
                .padding(.horizontal, 6).padding(.vertical, 2).background(.quaternary, in: RoundedRectangle(cornerRadius: 5))
        }
    }

    private var grid: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 0) {   // titles scroll with the grid: a pinned one needs a background (Tony: odd band)
                    // the top of the list: scrolling here shows the first title too (scrolling to the first row hid it: Tony)
                    Color.clear.frame(height: 0).id("top")
                    if model.searching && model.results.isEmpty {
                        Text(model.vietnamese ? "Không tìm thấy emoji nào" : "No emoji found")
                            .foregroundStyle(.secondary).frame(maxWidth: .infinity).padding(.top, 110)
                    } else {
                        let secs = model.sections
                        // titles are plain rows, not Section headers: macOS gives headers a background band (Tony)
                        ForEach(secs.indices, id: \.self) { si in
                            if !secs[si].title.isEmpty { header(secs[si].title, first: si == 0) }
                            ForEach(secs[si].rows) { r in row(r) }
                        }
                    }
                }
                .padding(.horizontal, 12)
            }
            .onChange(of: model.selected) { i in
                guard model.selectedByKeys, let r = model.row(of: i) else { return }
                if r.id == model.allRows.first?.id { proxy.scrollTo("top", anchor: .top) }   // back on top: the title shows too
                else { proxy.scrollTo(r.id) }   // the least scrolling that shows it
            }
            .id(model.listKey)
        }
    }

    private func header(_ t: String, first: Bool) -> some View {
        Text(t).font(.system(size: 11, weight: .semibold)).foregroundStyle(.secondary)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.top, first ? 0 : 8).padding(.bottom, 6)   // the list's own 8 pt inset is the first title's gap

    }

    private func row(_ r: EmojiPickerModel.Row) -> some View {
        let list = model.visible
        return HStack(spacing: 0) {
            ForEach(0..<Self.columns, id: \.self) { c in
                let idx = r.start + c
                if c < r.count, list.indices.contains(idx) { cell(list[idx], idx) }
                else { Color.clear.frame(maxWidth: .infinity).frame(height: Self.cell) }
            }
        }
        .id(r.id)
    }

    private func cell(_ e: Emoji, _ idx: Int) -> some View {
        Text(e.withSkin(model.tone))
            .font(.system(size: 28))
            .frame(maxWidth: .infinity).frame(height: Self.cell)
            .background(RoundedRectangle(cornerRadius: 8).fill(idx == model.selected ? Color.brand.opacity(0.22) : .clear))
            .contentShape(Rectangle())
            .onHover { inside in
                let m = NSEvent.mouseLocation
                guard inside, m != model.lastMouse else { return }
                model.lastMouse = m
                model.selectedByKeys = false; model.selected = idx
            }
            .onTapGesture { EmojiPicker.shared.choose(e) }
    }

    private var footer: some View {
        HStack(spacing: 10) {
            if let e = model.current {
                Text(e.withSkin(model.tone)).font(.system(size: 22))
                Text(model.vietnamese ? e.nameVI : e.nameEN).font(.callout).lineLimit(1).foregroundStyle(.secondary)
            }
            Spacer()
            HStack(spacing: 5) {   // branding (Tony): the app icon and the wordmark, quiet
                Image(nsImage: NSApp.applicationIconImage).resizable().frame(width: 16, height: 16)
                Wordmark(size: 13)
            }
            .opacity(0.85)
            Divider().frame(height: 16)
            Menu {
                ForEach(0..<6) { t in
                    Button(["✋", "✋🏻", "✋🏼", "✋🏽", "✋🏾", "✋🏿"][t]) { model.tone = t }
                }
            } label: { Text(["✋", "✋🏻", "✋🏼", "✋🏽", "✋🏾", "✋🏿"][model.tone]).font(.system(size: 18)) }
            .menuStyle(.borderlessButton).menuIndicator(.hidden).fixedSize()
            .help(model.vietnamese ? "Màu da" : "Skin tone")
        }
    }
}

/// The search field's caret. Its own view, so each blink redraws only the caret, not the whole emoji grid.
private struct BlinkingCaret: View {
    @State private var on = true
    var body: some View {
        Rectangle().fill(Color.brand).frame(width: 2, height: 18).opacity(on ? 1 : 0)
            .onReceive(Timer.publish(every: 0.53, on: .main, in: .common).autoconnect()) { _ in on.toggle() }
    }
}
