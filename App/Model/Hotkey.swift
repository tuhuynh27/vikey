import AppKit

/// The switch hotkey, stored in the core's format: low byte = key code (0xFE = modifiers only),
/// 0x100 ⌃, 0x200 ⌥, 0x400 ⌘, 0x800 ⇧, 0x8000 = beep on switch.
struct Hotkey: Equatable {
    static let defaultValue = 0x7A000206   // ⌥Z
    static let noKey = 0xFE

    var raw: Int

    var keyCode: Int { raw & 0xFF }
    var control: Bool { raw & 0x100 != 0 }
    var option: Bool { raw & 0x200 != 0 }
    var command: Bool { raw & 0x400 != 0 }
    var shift: Bool { raw & 0x800 != 0 }
    var beep: Bool {
        get { raw & 0x8000 != 0 }
        set { raw = newValue ? raw | 0x8000 : raw & ~0x8000 }
    }

    init(raw: Int) { self.raw = raw }

    /// Same key and modifiers: the beep bit and the default's legacy high bits (0x7A000206) don't count.
    static func sameKeys(_ a: Int, _ b: Int) -> Bool { (a & 0xFFF) == (b & 0xFFF) }

    init(keyCode: Int, modifiers: NSEvent.ModifierFlags, beep: Bool) {
        var r = keyCode & 0xFF
        if modifiers.contains(.control) { r |= 0x100 }
        if modifiers.contains(.option) { r |= 0x200 }
        if modifiers.contains(.command) { r |= 0x400 }
        if modifiers.contains(.shift) { r |= 0x800 }
        if beep { r |= 0x8000 }
        raw = r
    }

    var display: String {
        var s = ""
        if control { s += "⌃" }
        if option { s += "⌥" }
        if shift { s += "⇧" }
        if command { s += "⌘" }
        if keyCode != Hotkey.noKey { s += Hotkey.keyName(keyCode) }
        return s.isEmpty ? "—" : s
    }

    static func keyName(_ code: Int) -> String {
        let names: [Int: String] = [
            0: "A", 1: "S", 2: "D", 3: "F", 4: "H", 5: "G", 6: "Z", 7: "X", 8: "C", 9: "V", 11: "B", 12: "Q", 13: "W",
            14: "E", 15: "R", 16: "Y", 17: "T", 18: "1", 19: "2", 20: "3", 21: "4", 22: "6", 23: "5", 24: "=", 25: "9",
            26: "7", 27: "-", 28: "8", 29: "0", 30: "]", 31: "O", 32: "U", 33: "[", 34: "I", 35: "P", 36: "↩", 37: "L",
            38: "J", 39: "'", 40: "K", 41: ";", 42: "\\", 43: ",", 44: "/", 45: "N", 46: "M", 47: ".", 48: "⇥", 49: "Space",
            50: "`", 51: "⌫", 53: "⎋", 122: "F1", 120: "F2", 99: "F3", 118: "F4", 96: "F5", 97: "F6", 98: "F7", 100: "F8",
            101: "F9", 109: "F10", 103: "F11", 111: "F12",
        ]
        return names[code] ?? "#\(code)"
    }
}
