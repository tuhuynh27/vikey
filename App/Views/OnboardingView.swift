import SwiftUI

/// Hosts the onboarding in its own window so it can be shown from anywhere (before the menu bar is used).
enum OnboardingWindow {
    private static var window: NSWindow?

    @MainActor static func show() {
        if window?.isVisible != true { window = nil }   // opened again later (menu › Hướng dẫn): start from the first step
        if window == nil {
            let w = NSWindow(contentViewController: NSHostingController(rootView: OnboardingView().environmentObject(AppState.shared)))
            w.title = "Vikey"
            w.styleMask = [.titled, .closable]
            w.isReleasedWhenClosed = false
            window = w
        }
        WindowFocus.bringForward()
        window?.layoutIfNeeded()
        window?.centerExactly()
        window?.makeKeyAndOrderFront(nil)
    }

    @MainActor static func close() {
        UserDefaults.standard.set(true, forKey: OnboardingView.doneKey)
        window?.close()
    }
}

/// First run: Telex only → Accessibility (if needed) → type a few real sentences → the switch hotkey →
/// the emoji picker (optional).
struct OnboardingView: View {
    static let doneKey = "OnboardingDone"
    enum Step: Int, CaseIterable { case welcome, permission, practice, hotkey, emoji }

    @EnvironmentObject var state: AppState
    @AppStorage(PrefKey.hotkey) private var hotkeyRaw = Hotkey.defaultValue
    @AppStorage(PrefEmoji.enabled) private var emojiOn = 0
    @AppStorage(PrefEmoji.hotkey) private var emojiKey = 0
    @State private var step: Step = {
        if let s = UserDefaults.standard.string(forKey: "onboardingStep"), let i = Int(s), let st = Step(rawValue: i) { return st }   // screenshots
        return .welcome
    }()
    private var previewPermission: Bool { UserDefaults.standard.bool(forKey: "previewPermissionStep") }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 14) {
                Image(nsImage: NSApp.applicationIconImage).resizable().frame(width: 48, height: 48)
                VStack(alignment: .leading, spacing: 2) {
                    Wordmark(size: 24)
                    Text("Gõ tiếng Việt, tự chuyển Anh/Việt cho bạn.").foregroundStyle(.secondary)
                }
                Spacer()
                dots
            }
            .padding(.bottom, 22)
            Group {
                switch step {
                case .welcome: welcome
                case .permission: permission
                case .practice: PracticeStep(hotkey: Hotkey(raw: hotkeyRaw).display, vietnamese: state.language == 1)
                case .hotkey: hotkeyStep
                case .emoji: emojiStep
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            Spacer(minLength: 20)
            footer
        }
        .padding(28)
        .frame(width: 540, height: 470)
        .onChange(of: state.isRunning) { running in if running && step == .permission { step = .practice } }
    }

    private var dots: some View {
        HStack(spacing: 6) {
            ForEach(Step.allCases, id: \.self) { s in
                Circle().fill(s == step ? Color.brand : Color.secondary.opacity(0.3)).frame(width: 7, height: 7)
            }
        }
    }

    private var footer: some View {
        HStack {
            if step != .welcome {
                Button("Quay lại") { go(Step(rawValue: step.rawValue - 1)!) }.controlSize(.large)
            }
            Spacer()
            switch step {
            case .welcome:
                Button("Bắt đầu") { go(state.isRunning && !previewPermission ? .practice : .permission) }
                    .buttonStyle(.borderedProminent).controlSize(.large).keyboardShortcut(.defaultAction)
            case .permission:
                if !state.isRunning || previewPermission {
                    ProgressView().controlSize(.small)
                    Text("Đang chờ quyền…").foregroundStyle(.secondary).padding(.trailing, 6)
                }
                Button("Tiếp tục") { go(.practice) }
                    .buttonStyle(.borderedProminent).controlSize(.large)
                    .disabled(!state.isRunning || previewPermission)
            case .practice, .hotkey:
                Button("Tiếp tục") { go(Step(rawValue: step.rawValue + 1)!) }
                    .buttonStyle(.borderedProminent).controlSize(.large)
            case .emoji:
                Button("Xong") { OnboardingWindow.close() }
                    .buttonStyle(.borderedProminent).controlSize(.large)
                    .disabled(emojiOn != 0 && emojiKey == 0)
            }
        }
    }

    private func go(_ s: Step) { withAnimation(.easeInOut(duration: 0.15)) { step = s } }

    // MARK: steps

    private var welcome: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Vikey dùng kiểu gõ Telex").font(.title2.weight(.semibold))
            Text("Vikey chỉ hỗ trợ Telex, kiểu gõ phổ biến nhất. Bạn gõ tiếng Anh bình thường, Vikey tự nhận ra và không bỏ dấu nhầm.")
                .foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            Grid(alignment: .leading, horizontalSpacing: 18, verticalSpacing: 8) {
                GridRow { telex("aa", "â"); telex("aw", "ă"); telex("ee", "ê"); telex("oo", "ô") }
                GridRow { telex("ow", "ơ"); telex("uw", "ư"); telex("dd", "đ"); telex("w", "ư") }
                GridRow { telex("s", "sắc"); telex("f", "huyền"); telex("r", "hỏi"); telex("x", "ngã") }
                GridRow { telex("j", "nặng"); telex("z", "xoá dấu"); Color.clear.frame(width: 1, height: 1); Color.clear.frame(width: 1, height: 1) }
            }
            .padding(14)
            .background(.quaternary.opacity(0.5), in: RoundedRectangle(cornerRadius: 10))
        }
    }

    private func telex(_ keys: String, _ result: String) -> some View {
        HStack(spacing: 6) {
            Text(keys).font(.system(.body, design: .monospaced).weight(.semibold))
                .padding(.horizontal, 6).padding(.vertical, 2)
                .background(.background, in: RoundedRectangle(cornerRadius: 5))
            Image(systemName: "arrow.right").font(.caption).foregroundStyle(.tertiary)
            Text(result)
        }
    }

    private var permission: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Cho phép Vikey đọc phím gõ").font(.title2.weight(.semibold))
            Text("Vikey cần quyền Trợ năng (Accessibility) để biến phím bạn gõ thành chữ tiếng Việt. Mọi thứ xử lý ngay trên máy, không gửi đi đâu.")
                .foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            VStack(alignment: .leading, spacing: 8) {
                Label("Bấm nút bên dưới để mở Cài đặt hệ thống", systemImage: "1.circle.fill")
                Label("Bật Vikey trong danh sách Trợ năng", systemImage: "2.circle.fill")
                Label("Quay lại đây, Vikey sẽ tự bắt đầu", systemImage: "3.circle.fill")
            }
            .foregroundStyle(.secondary)
            HStack {
                Button("Mở Cài đặt hệ thống") {
                    VikeyCore.requestAccessibility()
                    if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility") {
                        NSWorkspace.shared.open(url)
                    }
                }
                .buttonStyle(.borderedProminent).controlSize(.large)
                Spacer()
            }
        }
    }

    private var hotkeyStep: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Phím chuyển Anh/Việt").font(.title2.weight(.semibold))
            Text("Vikey tự chuyển khi bạn gõ lẫn tiếng Anh. Khi muốn tự chuyển, bấm phím tắt này. Bấm vào ô để đổi phím.")
                .foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            HStack(spacing: 12) {
                HotkeyRecorder(raw: Binding(get: { hotkeyRaw }, set: { k in
                    guard emojiOn == 0 || !Hotkey.sameKeys(k, emojiKey) else { return }   // already the emoji picker's (only while it's on)
                    hotkeyRaw = k; Preferences.apply()
                }))
                    .controlSize(.large)
                Text(state.language == 1 ? "Đang gõ: Tiếng Việt" : "Đang gõ: English").foregroundStyle(.secondary)
            }
            Toggle(isOn: Binding(
                get: { Hotkey(raw: hotkeyRaw).beep },
                set: { var h = Hotkey(raw: hotkeyRaw); h.beep = $0; hotkeyRaw = h.raw; Preferences.apply() })) {
                Text("Âm báo khi chuyển")
            }
            .toggleStyle(.switch)
        }
    }

    private var emojiStep: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(spacing: 8) {
                Text("Bảng emoji").font(.title2.weight(.semibold))
                Text("không bắt buộc").font(.callout).foregroundStyle(.secondary)
            }
            Text("Bấm một phím tắt để mở bảng emoji ở giữa màn hình. Tìm bằng tiếng Việt hay tiếng Anh đều được: \"ngựa\" hay \"horse\" đều ra 🐴.")
                .foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            Toggle(isOn: Binding(get: { emojiOn != 0 }, set: { on in
                if on && Hotkey.sameKeys(emojiKey, hotkeyRaw) { emojiKey = 0 }   // a collision left from before: record it again
                emojiOn = on ? 1 : 0; Preferences.apply()
            })) {
                Text("Bật bảng emoji")
            }
            .toggleStyle(.switch)
            if emojiOn != 0 {
                HStack(spacing: 12) {
                    Text("Phím mở bảng emoji")
                    HotkeyRecorder(raw: Binding(get: { emojiKey }, set: { k in
                        guard !Hotkey.sameKeys(k, hotkeyRaw) else { return }
                        emojiKey = k & ~0x8000; Preferences.apply()
                    }), allowModifiersOnly: false, autoStart: emojiKey == 0)
                    .controlSize(.large)
                }
                if emojiKey == 0 { Text("Bấm một phím kèm ⌃, ⌥ hoặc ⌘, ví dụ ⌃⌘E.").font(.callout).foregroundStyle(.secondary) }
            }
        }
    }
}

