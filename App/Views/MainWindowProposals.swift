#if DEBUG
import AppKit
import SwiftUI

/// Debug only: renders the main window as it is and the redesign proposals to PNGs, off screen
/// (`-renderMainWindow <dir>`; never shown, the keyboard core isn't started).
enum MainWindowProposals {
    @MainActor static func run(dir: String) {
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        let dark = UserDefaults.standard.bool(forKey: "renderDark")
        NSApp.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
        let suffix = dark ? "-dark" : ""
        for tab in ["general", "auto", "viet", "emoji", "excluded"] {   // every tab of the main window
            UserDefaults.standard.set(tab, forKey: PrefKey.mainTab)
            render(AnyView(MainView().environmentObject(AppState.shared)), "tab-\(tab)\(suffix)", dir)
        }
        NSApp.terminate(nil)
    }

    @MainActor private static func render(_ view: AnyView, _ name: String, _ dir: String) {
        let host = NSHostingView(rootView: view.frame(width: 460))
        let size = host.fittingSize
        let window = NSWindow(contentRect: NSRect(origin: .zero, size: size), styleMask: [.titled, .closable, .miniaturizable],
                              backing: .buffered, defer: false)
        window.title = "Vikey"
        window.contentView = host
        host.layoutSubtreeIfNeeded()
        RunLoop.main.run(until: Date().addingTimeInterval(0.4))
        guard let frame = window.contentView?.superview else { return }   // include the title bar
        frame.layoutSubtreeIfNeeded()
        if let rep = frame.bitmapImageRepForCachingDisplay(in: frame.bounds) {
            frame.cacheDisplay(in: frame.bounds, to: rep)
            try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: "\(dir)/\(name).png"))
        }
    }
}

// MARK: shared pieces (static values: these are pictures of a design, not the working window)

private struct Header: View {
    var body: some View {
        HStack(spacing: 12) {
            Image(nsImage: NSApp.applicationIconImage).resizable().frame(width: 44, height: 44)
            VStack(alignment: .leading, spacing: 2) {
                Wordmark(size: 22)
                Text("Bấm ⌃⇧ để chuyển").font(.callout).foregroundStyle(.secondary)
            }
            Spacer()
            Picker("", selection: .constant(1)) { Text("Tiếng Việt").tag(1); Text("English").tag(0) }
                .pickerStyle(.segmented).labelsHidden().fixedSize()
        }
        .padding(.horizontal, 20).padding(.top, 6).padding(.bottom, 4)
    }
}

private struct HotkeyBox: View {
    let text: String
    var body: some View {
        Text(text).font(.system(.body, design: .rounded)).padding(.horizontal, 10).padding(.vertical, 3)
            .background(RoundedRectangle(cornerRadius: 6).fill(Color.secondary.opacity(0.12)))
    }
}

private func on(_ title: String, _ tip: String, _ v: Bool = true) -> some View {
    Toggle(isOn: .constant(v)) { Tip(title, tip) }
}

@ViewBuilder private func switchRows() -> some View {
    LabeledContent { HotkeyBox(text: "⌃⇧") } label: { Tip("Phím chuyển Anh/Việt", "") }
    on("Âm báo khi chuyển", "", false)
}
@ViewBuilder private func autoRows() -> some View {
    on("Tự chuyển Anh/Việt", "")
    on("Tạm dừng 5 giây", "")
    on("Nhớ theo ứng dụng", "")
    on("Nhớ theo trang web", "", false)
}
@ViewBuilder private func vietRows() -> some View {
    Picker(selection: .constant(1)) { Text("hoà, thuý").tag(1); Text("hòa, thúy").tag(0) } label: { Tip("Đặt dấu", "") }
    on("Kiểm tra chính tả", "")
    on("Trả lại phím khi sai", "")
    on("Chính tả teencode (zui, zậy, gòy)", "")
}
@ViewBuilder private func emojiRows() -> some View {
    on("Bảng emoji", "")
    LabeledContent { HotkeyBox(text: "⌃⌘E") } label: { Tip("Phím mở bảng emoji", "") }
}
@ViewBuilder private func generalRows() -> some View {
    on("Mở Vikey khi đăng nhập", "")
    on("Tự động cập nhật", "")
    LabeledContent { Button("Gửi góp ý…") {} } label: { Tip("Góp ý", "") }
}

// MARK: A — every section folds; a folded one says what's on

private struct ProposalA: View {
    let expanded: Set<String>
    var body: some View {
        VStack(spacing: 0) {
            Header()
            Form {
                Section { switchRows() }
                fold("auto", "Tự chuyển", "Bật · nhớ theo ứng dụng") { autoRows() }
                fold("viet", "Gõ tiếng Việt", "hoà, thuý · chính tả · teencode") { vietRows() }
                fold("emoji", "Bảng emoji", "⌃⌘E") { emojiRows() }
                fold("general", "Chung", "Mở khi đăng nhập · tự cập nhật") { generalRows() }
            }
            .formStyle(.grouped).scrollDisabled(true).padding(.bottom, 8)
        }
        .fixedSize(horizontal: false, vertical: true)
    }
    private func fold<C: View>(_ id: String, _ title: String, _ summary: String, @ViewBuilder _ rows: () -> C) -> some View {
        let content = rows()
        return Section {
            DisclosureGroup(isExpanded: .constant(expanded.contains(id))) { content } label: {
                HStack {
                    Text(title).font(.headline)
                    Spacer()
                    if !expanded.contains(id) { Text(summary).foregroundStyle(.secondary).lineLimit(1) }
                }
            }
        }
    }
}

