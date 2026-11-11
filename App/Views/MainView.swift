import SwiftUI

/// Vikey's one window: the language switch on top, the hotkey, then the settings in cards that fold. A closed card
/// says in one line what's on; the window remembers which cards are open.
struct MainView: View {
    @EnvironmentObject var state: AppState
    @AppStorage(PrefKey.hotkey) private var hotkeyRaw = Hotkey.defaultValue
    @AppStorage(PrefKey.modernOrthography) private var modern = 1
    @AppStorage(PrefKey.spelling) private var spelling = 1
    @AppStorage(PrefKey.restoreIfInvalid) private var restore = 1
    @AppStorage(PrefKey.teencode) private var teencode = 1
    @AppStorage(PrefKey.telexStyle) private var telexStyle = 0
    @AppStorage(PrefKey.mainTab) private var tab = "general"
    @AppStorage(PrefKey.autoSwitch) private var autoSwitch = 1
    @AppStorage(PrefKey.smartSwitch) private var smartSwitch = 1
    @AppStorage(PrefKey.pauseAfterManual) private var pauseAfterManual = 1
    @AppStorage(PrefKey.smartSwitchPerSite) private var perSite = true
    @AppStorage(PrefEmoji.enabled) private var emojiOn = 0
    @AppStorage(PrefEmoji.hotkey) private var emojiKey = 0
    @State private var launchAtLogin = LaunchAtLogin.isEnabled
    @State private var browserDenied = false
    @State private var excluded = ExcludedApps.all
    @State private var lastContentHeight: CGFloat = 0

    var body: some View {
        // taller than the screen (a long list of excluded apps): the window stops at its edge (HostedWindow) and this scrolls (#51)
        ViewThatFits(in: .vertical) {
            page
            ScrollView { page }
        }
        .onPreferenceChange(ContentHeightPreferenceKey.self) { newHeight in
            guard newHeight > 0, abs(newHeight - lastContentHeight) > 1 else { return }
            lastContentHeight = newHeight
            DispatchQueue.main.async {
                guard abs(newHeight - lastContentHeight) <= 1 else { return }
                Windows.main.fitToContent(height: newHeight)
            }
        }
        .alert("Vikey chưa được phép đọc địa chỉ trang web", isPresented: $browserDenied) {
            Button("Mở Cài đặt hệ thống") {
                NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Automation")!)
            }
            Button("Để sau", role: .cancel) {}
        } message: {
            Text("Vào Quyền riêng tư & Bảo mật › Tự động hoá, bật trình duyệt của bạn dưới Vikey, rồi bật lại tuỳ chọn này.")
        }
    }

    private var page: some View {
        VStack(spacing: 0) {
            header
            tabBar
            Form {
                switch tab {
                case "auto": Section { autoRows }
                case "viet": Section { vietRows }
                case "emoji": Section { emojiRows }
                case "excluded": Section { excludedRows }
                default:
                    Section { hotkeyRows }
                    Section { generalRows }
                }
            }
            .formStyle(.grouped)
            .scrollDisabled(true)
            .padding(.bottom, 8)
        }
        .frame(maxWidth: 460)   // an always-shown scroller (System Settings) takes its width from the page, not past the window's edge
        .fixedSize(horizontal: false, vertical: true)
        .background(
            GeometryReader { geo in
                Color.clear.preference(key: ContentHeightPreferenceKey.self, value: geo.size.height)
            }
        )
    }

    // MARK: tabs

    private static let tabs: [(id: String, title: String, icon: String)] = [
        ("general", "Chung", "gearshape"), ("auto", "Tự chuyển", "arrow.left.arrow.right"),
        ("viet", "Gõ tiếng Việt", "character.cursor.ibeam"), ("emoji", "Emoji", "face.smiling"), ("excluded", "Loại trừ", "nosign"),
    ]

    private func select(_ id: String) {
        tab = id
    }

