import Foundation

/// First launch of Vikey on a Mac that used OpenKey: bring over the settings, the per-app/per-site
/// language memory, macros and the typing log, so nothing has to be set up again.
enum Migration {
    static func fromOpenKeyIfNeeded() {
        Preferences.register()
        let defaults = UserDefaults.standard
        telexStyleFromOpenKeyIfNeeded()
        clearInvalidEmojiHotkey()
        guard !defaults.bool(forKey: "MigratedFromOpenKey") else { return }
        defaults.set(true, forKey: "MigratedFromOpenKey")
        guard let old = UserDefaults(suiteName: "com.tuyenmai.openkey") else { return }
        let keys = ["InputMethod", PrefKey.spelling, PrefKey.restoreIfInvalid, PrefKey.modernOrthography, PrefKey.useMacro,
                    PrefKey.macroInEnglish, PrefKey.autoCapsMacro, PrefKey.smartSwitch, PrefKey.smartSwitchPerSite,
                    PrefKey.autoSwitch, PrefKey.typingLogKeys, PrefKey.hotkey, "macroData", "smartSwitchKey", "TypingLogStats"]
        for key in keys {
            if let value = old.object(forKey: key) { defaults.set(value, forKey: key) }
        }
        // typing log file
        let fm = FileManager.default
        if let support = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask).first {
            let from = support.appendingPathComponent("OpenKey/typing-log.jsonl")
            let toDir = support.appendingPathComponent("Vikey")
            let to = toDir.appendingPathComponent("typing-log.jsonl")
            if fm.fileExists(atPath: from.path), !fm.fileExists(atPath: to.path) {
                try? fm.createDirectory(at: toDir, withIntermediateDirectories: true)
                try? fm.copyItem(at: from, to: to)
            }
        }
    }

    /// An emoji hotkey saved before the recorder's rules (no ⌃/⌥/⌘, e.g. ⇧E, or the switch hotkey's keys) would eat
    /// typing or the switch: cleared, so the settings ask for a new one.
    private static func clearInvalidEmojiHotkey() {
        let defaults = UserDefaults.standard
        let k = defaults.integer(forKey: PrefEmoji.hotkey)
        guard k != 0, k & 0x700 == 0 || Hotkey.sameKeys(k, defaults.integer(forKey: PrefKey.hotkey)) else { return }
        defaults.set(0, forKey: PrefEmoji.hotkey)
    }

    /// OpenKey's input type 2 / 3 were Simple Telex 1 / 2 (D56; VNI, 1, isn't in Vikey). Its own one-time step, so people
    /// who moved from OpenKey before Vikey had the setting get it too; a style already chosen in Vikey stays.
    private static func telexStyleFromOpenKeyIfNeeded() {
        let defaults = UserDefaults.standard
        guard !defaults.bool(forKey: "MigratedTelexStyle") else { return }
        let chosen = defaults.persistentDomain(forName: Bundle.main.bundleIdentifier ?? "")?[PrefKey.telexStyle] != nil
        if chosen { defaults.set(true, forKey: "MigratedTelexStyle"); return }
        // done only once OpenKey's setting was read: if it can't be read now, the next launch tries again
        guard let old = UserDefaults(suiteName: "com.tuyenmai.openkey"), let t = old.object(forKey: "InputType") as? Int else { return }
        if t == 2 || t == 3 { defaults.set(t - 1, forKey: PrefKey.telexStyle) }
        defaults.set(true, forKey: "MigratedTelexStyle")
    }
}
