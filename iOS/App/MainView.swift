import SwiftUI

/// Settings (shared with the keyboard), a place to try it, and the keyboard's status.
struct MainView: View {
    @AppStorage(VKSettings.Key.autoSwitch, store: VKSettings.defaults) private var autoSwitch = true
    @AppStorage(VKSettings.Key.pauseAfterManual, store: VKSettings.defaults) private var pause = true
    @AppStorage(VKSettings.Key.englishForWebFields, store: VKSettings.defaults) private var webEnglish = true
    @AppStorage(VKSettings.Key.modernOrthography, store: VKSettings.defaults) private var modern = true
    @AppStorage(VKSettings.Key.telexStyle, store: VKSettings.defaults) private var telexStyle = 0
    @AppStorage(VKSettings.Key.spelling, store: VKSettings.defaults) private var spelling = true
    @AppStorage(VKSettings.Key.restoreIfInvalid, store: VKSettings.defaults) private var restore = true
    @AppStorage(VKSettings.Key.autoCapitalize, store: VKSettings.defaults) private var autoCap = true
    @AppStorage(VKSettings.Key.doubleSpacePeriod, store: VKSettings.defaults) private var doubleSpace = true
    @AppStorage(VKSettings.Key.haptics, store: VKSettings.defaults) private var haptics = true
    @AppStorage(VKSettings.Key.diagnostics, store: VKSettings.defaults) private var diagnostics = false
    @AppStorage(VKSettings.Key.suggestions, store: VKSettings.defaults) private var suggestions = true
    @AppStorage(VKSettings.Key.autoCorrect, store: VKSettings.defaults) private var autoCorrect = true
    @AppStorage("didOnboard") private var didOnboard = true
    @State private var added = KeyboardStatus.isAdded
    @State private var sample = ""
    @State private var logText = ""
    /// The secret debug menu: five taps on the version (Tony, 2026-11-07).
    @AppStorage("debugMenu") private var debugMenu = false
    @State private var versionTaps = 0
    @State private var debugSent: String?
    @Environment(\.scenePhase) private var phase

