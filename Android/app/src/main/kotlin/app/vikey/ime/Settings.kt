package app.vikey.ime

import android.content.Context
import android.content.SharedPreferences

/** Settings the app writes and the keyboard reads (same keys as the Mac and iOS apps). */
object Settings {
    const val AUTO_SWITCH = "AutoSwitchByTyping"
    const val PAUSE_AFTER_MANUAL = "PauseAutoAfterManual"
    const val MODERN_ORTHOGRAPHY = "ModernOrthography"
    const val TELEX_STYLE = "TelexStyle"   // Int: 0 Telex, 1 Simple Telex 1, 2 Simple Telex 2 (D56)
    const val SPELLING = "Spelling"
    const val RESTORE_IF_INVALID = "RestoreIfInvalidWord"
    const val ENGLISH_FOR_WEB_FIELDS = "EnglishForWebFields"
    const val AUTO_CAPITALIZE = "AutoCapitalize"
    const val DOUBLE_SPACE_PERIOD = "DoubleSpacePeriod"
    const val HAPTICS = "Haptics"
    const val DIAGNOSTICS = "KeyboardDiagnostics"
    const val SUGGESTIONS = "Suggestions"
    const val AUTO_CORRECT = "AutoCorrect"
    const val LANGUAGE = "Language"
    const val DID_ONBOARD = "didOnboard"
    const val EMOJI_TONE = "emojiTone"

    val defaults = mapOf(
        AUTO_SWITCH to true, PAUSE_AFTER_MANUAL to true, MODERN_ORTHOGRAPHY to true, SPELLING to true,
        RESTORE_IF_INVALID to true, ENGLISH_FOR_WEB_FIELDS to true, AUTO_CAPITALIZE to true, DOUBLE_SPACE_PERIOD to true,
        HAPTICS to true, DIAGNOSTICS to false, SUGGESTIONS to true, AUTO_CORRECT to true,
    )

    fun prefs(c: Context): SharedPreferences = c.getSharedPreferences("vikey", Context.MODE_PRIVATE)
    fun bool(c: Context, key: String) = prefs(c).getBoolean(key, defaults[key] ?: false)
}
