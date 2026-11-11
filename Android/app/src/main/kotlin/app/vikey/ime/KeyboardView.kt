package app.vikey.ime

import android.content.Context
import android.content.res.Configuration
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.graphics.drawable.Drawable
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import androidx.core.content.ContextCompat
import app.vikey.R
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min

/** What a key does. */
sealed class KeyKind {
    data class Char(val c: String) : KeyKind()     // inserts this (letters stored lowercase)
    object Shift : KeyKind(); object Backspace : KeyKind(); object Space : KeyKind(); object Return : KeyKind()
    object Globe : KeyKind(); object Language : KeyKind(); object Emoji : KeyKind()
    data class Layer(val l: KeyboardView.Layer) : KeyKind()
}

/** One cell of the suggestion strip. */
data class Suggestion(val text: String, val quoted: Boolean = false, val highlighted: Boolean = false)

/**
 * The iOS 26 system keyboard geometry (402 pt wide base: key 43 tall, rows 54 apart, 6 gaps, 44 strip on top),
 * drawn on one canvas and handling its own touches so fast typing between keys never gets lost.
 * Port of iOS/Keyboard/KeyboardView.swift.
 */
class KeyboardView(ctx: Context) : View(ctx) {
    enum class Layer { LETTERS, NUMBERS, SYMBOLS }
    enum class Shift { OFF, ON, LOCKED }

    interface Listener {
        fun keyboardDidTap(kind: KeyKind)
        fun keyboardInsertAlternate(text: String)
        fun keyboardBackspaceRepeat(count: Int)
        fun keyboardMoveCursor(by: Int)
        fun keyboardCursorModeEnded()
        fun keyboardPickSuggestion(index: Int)
        fun keyboardGlobeHold()
    }

    var listener: Listener? = null
    var layer: Layer = Layer.LETTERS
        set(v) { if (field != v) { field = v; rebuild() } }
    var shift: Shift = Shift.OFF
        set(v) { if (field != v) { field = v; invalidate() } }
    var dark = false
        set(v) { if (field != v) { field = v; invalidate() } }
    var showsGlobe = false
        set(v) { if (field != v) { field = v; rebuild() } }
    var languageTitle = "Tiếng Việt"
        set(v) { if (field != v) { field = v; invalidate() } }
    var languageShort = "VI"
        set(v) { if (field != v) { field = v; invalidate() } }
    var returnTitle: String? = null      // null: the return arrow
        set(v) { if (field != v) { field = v; invalidate() } }
    var returnIsBlue = false
        set(v) { if (field != v) { field = v; invalidate() } }
    var haptics = false
    var diagnostics: String? = null
        set(v) { field = v; invalidate() }
    var suggestions: List<Suggestion> = emptyList()
        set(v) { if (field != v) { field = v; invalidate() } }
    /** The letters typed so far in the current word and the mode, for the fat-finger model. */
    var wordContext: (() -> Pair<String, Boolean>?)? = null   // null: no fat-finger guessing (password fields)
    /** Where the last typed key was touched; taken (set to null) by the controller. */
    var lastTapPoint: Pair<Float, Float>? = null
    var keyRects = KeyRects(ByteArray(0), FloatArray(0)); private set
    val feedback = KeyHaptics(ctx)