    var body: some View {
        NavigationStack {
            Form {
                Section {   // the wordmark header (design/BRAND.md) instead of a plain navigation title
                    HStack(spacing: 12) {
                        Image("Logo").resizable().frame(width: 40, height: 40)
                            .clipShape(RoundedRectangle(cornerRadius: 9, style: .continuous))
                        Wordmark(size: 32)
                        Spacer()
                        feedbackButton
                    }
                    .listRowBackground(Color.clear)
                    .listRowInsets(EdgeInsets(top: 4, leading: 4, bottom: 0, trailing: 0))
                }
                if !added {
                    Section {
                        Button { KeyboardStatus.openSettings() } label: {
                            Label("Thêm bàn phím Vikey trong Cài đặt", systemImage: "keyboard.badge.ellipsis")
                        }
                    } footer: {
                        Text("Cài đặt › Vikey › Bàn phím › bật Vikey.")
                    }
                }
                Section("Thử bàn phím") {
                    TextField("Chạm vào đây, chọn Vikey bằng 🌐 rồi gõ", text: $sample, axis: .vertical)
                        .lineLimit(2...5)
                        .onChange(of: sample) { _, t in
                            #if DEBUG
                            // simulator tests read what was typed from here
                            let url = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("typed.txt")
                            try? t.write(to: url, atomically: true, encoding: .utf8)
                            #endif
                        }
                }
                Section("Tự chuyển") {
                    Toggle(isOn: $autoSwitch) { Tip("Tự chuyển Anh/Việt", "Gõ một từ tiếng Anh khi đang ở tiếng Việt (hoặc ngược lại), Vikey tự sửa từ đó và chuyển chế độ. Bấm xoá ngay sau đó để hoàn tác.") }
                    Toggle(isOn: $pause) { Tip("Tạm dừng 5 giây", "Khi bạn tự bấm phím VI/EN, Vikey không tự chuyển trong 5 giây, để giữ đúng lựa chọn của bạn.") }
                        .disabled(!autoSwitch)
                    Toggle(isOn: $webEnglish) { Tip("Tiếng Anh cho web, email", "Trong ô nhập địa chỉ web, email hoặc tìm kiếm web, Vikey bắt đầu bằng tiếng Anh và quay lại ngôn ngữ cũ khi bạn rời ô đó.") }
                }
                Section("Gõ tiếng Việt") {
                    Picker(selection: $telexStyle) {
                        Text("Telex").tag(0)
                        Text("Telex đơn giản 1").tag(1)
                        Text("Telex đơn giản 2").tag(2)
                    } label: { Tip("Kiểu gõ", "Telex và Telex đơn giản 2: w đứng riêng ra ư. Telex đơn giản 1: w đứng riêng vẫn là w. Cả ba kiểu đều gõ uw, ow, aw ra ư, ơ, ă. Trên bàn phím điện thoại, [ và ] luôn là dấu ngoặc.") }
                    Picker(selection: $modern) {
                        Text("hoà, thuý").tag(true)
                        Text("hòa, thúy").tag(false)
                    } label: { Tip("Đặt dấu", "Vị trí dấu thanh với oa, oe, uy: kiểu mới (hoà, thuý) hoặc kiểu cũ (hòa, thúy).") }
                    Toggle(isOn: $spelling) { Tip("Kiểm tra chính tả", "Chỉ bỏ dấu khi từ đang gõ đúng quy tắc chính tả tiếng Việt.") }
                    Toggle(isOn: $restore) { Tip("Trả lại phím khi sai", "Khi gõ xong một từ sai chính tả tiếng Việt, Vikey trả lại đúng các phím bạn đã gõ.") }
                        .disabled(!spelling)
                }
                Section("Bàn phím") {
                    Toggle(isOn: $suggestions) { Tip("Gợi ý từ", "Hiện các từ gợi ý phía trên bàn phím, cả tiếng Việt lẫn tiếng Anh. Chạm vào một từ để dùng nó.") }
                    Toggle(isOn: $autoCorrect) { Tip("Tự sửa lỗi gõ", "Khi bạn chạm lệch phím (\"teh\" thay vì \"the\"), Vikey sửa từ đó khi bạn bấm phím cách. Bấm xoá ngay sau đó để giữ lại từ bạn gõ.") }
                    Toggle(isOn: $autoCap) { Tip("Tự viết hoa", "Chữ đầu tiên của câu được viết hoa, như bàn phím của iPhone.") }
                    Toggle(isOn: $haptics) { Tip("Rung khi gõ", "Rung nhẹ mỗi khi chạm phím, như bàn phím của iPhone. Cần bật Cho phép Truy cập đầy đủ: Cài đặt › Vikey › Bàn phím. Vikey chỉ dùng quyền này để rung, không bao giờ gửi gì ra khỏi máy.") }
                    Toggle(isOn: $doubleSpace) { Tip("Phím tắt dấu chấm", "Chạm nhanh hai lần phím cách để thêm dấu chấm và một khoảng trắng.") }
                }
                if debugMenu { debugSection }
                Section {
                    Button { KeyboardStatus.openSettings() } label: {
                        LabeledContent("Ngôn ngữ ứng dụng", value: Locale.current.localizedString(forLanguageCode: Bundle.main.preferredLocalizations.first ?? "en") ?? "")
                    }
                    .foregroundStyle(.primary)
                    Button("Xem lại hướng dẫn") { didOnboard = false }
                    NavigationLink("Ghi nhận") { AcknowledgementsView() }
                    Link("Gửi góp ý", destination: Self.feedbackURL)
                    Link("vikey.app", destination: URL(string: "https://vikey.app")!)
                    LabeledContent("Phiên bản", value: "\(Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "") (\(Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? ""))")   // version (build)
                        .contentShape(Rectangle())
                        .onTapGesture {
                            versionTaps += 1
                            if versionTaps >= 5 { versionTaps = 0; withAnimation { debugMenu.toggle() } }
                        }
                } footer: {
                    Text("Vikey được thiết kế và phát triển bởi Tony Dinh, dựa trên dự án mã nguồn mở OpenKey của Tuyen Mai. Mã nguồn mở theo giấy phép GPL-3.0.")
                }
            }
            .navigationTitle("Vikey")
            .toolbar(.hidden, for: .navigationBar)
            .tint(Color.brand)   // one accent color (design/BRAND.md)
        }
        .onChange(of: phase) { _, p in if p == .active { added = KeyboardStatus.isAdded; logText = KBLog.read() } }
        .onAppear { logText = KBLog.read() }
    }
}