/// Type a few of the website's demo sentences; each turns green when it comes out right.
private struct PracticeStep: View {
    let hotkey: String
    let vietnamese: Bool
    private static let lines: [(keys: String, result: String)] = [
        ("order traf suwax nhes", "order trà sữa nhé"),
        ("meeting lucs ba giowf nhes", "meeting lúc ba giờ nhé"),
        ("thanks, camr own nhieeuf", "thanks, cảm ơn nhiều"),
    ]
    @State private var typed = ["", "", ""]
    @FocusState private var focus: Int?

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Gõ thử").font(.title2.weight(.semibold))
            Text(vietnamese ? "Gõ đúng các phím bên dưới, kể cả tiếng Anh. Vikey bỏ dấu cho tiếng Việt và để nguyên tiếng Anh."
                            : "Bạn đang ở tiếng Anh: bấm \(hotkey) để sang tiếng Việt, rồi gõ các phím bên dưới.")
                .foregroundStyle(.secondary)
                .lineLimit(2, reservesSpace: true)   // both texts take two lines: switching VI/EN must not move the fields (Tony)
            ForEach(0..<Self.lines.count, id: \.self) { i in
                let ok = Self.normalize(typed[i]) == Self.normalize(Self.lines[i].result)
                VStack(alignment: .leading, spacing: 4) {
                    HStack(spacing: 6) {
                        Text("Gõ").foregroundStyle(.secondary)
                        Text(Self.lines[i].keys).font(.system(.body, design: .monospaced).weight(.semibold))
                        Image(systemName: "arrow.right").font(.caption).foregroundStyle(.tertiary)
                        Text(Self.lines[i].result)
                    }
                    .font(.callout)
                    HStack {
                        TextField("", text: $typed[i])
                            .textFieldStyle(.roundedBorder)
                            .font(.system(size: 15))
                            .focused($focus, equals: i)
                            .onSubmit { VikeyCore.resetTyping(); focus = min(i + 1, Self.lines.count - 1) }
                        Image(systemName: ok ? "checkmark.circle.fill" : "circle")
                            .foregroundStyle(ok ? Color.green : Color.secondary.opacity(0.4))
                            .font(.title3)
                    }
                }
                .onChange(of: ok) { done in
                    guard done && i + 1 < Self.lines.count else { return }
                    VikeyCore.resetTyping()   // the next field starts a new word, as after a click
                    focus = i + 1
                }
            }
        }
        .onAppear { DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { focus = 0 } }
    }

    private static func normalize(_ s: String) -> String {
        s.trimmingCharacters(in: .whitespaces).precomposedStringWithCanonicalMapping.lowercased()
    }
}
