import SwiftUI

/// Click, then press the shortcut: a key with modifiers (⌥Z) or modifiers alone (⌃⇧).
struct HotkeyRecorder: View {
    @Binding var raw: Int
    var allowModifiersOnly = true   // the emoji picker needs a real key
    var autoStart = false           // start recording as soon as it appears (first time the feature is turned on)
    @State private var recording = false
    @State private var monitor: Any?
    @State private var pendingModifiers: NSEvent.ModifierFlags = []

    var body: some View {
        Button {
            recording ? stop() : start()
        } label: {
            Text(recording ? "Bấm phím tắt…" : raw == 0 ? "Chưa đặt" : Hotkey(raw: raw).display)
                .font(.system(.body, design: .rounded).weight(.medium))
                .frame(minWidth: 110)
        }
        .onDisappear { stop() }
        .onAppear { if autoStart && !recording { start() } }
    }

    private func start() {
        recording = true
        pendingModifiers = []
        monitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .flagsChanged]) { event in
            let mods = event.modifierFlags.intersection([.control, .option, .command, .shift])
            let beep = Hotkey(raw: raw).beep
            if event.type == .keyDown {
                if event.keyCode == 53 && mods.isEmpty { stop(); return nil }   // Esc cancels
                if allowModifiersOnly ? !mods.isEmpty : !mods.isDisjoint(with: [.control, .option, .command]) {   // emoji: ⇧ alone isn't enough, ⇧E would take every capital E
                    raw = Hotkey(keyCode: Int(VikeyCore.hotkeyKeyCode(for: event)), modifiers: mods, beep: beep).raw
                    stop()
                }
                return nil
            }
            // modifiers only: record when they are released, if at least two were held
            if mods.isEmpty {
                if allowModifiersOnly && pendingModifiers.rawValue != 0 && [NSEvent.ModifierFlags.control, .option, .command, .shift].filter({ pendingModifiers.contains($0) }).count >= 2 {
                    raw = Hotkey(keyCode: Hotkey.noKey, modifiers: pendingModifiers, beep: beep).raw
                    stop()
                }
                pendingModifiers = []   // all let go: a refused ⇧E (or a lone ⇧) doesn't add ⇧ to the next modifiers
            } else if mods.rawValue > pendingModifiers.rawValue || !pendingModifiers.isSuperset(of: mods) {
                pendingModifiers = pendingModifiers.union(mods)
            }
            return nil
        }
    }

    private func stop() {
        recording = false
        if let m = monitor { NSEvent.removeMonitor(m) }
        monitor = nil
    }
}