extension MainView {
    /// The secret debug menu: the keyboard's nudges, shown the next time the keyboard opens.
    var debugSection: some View {
        Section {
            Toggle(isOn: $diagnostics) { Tip("Hiện thông số", "Phía trên các phím hiện thời gian xử lý mỗi lần chạm, số lần rung và số lần chạm iOS gửi tới, và ghi vào nhật ký. Nhật ký không chứa chữ bạn gõ, chỉ có thời gian và loại phím (chữ, dấu cách, xoá…). Cần Truy cập đầy đủ để bàn phím ghi được nhật ký.") }
            if diagnostics {
                ShareLink(item: logText.isEmpty ? "(nhật ký trống)" : logText, subject: Text("Vikey keyboard log")) {
                    Label("Gửi nhật ký", systemImage: "square.and.arrow.up")
                }
                Button("Xoá nhật ký", role: .destructive) { KBLog.clear(); logText = "" }
                Text(logText.isEmpty ? LocalizedStringKey("Chưa có gì. Mở bàn phím Vikey và gõ thử.") : LocalizedStringKey("\(logText.split(separator: "\n").count) dòng")).font(.footnote).foregroundStyle(.secondary)
            }
            Button { sendNudge("review") } label: { Label("Hiện lời mời đánh giá", systemImage: "star") }
            Button { sendNudge("bug") } label: { Label("Hiện lời mời góp ý", systemImage: "ladybug") }
            Button("Mở bảng đánh giá của Apple") { ReviewPrompt.request() }
            Button("Ẩn menu này", role: .destructive) { withAnimation { debugMenu = false } }
        } header: { Text("Menu ẩn") } footer: {
            Text(debugSent.map { "Mở bàn phím Vikey: lời mời \($0 == "review" ? "đánh giá" : "góp ý") hiện sau khi logo chạy xong." } ?? "Chạm 5 lần vào Phiên bản để ẩn hoặc hiện menu này.")
        }
    }

    private func sendNudge(_ k: String) {
        VKSettings.defaults.set("\(k):\(Date().timeIntervalSince1970)", forKey: VKSettings.Key.debugNudge)
        debugSent = k
    }

    /// Send feedback, top right of the header (Tony picked the capsule with text).
    var feedbackButton: some View {
        Link(destination: Self.feedbackURL) {
            Label("Góp ý", systemImage: "bubble.left.and.text.bubble.right")
                .font(.subheadline.weight(.semibold))
                .padding(.horizontal, 12).padding(.vertical, 7)
                .background(Capsule().fill(Color.brand.opacity(0.12)))
        }
        .foregroundStyle(Color.brand)
        .accessibilityLabel("Gửi góp ý")
    }

    /// vikey.app/feedback (English page when the app is in English) with the platform and version.
    static var feedbackURL: URL {
        let v = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? ""
        let en = (Bundle.main.preferredLocalizations.first ?? "vi") != "vi"
        return URL(string: "https://vikey.app\(en ? "/en" : "")/feedback?platform=ios&v=\(v)")!
    }
}

/// A setting's title with a (?) that explains it.
struct Tip: View {
    let title: LocalizedStringKey, tip: LocalizedStringKey
    @State private var shown = false
    init(_ title: LocalizedStringKey, _ tip: LocalizedStringKey) { self.title = title; self.tip = tip }
    var body: some View {
        HStack(spacing: 6) {
            Text(title)
            Button { shown = true } label: { Image(systemName: "questionmark.circle").foregroundStyle(.tertiary) }
                .buttonStyle(.plain)
                .popover(isPresented: $shown) {
                    Text(tip).font(.callout).padding().frame(width: 280).fixedSize(horizontal: false, vertical: true)
                        .presentationCompactAdaptation(.popover)
                }
        }
    }
}