// MARK: A, styled like today's sections: the header folds, open sections keep their normal rows

private struct ProposalA2: View {
    let expanded: Set<String>
    var body: some View {
        VStack(spacing: 0) {
            Header()
            Form {
                Section { switchRows() }
                fold("auto", "Tự chuyển", "Bật · nhớ theo ứng dụng") { autoRows() }
                fold("viet", "Gõ tiếng Việt", "hoà, thuý · teencode") { vietRows() }
                fold("emoji", "Bảng emoji", "⌃⌘E") { emojiRows() }
                fold("general", "Chung", "Tự cập nhật") { generalRows() }
            }
            .formStyle(.grouped).scrollDisabled(true).padding(.bottom, 8)
        }
        .fixedSize(horizontal: false, vertical: true)
    }
    private func fold<C: View>(_ id: String, _ title: String, _ summary: String, @ViewBuilder _ rows: () -> C) -> some View {
        let open = expanded.contains(id), content = rows()
        return Section {
            if open { content }
        } header: {
            HStack(spacing: 6) {
                Image(systemName: "chevron.right").font(.caption.weight(.semibold)).rotationEffect(.degrees(open ? 90 : 0))
                Text(title)
                Spacer()
                if !open { Text(summary).fontWeight(.regular).foregroundStyle(.secondary) }
            }
        }
    }
}

// MARK: A (picked): cards fold; the title is the card's first row, open rows keep their dividers

private struct ProposalA4: View {
    let expanded: Set<String>
    var body: some View {
        VStack(spacing: 0) {
            Header()
            Form {
                Section { switchRows() }
                fold("auto", "Tự chuyển", "Bật · nhớ theo ứng dụng") { autoRows() }
                fold("viet", "Gõ tiếng Việt", "hoà, thuý · chính tả · teencode") { vietRows() }
                fold("emoji", "Bảng emoji", "Bật · ⌃⌘E") { emojiRows() }
                fold("general", "Chung", "Mở khi đăng nhập · tự cập nhật") { generalRows() }
            }
            .formStyle(.grouped).scrollDisabled(true).padding(.bottom, 8)
        }
        .fixedSize(horizontal: false, vertical: true)
    }
    private func fold<C: View>(_ id: String, _ title: String, _ summary: String, @ViewBuilder _ rows: () -> C) -> some View {
        let open = expanded.contains(id), content = rows()
        return Section {
            HStack(spacing: 8) {
                Image(systemName: "chevron.right").font(.system(size: 11, weight: .semibold)).foregroundStyle(.secondary)
                    .rotationEffect(.degrees(open ? 90 : 0)).frame(width: 12)
                Text(title).fontWeight(.semibold)
                Spacer()
                if !open { Text(summary).foregroundStyle(.secondary).lineLimit(1) }
            }
            .contentShape(Rectangle())
            if open { content }
        }
    }
}

// MARK: B — tabs, one short page each

private struct ProposalB: View {
    let tab: Int
    var body: some View {
        VStack(spacing: 0) {
            Header()
            Picker("", selection: .constant(tab)) {
                Text("Chung").tag(0); Text("Gõ phím").tag(1); Text("Emoji").tag(2)
            }
            .pickerStyle(.segmented).labelsHidden().fixedSize().padding(.top, 8)
            Form {
                if tab == 0 {
                    Section { switchRows() }
                    Section { generalRows() }
                } else if tab == 1 {
                    Section("Tự chuyển") { autoRows() }
                    Section("Gõ tiếng Việt") { vietRows() }
                } else {
                    Section { emojiRows() }
                }
            }
            .formStyle(.grouped).scrollDisabled(true).padding(.bottom, 8)
        }
        .fixedSize(horizontal: false, vertical: true)
    }
}

// MARK: C — the four things people change, the rest behind "More"

private struct ProposalC: View {
    let more: Bool
    var body: some View {
        VStack(spacing: 0) {
            Header()
            Form {
                Section {
                    LabeledContent { HotkeyBox(text: "⌃⇧") } label: { Tip("Phím chuyển Anh/Việt", "") }
                    on("Tự chuyển Anh/Việt", "")
                    Picker(selection: .constant(1)) { Text("hoà, thuý").tag(1); Text("hòa, thúy").tag(0) } label: { Tip("Đặt dấu", "") }
                    on("Bảng emoji  ⌃⌘E", "")
                }
                Section {
                    DisclosureGroup(isExpanded: .constant(more)) {
                        on("Âm báo khi chuyển", "", false)
                        on("Tạm dừng 5 giây", "")
                        on("Nhớ theo ứng dụng", "")
                        on("Nhớ theo trang web", "", false)
                        on("Kiểm tra chính tả", "")
                        on("Trả lại phím khi sai", "")
                        on("Chính tả teencode (zui, zậy, gòy)", "")
                        generalRows()
                    } label: { Text("Tuỳ chọn khác").font(.headline) }
                }
            }
            .formStyle(.grouped).scrollDisabled(true).padding(.bottom, 8)
        }
        .fixedSize(horizontal: false, vertical: true)
    }
}
#endif
