package app.vikey.ui

import android.content.Intent
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.HelpOutline
import androidx.compose.material.icons.outlined.Keyboard
import androidx.compose.material.icons.outlined.Share
import androidx.compose.material.icons.outlined.UnfoldMore
import androidx.compose.material.icons.outlined.Check
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Popup
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import app.vikey.BuildConfig
import app.vikey.KeyboardStatus
import app.vikey.R
import app.vikey.ime.KBLog
import app.vikey.ime.Settings

/** Settings (shared with the keyboard), a place to try it, and the keyboard's status. Port of iOS/App/MainView.swift. */
@Composable
fun MainScreen(onReviewOnboarding: () -> Unit, onAcknowledgements: () -> Unit) {
    val ctx = LocalContext.current
    val prefs = remember { Settings.prefs(ctx) }
    fun pref(key: String): MutableState<Boolean> = mutableStateOf(prefs.getBoolean(key, Settings.defaults[key] ?: false))
    var autoSwitch by remember { pref(Settings.AUTO_SWITCH) }
    var pause by remember { pref(Settings.PAUSE_AFTER_MANUAL) }
    var webEnglish by remember { pref(Settings.ENGLISH_FOR_WEB_FIELDS) }
    var modern by remember { pref(Settings.MODERN_ORTHOGRAPHY) }
    var telexStyle by remember { mutableStateOf(prefs.getInt(Settings.TELEX_STYLE, 0)) }
    var spelling by remember { pref(Settings.SPELLING) }
    var restore by remember { pref(Settings.RESTORE_IF_INVALID) }
    var autoCap by remember { pref(Settings.AUTO_CAPITALIZE) }
    var doubleSpace by remember { pref(Settings.DOUBLE_SPACE_PERIOD) }
    var haptics by remember { pref(Settings.HAPTICS) }
    var diagnostics by remember { pref(Settings.DIAGNOSTICS) }
    var suggestions by remember { pref(Settings.SUGGESTIONS) }
    var autoCorrect by remember { pref(Settings.AUTO_CORRECT) }
    fun save(key: String, v: Boolean) = prefs.edit().putBoolean(key, v).apply()
    var added by remember { mutableStateOf(KeyboardStatus.isAdded(ctx)) }
    var selected by remember { mutableStateOf(KeyboardStatus.isSelected(ctx)) }
    var sample by remember { mutableStateOf("") }
    var logText by remember { mutableStateOf(KBLog.read(ctx)) }
    val owner = LocalLifecycleOwner.current
    DisposableEffect(owner) {
        val obs = LifecycleEventObserver { _, e -> if (e == Lifecycle.Event.ON_RESUME) { added = KeyboardStatus.isAdded(ctx); selected = KeyboardStatus.isSelected(ctx); logText = KBLog.read(ctx) } }
        owner.lifecycle.addObserver(obs); onDispose { owner.lifecycle.removeObserver(obs) }
    }

    LazyColumn(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background).statusBarsPadding().navigationBarsPadding().imePadding(), contentPadding = PaddingValues(16.dp)) {
        item {   // the wordmark header (design/BRAND.md)
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(start = 4.dp, top = 8.dp, bottom = 16.dp)) {
                Image(painterResource(R.drawable.logo), null, Modifier.size(40.dp).clip(RoundedCornerShape(9.dp)))
                Spacer(Modifier.width(12.dp)); Wordmark(32.sp)
            }
        }
        if (!added || !selected) item {
            Card {
                Row(Modifier.clickable { if (!added) KeyboardStatus.openSettings(ctx) else KeyboardStatus.showPicker(ctx) }.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Outlined.Keyboard, null, tint = brand); Spacer(Modifier.width(12.dp))
                    Text(stringResource(if (!added) R.string.add_keyboard else R.string.select_keyboard), color = brand)
                }
            }
            if (!added) Footer(stringResource(R.string.add_keyboard_footer))
            Spacer(Modifier.height(16.dp))
        }
        item {
            SectionTitle(stringResource(R.string.try_keyboard))
            Card { OutlinedTextField(sample, { sample = it }, Modifier.fillMaxWidth().padding(8.dp), placeholder = { Text(stringResource(R.string.try_placeholder)) }, minLines = 2, maxLines = 5,
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences)) }
            Spacer(Modifier.height(16.dp))
        }
        item {
            SectionTitle(stringResource(R.string.sec_auto))
            Card {
                ToggleRow(R.string.auto_switch, R.string.auto_switch_tip, autoSwitch) { autoSwitch = it; save(Settings.AUTO_SWITCH, it) }
                ToggleRow(R.string.pause, R.string.pause_tip, pause, enabled = autoSwitch) { pause = it; save(Settings.PAUSE_AFTER_MANUAL, it) }
                ToggleRow(R.string.web_english, R.string.web_english_tip, webEnglish, last = true) { webEnglish = it; save(Settings.ENGLISH_FOR_WEB_FIELDS, it) }
            }
            Spacer(Modifier.height(16.dp))
        }
        item {
            SectionTitle(stringResource(R.string.sec_vietnamese))
            Card {
                Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                    Tip(R.string.telex_style, R.string.telex_style_tip, Modifier.weight(1f))
                    var open by remember { mutableStateOf(false) }
                    val names = listOf("Telex", stringResource(R.string.telex_simple1), stringResource(R.string.telex_simple2))
                    Box {
                        Row(Modifier.clip(RoundedCornerShape(8.dp)).clickable { open = true }.background(MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f)).padding(start = 12.dp, end = 6.dp, top = 6.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                            Text(names[telexStyle.coerceIn(0, 2)], maxLines = 1)
                            Icon(Icons.Outlined.UnfoldMore, null, Modifier.size(18.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                        DropdownMenu(open, { open = false }) {
                            names.forEachIndexed { i, n ->
                                DropdownMenuItem({ Text(n) }, { telexStyle = i; prefs.edit().putInt(Settings.TELEX_STYLE, i).apply(); open = false }, leadingIcon = { if (telexStyle == i) Icon(Icons.Outlined.Check, null) else Spacer(Modifier.size(24.dp)) })
                            }
                        }
                    }
                }
                HorizontalDivider(Modifier.padding(start = 16.dp))
                Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                    Tip(R.string.tone_placement, R.string.tone_placement_tip, Modifier.weight(1f))
                    // a menu like the iOS picker: the value and a chevron, never two lines
                    var open by remember { mutableStateOf(false) }
                    Box {
                        Row(Modifier.clip(RoundedCornerShape(8.dp)).clickable { open = true }.background(MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f)).padding(start = 12.dp, end = 6.dp, top = 6.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                            Text(if (modern) "hoà, thuý" else "hòa, thúy", maxLines = 1)
                            Icon(Icons.Outlined.UnfoldMore, null, Modifier.size(18.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                        DropdownMenu(open, { open = false }) {
                            DropdownMenuItem({ Text("hoà, thuý") }, { modern = true; save(Settings.MODERN_ORTHOGRAPHY, true); open = false }, leadingIcon = { if (modern) Icon(Icons.Outlined.Check, null) else Spacer(Modifier.size(24.dp)) })
                            DropdownMenuItem({ Text("hòa, thúy") }, { modern = false; save(Settings.MODERN_ORTHOGRAPHY, false); open = false }, leadingIcon = { if (!modern) Icon(Icons.Outlined.Check, null) else Spacer(Modifier.size(24.dp)) })
                        }
                    }
                }
                HorizontalDivider(Modifier.padding(start = 16.dp))
                ToggleRow(R.string.spelling, R.string.spelling_tip, spelling) { spelling = it; save(Settings.SPELLING, it) }
                ToggleRow(R.string.restore, R.string.restore_tip, restore, enabled = spelling, last = true) { restore = it; save(Settings.RESTORE_IF_INVALID, it) }
            }
            Spacer(Modifier.height(16.dp))
        }
        item {
            SectionTitle(stringResource(R.string.sec_keyboard))
            Card {
                ToggleRow(R.string.suggestions, R.string.suggestions_tip, suggestions) { suggestions = it; save(Settings.SUGGESTIONS, it) }
                ToggleRow(R.string.autocorrect, R.string.autocorrect_tip, autoCorrect) { autoCorrect = it; save(Settings.AUTO_CORRECT, it) }
                ToggleRow(R.string.autocap, R.string.autocap_tip, autoCap) { autoCap = it; save(Settings.AUTO_CAPITALIZE, it) }
                ToggleRow(R.string.haptics, R.string.haptics_tip, haptics) { haptics = it; save(Settings.HAPTICS, it) }
                ToggleRow(R.string.double_space, R.string.double_space_tip, doubleSpace, last = true) { doubleSpace = it; save(Settings.DOUBLE_SPACE_PERIOD, it) }
            }
            Spacer(Modifier.height(16.dp))
        }
        item {
            SectionTitle(stringResource(R.string.sec_debug))
            Card {
                ToggleRow(R.string.diagnostics, R.string.diagnostics_tip, diagnostics, last = !diagnostics) { diagnostics = it; save(Settings.DIAGNOSTICS, it) }
                if (diagnostics) {
                    LinkRow(stringResource(R.string.send_log), icon = { Icon(Icons.Outlined.Share, null, tint = brand) }) {
                        val i = Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_SUBJECT, "Vikey keyboard log").putExtra(Intent.EXTRA_TEXT, logText.ifEmpty { ctx.getString(R.string.log_empty_share) })
                        ctx.startActivity(Intent.createChooser(i, null))
                    }
                    LinkRow(stringResource(R.string.clear_log), color = MaterialTheme.colorScheme.error) { KBLog.clear(ctx); logText = "" }
                    Text(if (logText.isEmpty()) stringResource(R.string.log_empty) else stringResource(R.string.log_lines, logText.lines().count { it.isNotEmpty() }),
                        style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(16.dp))
                }
            }
            Spacer(Modifier.height(16.dp))
        }
        item {
            Card {
                // per-app language (Android 13+), like the iOS row that opens the app's page in Settings
                if (android.os.Build.VERSION.SDK_INT >= 33) {
                    Row(Modifier.fillMaxWidth().clickable { KeyboardStatus.openAppLanguage(ctx) }.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text(stringResource(R.string.app_language), Modifier.weight(1f))
                        Text(appLanguageName(ctx), color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1)
                    }
                    HorizontalDivider(Modifier.padding(start = 16.dp))
                }
                LinkRow(stringResource(R.string.review_onboarding), onClick = onReviewOnboarding)
                LinkRow(stringResource(R.string.acknowledgements), onClick = onAcknowledgements)
                LinkRow(stringResource(R.string.send_feedback)) {   // vikey.app/feedback (English page when the app is in English), like iOS
                    val en = ctx.getString(R.string.lang_code) != "vi"
                    ctx.startActivity(Intent(Intent.ACTION_VIEW, android.net.Uri.parse("https://vikey.app${if (en) "/en" else ""}/feedback?platform=android&v=${BuildConfig.VERSION_NAME}")))
                }
                LinkRow("vikey.app") { ctx.startActivity(Intent(Intent.ACTION_VIEW, android.net.Uri.parse("https://vikey.app"))) }
                Row(Modifier.fillMaxWidth().padding(16.dp)) { Text(stringResource(R.string.version), Modifier.weight(1f)); Text(BuildConfig.VERSION_NAME, color = MaterialTheme.colorScheme.onSurfaceVariant) }
            }
            Footer(stringResource(R.string.footer))
        }
    }
}

/** The language the app shows, in that language ("Tiếng Việt", "English"), like the iOS row: the strings resolve to
 *  vi or en whatever the phone's language is, so the name comes from the strings, not the system locale. */
private fun appLanguageName(c: android.content.Context): String {
    val l = java.util.Locale(c.getString(R.string.lang_code))
    return l.getDisplayLanguage(l).replaceFirstChar { it.uppercase(l) }
}

@Composable fun Wordmark(size: androidx.compose.ui.unit.TextUnit) {
    Row { Text("Vi", fontSize = size, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.onBackground); Text("key", fontSize = size, fontWeight = FontWeight.Bold, color = brand) }
}

@Composable fun SectionTitle(t: String) = Text(t.uppercase(), style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(start = 16.dp, bottom = 6.dp))
@Composable fun Footer(t: String) = Text(t, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(16.dp, 6.dp))
@Composable fun Card(content: @Composable ColumnScope.() -> Unit) = Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(MaterialTheme.colorScheme.surfaceVariant), content = content)

