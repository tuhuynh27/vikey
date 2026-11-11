package app.vikey.ime

import android.content.res.Configuration
import android.inputmethodservice.InputMethodService
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.UserDictionary
import android.text.InputType
import android.view.Choreographer
import android.view.View
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.view.inputmethod.InputMethodManager
import android.widget.FrameLayout
import app.vikey.BuildConfig
import app.vikey.R
import java.text.BreakIterator
import kotlin.math.abs

/**
 * The Vikey keyboard: keys go through the same engine and driver as the Mac app (Engine/Driver),
 * and its edits are applied to the text field. Port of iOS/Keyboard/KeyboardViewController.swift.
 */
class VikeyIME : InputMethodService(), KeyboardView.Listener, EmojiKeyboardView.Listener {
    private lateinit var driver: Driver
    private lateinit var root: FrameLayout
    private lateinit var keyboard: KeyboardView
    private var emojiView: EmojiKeyboardView? = null
    /** The user's personal-dictionary shortcuts (Settings › Languages & input › Personal dictionary), like the
     *  system keyboard and iOS Text Replacements: "omw" → "On my way!". Only the current keyboard may read them. */
    private var textReplacements: Map<String, String> = emptyMap()
    private var emojiQuery = ""
    /** Our own copy of the text before the cursor, kept up to date from our edits (asking the app is an IPC). */
    private var lastContext: String? = null
    private var cursorPos = -1                      // where we think the cursor is (UTF-16 units)
    private var selLength = 0                       // characters selected after the cursor
    private val recentContexts = ArrayList<String>()
    private var lastShiftTap = 0.0
    private var lastSpace = 0.0
    private var fieldLanguage: Int? = null          // language to restore after a web/email field
    private var wordPrefix = StringBuilder()
    private val wordXs = ArrayList<Float>(); private val wordYs = ArrayList<Float>()
    private var wordCaps = 0
    private var candidates: List<Candidate> = emptyList()
    private var willCorrect = false
    private var pendingFix: Pair<String, Boolean>? = null       // text, vietnamese
    private var lastCorrection: Pair<String, String>? = null    // typed, inserted
    private var keepWord: String? = null
    private var webFieldsEnglish = true; private var autoCapitalize = true; private var doubleSpacePeriod = true; private var haptics = true
    private var suggestionsOn = true; private var autoCorrectOn = true
    private var inputType = 0; private var imeOptions = 0
    private val handler = Handler(Looper.getMainLooper())
    private val ic: InputConnection? get() = currentInputConnection

    /** The keyboard speaks the phone's language, not the app's per-app language (Android 13+), like the iOS keyboard
     *  extension, which a per-app language never reaches (otherwise it would depend on whether this process restarted). */
    private fun str(id: Int): String {
        val cfg = Configuration(resources.configuration); cfg.setLocales(android.content.res.Resources.getSystem().configuration.locales)
        return createConfigurationContext(cfg).getString(id)
    }

