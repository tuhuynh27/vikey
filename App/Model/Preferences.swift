import Foundation
import ServiceManagement

/// UserDefaults keys shared with the keyboard core (VikeyCore.mm reads them in VKLoadSettings).
enum PrefKey {
    static let spelling = "Spelling"
    static let restoreIfInvalid = "RestoreIfInvalidWord"
    static let modernOrthography = "ModernOrthography"
    static let useMacro = "UseMacro"
    static let macroInEnglish = "UseMacroInEnglishMode"
    static let autoCapsMacro = "vAutoCapsMacro"
    static let smartSwitch = "UseSmartSwitchKey"
    static let smartSwitchPerSite = "SmartSwitchPerSite"
    static let autoSwitch = "AutoSwitchByTyping"
    static let pauseAfterManual = "PauseAutoAfterManual"
    static let typingLogKeys = "TypingLogKeys"
    static let typingLogEnabled = "TypingLogEnabled"
    static let hotkey = "SwitchKeyStatus"
    static let teencode = "Teencode"
    static let telexStyle = "TelexStyle"   // D56: 0 Telex, 1 Simple Telex 1, 2 Simple Telex 2
    static let excludedApps = "ExcludedApps"   // bundle ids Vikey stays out of (array of String)
    static let mainTab = "MainWindowTab"   // the main window's tab
}

/// Defaults every setting starts with (registered so @AppStorage and the core agree).
enum Preferences {
    static func register() {
        UserDefaults.standard.register(defaults: [
            PrefKey.spelling: 1, PrefKey.restoreIfInvalid: 1, PrefKey.modernOrthography: 1,
            PrefKey.useMacro: 1, PrefKey.macroInEnglish: 1, PrefKey.autoCapsMacro: 0,
            PrefKey.smartSwitch: 1, PrefKey.smartSwitchPerSite: false, PrefKey.autoSwitch: 1, PrefKey.pauseAfterManual: 1,
            PrefKey.typingLogKeys: 1, PrefKey.typingLogEnabled: 0, PrefKey.hotkey: Hotkey.defaultValue,
            PrefKey.teencode: 1, PrefKey.telexStyle: 0, PrefKey.mainTab: "general",
            PrefKey.excludedApps: ExcludedApps.defaults,
        ])
    }

    /// Call after changing a setting so the keyboard core picks it up.
    static func apply() { VikeyCore.reloadSettings() }
}

enum LaunchAtLogin {
    static var isEnabled: Bool { SMAppService.mainApp.status == .enabled }
    static func set(_ on: Bool) {
        do {
            if on { try SMAppService.mainApp.register() } else { try SMAppService.mainApp.unregister() }
        } catch {
            NSLog("Vikey: launch at login change failed: \(error)")
        }
    }
}