    companion object {
        /** The strip is shown when suggestions or auto-correct are on; without it the keys start right away. */
        var barOn = false
        const val BAR_H = 44f
        /** Emoji search: the search field and a row of results take the top instead of the strip (dp). */
        var searchTop: Float? = null
        fun diagH(compact: Boolean) = if (Diag.on) (if (compact) 30f else 40f) else 0f
        data class Metrics(val top: Float, val keyH: Float, val pitch: Float, val bottom: Float)
        fun metrics(compact: Boolean): Metrics {
            searchTop?.let { t -> return if (compact) Metrics(t, 34f, 40f, 4f) else Metrics(t, 43f, 54f, 3f) }
            val extra = diagH(compact) + (if (barOn) BAR_H else 0f)
            return if (compact) Metrics(8f + extra, 34f, 40f, 4f) else Metrics(11f + extra - (if (barOn) 3f else 0f), 43f, 54f, 3f)
        }
        /** Keyboard height in dp. */
        fun keyboardHeight(compact: Boolean): Float { val m = metrics(compact); return m.top + 3 * m.pitch + m.keyH + m.bottom }

        val alternateTable: Map<String, List<String>> = mapOf(
            "a" to listOf("a", "ă", "â", "á", "à", "ả", "ã", "ạ"), "e" to listOf("e", "ê", "é", "è", "ẻ", "ẽ", "ẹ"),
            "o" to listOf("o", "ô", "ơ", "ó", "ò", "ỏ", "õ", "ọ"), "u" to listOf("u", "ư", "ú", "ù", "ủ", "ũ", "ụ"),
            "i" to listOf("i", "í", "ì", "ỉ", "ĩ", "ị"), "y" to listOf("y", "ý", "ỳ", "ỷ", "ỹ", "ỵ"), "d" to listOf("d", "đ"),
            "." to listOf(".", "…"), "-" to listOf("-", "–", "—", "•"), "'" to listOf("'", "’", "‘", "`"), "\"" to listOf("\"", "“", "”", "„", "«", "»"),
            "!" to listOf("!", "¡"), "?" to listOf("?", "¿"), "$" to listOf("$", "₫", "€", "£", "¥", "₩", "¢"), "0" to listOf("0", "°"), "&" to listOf("&", "§"),
            "/" to listOf("/", "\\"), "%" to listOf("%", "‰"),
        )
    }

    private class Key(val kind: KeyKind) { val frame = RectF(); var pressed = false }
    private var keys: List<Key> = emptyList()
    private val letterKeys = HashMap<Char, Key>()
    private val d = resources.displayMetrics.density
    private val compact get() = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE && resources.configuration.smallestScreenWidthDp < 600
    private val compactWidth get() = resources.configuration.screenWidthDp < 600

