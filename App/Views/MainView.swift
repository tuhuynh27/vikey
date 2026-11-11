import SwiftUI

/// Vikey's one window: the language switch on top, then every setting, compact.
struct MainView: View {
    @EnvironmentObject var state: AppState
    @AppStorage(PrefKey.hotkey) private var hotkeyRaw = Hotkey.defaultValue
    @AppStorage(PrefKey.modernOrthography) private var modern = 1
    @AppStorage(PrefKey.spelling) private var spelling = 1
    @AppStorage(PrefKey.restoreIfInvalid) private var restore = 1
    @AppStorage(PrefKey.autoSwitch) private var autoSwitch = 1
    @AppStorage(PrefKey.smartSwitch) private var smartSwitch = 1
    @AppStorage(PrefKey.pauseAfterManual) private var pauseAfterManual = 1
    @AppStorage(PrefKey.smartSwitchPerSite) private var perSite = true
    @AppStorage(PrefEmoji.enabled) private var emojiOn = 0
    @AppStorage(PrefEmoji.hotkey) private var emojiKey = 0
    @State private var launchAtLogin = LaunchAtLogin.isEnabled
    @State private var browserDenied = false

    var body: some View {
        VStack(spacing: 0) {
            header
            Form {
                Section {
                    LabeledContent {
                        HotkeyRecorder(raw: Binding(get: { hotkeyRaw }, set: { hotkeyRaw = $0; Preferences.apply() }))
                    } label: {
                        Tip("Phím chuyển Anh/Việt", "Bấm tổ hợp phím này để chuyển giữa tiếng Việt và tiếng Anh. Bấm vào ô để đặt phím khác.")
                    }
                    Toggle(isOn: Binding(
                        get: { Hotkey(raw: hotkeyRaw).beep },
                        set: { var h = Hotkey(raw: hotkeyRaw); h.beep = $0; hotkeyRaw = h.raw; Preferences.apply() })) {
                        Tip("Âm báo khi chuyển", "Phát một âm ngắn mỗi lần bạn bấm phím chuyển Anh/Việt: lên giọng khi sang tiếng Việt, xuống giọng khi sang tiếng Anh.")
                    }
                }
                Section("Tự chuyển") {
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
                Section("Gõ tiếng Việt") {
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
                }
                Section {
                    Toggle(isOn: Binding(get: { emojiOn != 0 }, set: { emojiOn = $0 ? 1 : 0; Preferences.apply() })) {
                        Tip("Bảng emoji", "Bấm phím tắt để mở bảng emoji ở giữa màn hình. Tìm bằng tiếng Việt hoặc tiếng Anh, gõ Telex vẫn tìm được: \"ngựa\", \"horse\" đều ra 🐴. Enter để chèn, Esc để đóng.")
                    }
                    if emojiOn != 0 {
                        LabeledContent {
                            HotkeyRecorder(raw: Binding(get: { emojiKey }, set: { k in
                                guard k != hotkeyRaw & ~0x8000 else { return }   // already the Vietnamese/English switch
                                emojiKey = k & ~0x8000; Preferences.apply()
                            }), allowModifiersOnly: false, autoStart: emojiKey == 0)
                        } label: {
                            Tip("Phím mở bảng emoji", "Một phím kèm ⌃, ⌥ hoặc ⌘, ví dụ ⌃⌘E. Bấm vào ô để đặt phím khác.")
                        }
                    }
                }
                Section {
                    Toggle(isOn: Binding(get: { launchAtLogin }, set: { LaunchAtLogin.set($0); launchAtLogin = LaunchAtLogin.isEnabled })) {
                        Tip("Mở Vikey khi đăng nhập", "Tự mở Vikey mỗi khi bạn đăng nhập vào máy Mac.")
                    }
                    LabeledContent {
                        Button("Gửi góp ý…") { Feedback.open() }
                    } label: {
                        Tip("Góp ý", "Báo lỗi hoặc góp ý cho Vikey: mở trang vikey.app/feedback trong trình duyệt.")
                    }
                }
            }
            .formStyle(.grouped)
            .scrollDisabled(true)
            .padding(.bottom, 8)
        }
        .frame(width: 460)
        .fixedSize(horizontal: false, vertical: true)
        .alert("Vikey chưa được phép đọc địa chỉ trang web", isPresented: $browserDenied) {
            Button("Mở Cài đặt hệ thống") {
                NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Automation")!)
            }
            Button("Để sau", role: .cancel) {}
        } message: {
            Text("Vào Quyền riêng tư & Bảo mật › Tự động hoá, bật trình duyệt của bạn dưới Vikey, rồi bật lại tuỳ chọn này.")
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
