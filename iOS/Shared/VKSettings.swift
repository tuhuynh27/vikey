import Foundation

/// Settings the host app writes and the keyboard reads (shared app group; the keyboard only reads).
enum VKSettings {
    static let group = "group.com.devuap.vikey"
    /// Reads of the shared defaults; the performance tests check typing never reads them.
    static var reads = 0
    static var defaults: UserDefaults { reads += 1; return UserDefaults(suiteName: group) ?? .standard }

    enum Key {
        static let autoSwitch = "AutoSwitchByTyping"
        static let pauseAfterManual = "PauseAutoAfterManual"
        static let modernOrthography = "ModernOrthography"
        static let telexStyle = "TelexStyle"   // D56: 0 Telex, 1 Simple Telex 1, 2 Simple Telex 2
        static let spelling = "Spelling"
        static let restoreIfInvalid = "RestoreIfInvalidWord"
        static let englishForWebFields = "EnglishForWebFields"
        static let autoCapitalize = "AutoCapitalize"
        static let doubleSpacePeriod = "DoubleSpacePeriod"
        static let haptics = "Haptics"
        static let diagnostics = "KeyboardDiagnostics"
        static let suggestions = "Suggestions"
        static let autoCorrect = "AutoCorrect"
        /// The app's secret debug menu: "review:<time>" or "bug:<time>" shows that nudge the next time the keyboard opens.
        static let debugNudge = "DebugNudge"
    }

    static let defaultValues: [String: Any] = [
        Key.autoSwitch: true, Key.pauseAfterManual: true, Key.modernOrthography: true, Key.telexStyle: 0, Key.spelling: true,
        Key.restoreIfInvalid: true, Key.englishForWebFields: true,
        Key.autoCapitalize: true, Key.doubleSpacePeriod: true, Key.haptics: true, Key.diagnostics: false,
        Key.suggestions: true, Key.autoCorrect: true,
    ]

    static func register() { defaults.register(defaults: defaultValues) }
    static func bool(_ key: String) -> Bool { register(); return defaults.bool(forKey: key) }
}