    override fun onCreate() {
        super.onCreate()
        driver = Driver()
        KBLog.init(this)
        if (BuildConfig.DEBUG) registerReceiver(debugReceiver, android.content.IntentFilter("app.vikey.DEBUG"), RECEIVER_EXPORTED)
        driver.language = Settings.prefs(this).getInt(Settings.LANGUAGE, 1)
        Thread { EmojiIndex.get(this) }.start()   // the emoji keyboard opens instantly
        loadTextReplacements()
    }
    override fun onDestroy() { if (BuildConfig.DEBUG) unregisterReceiver(debugReceiver); driver.close(); super.onDestroy() }

    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        if (Diag.on && level >= TRIM_MEMORY_RUNNING_LOW) { KBLog.add("memory warning (level $level)"); KBLog.flush() }
    }

    /** Reads the personal dictionary off the main thread; shortcuts of ASCII letters only, like iOS. */
    private fun loadTextReplacements() {
        Thread {
            val r = HashMap<String, String>()
            try {
                contentResolver.query(UserDictionary.Words.CONTENT_URI, arrayOf(UserDictionary.Words.SHORTCUT, UserDictionary.Words.WORD),
                    "${UserDictionary.Words.SHORTCUT} IS NOT NULL AND ${UserDictionary.Words.SHORTCUT} != ''", null, null)?.use { c ->
                    while (c.moveToNext()) {
                        val short = c.getString(0) ?: continue; val word = c.getString(1) ?: continue
                        if (short.isNotEmpty() && short.all { it.isLetter() && it.code < 128 } && !short.equals(word, ignoreCase = true)) r[short.lowercase()] = word
                    }
                }
            } catch (_: Exception) {}   // no dictionary provider (some OEMs): no shortcuts
            handler.post { textReplacements = r }
        }.start()
    }

    /** Debug builds only: the live test (Android/Tests/livetest.py) sets the language quietly and toggles settings.
     *  adb shell am broadcast -a app.vikey.DEBUG -p app.vikey --ei lang 1 --ez autocap false */
    private val debugReceiver = object : android.content.BroadcastReceiver() {
        override fun onReceive(c: android.content.Context, i: android.content.Intent) {
            if (i.hasExtra("lang")) { driver.language = i.getIntExtra("lang", 1); driver.resetContext(); clearWord(); if (::keyboard.isInitialized) refresh() }
            if (i.getBooleanExtra("letters", false) && ::keyboard.isInitialized) { keyboard.layer = KeyboardView.Layer.LETTERS; updateAutoShift(lastContext ?: "") }
            if (i.hasExtra("replace")) {   // "omw=On my way!;brb=Be right back" (tests)
                textReplacements = i.getStringExtra("replace")!!.split(";").filter { "=" in it }.associate { val (k, v) = it.split("=", limit = 2); k.lowercase() to v }
            }
            if (i.hasExtra("autocap")) { Settings.prefs(c).edit().putBoolean(Settings.AUTO_CAPITALIZE, i.getBooleanExtra("autocap", true)).commit(); if (::keyboard.isInitialized) { loadSettings(); updateAutoShift(lastContext ?: "") } }
        }
    }

    override fun onCreateInputView(): View {
        KeyboardView.searchTop = null
        root = FrameLayout(this)
        root.setBackgroundColor(if (isDark) 0xFF2B2B2D.toInt() else 0xFFD1D4DA.toInt())   // the system keyboard's background
        root.setOnApplyWindowInsetsListener { v, insets ->   // keep the keys above the gesture bar
            // gesture nav: the IME's own back/switcher strip comes as captionBar, taller than the navigationBars handle
            val b = if (Build.VERSION.SDK_INT >= 30) insets.getInsets(android.view.WindowInsets.Type.navigationBars() or android.view.WindowInsets.Type.captionBar()).bottom else @Suppress("DEPRECATION") insets.systemWindowInsetBottom
            v.setPadding(0, 0, 0, b); insets
        }
        keyboard = KeyboardView(this)
        keyboard.listener = this
        // a password types exactly the key pressed: no guessing the letter from the word before it
        keyboard.wordContext = { if (isPasswordField) null else wordPrefix.toString() to (driver.language == 1) }
        root.addView(keyboard, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, heightPx()))
        emojiView = null
        return root
    }

    override fun onEvaluateFullscreenMode() = false

    private val compact get() = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE && resources.configuration.smallestScreenWidthDp < 600
    private fun heightPx() = (KeyboardView.keyboardHeight(compact) * resources.displayMetrics.density).toInt()
    private fun applyHeight() {
        keyboard.layoutParams = keyboard.layoutParams.apply { height = heightPx() }
        keyboard.relayout(); root.requestLayout()
    }

    override fun onStartInputView(info: EditorInfo, restarting: Boolean) {
        super.onStartInputView(info, restarting)
        emojiView?.let { if (it.visibility == View.VISIBLE) emojiClose() }
        if (KeyboardView.searchTop != null && emojiView?.searching != true) { KeyboardView.searchTop = null; applyHeight() }
        loadSettings()
        startField(info)
        refresh()
        if (!restarting) loadTextReplacements()   // the user may have edited the personal dictionary
        if (Diag.on) startStallWatch()
        if (Diag.on) KBLog.add("open: haptics=${keyboard.haptics} inputType=${info.inputType} ime=${info.imeOptions} width=${resources.configuration.screenWidthDp}")
    }

    override fun onFinishInputView(finishingInput: Boolean) { stopStallWatch(); KBLog.flush(); super.onFinishInputView(finishingInput) }

    /** Debug log only: any main-thread freeze over 50 ms, and the key it followed (port of the iOS CADisplayLink watch). */
    private var stallWatching = false
    private var lastFrame = 0L
    private val frameTick = object : Choreographer.FrameCallback {
        override fun doFrame(t: Long) {
            if (!stallWatching) return
            val gap = if (lastFrame == 0L) 0.0 else (t - lastFrame) / 1e6
            lastFrame = t
            if (gap > 50) KBLog.add("stall ${Diag.time(gap)} after key=${Diag.lastKind}")
            Choreographer.getInstance().postFrameCallback(this)
        }
    }
    private fun startStallWatch() { if (stallWatching) return; stallWatching = true; lastFrame = 0; Choreographer.getInstance().postFrameCallback(frameTick) }
    private fun stopStallWatch() { stallWatching = false; Choreographer.getInstance().removeFrameCallback(frameTick) }

    override fun onConfigurationChanged(c: Configuration) {
        super.onConfigurationChanged(c)
        if (::keyboard.isInitialized) { applyHeight(); refresh() }
    }

    private fun loadSettings() {
        val p = Settings.prefs(this)
        fun b(k: String) = p.getBoolean(k, Settings.defaults[k] ?: false)
        driver.autoSwitch = b(Settings.AUTO_SWITCH)
        driver.pauseAfterManual = if (b(Settings.PAUSE_AFTER_MANUAL)) 5.0 else 0.0
        driver.modernOrthography = b(Settings.MODERN_ORTHOGRAPHY)
        driver.telexStyle = p.getInt(Settings.TELEX_STYLE, 0)
        driver.spelling = b(Settings.SPELLING)
        driver.restoreIfInvalid = b(Settings.RESTORE_IF_INVALID)
        driver.applySettings()
        webFieldsEnglish = b(Settings.ENGLISH_FOR_WEB_FIELDS)
        autoCapitalize = b(Settings.AUTO_CAPITALIZE)
        doubleSpacePeriod = b(Settings.DOUBLE_SPACE_PERIOD)
        haptics = b(Settings.HAPTICS)
        suggestionsOn = b(Settings.SUGGESTIONS)
        autoCorrectOn = b(Settings.AUTO_CORRECT)
        KeyboardView.barOn = suggestionsOn || autoCorrectOn
        Diag.on = b(Settings.DIAGNOSTICS)
        keyboard.diagnostics = if (Diag.on) str(R.string.diag_hint) else null
        keyboard.haptics = haptics
        keyboard.showsGlobe = if (Build.VERSION.SDK_INT >= 28) shouldOfferSwitchingToNextInputMethod() else true
        applyHeight()
    }

    private val isWebField: Boolean get() {
        if (inputType and InputType.TYPE_MASK_CLASS != InputType.TYPE_CLASS_TEXT) return false
        val v = inputType and InputType.TYPE_MASK_VARIATION
        return v == InputType.TYPE_TEXT_VARIATION_URI || v == InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS || v == InputType.TYPE_TEXT_VARIATION_WEB_EMAIL_ADDRESS
    }

    /** Passwords go in exactly as pressed: no Telex, no auto switch (English mode switches too), no smart punctuation. */
    private val isPasswordField: Boolean get() {
        val v = inputType and InputType.TYPE_MASK_VARIATION
        return when (inputType and InputType.TYPE_MASK_CLASS) {
            InputType.TYPE_CLASS_TEXT -> v == InputType.TYPE_TEXT_VARIATION_PASSWORD || v == InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD || v == InputType.TYPE_TEXT_VARIATION_WEB_PASSWORD
            InputType.TYPE_CLASS_NUMBER -> v == InputType.TYPE_NUMBER_VARIATION_PASSWORD
            else -> false
        }
    }

    /** A new text field: start fresh; web addresses, email and passwords start in English (and go back after). */
    private fun startField(info: EditorInfo) {
        inputType = info.inputType; imeOptions = info.imeOptions
        driver.resetContext()
        if (isPasswordField || (isWebField && webFieldsEnglish)) {
            if (fieldLanguage == null) fieldLanguage = driver.language
            driver.language = 0
        } else fieldLanguage?.let { driver.language = it; fieldLanguage = null }
        readContext()
        val known = info.initialSelStart >= 0 && info.initialSelEnd >= 0   // -1 when the editor doesn't know
        cursorPos = if (known) minOf(info.initialSelStart, info.initialSelEnd) else -1
        selLength = if (known) abs(info.initialSelEnd - info.initialSelStart) else 0   // start > end when selected backwards
        recentPositions.clear()   // the last field's positions mean nothing here
        lastCorrection = null; keepWord = null   // nor its auto-correct: Backspace here deletes, it doesn't undo an old fix
        clearWord()
        updateAutoShift(lastContext ?: "")
    }

    private fun readContext() {
        val t0 = SystemClock.elapsedRealtimeNanos()
        lastContext = ic?.getTextBeforeCursor(2000, 0)?.toString()
        Diag.readMs = (SystemClock.elapsedRealtimeNanos() - t0) / 1e6
    }

    /** Auto-correct and suggestions only where the system keyboard offers them. */
    private val correctsHere: Boolean get() {
        if (inputType and InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS != 0) return false
        val cls = inputType and InputType.TYPE_MASK_CLASS
        if (cls != InputType.TYPE_CLASS_TEXT) return false
        return !(isWebField || isPasswordField)
    }

    private val now get() = SystemClock.uptimeMillis() / 1000.0

    private val isDark get() = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK == Configuration.UI_MODE_NIGHT_YES
    private fun refresh() {
        keyboard.dark = isDark
        root.setBackgroundColor(if (isDark) 0xFF2B2B2D.toInt() else 0xFFD1D4DA.toInt())
        keyboard.languageTitle = if (driver.language == 1) "Tiếng Việt" else "English"
        keyboard.languageShort = if (driver.language == 1) "VI" else "EN"
        val (title, blue) = returnTitle()
        keyboard.returnTitle = title; keyboard.returnIsBlue = blue
        emojiView?.dark = keyboard.dark
        if (emojiView?.searching == true) updateEmojiQuery()
    }

    private val hasEnterAction get() = imeOptions and EditorInfo.IME_FLAG_NO_ENTER_ACTION == 0 && inputType and InputType.TYPE_TEXT_FLAG_MULTI_LINE == 0
    private fun returnTitle(): Pair<String?, Boolean> {
        if (!hasEnterAction) return null to false
        return when (imeOptions and EditorInfo.IME_MASK_ACTION) {
            EditorInfo.IME_ACTION_GO -> str(R.string.ret_go) to true
            EditorInfo.IME_ACTION_SEARCH -> str(R.string.ret_search) to true
            EditorInfo.IME_ACTION_SEND -> str(R.string.ret_send) to true
            EditorInfo.IME_ACTION_DONE -> str(R.string.ret_done) to true
            EditorInfo.IME_ACTION_NEXT -> str(R.string.ret_next) to false
            else -> null to false
        }
    }

    // MARK: text changes from outside (cursor moved, field cleared, another keyboard)

    override fun onUpdateSelection(oldStart: Int, oldEnd: Int, newStart: Int, newEnd: Int, candStart: Int, candEnd: Int) {
        super.onUpdateSelection(oldStart, oldEnd, newStart, newEnd, candStart, candEnd)
        if (!::keyboard.isInitialized) return
        selLength = abs(newEnd - newStart)   // start > end when selected backwards
        // the app may lag a few of our edits behind: a position we produced recently is not a move. It reports them
        // in order, so the ones before it are done with: a tap back onto one of them later is a move ("tô|i" + s → "tôsi")
        val i = if (newStart == newEnd) recentPositions.indexOf(newStart) else -1
        if (i >= 0) { recentPositions.subList(0, i + 1).clear(); return }
        if (newStart == newEnd && newStart == cursorPos) return   // the app caught up with our edit
        if (Diag.on) KBLog.add("reset: text changed outside the keyboard (cursor moved, or the app changed it)")
        driver.resetContext()
        clearWord()
        lastCorrection = null; keepWord = null
        recentContexts.clear(); recentPositions.clear()
        readContext()
        cursorPos = minOf(newStart, newEnd)
        updateAutoShift(lastContext ?: "")
        refresh()
    }
    private val recentPositions = ArrayList<Int>()

    // MARK: keys

    override fun keyboardDidTap(kind: KeyKind) {
        if (emojiView?.searching == true && emojiSearchKey(kind)) return
        if (Diag.on) Diag.lastKind = when (kind) { is KeyKind.Char -> if (kind.c[0].isLetter()) "letter" else "symbol"; KeyKind.Space -> "space"; KeyKind.Backspace -> "delete"; KeyKind.Return -> "return"; else -> "other" }
        when (kind) {
            is KeyKind.Char -> {
                val c = kind.c
                if (!isPasswordField && snapsBack(c)) {   // "this ." → "this." like the system keyboard (Tony); our own delete, so not a Backspace undo of an auto switch
                    ic?.deleteSurroundingText(1, 0); driver.resetContext(); afterEdit(1, "")
                }
                val before = lastContext ?: ""
                if (c == "-" && before.endsWith("-") && !before.endsWith("--") && smartDashesHere) {   // smart dashes: "--" → "—"
                    backspace(); clearWord(); type("—"); return
                }
                lastCorrection = null
                if (c in listOf(".", ",", "?", "!") && wordPrefix.isNotEmpty() && autoCorrectAtBreak(c)) {   // "dont?" → "don't?"
                    if (c != ",") updateAutoShift(lastContext ?: "")
                    return
                }
                val ch = c[0]
                if (ch.isLetter() && ch.code < 128 && wordPrefix.length < 30) {
                    wordPrefix.append(ch)
                    val t = keyboard.lastTapPoint ?: (Float.NaN to Float.NaN)
                    keyboard.lastTapPoint = null
                    wordXs.add(t.first); wordYs.add(t.second)
                    if (keyboard.layer == KeyboardView.Layer.LETTERS && keyboard.shift != KeyboardView.Shift.OFF) wordCaps++
                } else clearWord()
                var s = c
                if (keyboard.layer == KeyboardView.Layer.LETTERS && keyboard.shift != KeyboardView.Shift.OFF) s = c.uppercase()
                type(s)
                if (keyboard.shift == KeyboardView.Shift.ON) keyboard.shift = KeyboardView.Shift.OFF
                if (keyboard.layer != KeyboardView.Layer.LETTERS && c == "'") keyboard.layer = KeyboardView.Layer.LETTERS
            }
            KeyKind.Space -> { if (!autoCorrectAtBreak()) spaceKey(); clearWord() }
            KeyKind.Return -> {
                clearWord()
                if (hasEnterAction && (imeOptions and EditorInfo.IME_MASK_ACTION) != EditorInfo.IME_ACTION_NONE && (imeOptions and EditorInfo.IME_MASK_ACTION) != EditorInfo.IME_ACTION_UNSPECIFIED) {
                    if (!isPasswordField) {   // the word ends first, as on iOS ("server" was searched for as "sẻver")
                        val e = driver.typeCharacter("\n", now); apply(Edit(e.deleteCount, e.text.removeSuffix("\n"), e.languageChanged))
                    }
                    driver.resetContext(); ic?.performEditorAction(imeOptions and EditorInfo.IME_MASK_ACTION)
                } else type("\n")
            }
            KeyKind.Backspace -> {
                if (deleteSelection() || undoCorrection()) return
                backspace()
            }
            KeyKind.Shift -> {
                val t = now
                keyboard.shift = if (keyboard.shift == KeyboardView.Shift.LOCKED) KeyboardView.Shift.OFF
                    else if (t - lastShiftTap < 0.35) KeyboardView.Shift.LOCKED
                    else if (keyboard.shift == KeyboardView.Shift.ON) KeyboardView.Shift.OFF else KeyboardView.Shift.ON
                lastShiftTap = t
            }
            is KeyKind.Layer -> keyboard.layer = kind.l
            KeyKind.Language -> { lastCorrection = null; driver.manualSwitch(now); fieldLanguage = null; saveLanguage(); refresh() }
            KeyKind.Globe -> if (Build.VERSION.SDK_INT >= 28) switchToNextInputMethod(false) else keyboardGlobeHold()
            KeyKind.Emoji -> showEmoji()
        }
    }

    override fun keyboardGlobeHold() { (getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager).showInputMethodPicker() }

    override fun keyboardBackspaceRepeat(count: Int) {
        // like the system keyboard: characters first, whole words after ~2 seconds of holding
        if (deleteSelection()) return
        if (count <= 20) { backspace(); return }
        val before = lastContext ?: ""
        if (before.isEmpty()) return
        var end = before.length; var k = 0
        while (end > 0 && before[end - 1] == ' ') { end--; k++ }
        while (end > 0 && before[end - 1] != ' ' && before[end - 1] != '\n') { end--; k++ }
        k = maxOf(k, 1)
        ic?.deleteSurroundingText(k, 0)
        driver.resetContext()
        clearWord()
        afterEdit(k, "")
    }

    override fun keyboardInsertAlternate(text: String) {
        clearWord()
        type(text)   // a character the engine doesn't know passes through and starts a new word
        if (keyboard.shift == KeyboardView.Shift.ON) keyboard.shift = KeyboardView.Shift.OFF
    }

    override fun keyboardMoveCursor(by: Int) {
        val c = ic ?: return
        val pos = if (cursorPos >= 0) cursorPos else (c.getTextBeforeCursor(4000, 0)?.length ?: return)
        val np = maxOf(0, pos + by)
        c.setSelection(np, np); cursorPos = np; selLength = 0
    }

    override fun keyboardCursorModeEnded() {
        driver.resetContext()
        clearWord()
        lastCorrection = null; keepWord = null
        readContext()
        updateAutoShift(lastContext ?: "")
    }

    private fun spaceKey() {
        // double space: ". " like the system keyboard
        val before = lastContext ?: ""
        if (doubleSpacePeriod && !isPasswordField && now - lastSpace < 0.6 && before.endsWith(" ") && before.length >= 2 && before[before.length - 2].let { it.isLetterOrDigit() }) {
            val c = ic
            c?.beginBatchEdit(); c?.deleteSurroundingText(1, 0); c?.commitText(". ", 1); c?.endBatchEdit()
            driver.resetContext()
            lastSpace = 0.0
            afterEdit(1, ". ")
            return
        }
        type(" ")
        lastSpace = now
        if (keyboard.layer != KeyboardView.Layer.LETTERS) keyboard.layer = KeyboardView.Layer.LETTERS
    }

    private fun type(s: String) = apply(if (isPasswordField) Edit(0, s, false) else driver.typeCharacter(s, now))   // the engine never sees a password
    /** Delete with text selected removes the selection, like the system keyboard (deleteSurroundingText skips it). */
    private fun deleteSelection(): Boolean {
        if (selLength == 0) return false
        ic?.commitText("", 1); driver.resetContext(); clearWord(); afterEdit(0, "")
        return true
    }
    private fun backspace() {
        if (wordPrefix.isNotEmpty()) {   // every delete shortens the word, a held Delete too ("isnt", held back to "i", + t is "it")
            wordPrefix.setLength(wordPrefix.length - 1); wordXs.removeAt(wordXs.size - 1); wordYs.removeAt(wordYs.size - 1)
            wordCaps = minOf(wordCaps, wordPrefix.length)
        }
        val before = lastContext ?: ""
        if (before.isNotEmpty() && wordPrefix.isEmpty() && !before.last().isLetterOrDigit()) {   // emoji, symbols: delete one grapheme, not one code unit
            val n = lastGraphemeLength(before)
            if (n > 1) { ic?.deleteSurroundingText(n, 0); driver.resetContext(); afterEdit(n, ""); return }
        }
        apply(driver.backspace(now))
    }
    private fun lastGraphemeLength(s: String): Int {
        val tail = s.takeLast(40)
        val it = BreakIterator.getCharacterInstance(); it.setText(tail)
        val end = tail.length; val start = it.preceding(end)
        return if (start == BreakIterator.DONE) 1 else end - start
    }

    private fun apply(e: Edit) {
        // Send only what changes: "toi" + s comes back as delete "oi", insert "ói"; the "o" is kept.
        var deletes = e.deleteCount
        var insert = e.text
        val before = lastContext
        if (deletes > 0 && before != null && before.length >= deletes) {
            val old = before.takeLast(deletes)
            var same = 0
            while (same < old.length && same < insert.length && old[same] == insert[same]) same++
            deletes -= same
            insert = insert.substring(same)
        }
        val c = ic
        c?.beginBatchEdit()
        if (deletes > 0) c?.deleteSurroundingText(deletes, 0)
        if (insert.isNotEmpty()) c?.commitText(insert, 1)
        c?.endBatchEdit()
        Diag.edits += deletes + (if (insert.isEmpty()) 0 else 1)
        if (e.languageChanged) { saveLanguage(); refresh() }
        afterEdit(deletes, insert)
    }

    private fun afterEdit(deleted: Int, inserted: String) {
        selLength = 0   // our edit replaced any selection; onUpdateSelection may not have come yet
        var c = lastContext ?: ""
        c = c.dropLast(minOf(deleted, c.length)) + inserted
        if (c.length > 4000) c = c.takeLast(2000)
        lastContext = c
        if (cursorPos >= 0) { cursorPos = maxOf(0, cursorPos - deleted) + inserted.length; recentPositions.add(cursorPos); if (recentPositions.size > 12) recentPositions.removeAt(0) }
        updateAutoShift(c)
        scheduleSuggestions()
        if (Diag.on) {
            val end = SystemClock.uptimeMillis()
            val hold = maxOf(0L, Diag.touchUp - Diag.touchStart).toDouble(); val work = maxOf(0L, end - Diag.touchUp).toDouble()
            keyboard.diagnostics = "finger down ${Diag.time(hold)} · lift→text ${Diag.time(work)} · read ${Diag.time(Diag.readMs)}\nvibrations ${Diag.haptics} · touches ${Diag.touches} · edits ${Diag.edits}"
            KBLog.add("key=${Diag.lastKind} hold=${Diag.time(hold)} work=${Diag.time(work)} read=${Diag.time(Diag.readMs)} vib=${Diag.haptics} late=${Diag.late} delay=${Diag.time(Diag.delayMs)} touches=${Diag.touches} edits=${Diag.edits} suggest=${Diag.time(Diag.suggestMs)} lang=${driver.language}")
            Diag.haptics = 0; Diag.touches = 0; Diag.edits = 0; Diag.late = 0; Diag.suggestMs = 0.0; Diag.delayMs = 0.0; Diag.readMs = 0.0
        }
    }

    // MARK: suggestions and auto-correct

    private fun clearWord() {
        wordPrefix.setLength(0); wordXs.clear(); wordYs.clear(); wordCaps = 0
        candidates = emptyList(); willCorrect = false; pendingFix = null
        if (::keyboard.isInitialized) keyboard.suggestions = emptyList()
    }

    /** The word before the cursor as it shows in the field (after Telex: "tôi" for "tooi"). */
    private val shownWord: String get() { val c = lastContext ?: ""; var i = c.length; while (i > 0 && c[i - 1].isLetter()) i--; return c.substring(i) }

    /** Capitalised like the typed word ("Teh" → "The"). */
    private fun cased(s: String) = if (shownWord.firstOrNull()?.isUpperCase() == true) s.substring(0, 1).uppercase() + s.substring(1) else s

    private var suggestionsDirty = false
    private fun scheduleSuggestions() {
        if (suggestionsDirty) return
        suggestionsDirty = true
        handler.post { updateSuggestionsIfNeeded() }
    }
    private fun updateSuggestionsIfNeeded() {
        if (!suggestionsDirty) return
        val t0 = SystemClock.elapsedRealtimeNanos()
        updateSuggestions()
        Diag.suggestMs = maxOf(Diag.suggestMs, (SystemClock.elapsedRealtimeNanos() - t0) / 1e6)
    }

    private fun updateSuggestions() {
        suggestionsDirty = false
        if (!KeyboardView.barOn || !correctsHere || wordPrefix.isEmpty() || shownWord.isEmpty()) {
            if (candidates.isNotEmpty() || keyboard.suggestions.isNotEmpty()) { candidates = emptyList(); willCorrect = false; pendingFix = null; keyboard.suggestions = emptyList() }
            return
        }
        val typed = wordPrefix.toString().lowercase()
        val vi = driver.language == 1
        val rects = keyboard.keyRects
        if (rects.isEmpty) return
        val touched = wordXs.none { it.isNaN() }
        candidates = Driver.candidates(typed, if (touched) wordXs.toFloatArray() else null, if (touched) wordYs.toFloatArray() else null, vi, rects, 6)
        // never auto-correct acronyms / names typed with capitals inside, or a word put back by backspace
        willCorrect = autoCorrectOn && wordCaps < 2 && keepWord != typed && wordPrefix.length >= 3 && Driver.shouldCorrect(typed, candidates)
        pendingFix = if (willCorrect) candidates.firstOrNull()?.let { cased(it.text) to it.vietnamese } else null
        val shownNow = shownWord
        val typedCased = if (wordCaps == wordPrefix.length && wordPrefix.length > 1) typed.uppercase()
            else if (shownNow.firstOrNull()?.isUpperCase() == true) typed.substring(0, 1).uppercase() + typed.substring(1) else typed
        if (autoCorrectOn && keepWord != typed) Contractions.fix(typedCased)?.let { c ->
            if (driver.language == 0 || Driver.wordScore(true, typed) < 10) { pendingFix = c to false; willCorrect = true }
        }
        if (autoCorrectOn && keepWord != typed) textReplacements[typed]?.let { r ->   // the user's own shortcuts win
            pendingFix = r to (driver.language == 1); willCorrect = true
        }
        val comp = if (suggestionsOn) Driver.completions(typed, vi, 6) else emptyList()
        val shown = shownWord
        val cells = arrayListOf(Suggestion(shown, quoted = true))
        val seen = hashSetOf(shown.lowercase())
        fun add(c: Candidate) {
            val t = cased(c.text)
            if (cells.size >= 3 || t.lowercase() in seen) return
            seen.add(t.lowercase()); cells.add(Suggestion(t))
        }
        pendingFix?.let { fix -> if (willCorrect && fix.first.lowercase() !in seen) { seen.add(fix.first.lowercase()); cells.add(Suggestion(fix.first, highlighted = true)) } }
        if (suggestionsOn) {
            val isWord = candidates.any { it.letters == typed }
            val fixes = candidates.filter { it.score > -9 }
            for (c in if (isWord) comp + fixes else fixes + comp) add(c)
        }
        if (cells.size == 1) cells[0] = cells[0].copy(quoted = false)
        keyboard.suggestions = if (!suggestionsOn && !willCorrect) emptyList() else cells
    }

    /** Space after a word: put in the correction (like the system keyboard). Returns true when it typed the space itself. */
    private fun autoCorrectAtBreak(end: String = " "): Boolean {
        updateSuggestionsIfNeeded()
        if (!willCorrect) return false
        val fix = pendingFix ?: return false
        replaceWord(fix.first, fix.second, recordUndo = true, end = end)
        return true
    }

    private fun replaceWord(word: String, vietnamese: Boolean, recordUndo: Boolean, end: String = " ") {
        val old = shownWord
        val insert = word + end
        val c = ic
        c?.beginBatchEdit(); if (old.isNotEmpty()) c?.deleteSurroundingText(old.length, 0); c?.commitText(insert, 1); c?.endBatchEdit()
        Diag.edits += old.length + 1
        driver.resetContext()
        if ((driver.language == 1) != vietnamese && driver.autoSwitch) { driver.language = if (vietnamese) 1 else 0; saveLanguage(); refresh() }
        lastCorrection = if (recordUndo) old to insert else null
        lastSpace = if (end == " ") now else 0.0
        clearWord()
        afterEdit(old.length, insert)
    }

    /** Backspace right after an auto-correct puts the typed word back (and keeps it this time). */
    private fun undoCorrection(): Boolean {
        val c = lastCorrection
        if (c == null || (lastContext ?: "").endsWith(c.second).not()) { lastCorrection = null; return false }
        lastCorrection = null
        val conn = ic
        conn?.beginBatchEdit(); conn?.deleteSurroundingText(c.second.length, 0); conn?.commitText(c.first, 1); conn?.endBatchEdit()
        driver.resetContext()
        keepWord = c.first.lowercase()
        clearWord()
        afterEdit(c.second.length, c.first)
        return true
    }

    override fun keyboardPickSuggestion(index: Int) {
        val cells = keyboard.suggestions
        val cell = cells.getOrNull(index) ?: return
        lastCorrection = null
        if (cell.quoted || cell.text == shownWord) {
            keepWord = wordPrefix.toString().lowercase()
            willCorrect = false
            spaceKey(); clearWord()
            return
        }
        val c = candidates.firstOrNull { cased(it.text) == cell.text }
        var vi = driver.language == 1
        if (c != null) vi = c.vietnamese
        else Driver.completions(wordPrefix.toString().lowercase(), driver.language == 1, 6).firstOrNull { cased(it.text) == cell.text }?.let { vi = it.vietnamese }
        replaceWord(cell.text, vi, recordUndo = false)
    }

    private fun saveLanguage() { Settings.prefs(this).edit().putInt(Settings.LANGUAGE, driver.language).apply() }

    /** iOS turns smart dashes off for web, email and password fields; Android has no field flag, so the same rule. */
    private val smartDashesHere: Boolean get() = correctsHere

    /** Punctuation typed right after a space goes back onto the word: "this ." → "this.", "hello "" → "hello"".
     *  Quotes and brackets only when they close one that is open in this paragraph. Not : or ; (Tony): they start
     *  emoticons, "haha :)" must not become "haha:)", and "Hello : " stays as typed. */
    private fun snapsBack(c: String): Boolean {
        if (c.length != 1 || c[0] !in ".,?!)\"") return false
        val before = lastContext ?: return false
        if (!before.endsWith(" ") || before.length < 2) return false
        val p = before[before.length - 2]
        if (p == ' ' || p == '\n') return false
        val para = before.substringAfterLast('\n')
        if (c == ")") return para.count { it == '(' } > para.count { it == ')' }
        if (c == "\"") return para.count { it == '"' } % 2 == 1
        return true
    }

    /** Capital letter at the start of a sentence, like the system keyboard. */
    private fun updateAutoShift(before: String) {
        if (keyboard.layer != KeyboardView.Layer.LETTERS || keyboard.shift == KeyboardView.Shift.LOCKED) return
        val capAll = inputType and InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS != 0
        val capWords = inputType and InputType.TYPE_TEXT_FLAG_CAP_WORDS != 0
        val capSentences = inputType and InputType.TYPE_TEXT_FLAG_CAP_SENTENCES != 0
        if (!autoCapitalize || !(capAll || capWords || capSentences)) { if (keyboard.shift == KeyboardView.Shift.ON) keyboard.shift = KeyboardView.Shift.OFF; return }
        val trimmed = before.replace(" ", "")
        var start = before.isEmpty() || before.endsWith("\n")
        // a sentence ends at . ? ! … , also with closing quotes or brackets after it: go." Then, (wow.) Next
        val core = trimmed.trimEnd('"', '”', '’', '\'', ')', ']')
        if (!start && before.endsWith(" ") && core.isNotEmpty()) start = core.last() in ".?!…"
        if (capAll) { keyboard.shift = KeyboardView.Shift.ON; return }
        if (capWords && (before.isEmpty() || before.endsWith(" "))) start = true
        keyboard.shift = if (start) KeyboardView.Shift.ON else KeyboardView.Shift.OFF
    }

    // MARK: emoji keyboard

    private fun showEmoji() {
        clearWord()
        driver.resetContext()
        val ev = emojiView ?: EmojiKeyboardView(this).also { v ->
            v.listener = this
            root.addView(v, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, heightPx()))
            emojiView = v
        }
        ev.dark = keyboard.dark; ev.haptics = keyboard.haptics; ev.feedback = keyboard.feedback
        ev.load()
        browseEmoji()
    }

    private fun browseEmoji() {
        val ev = emojiView ?: return
        KeyboardView.searchTop = null
        applyHeight()
        keyboard.visibility = View.INVISIBLE
        ev.visibility = View.VISIBLE
        ev.layoutParams = ev.layoutParams.apply { height = heightPx() }
        ev.setSearching(false)
    }

    override fun emojiClose() {
        KeyboardView.searchTop = null
        applyHeight()
        emojiView?.visibility = View.GONE
        keyboard.visibility = View.VISIBLE
        keyboard.layer = KeyboardView.Layer.LETTERS
        updateAutoShift(lastContext ?: "")
    }

    override fun emojiStartSearch() {
        val ev = emojiView ?: return
        KeyboardView.searchTop = EmojiKeyboardView.SEARCH_HEIGHT
        applyHeight()
        keyboard.visibility = View.VISIBLE
        keyboard.layer = KeyboardView.Layer.LETTERS
        keyboard.shift = KeyboardView.Shift.OFF
        ev.layoutParams = ev.layoutParams.apply { height = (EmojiKeyboardView.SEARCH_HEIGHT * resources.displayMetrics.density).toInt() }
        ev.bringToFront()
        ev.setSearching(true)
        emojiQuery = ""
        updateEmojiQuery()
    }

    override fun emojiInsert(text: String) {
        ic?.commitText(text, 1)
        driver.resetContext()
        clearWord()
        afterEdit(0, text)
    }

    override fun emojiDelete() {
        if (deleteSelection()) return
        val before = lastContext ?: ""
        val n = if (before.isEmpty()) 1 else lastGraphemeLength(before)
        ic?.deleteSurroundingText(n, 0)
        driver.resetContext()
        clearWord()
        afterEdit(n, "")
    }

    /** Keys while searching go to the query, not the app. Returns false for keys that work as usual. */
    private fun emojiSearchKey(kind: KeyKind): Boolean {
        when (kind) {
            is KeyKind.Char -> emojiQuery += kind.c.lowercase()
            KeyKind.Space -> if (emojiQuery.isNotEmpty() && !emojiQuery.endsWith(" ")) emojiQuery += " "
            KeyKind.Backspace -> if (emojiQuery.isNotEmpty()) emojiQuery = emojiQuery.dropLast(1)
            KeyKind.Return, KeyKind.Emoji -> { browseEmoji(); return true }
            else -> return false
        }
        if (keyboard.shift == KeyboardView.Shift.ON) keyboard.shift = KeyboardView.Shift.OFF
        updateEmojiQuery()
        return true
    }

    /** The query reads as Telex in Vietnamese mode ("nguwaj" shows "ngựa"); search uses the raw keys too. */
    private fun updateEmojiQuery() {
        val ev = emojiView ?: return
        val vi = driver.language == 1
        val shown = if (vi) Telex.convert(emojiQuery) else emojiQuery
        val found = if (emojiQuery.trim().isEmpty()) emptyList() else EmojiIndex.get(this).search(emojiQuery, 80)
        ev.setQuery(shown, found)
    }
}