/** A setting's title with a (?) that explains it. */
@Composable fun Tip(title: Int, tip: Int, modifier: Modifier = Modifier) {
    var shown by remember { mutableStateOf(false) }
    Row(modifier, verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(title))
        Spacer(Modifier.width(6.dp))
        Box {
            Icon(Icons.AutoMirrored.Outlined.HelpOutline, null, Modifier.size(18.dp).clickable { shown = true }, tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f))
            // below the (?) like the iOS popover; focusable so Back and a tap outside close it
            val below = with(androidx.compose.ui.platform.LocalDensity.current) { 24.dp.roundToPx() }
            if (shown) Popup(onDismissRequest = { shown = false }, alignment = Alignment.TopStart, offset = androidx.compose.ui.unit.IntOffset(0, below),
                properties = androidx.compose.ui.window.PopupProperties(focusable = true)) {
                Surface(Modifier.widthIn(max = 280.dp), shape = RoundedCornerShape(12.dp), tonalElevation = 6.dp, shadowElevation = 6.dp) {
                    Text(stringResource(tip), Modifier.padding(14.dp), style = MaterialTheme.typography.bodyMedium)
                }
            }
        }
    }
}

@Composable fun ToggleRow(title: Int, tip: Int, value: Boolean, enabled: Boolean = true, last: Boolean = false, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.weight(1f).alpha(if (enabled) 1f else 0.4f)) { Tip(title, tip) }
        Switch(value, onChange, enabled = enabled)
    }
    if (!last) HorizontalDivider(Modifier.padding(start = 16.dp))
}

@Composable fun LinkRow(title: String, color: Color = brand, icon: (@Composable () -> Unit)? = null, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().clickable(onClick = onClick).padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
        icon?.let { it(); Spacer(Modifier.width(12.dp)) }
        Text(title, color = color)
    }
    HorizontalDivider(Modifier.padding(start = 16.dp))
}