    // touches
    private var pointerId = -1                 // the touch that owns the current key
    private var touchKey: Key? = null
    private var touchStartX = 0f; private var touchStartY = 0f
    private var slideFromLayer = false
    private val handler = Handler(Looper.getMainLooper())
    private var hold: Runnable? = null
    private var repeatCount = 0
    private var cursorMode = false
    private var cursorAnchor = 0f
    private var lastHaptic = 0L
    // popup + alternates
    private var popupKey: Key? = null
    private var alternatesFor: Key? = null
    private var alternates: List<String> = emptyList()
    private var alternateIndex = 0
    private val alternatesRect = RectF()

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER }
    private val tmp = RectF()
    private val path = Path()
    private val icons = HashMap<Int, Drawable>()
    /** iOS .semibold (600); Roboto has it from Android 9, medium before. */
    private val semibold: Typeface = if (android.os.Build.VERSION.SDK_INT >= 28) Typeface.create(Typeface.DEFAULT, 600, false) else Typeface.create("sans-serif-medium", Typeface.NORMAL)
    private var lastX = 0f

    init { rebuild() }

    private fun icon(id: Int): Drawable = icons.getOrPut(id) { ContextCompat.getDrawable(context, id)!!.mutate() }

    // MARK: layout

    private fun rowKinds(): List<List<KeyKind>> {
        fun chars(s: String) = s.map { KeyKind.Char(it.toString()) as KeyKind }
        val bottom = ArrayList<KeyKind>()
        bottom.add(if (layer == Layer.LETTERS) KeyKind.Layer(Layer.NUMBERS) else KeyKind.Layer(Layer.LETTERS))
        bottom.add(KeyKind.Emoji)
        if (showsGlobe) bottom.add(KeyKind.Globe)
        bottom.addAll(listOf(KeyKind.Language, KeyKind.Space, KeyKind.Return))
        return when (layer) {
            Layer.LETTERS -> listOf(chars("qwertyuiop"), chars("asdfghjkl"), listOf(KeyKind.Shift) + chars("zxcvbnm") + listOf(KeyKind.Backspace), bottom)
            Layer.NUMBERS -> listOf(chars("1234567890"), chars("-/:;()$&@\""), listOf<KeyKind>(KeyKind.Layer(Layer.SYMBOLS)) + chars(".,?!'") + listOf(KeyKind.Backspace), bottom)
            Layer.SYMBOLS -> listOf(chars("[]{}#%^*+="), chars("_\\|~<>€£¥•"), listOf<KeyKind>(KeyKind.Layer(Layer.NUMBERS)) + chars(".,?!'") + listOf(KeyKind.Backspace), bottom)
        }
    }

    private fun rebuild() {
        keys = rowKinds().flatten().map { Key(it) }
        touchKey = null; popupKey = null; alternatesFor = null
        layoutKeys()
        invalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) { layoutKeys() }
    fun relayout() { layoutKeys(); invalidate() }

    private fun layoutKeys() {
        val W = width.toFloat()
        if (W <= 0) return
        val m = metrics(compact)
        val s = W / (402 * d)
        val side = 6.67f * s * d; val gap = 6f * s * d
        val key = (W - 2 * side - 9 * gap) / 10
        val rows = rowKinds()
        val frames = ArrayList<RectF>()
        for ((r, row) in rows.withIndex()) {
            val y = (m.top + r * m.pitch) * d
            fun rect(x: Float, w: Float) = RectF(x, y, x + w, y + m.keyH * d)
            when (r) {
                0 -> for (i in row.indices) frames.add(rect(side + i * (key + gap), key))
                1 -> if (row.size == 9) { val x0 = side + (key + gap) / 2; for (i in 0 until 9) frames.add(rect(x0 + i * (key + gap), key)) }
                     else for (i in row.indices) frames.add(rect(side + i * (key + gap), key))
                2 -> {
                    val fn = 45.3f * s * d
                    frames.add(rect(side, fn))
                    val inner = row.size - 2
                    if (inner == 7) { val x0 = side + (key + gap) * 1.5f; for (i in 0 until 7) frames.add(rect(x0 + i * (key + gap), key)) }
                    else {
                        val left = side + fn + gap * 2.5f; val right = W - side - fn - gap * 2.5f
                        val w = (right - left - (inner - 1) * gap) / inner
                        for (i in 0 until inner) frames.add(rect(left + i * (w + gap), w))
                    }
                    frames.add(rect(W - side - fn, fn))
                }
                else -> {
                    val small = 43.3f * s * d; val ret = 92.6f * s * d
                    var x = side
                    for (kind in row) when (kind) {
                        KeyKind.Space -> { val w = W - side - ret - gap - x; frames.add(rect(x, w)); x += w + gap }
                        KeyKind.Return -> frames.add(rect(W - side - ret, ret))
                        else -> { frames.add(rect(x, small)); x += small + gap }
                    }
                }
            }
        }
        letterKeys.clear()
        val letters = ArrayList<Byte>(); val rects = ArrayList<Float>()
        for ((i, k) in keys.withIndex()) {
            if (i >= frames.size) break
            k.frame.set(frames[i])
            val kind = k.kind
            if (layer == Layer.LETTERS && kind is KeyKind.Char && kind.c[0].isLetter() && kind.c[0].code < 128) {
                letters.add(kind.c[0].code.toByte())
                rects.addAll(listOf(k.frame.left / d, k.frame.top / d, k.frame.width() / d, k.frame.height() / d))
                letterKeys[kind.c[0]] = k
            }
        }
        keyRects = KeyRects(letters.toByteArray(), rects.toFloatArray())
    }

    val sideInset get() = 6.67f * (width / (402 * d)) * d
    private fun barRect(): RectF { val dh = diagH(compact) * d; val side = sideInset; return RectF(side, dh + 2 * d, width - side, dh + (BAR_H - 2) * d) }

    // MARK: drawing

    private val brand get() = if (dark) Color.rgb(158, 153, 255) else Color.rgb(79, 69, 229)
    private val keyColor get() = if (dark) Color.rgb(107, 107, 107) else Color.WHITE
    private val pressedColor get() = if (dark) Color.rgb(148, 148, 148) else Color.rgb(189, 194, 204)
    private val fg get() = if (dark) Color.WHITE else Color.BLACK
    private val secondary get() = if (dark) Color.argb(153, 255, 255, 255) else Color.argb(153, 60, 60, 67)

    override fun onDraw(c: Canvas) {
        val W = width.toFloat()
        // strip + diagnostics
        if (barOn && searchTop == null) drawBar(c)
        diagnostics?.let { if (searchTop == null) {
            text.color = secondary; text.typeface = Typeface.MONOSPACE; text.textSize = 11 * d
            val lines = it.split("\n")
            for ((i, l) in lines.withIndex()) c.drawText(l, W / 2, (12 + i * 14) * d, text)
        } }
        // keys
        val r = 8.5f * d
        for (k in keys) {
            var bg = keyColor; var tint = fg
            if (k.kind == KeyKind.Return && returnIsBlue) { bg = Color.rgb(0, 122, 255); tint = Color.WHITE }
            if (k.kind == KeyKind.Language) tint = brand
            if (cursorMode) bg = if (dark) Color.rgb(82, 82, 82) else Color.rgb(237, 237, 237)
            if (k.pressed) bg = pressedColor
            // the 1 pt shadow under each key (black at 0.22, like iOS)
            fill.color = Color.argb(56, 0, 0, 0)
            tmp.set(k.frame); tmp.offset(0f, d); c.drawRoundRect(tmp, r, r, fill)
            fill.color = bg; c.drawRoundRect(k.frame, r, r, fill)
            drawLabel(c, k, tint)
        }
        popupKey?.let { drawPopup(c, it) }
        alternatesFor?.let { drawAlternates(c) }
    }

    private fun label(kind: KeyKind): Pair<String?, Int?> = when (kind) {
        is KeyKind.Char -> (if (shift != Shift.OFF && layer == Layer.LETTERS) kind.c.uppercase() else kind.c) to null
        KeyKind.Shift -> null to (if (shift == Shift.LOCKED) R.drawable.ic_capslock else if (shift == Shift.ON) R.drawable.ic_shift_fill else R.drawable.ic_shift)
        KeyKind.Backspace -> null to R.drawable.ic_delete
        KeyKind.Space -> (if (cursorMode) "" else languageTitle) to null
        KeyKind.Return -> if (returnTitle == null) null to R.drawable.ic_return else returnTitle to null
        KeyKind.Globe -> null to R.drawable.ic_globe
        KeyKind.Emoji -> null to R.drawable.ic_emoji
        KeyKind.Language -> languageShort to null
        is KeyKind.Layer -> (when (kind.l) { Layer.LETTERS -> "ABC"; Layer.NUMBERS -> "123"; Layer.SYMBOLS -> "#+=" }) to null
    }

    private fun drawLabel(c: Canvas, k: Key, tint: Int) {
        var (t, ic) = label(k.kind)
        if (cursorMode && k.kind is KeyKind.Char) t = ""
        if (ic != null) {
            val dr = icon(ic); dr.setTint(tint)
            val sz = (19 * d * 1.25f).toInt()
            val cx = k.frame.centerX().toInt(); val cy = k.frame.centerY().toInt()
            dr.setBounds(cx - sz / 2, cy - sz / 2, cx + sz / 2, cy + sz / 2); dr.draw(c)
            return
        }
        if (t.isNullOrEmpty()) return
        text.color = tint
        when (k.kind) {
            is KeyKind.Char -> { text.typeface = Typeface.DEFAULT; text.textSize = (if (layer == Layer.LETTERS) 23f else 22f) * d }
            KeyKind.Space -> { text.typeface = Typeface.DEFAULT; text.textSize = 15 * d
                val maxW = k.frame.width() - 8 * d; while (text.measureText(t) > maxW && text.textSize > 9 * d) text.textSize -= d }
            KeyKind.Language -> { text.typeface = semibold; text.textSize = 16 * d }
            KeyKind.Return -> { text.typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL); text.textSize = 16 * d }
            else -> { text.typeface = Typeface.DEFAULT; text.textSize = 16 * d }
        }
        val y = k.frame.centerY() - (text.descent() + text.ascent()) / 2 - d
        c.drawText(t, k.frame.centerX(), y, text)
    }

    private fun drawBar(c: Canvas) {
        val b = barRect(); val w = b.width() / 3
        val items = suggestions
        for (i in 0 until 3) {
            val it = items.getOrNull(i) ?: continue
            tmp.set(b.left + i * w + 2 * d, b.top + 2 * d, b.left + (i + 1) * w - 2 * d, b.bottom - 2 * d)
            if (it.highlighted) { fill.color = keyColor; c.drawRoundRect(tmp, 8 * d, 8 * d, fill) }
            text.color = fg; text.typeface = Typeface.DEFAULT; text.textSize = 17 * d
            var s = if (it.quoted) "“${it.text}”" else it.text
            while (text.measureText(s) > tmp.width() - 8 * d && s.length > 3) s = s.substring(0, s.length / 2 - 1) + "…" + s.substring(s.length / 2 + 1)
            c.drawText(s, tmp.centerX(), tmp.centerY() - (text.descent() + text.ascent()) / 2, text)
        }
        fill.color = (fg and 0xFFFFFF) or (0x2E shl 24)
        for (i in 0 until 2) {
            val hidden = items.size < 2 || (items.getOrNull(i)?.highlighted == true) || (items.getOrNull(i + 1)?.highlighted == true)
            if (!hidden) c.drawRect(b.left + (i + 1) * w - 0.5f * d, b.top + b.height() * 0.28f, b.left + (i + 1) * w + 0.5f * d, b.top + b.height() * 0.72f, fill)
        }
    }

    /** The balloon above a pressed letter: the key itself grows upward into a wider bubble. */
    private fun drawPopup(c: Canvas, key: Key) {
        val k = key.frame
        val extra = 13 * d; val bubbleH = 54 * d
        val bubble = RectF(k.left - extra, k.top - bubbleH + 6 * d, k.right + extra, k.top + 6 * d)
        if (bubble.left < d) bubble.offset(d - bubble.left, 0f)
        if (bubble.right > width - d) bubble.offset(width - d - bubble.right, 0f)
        val label = label(key.kind).first ?: return
        fill.color = keyColor
        fill.setShadowLayer(3 * d, 0f, d, Color.argb(71, 0, 0, 0))
        if (bubble.top < 0) {
            // no room above (top row, no strip): a widened key that grows to the top edge
            tmp.set(bubble.left, 0f, bubble.right, k.bottom)
            c.drawRoundRect(tmp, 10 * d, 10 * d, fill); fill.clearShadowLayer()
            text.color = fg; text.typeface = Typeface.DEFAULT; text.textSize = 30 * d
            c.drawText(label, tmp.centerX(), tmp.top + tmp.height() * 0.35f - (text.descent() + text.ascent()) / 2, text)
            return
        }
        val r = 10 * d; val rk = 8.5f * d; val neck = k.top + 4 * d
        path.reset()
        path.moveTo(bubble.left + r, bubble.top)
        path.lineTo(bubble.right - r, bubble.top)
        path.quadTo(bubble.right, bubble.top, bubble.right, bubble.top + r)
        path.lineTo(bubble.right, bubble.bottom - 10 * d)
        path.cubicTo(bubble.right, bubble.bottom, k.right, neck, k.right, neck + 8 * d)
        path.lineTo(k.right, k.bottom - rk)
        path.quadTo(k.right, k.bottom, k.right - rk, k.bottom)
        path.lineTo(k.left + rk, k.bottom)
        path.quadTo(k.left, k.bottom, k.left, k.bottom - rk)
        path.lineTo(k.left, neck + 8 * d)
        path.cubicTo(k.left, neck, bubble.left, bubble.bottom, bubble.left, bubble.bottom - 10 * d)
        path.lineTo(bubble.left, bubble.top + r)
        path.quadTo(bubble.left, bubble.top, bubble.left + r, bubble.top)
        path.close()
        c.drawPath(path, fill); fill.clearShadowLayer()
        text.color = fg; text.typeface = Typeface.DEFAULT; text.textSize = 38 * d
        c.drawText(label, bubble.centerX(), bubble.top + (bubble.height() - 8 * d) / 2 + 2 * d - (text.descent() + text.ascent()) / 2, text)
    }

    private fun drawAlternates(c: Canvas) {
        fill.color = keyColor
        fill.setShadowLayer(4 * d, 0f, d, Color.argb(71, 0, 0, 0))
        c.drawRoundRect(alternatesRect, 10 * d, 10 * d, fill); fill.clearShadowLayer()
        val w = 34 * d
        for ((i, s) in alternates.withIndex()) {
            tmp.set(alternatesRect.left + 4 * d + i * w, alternatesRect.top, alternatesRect.left + 4 * d + (i + 1) * w, alternatesRect.bottom)
            if (i == alternateIndex) { fill.color = Color.rgb(0, 122, 255); tmp.inset(d, 5 * d); c.drawRoundRect(tmp, 7 * d, 7 * d, fill); tmp.inset(-d, -5 * d) }
            text.color = if (i == alternateIndex) Color.WHITE else fg; text.typeface = Typeface.DEFAULT; text.textSize = 24 * d
            c.drawText(s, tmp.centerX(), tmp.centerY() - (text.descent() + text.ascent()) / 2, text)
        }
    }

    // MARK: touches

    private fun keyAt(x: Float, y: Float): Key? {
        var best: Key? = null; var bestD = Float.MAX_VALUE
        for (k in keys) {
            tmp.set(k.frame); tmp.inset(-3 * d, -5.5f * d)
            if (tmp.contains(x, y)) return k
            val dx = max(max(tmp.left - x, 0f), x - tmp.right); val dy = max(max(tmp.top - y, 0f), y - tmp.bottom)
            val dist = dx * dx + dy * dy
            if (dist < bestD) { bestD = dist; best = k }
        }
        return if (bestD < (44 * d) * (44 * d)) best else null   // edges and gaps belong to the nearest key
    }

    private fun tick() {
        val t = SystemClock.uptimeMillis()
        if (t - lastHaptic <= 40) return
        lastHaptic = t; Diag.haptics++
        feedback.tick(this)
    }
    private fun press(k: Key, late: Boolean = false) {
        k.pressed = true
        if (late) { Diag.late++; return }
        if (haptics) tick() else feedback.click()
    }
    private fun release(k: Key) { k.pressed = false }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex; began(e.getPointerId(i), e.getX(i), e.getY(i), e.eventTime)
            }
            MotionEvent.ACTION_MOVE -> { val i = e.findPointerIndex(pointerId); if (i >= 0) moved(e.getX(i), e.getY(i)) }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val i = e.actionIndex
                if (e.getPointerId(i) == barPointer) barUp(e.getX(i), e.getY(i))
                else if (e.getPointerId(i) == pointerId) touchKey?.let { finish(it) } ?: run { pointerId = -1 }
            }
            MotionEvent.ACTION_CANCEL -> { touchKey?.let { release(it) }; barPointer = -1; barIndex = -1; reset() }
        }
        invalidate()
        return true
    }

    private fun began(id: Int, x: Float, y: Float, eventTime: Long) {
        Diag.touches++
        Diag.touchStart = SystemClock.uptimeMillis()
        Diag.delayMs = max(Diag.delayMs, (Diag.touchStart - eventTime).toDouble())
        if (barOn && searchTop == null && suggestions.isNotEmpty() && barRect().contains(x, y)) {   // the strip: picks on lift, like a UIButton
            if (touchKey == null) { val i = barCell(x); if (i < suggestions.size) { barPointer = id; barIndex = i } }
            return
        }
        // a new touch while another is down: the earlier key is typed now (fast two-thumb typing). First, as it may
        // change the layer (a symbol after a slide off 123 goes back to ABC): the new touch is on those keys
        touchKey?.let { finish(it) }
        var k = keyAt(x, y) ?: return
        var kind = k.kind
        // fat-finger: a touch near a letter's border goes to the letter most likely next in this word
        if (kind is KeyKind.Char && layer == Layer.LETTERS && !keyRects.isEmpty) wordContext?.invoke()?.let { (prefix, vi) ->
            Driver.pickLetter(x / d, y / d, prefix.lowercase(), vi, keyRects)?.let { pc -> letterKeys[pc]?.let { k = it; kind = it.kind } }
        }
        pointerId = id; touchKey = k; touchStartX = x; touchStartY = y; lastX = x; slideFromLayer = false
        press(k, late = SystemClock.uptimeMillis() - eventTime > 50)
        when (kind) {
            is KeyKind.Char -> {
                if (compactWidth) popupKey = k
                val kk = k; startHold(450) { showAlternates(kk) }
            }
            KeyKind.Backspace -> { listener?.keyboardDidTap(KeyKind.Backspace); repeatCount = 0; startHold(450) { startRepeat() } }
            KeyKind.Space -> startHold(400) { enterCursorMode(lastX) }
            KeyKind.Shift -> listener?.keyboardDidTap(KeyKind.Shift)
            KeyKind.Globe -> startHold(500) { listener?.keyboardGlobeHold(); touchKey?.let { release(it) }; reset() }
            is KeyKind.Layer -> {
                // the system keyboard switches on touch-down and lets you slide onto a symbol
                listener?.keyboardDidTap(kind)
                slideFromLayer = true
                keyAt(x, y)?.let { nc -> touchKey = nc; nc.pressed = true }
            }
            else -> {}
        }
    }

    private var barPointer = -1; private var barIndex = -1
    private fun barCell(x: Float): Int { val b = barRect(); return ((x - b.left) / (b.width() / 3)).toInt().coerceIn(0, 2) }
    /** Lift on the strip: pick the cell the finger went down on if it is still over it (sliding off cancels). */
    private fun barUp(x: Float, y: Float) {
        val i = barIndex; barPointer = -1; barIndex = -1
        val b = barRect(); b.inset(0f, -8 * d)
        if (i >= 0 && b.contains(x, y) && barCell(x) == i && i < suggestions.size) listener?.keyboardPickSuggestion(i)
    }

    private fun moved(x: Float, y: Float) {
        lastX = x
        val current = touchKey ?: return
        if (cursorMode) {
            val step = 9 * width / (402 * d)
            val n = ((x - cursorAnchor) / step).toInt()
            if (n != 0) { listener?.keyboardMoveCursor(n); cursorAnchor += n * step }
            return
        }
        if (alternatesFor != null) { alternateIndex = (((x - alternatesRect.left - 4 * d) / (34 * d)).toInt()).coerceIn(0, alternates.size - 1); return }
        val k = current.kind
        if (k == KeyKind.Backspace || k == KeyKind.Shift) return
        if (!slideFromLayer && hypot(x - touchStartX, y - touchStartY) <= 14 * d) return   // a tap wobbles
        val c = keyAt(x, y) ?: return
        if (c === current) return
        tmp.set(c.frame); tmp.inset(2 * d, 2 * d)
        if (!tmp.contains(x, y)) return
        val nk = c.kind
        if (nk == KeyKind.Globe || nk == KeyKind.Backspace || nk == KeyKind.Shift) return
        release(current)
        touchKey = c; c.pressed = true
        cancelHold()
        popupKey = if (nk is KeyKind.Char && compactWidth) c else null
        if (nk is KeyKind.Char) startHold(450) { showAlternates(c) }
    }

    /** The touch that owns a key ended (or a newer touch began): type that key. */
    private fun finish(k: Key) {
        Diag.touchUp = SystemClock.uptimeMillis()
        cancelHold()
        release(k)
        popupKey = null
        val wasSlide = slideFromLayer
        if (cursorMode) {
            cursorMode = false
            listener?.keyboardCursorModeEnded()
        } else if (alternatesFor != null) {
            alternates.getOrNull(alternateIndex)?.let { listener?.keyboardInsertAlternate(it) }
            alternatesFor = null
        } else when (val kind = k.kind) {
            KeyKind.Backspace, KeyKind.Shift, is KeyKind.Layer -> {}   // handled on touch-down
            else -> {
                lastTapPoint = touchStartX / d to touchStartY / d
                listener?.keyboardDidTap(kind)
                if (wasSlide && kind is KeyKind.Char) listener?.keyboardDidTap(KeyKind.Layer(Layer.LETTERS))
            }
        }
        reset()
    }

    private fun reset() {
        cancelHold()
        pointerId = -1; touchKey = null; slideFromLayer = false
        popupKey = null; alternatesFor = null
        if (cursorMode) { cursorMode = false; listener?.keyboardCursorModeEnded() }
    }

    private fun startHold(ms: Long, f: () -> Unit) { cancelHold(); val r = Runnable { hold = null; f(); invalidate() }; hold = r; handler.postDelayed(r, ms) }
    private fun cancelHold() { hold?.let { handler.removeCallbacks(it) }; hold = null }

    private fun startRepeat() {
        val r = object : Runnable { override fun run() { repeatCount++; listener?.keyboardBackspaceRepeat(repeatCount); handler.postDelayed(this, 90) } }
        hold = r; handler.postDelayed(r, 90)
    }

    private fun enterCursorMode(x: Float) {
        cursorMode = true
        cursorAnchor = x
        popupKey = null
        if (haptics) tick()
    }

    private fun showAlternates(k: Key) {
        val kind = k.kind as? KeyKind.Char ?: return
        val list = alternateTable[kind.c] ?: return
        val upper = shift != Shift.OFF && layer == Layer.LETTERS
        popupKey = null
        alternates = list.map { if (upper) it.uppercase() else it }
        alternateIndex = 0
        val w = 34 * d; val h = 46 * d
        val total = alternates.size * w + 8 * d
        var x = k.frame.left - 4 * d
        if (x + total > width - 2 * d) x = width - 2 * d - total
        x = max(2 * d, x)
        alternatesRect.set(x, max(0f, k.frame.top - h - 6 * d), x + total, max(0f, k.frame.top - h - 6 * d) + h)
        alternatesFor = k
        if (haptics) tick()
    }
}