    private var tabBar: some View {
        HStack(spacing: 2) {
            ForEach(Self.tabs, id: \.id) { t in
                let on = tab == t.id
                Button { select(t.id) } label: {
                    VStack(spacing: 3) {
                        Image(systemName: t.icon).font(.system(size: 18)).frame(height: 22)
                        Text(t.title).font(.system(size: 11))
                    }
                    .frame(width: 80, height: 50)
                    .foregroundStyle(on ? Color.accentColor : Color.secondary)
                    .background(RoundedRectangle(cornerRadius: 8).fill(on ? Color.primary.opacity(0.08) : .clear))
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel(t.title)
                .accessibilityAddTraits(on ? .isSelected : [])
            }
        }
        .padding(.top, 8)
    }

    // MARK: rows of each tab

    @ViewBuilder private var hotkeyRows: some View {
                LabeledContent {
                    HotkeyRecorder(raw: Binding(get: { hotkeyRaw }, set: { k in
                        guard emojiOn == 0 || !Hotkey.sameKeys(k, emojiKey) else { return }   // already the emoji picker's (only while it's on)
                        hotkeyRaw = k; Preferences.apply()
                    }))
                } label: {
                    Tip("Phím chuyển Anh/Việt", "Bấm tổ hợp phím này để chuyển giữa tiếng Việt và tiếng Anh. Bấm vào ô để đặt phím khác.")
                }
                Toggle(isOn: Binding(
                    get: { Hotkey(raw: hotkeyRaw).beep },
                    set: { var h = Hotkey(raw: hotkeyRaw); h.beep = $0; hotkeyRaw = h.raw; Preferences.apply() })) {
                    Tip("Âm báo khi chuyển", "Phát một âm ngắn mỗi lần bạn bấm phím chuyển Anh/Việt: lên giọng khi sang tiếng Việt, xuống giọng khi sang tiếng Anh.")
                }
    }
    @ViewBuilder private var autoRows: some View {
                Toggle(isOn: flag($autoSwitch)) {
                    Tip("Tự chuyển Anh/Việt", "Gõ một từ tiếng Anh khi đang ở tiếng Việt (hoặc ngược lại), Vikey tự sửa từ đó và chuyển chế độ. Bấm Xoá ngay sau đó để hoàn tác.")
                }
                Toggle(isOn: flag($pauseAfterManual)) {
                    Tip("Tạm dừng 5 giây", "Khi bạn tự bấm chuyển Anh/Việt, Vikey không tự chuyển trong 5 giây, để giữ đúng lựa chọn của bạn.")
                }
                .disabled(autoSwitch == 0)
                Toggle(isOn: flag($smartSwitch)) {
                    Tip("Nhớ theo ứng dụng", "Vikey nhớ bạn dùng tiếng Anh hay tiếng Việt ở mỗi ứng dụng và tự chuyển lại khi bạn quay về ứng dụng đó.")
                }
                Toggle(isOn: Binding(get: { perSite }, set: { setPerSite($0) })) {
                    Tip("Nhớ theo trang web", "Trong Safari, Chrome, Edge, Brave, Arc, Vivaldi, Opera: nhớ tiếng Anh hay tiếng Việt cho từng trang web. Cần quyền đọc địa chỉ tab đang mở, chỉ dùng trên máy bạn.")
                }
                .disabled(smartSwitch == 0)
    }
    @ViewBuilder private var vietRows: some View {
                Picker(selection: Binding(get: { telexStyle }, set: { telexStyle = $0; Preferences.apply() })) {
                    Text("Telex").tag(0)
                    Text("Telex đơn giản 1").tag(1)
                    Text("Telex đơn giản 2").tag(2)
                } label: {
                    Tip("Kiểu gõ", "Telex: [ ra ơ, ] ra ư, w ra ư. Telex đơn giản 1: [ ] là dấu ngoặc, w đứng riêng vẫn là w. Telex đơn giản 2: [ ] là dấu ngoặc, w vẫn ra ư. Cả ba kiểu đều gõ uw, ow, aw ra ư, ơ, ă.")
                }
                Picker(selection: Binding(get: { modern }, set: { modern = $0; Preferences.apply() })) {
                    Text("hoà, thuý").tag(1)
                    Text("hòa, thúy").tag(0)
                } label: {
                    Tip("Đặt dấu", "Vị trí dấu thanh với oa, oe, uy: kiểu mới (hoà, thuý) hoặc kiểu cũ (hòa, thúy).")
                }
                Toggle(isOn: flag($spelling)) {
                    Tip("Kiểm tra chính tả", "Chỉ bỏ dấu khi từ đang gõ đúng quy tắc chính tả tiếng Việt.")
                }
                Toggle(isOn: flag($restore)) {
                    Tip("Trả lại phím khi sai", "Khi gõ xong một từ sai chính tả tiếng Việt, Vikey trả lại đúng các phím bạn đã gõ.")
                }
                .disabled(spelling == 0)
                Toggle(isOn: flag($teencode)) {
                    Tip("Chính tả teencode", "Giữ các từ kiểu chat như zui zẻ, zậy, dzô, gòy, thík, bíe thay vì trả lại phím. Chỉ những từ teencode quen thuộc, nên từ tiếng Anh như zero, look vẫn giữ nguyên.")
                }
    }
    @ViewBuilder private var emojiRows: some View {
                Toggle(isOn: Binding(get: { emojiOn != 0 }, set: { on in
                    if on && Hotkey.sameKeys(emojiKey, hotkeyRaw) { emojiKey = 0 }   // a collision left from before: record it again
                    emojiOn = on ? 1 : 0; Preferences.apply()
                })) {
                    Tip("Bảng emoji", "Bấm phím tắt để mở bảng emoji ở giữa màn hình. Tìm bằng tiếng Việt hoặc tiếng Anh, gõ Telex vẫn tìm được: \"ngựa\", \"horse\" đều ra 🐴. Enter để chèn, Esc để đóng.")
                }
                if emojiOn != 0 {
                    LabeledContent {
                        HotkeyRecorder(raw: Binding(get: { emojiKey }, set: { k in
                            guard !Hotkey.sameKeys(k, hotkeyRaw) else { return }   // already the Vietnamese/English switch
                            emojiKey = k & ~0x8000; Preferences.apply()
                        }), allowModifiersOnly: false, autoStart: emojiKey == 0)
                    } label: {
                        Tip("Phím mở bảng emoji", "Một phím kèm ⌃, ⌥ hoặc ⌘, ví dụ ⌃⌘E. Bấm vào ô để đặt phím khác.")
                    }
                }
    }
    @ViewBuilder private var excludedRows: some View {
                Text("Trong các ứng dụng này Vikey không gõ tiếng Việt và không tự chuyển (hợp với phần mềm điều khiển máy từ xa). Phím chuyển Anh/Việt vẫn dùng được.")
                    .font(.callout).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                ForEach(excluded.filter(ExcludedApps.isInstalled), id: \.self) { id in
                    let info = ExcludedApps.info(id)
                    HStack(spacing: 8) {
                        if let icon = info.icon { Image(nsImage: icon).resizable().frame(width: 18, height: 18) }
                        else { Image(systemName: "app").frame(width: 18, height: 18).foregroundStyle(.secondary) }
                        Text(info.name)
                        Spacer()
                        Button { ExcludedApps.remove(id); excluded = ExcludedApps.all } label: { Image(systemName: "minus.circle") }
                            .buttonStyle(.plain).foregroundStyle(.secondary)
                            .accessibilityLabel("Bỏ \(info.name)")
                            .help(id)
                    }
                }
                HStack {
                    Menu("Thêm ứng dụng") {
                        ForEach(ExcludedApps.runningApps().filter { !excluded.contains($0.id) }, id: \.id) { app in
                            Button(app.name) { ExcludedApps.add(app.id); excluded = ExcludedApps.all }
                        }
                        Divider()
                        Button("Chọn ứng dụng khác…") { ExcludedApps.pickAndAdd(); excluded = ExcludedApps.all }
                    }
                    .fixedSize()
                    Spacer()
                    Button("Mặc định") { ExcludedApps.resetToDefaults(); excluded = ExcludedApps.all }
                }
    }
    @ViewBuilder private var generalRows: some View {
                Toggle(isOn: Binding(get: { launchAtLogin }, set: { LaunchAtLogin.set($0); launchAtLogin = LaunchAtLogin.isEnabled })) {
                    Tip("Mở Vikey khi đăng nhập", "Tự mở Vikey mỗi khi bạn đăng nhập vào máy Mac.")
                }
                LabeledContent {
                    Button("Gửi góp ý…") { Feedback.open() }
                } label: {
                    Tip("Góp ý", "Báo lỗi hoặc góp ý cho Vikey: mở trang vikey.app/feedback trong trình duyệt.")
                }
    }

    /// Turning per-site memory on asks for the browser permission right away.
    private func setPerSite(_ on: Bool) {
        perSite = on
        Preferences.apply()
        guard on else { return }
        VikeyCore.requestBrowserAccess { denied in
            guard denied else { return }
            perSite = false
            Preferences.apply()
            browserDenied = true
        }
    }

    private var header: some View {
        HStack(spacing: 12) {
            Image(nsImage: NSApp.applicationIconImage)
                .resizable()
                .frame(width: 44, height: 44)
            VStack(alignment: .leading, spacing: 2) {
                Wordmark(size: 22)
                if state.isRunning {
                    Text("Bấm \(Hotkey(raw: hotkeyRaw).display) để chuyển")
                        .font(.callout).foregroundStyle(.secondary)
                } else {
                    Button("Cấp quyền Trợ năng…") { OnboardingWindow.show() }
                        .buttonStyle(.link)
                }
            }
            Spacer()
            Picker("", selection: Binding(get: { state.language }, set: { state.setLanguage($0) })) {
                Text("Tiếng Việt").tag(1)
                Text("English").tag(0)
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .fixedSize()
        }
        .padding(.horizontal, 20)
        .padding(.top, 6)
        .padding(.bottom, 4)
    }

}

/// A setting's title with a (?) that explains it: a tooltip on hover, a popover on click.
struct Tip: View {
    let title: String, tip: String
    @State private var shown = false
    init(_ title: String, _ tip: String) { self.title = title; self.tip = tip }
    var body: some View {
        HStack(spacing: 5) {
            Text(title)
            Button { shown.toggle() } label: {
                Image(systemName: "questionmark.circle").foregroundStyle(.tertiary)
            }
            .buttonStyle(.plain)
            .help(tip)
            .popover(isPresented: $shown, arrowEdge: .bottom) {
                Text(tip)
                    .frame(width: 260, alignment: .leading)
                    .fixedSize(horizontal: false, vertical: true)
                    .padding(12)
            }
        }
    }
}

/// Binds an Int (0/1) preference used by the C core to a Toggle, applying it on change.
func flag(_ value: Binding<Int>) -> Binding<Bool> {
    Binding(get: { value.wrappedValue != 0 }, set: { value.wrappedValue = $0 ? 1 : 0; Preferences.apply() })
}

/// Reports the natural layout height of MainView's content to HostedWindow.
private struct ContentHeightPreferenceKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) {
        value = max(value, nextValue())
    }
}
