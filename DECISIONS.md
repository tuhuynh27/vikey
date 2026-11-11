# Vikey decisions log

Product decisions for Vikey (tonyminibot/Vikey, based on OpenKey by Tuyen Mai), written as user stories.
Each entry records what was asked, what was decided, and its status. Newest first.

Status values: **Shipped** (in a build Tony has), **Agreed** (decided, not built yet), **Rejected**.

## 60. English word forms the list is missing: "resets" stays "resets"

**Status:** Built 2026-11-08, not in a release yet (Tony's report).

**Problem.** On the Mac, "this get resets " gave "this get rêts ". The English list comes from subtitles: it has "reset" but not "resets". So in English mode the keys weren't English, and their Telex replay "rêts" has the same lookup key as the real word "rết" (the trailing s reads as the sắc tone), so the word was converted. In Vietnamese mode the doubled s gave "reets" (D19: the dictionary decides at the end, and "resets" wasn't in it).

**Fix.** Real English word forms come from SCOWL, the database behind the standard en_US spell-check dictionaries: its en_US Hunspell dictionary, with every word expanded by its own affix rules (plurals, past tense, -ing, irregular forms). `Tools/autoswitch/build_forms.py` keeps the forms of entries whose base word is in our list and that the list is missing (18,197, ~180 KB): "resets", "quests", "resizes". `Engine/EnglishFormsData.h` holds them; the engine counts them as English, rarer than every listed word, and never when the letters are a Vietnamese word ("phis", Greek letters, is phí). Words already in the list keep their own score.

Tony rejected a first version that guessed from suffixes (any listed word + -s, -es, -ed, -ing): English doesn't work that way. Also tried: every word SCOWL knows (48,102, not only forms of listed words). Worse: "nos", "cos", "sex" stopped converting to nó, có, sẽ; manual switches 58 → 60 (chat).

**Result.** Rules D60 (8 cases), reported bug R21. All other rules, reported bugs and the Telex lock unchanged. Golden: "reie" → "resize" (chat, both sets), "quets" → "quests" (synthetic). Manual switches 58 → 57 (chat), 71 → 70 (chat_typos), 111 → 110 (synthetic); wrong words unchanged.

**Every decision has a test.** `rules/decisions.py` (in check.sh) fails when a decision has neither cases tagged with it nor a row in `rules/decisions.tsv` naming its tests, or saying why it has none. Open (behavior, no automated test yet): D1, D3, D15, D21, D54, all app-level (they need a live test on that platform).

## 59. ":" and ";" after a space stay where they are (phone keyboards)

**Status:** Shipped 2026-11-02 (Tony changed his mind on the earlier behavior).

**Problem.** On iPhone and Android, punctuation typed right after a space went back onto the word, like the system keyboard: "Hello : " gave "Hello: ", and the emoticon "haha :)" gave "haha:)".

**Fix.** Only . , ? ! and closing ) " still snap back ("this ." → "this."). : and ; stay as typed, so emoticons (:) :D ;)) and "Hello : " keep their space. Desktop typing never had this behavior.

**Result.** iOS test `testColonAndSemicolonStay` ("haha :) ", "hello : ", "ok ;) "); `testPeriodSnapsBack` still passes.

## 58. "docs" in Vietnamese mode is English

**Status:** Shipped 2026-11-02 (bug from an X user, via Tony).

**Problem.** In Vietnamese mode "docs" gave "dóc": the subtitle corpus has "docs" at English score -1, below "dóc" (1).

**Fix.** "docs" gets English score 20 (build_dict.py `EN_OVERRIDE`, patched in the built data like D48). 10 was not enough. "nói dóc" (to lie) still converts: the word before decides it.

**Result.** Manual switches 64 → 61 (chat), 77 → 74 (chat_typos), 123 → 120 (synthetic); wrong words unchanged; three golden lines now keep "docs". Rules D58 (3 cases), reported bug R20.

## 57. Backspace over punctuation lost the word before it

**Status:** Agreed (2026-11-01): a bug fix, in PR #33.

**Problem.** Typing a word, a punctuation mark, Backspace, then more letters and a space could erase the first word: `vieejt.` ⌫ `nam ` left only `nam`. Backspace brought back the word's letters but not its keys, so the restore at the space rebuilt the word from the new keys alone.

**Fix.** When Backspace brings a word back, its keys are rebuilt from the letters (as after D50). A word whose keys can't all be rebuilt (a very long word, or a character no key makes) is never restored. Cases tagged D57 in `rules.tsv`.

---

## 56. Telex style: Telex, Simple Telex 1, Simple Telex 2

**Status:** Agreed (2026-10-31), asked in #28.

**Problem.** In Telex, a lone `[` types ơ and `]` types ư, so typing brackets takes two presses. OpenKey had two Simple Telex styles for this, which Vikey removed when it forked.

**Decision.** A "Kiểu gõ" (typing style) setting on every platform, Telex by default:
- **Telex:** as before. `[` gives ơ, `]` gives ư, a lone `w` gives ư.
- **Simple Telex 1:** `[` and `]` are brackets and end the word like `.` and `,` (spell check and auto switch run, as at any punctuation). A `w` that joins no vowel stays `w` (`tw` stays `tw`).
- **Simple Telex 2:** `[` and `]` are brackets and end the word, as in Simple Telex 1. A lone `w` still gives ư.

`uw`, `ow`, `aw` give ư, ơ, ă in all three. Users coming from OpenKey keep their Simple Telex choice. Test cases: `Tools/autoswitch/rules/telex_simple1.tsv` and `telex_simple2.tsv`.

---

## 55. Linux: an Arch Linux package

**Status:** Agreed (2026-10-31), in PR #27.

**Decision.** Vikey ships an Arch Linux package (`.pkg.tar.zst`, x64 and ARM64) next to the .deb and .rpm. CI repacks it from the .deb, so the files are the same. The download page lists Arch as supported and shows an Arch button on the Linux card.

---

## 54. Linux: pre-edit in apps that never report the text around the cursor

**Status:** Fix for #24 (2026-10-31).

**Problem.** On Wayland the desktop (GNOME Shell, KWin) relays IBus for every app and always claims it can delete text around the cursor. Terminals (GNOME Terminal, Ghostty, kitty) and Zed ignore that delete, so `gox` showed `goõ`; GNOME Terminal on Ubuntu 24.04 crashed on it. Synthetic Backspace keys are dropped on Wayland.

**Decision.** Apps that send their surrounding text after they get the focus keep the direct mode (text typed straight in, as on macOS and Windows). Apps that never do get pre-edit: the current word and the two before it (what auto switch may still rewrite) are shown underlined until they are final. Everything is committed before any key that goes to the app (Enter, Tab, arrows, shortcuts).

---

## How behavior is locked down

Every engine rule decided here has test cases in `Tools/autoswitch/rules/rules.tsv`, tagged with its decision (D5.2, D8.1, ...). Run before and after any rule change:

```sh
Tools/autoswitch/check.sh
```

It checks:
1. **Rule cases.** A failure names the decision it breaks, e.g. "FAIL D5.2: common Vietnamese word (tô) is never switched away from".
2. **Golden snapshot of typing.** The chat corpus, with and without typos, is typed with no manual switching. Every changed line is printed: intended, before, now.
3. **Telex lock.** Every Vietnamese syllable, typed in 3 tone orders in Vietnamese mode, must keep its output.
4. **Metrics.** Manual switches and wrong words must not get worse.

If every listed change is intended, accept them with `Tools/autoswitch/check.sh update`. Add a case for the new decision to `rules.tsv`. GitHub Actions runs the same check on every push.

---

## 53. Teencode spelling (engine, off by default)

**Status:** In the engine 2026-10-31, **on by default** (Tony: "ok, on by default"); an app setting comes with the main window revamp. Rules D53 (9 cases).

**Asked by Tony** (from a VietTelex request on X): chat spelling like "zui zẻ", "zậy", "dzô", "bíe", "thík", "gòy", "ừk" shouldn't be put back to its keys. "Be conservative"; "maybe build a dictionary of teencode".

**Rules** (`vTeencode`): z / dz count as d; open rimes "ie" (bíe, mịe) and "oy" with huyền only (gòy; "oxy" stays); k after i or ư (thík, ừk; "look" stays). **Dictionary:** at the end of the word, a word that is valid only by these rules stays only if it is in `Engine/TeencodeData.h` (193 words: ViSoLex, MIT, plus forms that occur in our chat data); anything else goes back to its keys ("zero" never becomes "zẻo"). "wá" isn't possible in Telex (w is ư). "kó", "nhìu", "bít" already worked.

**Ties (asked by Tony):** with the option on, dictionary words that occur in our data get a Vietnamese score from their frequency (`kTeencodeVnScores`), so the usual tie rules decide them: "zoo" alone → zô, "đi zoo đây" → đi zô đây, "the zoo" → the zoo. Unmarked z-words that are English (zoom, zone, zero, zip) get no score.

**Measured (option on vs off).** VietTelex's 50 teencode chat lines (MIT): words right 87.7% → 94.7%, lines 28 → 40 (the rest is "wá"). Val 5M words: accuracy 99.88% both (wrong words 0.1194% → 0.1195%), manual switches 0.1569 → 0.1571 per 100; chat and synthetic corpora identical; 394 teencode words in the real data now come out as their writers typed them (zị, zậy, dzô, gòy…), 13 other words changed. Known: "Zeiss" → "Zeis" (a doubled tone key, D19).

**Also fixed (always on):** after a space, "(" "." "," etc. could rewrite the word before the space ("zẻo (" → "zzero("), like the hyphen in D52. Val wrong words 0.120% → 0.119%.

## 52. Two keys that deleted the character before the word

**Status:** Shipped 2026-10-30.

**Problem.** Running the realistic-typing simulator over the 5M-word val set (not just the chat corpus) found keys that changed text before the word being typed:
- "ử", space, "-" gave "wr wr-": the hyphen check (a hyphen glued to a word puts it back to English) also ran after a space, on the word before it.
- "ís" + w ("iứ", the tone moves onto the new ư) deleted one character too many: "ab isw" gave "abiứ". The new ư isn't on screen yet, but the tone move counted it.
- Also, D51 kept the look-back words while the cursor was away, so a word typed elsewhere could rewrite the words at the old spot ("cơn ác" ← ← ← "ok" gave "casc ok"). They're hidden while away now and come back with the word.

**Fix.** The hyphen check only for a hyphen right after the word; the tone move deletes one fewer when the key only added ư/ơ.

**Result.** 0 changes to text before the word across 5M words of realistic typing (was 50+). Val 99.88% unchanged, goldens unchanged. Rules D52 (3 cases).

## 51. ← → back to the same spot keeps the word

**Status:** Shipped 2026-10-30.

**Problem.** The realistic-typing simulator (`sim/rtsim`, below) moved the cursor away and back while typing, like a person checking something: "th ← → if" gave "thif" instead of "thì". The arrow started a new word, so the engine forgot "th"; in English mode the word buffer was dropped too, so a word typed in the wrong language wasn't fixed at its end. 28% of cursor round trips left a wrong word.

**Fix.** On the first ← inside (or right after) a word, the engine keeps a copy of its state; more ← count away, → counts back, and when the cursor is back at the word's end the copy comes back. Any other key, a click or Shift+arrow forgets it (typing somewhere else is a new word there). The Mac, Windows and Linux cores do the same for the English-mode word buffer. On the Mac, arrow keys carry the Fn and numeric-pad flags, which counted as "another modifier"; a plain arrow no longer does.

**Result.** Cursor round trips needing a retyped word: 28% → under 2%. Goldens unchanged. Rules D51 (3 cases); the harness gets `{` `}` for ← →.

**Later fix.** A click or a language switch while away didn't forget it (the desktops' English-mode copy, and the engine's across ⌥Z), so → brought the old word back at the new spot and the next key rewrote text there ("meeting th ← click → if " gave "meeting thì "). Both forget it now. Goldens unchanged; 5 more D51 rules.

## 50. Backspace on a toned letter takes all its keys

**Status:** Shipped 2026-10-30.

**Problem.** "có", Backspace, "o" gave "coo" (found by the simulator). Backspace removed the letter "ó" from the screen but only one key ("s") from the engine's key list, so "o" was still there and the next "o" made two.

**Fix.** When Backspace takes a letter made of several keys (a tone, â ê ô ă ơ ư đ) and what's left is plain letters, the key list becomes exactly those letters. Words with marks left in them keep the old behavior (rebuilding their keys in typing order broke English words typed in Vietnamese mode, e.g. "analytic" + w ⌫ → "aanlytics").

**Result.** "co", "se", "toán" (tone kept). Typos fixed with Backspace that still needed the word retyped: 15% fewer. Goldens unchanged. Rules D50 (3 cases).

**Realistic typing simulator.** `Tools/autoswitch/sim/rtsim.cpp` types every chat message twice (clean, and like a person: neighbouring-key typos fixed with Backspace, Backspace into the last word, ← → trips, clicks, an edit earlier in the text, ⌥Z round trips), over 3 seeds. It fails check.sh (and CI) if a key ever changes text from before a cursor move, click or switch (the "xin heín" class), if an edit elsewhere comes out wrong, if a word can't be fixed even by retyping, or if slips get more expensive than `rules/golden/rtsim.json`.

## 49. Backspace never reopens a word from before a language switch

**Status:** Shipped 2026-10-30.

**Problem.** In Vietnamese: "xin ", ⌥Z, "hello", ⌥Z, Backspace, "s" gave "xin heín" (Tony). The engine keeps the last words so Backspace can go back into one and keep editing it. Words typed in English mode never enter that list, so after switching back, Backspace reopened "xin" as if it were the word on the screen, and "s" edited it.

**Fix.** `vAutoSwitchForget` (called on every switch, app change, click and reset) clears those words, except the one an auto switch just converted, which is on screen (PR #8 by Ryan-LT; it replaced a narrower first fix that only cleared them on a language change). Without a switch, going back into the last word works as before ("tooi ddi hocj ⌫ s" → "tôi đi hóc").

**Result.** "xin hells". Goldens and metrics unchanged. Rules D49 (3 cases); the test harness gets `#` for the switch hotkey.

## 48. Lowercase "os" in Vietnamese is "ó"

**Status:** Shipped 2026-10-30.

**Problem.** In Vietnamese mode "la với os" stayed "os" and switched to English (Tony): "os" (OS) is in the developer word list at the top English score (40), while "ó" is a common chat interjection.

**Fix.** "os" gets English score 0 (build_dict.py `EN_OVERRIDE`; patched in the built data like D40). At 0, a Vietnamese sentence gives "ó" and an English one keeps "os" ("the os is", "my os is linux" in either mode). OS the acronym is typed in caps, which stays as typed. A sweep: 10 and 0 behave the same; -10 and below turn English-mode "os" into "ó".

**Result.** Val macro 0.119% → 0.120% wrong words (99.88%); chat_typos one manual switch and one wrong word fewer. Rules D48 (3 cases).

## 46. Linux: no per-website memory, no automatic install (approved)

**Status:** Approved by Tony, 2026-10-30 (Telegram 247): "This is ok and approved." Do not raise these again.

**Per-website memory.** Linux gives an input method no way to read the browser's address bar (no accessibility hook on Wayland, nothing in IBus). Vikey for Linux remembers the language per app only. Getting it would need a browser extension; not planned.

**Automatic install of updates.** Installing silently needs root or an apt/dnf repository. Vikey for Linux checks vikey.app/release.json once a day and shows a notification with a link to the download page; the user installs the new .deb/.rpm.

---

## 45. Linux catches up with the Mac app

**Asked by Tony** (2026-10-29): "Use the iOS and macOS app as the baseline. Port all the features to Windows/Linux/Android. Try to reach 100% feature parity."

**Decided**

- The Linux core (`Linux/ibus/Core.cpp`) follows `VikeyCore.mm` again: undo then type on (D32), digits start a word (D33), look forward (D35); D39/D41/D42 come with the shared engine. Live cases for each in `Linux/Tests/cases-linux.tsv`.
- Switch sound: the Mac's two notes (`.caf` → `.wav`), played inside the engine with GStreamer, not the system bell.
- Emoji picker: `vikey-settings --emoji`, a port of the Mac panel and its search (English, Vietnamese, Telex keys, Frequently Used, sections, arrows by row, Enter, Esc clears then closes, closes on click elsewhere). The engine opens it on the hotkey and types the choice into the app through the command file.
- Guide (first run, menu "Hướng dẫn…"): the Mac's steps and copy; the Accessibility step becomes "add Vikey to the input sources", checked live on GNOME.
- Menu "Kiểm tra cập nhật…" and a real "Tự động cập nhật" switch: a daily check of vikey.app/release.json with a notification; the user installs the package (no repo yet).
- "Ghi nhận" in About (the Mac list without Sparkle). Every row name and tip re-checked against MainView.swift.
- Not matched: per-website memory (needs a browser extension), positioning the picker as a pure Wayland client (it runs on XWayland), installing updates by itself (`Linux/PLAN.md`).

**Verified.** Live typing in the Ubuntu 24.04 VM (GNOME Wayland) from the installed package: 25/25 (14 shared + 11 Linux). Picker, guide, update dialogs, notification and the panel menu checked by hand with screenshots; the switch sound recorded from the audio output.
## 44. Windows catches up with the Mac (parity pass)

**Status:** Built 2026-10-30 on branch `windows`, not in a release yet.

**Asked by Tony.** "Use the iOS and macOS app as the baseline. Port all the features to Windows/Linux/Android. Try to reach 100% feature parity."

**Decided (Windows)**

- The platform core (`Windows/App/Core.cpp`) has every VikeyCore.mm change since 1.0.14: Backspace undo then typing on (D32), digits start a word (D33), look forward (D35), the emoji hotkey, and the "sparkle" switch sound (the Mac's two .caf files as WAV resources). D39, D41 and D42 come with the shared engine. Live test: 23 cases (9 new, one per rule), all pass in the ARM64 VM.
- Emoji picker: same search (English, Vietnamese, Telex keys, emoticons), same layout (Frequently Used first, sections, 9 columns), same keys. A popup that never takes the focus; the keyboard hook hands it the keys, so the app in front keeps its caret. Drawn with Direct2D/DirectWrite: GDI+ can't draw colour emoji. Country flags are left out: Windows' emoji font draws them as two letters.
- Onboarding: the Mac's steps minus Accessibility (welcome with the Telex table, practice, hotkey + sound, emoji). Tray menu and About match the Mac (Hướng dẫn…, Gửi góp ý…, vikey.app · GitHub · Ghi nhận); Ghi nhận opens an Acknowledgements window (TweetNaCl and Inno Setup instead of Sparkle).
- Windows open in the middle of the screen with the mouse. Settings rows are 34 px so the window, with the emoji rows, fits a 1366×768 screen.
- No UI Automation polling while a Vikey window is in front: it stalled Vikey's UI thread for seconds.

**Details:** `Windows/PLAN.md`.

---
## 43. Vikey for Android

**Status:** Built 2026-10-29 on branch `android` (release build signed, live test 14/14 on the emulator). Publishing waits for a try on Tony's phone.

**User story.** As a Vikey user on Android, I want the same keyboard as on iPhone: Telex, auto switch, suggestions, auto-correct, emoji, in the same layout and with the same settings.

**Decisions**

- An `InputMethodService` with one Canvas-drawn keyboard view, not Compose: touches are handled by the view itself so fast two-thumb typing never loses a key, the same reason the iOS keyboard handles touches in one `UIView`.
- Same engine, same driver: `Engine/` is built with the NDK; `vikey_jni.cpp` is the Android twin of `VKDriver.mm`, and the IME logic is a port of `KeyboardViewController.swift` (text context kept locally; one `getTextBeforeCursor` only when the app reports an outside change).
- The keyboard owns its background and keeps the keys above the gesture bar (Android 15 edge-to-edge).
- Haptics don't need a permission on Android, so "Rung khi gõ" is on without the Full Access step; the onboarding's step 4 is "switch to Vikey" instead.
- Emoji: the device's font decides what is shown (`Paint.hasGlyph`) instead of an OS-version table.
- Distribution: a signed APK on the GitHub release + vikey.app first (the download page already has the Android card); Google Play once the company account exists (D-U-N-S found, signup in progress, [[google-play-console]]).
- Minimum Android 8.0 (API 26); arm64 + x86_64 (emulator) only.

**Details:** `Android/PLAN.md`.

---
## 42. Acronyms don't flip the word before them

**Status:** Shipped 2026-10-29.

**Problem.** "e liên hệ bên ASL" came out "e liên hệ been ASL" (Tony). ASL in Telex is "ÁL", invalid, so its keys go back and the mode switches to English; look-back then flips the tie before it (bên 29 vs been 32) to "been". Same with "team bên US".

**Fix.** When the word put back has 2+ capitals, look-back stays off. The acronym itself is still put back as typed. Vietnamese text is full of acronyms, so they say nothing about the words before them.

**Result.** Locked test 99.88% (manual switches 0.151 → 0.150/100, social and tech wrong words -0.01); chat golden: "team bên US". Rules D42 (2 cases).

## 41. A lone all-caps word converts when Telex marks it

**Status:** Shipped 2026-10-29.

**Problem.** In English mode, THOONG, THUOWNGF, VIEETJ stayed as typed (Tony). D23 converts all-caps words only inside a run of capitals, so a lone HW or CWS stays an acronym. The first word of a Caps Lock run never converted either ("TOOI DDI HOCJ" gave "TOOI ĐI HỌC").

**Fix.** A lone all-caps word of 4+ letters converts when its keys carry a Telex mark: a doubled a/e/o/d, or w after a/o/u. The D23 floor (a common syllable, no English reading) still applies. Acronyms rarely have these marks: HW, IRS, USA, NASA stay.

**Result.** Caps group 1.75% → 1.41% wrong words (tuning set 1.40%); everything else unchanged, locked test 99.88%. Rules D41 (3 cases); D23's run case now expects "TÔI ĐI HỌC".

## 40. "sê" is not a Vietnamese word

**Status:** Shipped 2026-10-29.

**Problem.** In Vietnamese mode "see you" came out as "sê you" (Tony). The Vietnamese word list had "sê" at 11 (from names and loan spellings: Sê San, sê-ri), and "you" also counts as Vietnamese text in web data, so nothing switched to English.

**Fix.** "sê" drops to the lowest score (-120) in the built data; build_dict.py drops it on the next rebuild. A full rebuild today would change hundreds of other words (the source data moved since the last build), so only this word changed.

**Result.** Locked test 0.117% → 0.116% wrong words (99.88%); chat sets one manual switch fewer each. Rule D40.

**Also checked (Tony's report).** "we" → "ưe" happens only inside the 5 seconds after a manual switch, when auto-switch is paused on purpose. "ở DDaf" was the D39 bug (Shift held for both Ds).

## 39. DD counts as one capital (Đ)

**Status:** Shipped 2026-10-29.

**Problem.** "DDaf Nawngx" (Shift held for both Ds) stayed "DDaf" instead of "Đà". The acronym rule (D23, D27) keeps words with 2+ capitals as typed, and DD made two.

**Fix.** A capital D right after a capital D is the same letter, Đ, so it isn't counted again. DDR and DDoS still have two capitals and stay as typed.

**Result.** Locked test unchanged: 99.88%, caps group 1.75%. Rules: D39 (2 cases).

## 38. Vikey for Linux

**Status:** Shipped 2026-10-29 as part of 1.0.14 (branch `linux` → main), Phases 0–6 of `Linux/PLAN.md`.

**User story.** As a Linux user, I want the same Vikey as on the Mac: same engine, same auto switch, same settings window.

**Decided**

- Vikey is an **IBus engine** (`ibus-engine-vikey`): the only way to type in every app on X11 and Wayland. No pre-edit; text is committed straight, as on Mac/Windows.
- Same engine, same rule lock on Linux CI (x64 + ARM64). The platform core (`Linux/ibus/Core.cpp`) is the Windows/macOS state machine with an output sink.
- `vikey-settings` (GTK4 + libadwaita) mirrors the Mac window row by row; the IBus panel menu mirrors the menu bar. Settings in `~/.config/vikey/settings.json`; the engine watches it. Language switches go through a one-shot command file.
- Per-app memory from the X11 focus window (XWayland apps only under Wayland). Per-website memory is not available (would need a browser extension). Auto update is left to the package manager (apt/dnf repo later).
- Packages: .deb (Ubuntu/Debian) and .rpm (Fedora) from CPack, x64 + ARM64, published with `release.sh --linux`.

**Verified.** 14 live typing cases pass in an Ubuntu 24.04 VM (GNOME, Wayland session) from the installed .deb; per-app memory remembers the language per app.

---

## 37. Capitalised names mid-sentence: tried, not shipped

**Asked by Tony.** "do #2 too" (Max/Mã, Los Angeles, Bar, Docs).

**Tried.** For a capitalised word in the middle of a sentence that has both readings, decide by how often each capitalised form appears mid-sentence in real Vietnamese text (93M web words + crawl: Bar 564 vs Bả 27, Los 766 vs Ló 3, but Mã 3457 vs Max 1239, Lê 13245 vs Lee 1625). Tuning set: 0.121% either way (social -0.01, tech +0.01; 32 words fixed, 14 broken out of 5M). The remaining capital errors are specific names in specific texts (a story character "Max"): no general count settles them.

**Decided.** Not shipped. Engine support stays, off unless counts are loaded (`vAutoSwitchSetCapitalised`, `SIM_CAP`).

## 36. Vikey for Windows

**Status:** Agreed 2026-10-29, built on branch `windows` (Phases 0–5 of `Windows/PLAN.md` done); ships after the code-signing certificate arrives.

**User story.** As a Windows user, I want the same Vikey as on the Mac: same engine, same auto switch, same settings window, same words.

**Decided**

- Same engine, byte for byte: the Windows app feeds keys to `Engine/` and applies its output; the Windows CI runs the same rule lock (`check.ps1`) on x64 and ARM64.
- UI 1:1 with the Mac app (window, tray menu, onboarding, About, typing log), native Win32 + GDI+, no framework. Hotkey default Alt+Z.
- Per-site memory reads the address bar through UI Automation (Chrome, Edge, Brave, Vivaldi, Opera, Firefox).
- Per-user install (no admin prompt), Inno Setup, plus a portable zip. Auto update through the Mac appcast with Windows items, signed with the same EdDSA key.
- Code signing with a Certum Open Source Code Signing certificate in Tony's name (individuals only); unsigned builds are not published.
- Versions follow `project.yml` MARKETING_VERSION, released with `Tools/release/release.sh --windows <ver>` after the Mac release.

**Verified.** 14 live typing cases pass in a Windows 11 VM on ARM64 and on the x64 build under emulation; update from a local appcast installs and relaunches.

---
## 35. Look forward: the next word can flip a word put back to English

**Asked by Tony.** "go" on fix #1 for the top tie errors (Vietnamese words right after English stayed English: "em í ạ", "office ít nhất", "lát nữa").

**Decided.**
- A word auto switch put back to English is now the previous word for the next English-mode check. When the next word turns Vietnamese, the earlier word flips back too if word pairs say it reads Vietnamese before that next word at least 100x more often ("its nhất" -> "ít nhất", "last nữa" -> "lát nữa"). Mac, iOS driver, simulator and test harness.
- D22 ("after a clearly English word, a tie stays English") doesn't apply when that word was only put back to English by auto switch.
- Word-pair table now also holds tie word + the word after it (seen 20+ times): 160k pairs, ~2.4 MB of data.
- Tuning set: wrong words 0.123 -> 0.121%, manual switches 0.154 -> 0.158 (tech/mixed +0.01). All-caps typing 2.60 -> 1.77%. Locked test below.

**Status:** Built 2026-10-29, not in a release yet.

## 34. Longer context for ties: word pairs beat D22; triples not worth it

**Asked by Tony.** "longer context for ties (3-word phrases)".

**Decided.**
- Word triples (322k Vietnamese + 262k English, from the same 180M words) tried two words back before word pairs. No gain on the tuning set at any weight (0.124% either way): the remaining tie errors are not about missing context. Not shipped (would add ~5 MB); engine support stays, off unless loaded (`vAutoSwitchSetTrigrams`).
- What the error list showed instead: D22 ("right after a clearly English word, a tie stays English") overruled word-pair evidence. Now a pair vote that is 10x sure of the Vietnamese reading wins: "test có work", "em sẽ optimize", "team bên US", "à la carte".
- Tuning set: 0.124 -> 0.123% wrong, manual switches 0.159 -> 0.154. Locked test (once): 0.121 -> 0.118% wrong, manual 0.154 -> 0.147. Chat golden: manual switches 80 -> 66.

**Status:** Built 2026-10-29, not in a release yet.

## 33. Words after digits

**Asked by Tony.** "Cool, do that" (numbers glued to Vietnamese words in chat: 5trăm, 100tỷ, 4tuổi, 1cái).

**Decided.**
- Digits after a space (or at the start) begin a new word: Telex and auto-switch work on the letters after them ("mua 1casi" -> "1cái", "5trawm" -> "5trăm") in both modes. Digits glued to letters ("abc1def", "md5sum") keep the whole thing a code, as before.
- A lone w right after a digit stays w: units like 100W, 65w (was "100Ư").
- Tuning set: 0.126 -> 0.124% wrong words (social 0.23 -> 0.22%). Smaller than the ~0.02-point estimate: most digit-glued errors were units (W) and codes, not Vietnamese words.

**Status:** Built 2026-10-29, not in a release yet.

## 32. Backspace undo, then typing on

**Asked by Tony.** "bộ source vaay " -> "vây" -> Backspace -> "vaay" -> "j" + space should give "vậy".

**Decided.**
- After Backspace undoes a fix and returns to English, the restored keys are the current word again, so typing on checks the whole word ("vaayj" -> "vậy"). Finishing the same word unchanged keeps it as typed (no redo of the fix just undone). Mac, iOS driver; `driver/undo_test` in check.sh.
- iOS driver undo now matches the Mac: Backspace takes the break key too, the cursor stays at the end of the restored word.
- Not done (Tony: not useful): the reverse case, a Vietnamese word fixed to English, undone, then continued.

**Status:** Built 2026-10-29, not in a release yet.

## 31. Website in English; brand guideline

**Asked by Tony.** English version of the website (Vietnamese stays the default, easy switch, flag); a brand guideline used everywhere.

**Decided.**
- English pages at /en/ (same file names in `site/pages_en/`), a VI/EN pill with the target language's flag that keeps the page, hreflang + sitemap. The release script needs both changelogs.
- `design/BRAND.md`: name, wordmark ("Vi" ink + "key" indigo, bold), one accent color #4F46E5 (#8C87FF dark), system font, icon rules. `App/Model/Brand.swift` shared by Mac and iOS.
- Footer without the OpenKey line (credited in the FAQ and Acknowledgements). Settings labels shortened to one line on iOS and Mac. Mac switch sound "sparkle" (rising into Vietnamese, falling into English).

**Status:** Website live 2026-10-29; app parts not in a release yet.

## 30. Word pairs decide ties

**Asked by Tony.** "yes, try 4" (context beyond one word, for true ties like bus/bú, been/bên, mix/mĩ).

**Decided.**
- For a word that is a word both ways (734 ties), the engine looks up how often each reading follows the previous word in real text: Vietnamese web + forums with the English inside them (93M words), English web + Reddit (87M), our crawl (train part). Test-set sites and text excluded. Vote = 10·log10(Vietnamese pair count / English pair count), × 2, added to the frequency gap; common Vietnamese words still only switch on a strong English vote.
- Table: pairs whose second word is a tie form, seen 10+ times: 101k pairs, ~1.5 MB of data (`Engine/BigramData.h`, `Tools/autoswitch/build_bigrams.py`). Binary search, only for ties: per-key speed unchanged within the budget.
- Tuning set: wrong words 0.148 → 0.127%, manual switches 0.189 → 0.160 per 100. Locked test (looked at once): 0.139 → 0.119% wrong (99.86 → 99.88%), manual 0.178 → 0.151; mixed-typing groups 99.76 → 99.79%. Every group equal or better. "cho con bú", "xe bus", "ở bên", "it's been", "I lost it", "sang mĩ", "cách mix đồ".
- iOS contraction fixes: a rare Vietnamese reading ("thát") no longer blocks "that's"; only common Vietnamese words (score 10+, "im") do.

**Status:** Built 2026-10-29, not in a release yet.

## 29. Tuning set vs locked test set, mixed across topics

**Asked by Tony.** Tune against one set and keep another locked; "make sure we have a good mix in each, i don't want our engine to be tuned toward a specific topic/use-case".

**Decided.**
- Every test source is split 50/50 by message (md5 of the line, stable): `val` (tune against) and `test` (locked: only for reporting, never for choosing). 20% of our crawl leaves the counting text and is split between them. Built by `vikey-data/evalsets/build.py`; VietMix splits stay local.
- Groups: social/chat (synthetic chat, ViLexNorm, ViCTSD, VietMix, news/car-forum comments), tech (Tinhte, Viblo, tech forums), news (VnExpress), mixed (synthetic everyday), Vietnamese-only, English-only. The headline is the macro average: each group counts the same, whatever its size. All-caps typing is a guardrail reported beside it.
- `vikey-data/evalsets/run.py val` is what tuning looks at; `run.py test` only for release numbers. check.sh's ViLexNorm auto-correct lock uses the val half.
- Found and fixed a leak: the keep-list read the Tinhte test set. Web data is being filtered of test-set domains.
- Baseline (current engine): val 99.85%, locked test 99.86% (macro, lazy typist). Groups (test): social 0.25%, tech 0.23%, news 0.08%, mixed 0.23%, Vietnamese-only 0.01%, English-only 0.03% wrong words; caps 2.70%.

**Status:** Built 2026-10-29.

## 28. Web Vietnamese data; Early Switch rejected

**Asked by Tony.** "we need about 10 times data both vietnamese and english"; "Early Switch: if we detect 3 words in a row that are clearly in VN/EN, switch right away".

**Decided.**
- 93M words of spam-filtered web Vietnamese (FineWeb-2 incl. a forum/chatty slice, HPLT 2, MADLAD-400; ODC-By/CC0; `vikey-data/bulk`) mixed half and half into the Vietnamese frequencies, lower-only like the chat crawl (D27). English used inside that Vietnamese text raises those English words. Lazy wrong words vs 1.0.13: chat 0.26 → 0.25%, everyday mixed 0.23 → 0.22%, Vietnamese-only 0.05 → 0.04%, VietMix 0.69 → 0.65%, others equal; fewer manual switches.
- "aj", "ej", "oj", "ix" are not English words (subtitle junk): "aj" is ạ, also right after English ("esports ạ"). Contraction stems (didn, wasn…) are English.
- **Early Switch rejected (Tony, after the numbers):** switching after 3 words that can only be one language. To-Vietnamese never fires (the first Vietnamese-only word already switches); to-English helped synthetic chat (0.23 → 0.21%) but broke real code-mixed text, where English phrases are followed by Vietnamese again ("predict stock price v ạ"): ViLexNorm 14 words broken, 0 fixed. Kept in the simulator only (`SIM_EARLY=N`).

**Status:** Built 2026-10-29, not in a release yet.

## 27. Chat frequencies and VietMix fixes (Mac + iOS engine)

**Asked by Tony.** "see if anything we can improve (which can apply to both macOS + iOS app)" after the VietMix dataset (real Vietnamese–English code-mixed posts, held out).

**Decided.**
- Vietnamese word frequencies now mix the subtitles with 2.5M words of our own crawl of Vietnamese forums and comments (`vikey-data/freq/count_informal.py`, `build_dict.py`). Chat can only lower a formal-only syllable (vía, mêm) or add a chat word (ạ, đc, đt); it never raises a word the subtitles have ("lít", "thí" are common in car forums, but "list", "this" are what mixed typing means: raising them broke "This is" typed in VI mode).
- English words that Vietnamese chat uses get at least their in-chat frequency; "meme" added.
- đ shorthand with no vowel (đ, đc, đt, đk) counts as Vietnamese.
- Restoring a foreign word puts back the keys Telex undid on the way: "deepseek", not "deepsek" (only for 2+ vowel groups: "cuooocf" keeps the Telex undo).
- "(" right after a word ends it: "web(" not "ưeb(".
- An all-caps word not inside a run of capitals (after a lowercase word, or first) is an acronym unless the syllable is common (score 15): "con NAS". All-caps conversion from English needs a common syllable (15): "USA EST IRS" stays.
- Lazy typist, wrong words, before → after: chat 0.28 → 0.26%, everyday mixed 0.26 → 0.23%, Viblo 0.24 → 0.22%, Tinhte 0.28 → 0.26%, Vietnamese-only 0.09 → 0.05%, VietMix 0.75 → 0.69%, ViCTSD 0.20 → 0.19%, ViLexNorm 0.54 → 0.44%; English-only unchanged. Manual switches on chat 92 → 86.
- Rare syllables that now go English in VI mode (Telex lock updated): bín, bôp, bú, cỏn, dôm, hôt, mốn, séc, soả, trút, vía, ă (aw).

**Status:** Built 2026-10-28, not in a release yet. Rules D27 in rules.tsv.

## 26. Auto-correct and the suggestion strip on iOS

**Story:** As someone typing fast on the phone, I want a word I mistyped by one key fixed when I press space, and words to pick from above the keys, in both Vietnamese and English.

**Decided:**
- A strip of three words above the keys, like the system keyboard: the word as typed (in quotes), the correction (highlighted when space will put it in), then other fixes or ways to finish the word. Fixes come first when the typed word isn't a word.
- Candidates are one-key edits (a nearby key, an extra, a missing or two swapped keys), scored by word frequency, by where each key was actually touched, and by mode (the other language costs a bit). `Engine/TouchModel::candidates`.
- Auto-correct is conservative: never on a real word (either language), valid Vietnamese spelling, English inflections, words under 3 letters, or words with 2+ capitals. The correction must clearly win.
- Backspace right after a correction puts the typed word back and keeps it. Tapping the quoted word keeps it too.
- The correction's language becomes the mode (when auto switch is on).
- Off in web address, email and number fields, and wherever the app turns off autocorrection.
- Settings: "Gợi ý từ" and "Tự sửa lỗi gõ", both on by default. The strip only shows when one of them is on.
- The strip is worked out after the key's text is sent, so it never delays typing.

- Tested on real data (vikey-data/public, private): finger scatter calibrated on 60k real taps (Google TSI, Aalto): ~1.8 mm, i.e. 0.30 key width × 0.20 key height on iPhone; the model uses σ 0.35 × 0.25.
- A keep-list of 16k words people really type (shorthand, teencode, slang, brands: ntn, cta, xog…), from 2.65M words of crawled Vietnamese forums/comments (`Engine/KeepWords.inc`, `Tools/autoswitch/keep/gen_inc.py`). Real social comments (ViLexNorm, held out): right words broken 0.17% → 0.06%.
- A long non-word gets a rarer fix (floor −4 for 6+ letters): real English typos (GitHub Typo Corpus) fixed 15% → 33% ("occured", "funtion", "defualt").

**Locked by:** check.sh auto-correct runs (clean typing ≤ 0.10% false fixes; sloppy typing errors cut by ≥ 25%); real data (GitHub typos fixed ≥ 30%, wrong ≤ 5%; ViLexNorm right words broken ≤ 0.10%); iOS tests: teh→the plus Backspace undo, clean mixed text unchanged, strip contents, strip time per key, key time per key.

**Status:** Agreed (built, not in a TestFlight build yet).

## 25. Fat-finger model on iOS

**Status:** Built 2026-10-28, not shipped yet. Locked in `check.sh` (touchsim) and iOS tests.

**Asked by Tony.** "Fat finger detection: give each key a bigger region depending on the current typed word. Probably a small neural network?"

**Decided.**
- No neural network for now: a statistical model is tiny, fast and testable. `Engine/TouchModel` scores each key near the touch by (2D Gaussian distance, σ = 0.35 key) + 0.5 × log P(key | letters of the word so far). The prior comes from the dictionaries (sorted lists, so every next letter's mass is two binary searches), mixed 80% current mode / 20% the other language, tuned on mixed text. A touch inside the central 80% of a key always types that key (retuned so careful typists lose nothing).
- Measured with `Tools/autoswitch/touch/touchsim` (touches scattered around key centers, σ = 0.30 key; mode taken from the engine as it types): wrong words 26.8% → 13.6% on chat, 27% → 13.7% everyday mixed, 26% → 14% Viblo; English-only and Vietnamese-only halve too. Careful typing (σ = 0.20): unchanged. 3.5 µs per touch.
- Next: a mixed-text word-frequency list (English inside Vietnamese, chat shorthand) to replace the subtitle frequencies, then the suggestion bar and auto-correct on the same model.

## 24. Performance is locked by tests

**Status:** Done 2026-10-28.

**Asked by Tony.** The first iOS builds felt laggy. "We should have tests guarantee performance in case we do something that make it slow in the future."

**Decided.**
- `check.sh` (and CI) types the chat corpus through Engine/Driver and fails when a key takes more than 1 µs on average or 20 µs at the 99.9th percentile. Today: ~155 ns average, ~1.8 µs p99.9, so slower CI machines still pass.
- `check.sh` also checks the driver (what the iOS keyboard runs) gives exactly the simulator's output.
- iOS tests (`iOS/Tests`, CI job "ios"): at most one read of the text field per key (each is a cross-process call on a device), no shared-settings reads while typing, every key shadow has a fixed path (no offscreen rendering), and under 300 µs of keyboard logic per key in the simulator (~51 µs today). The lag in build 2 came from exactly these, not from the engine.

## 23. Capitals, Caps Lock Vietnamese and Wi-Fi

**Status:** Done 2026-10-28 (1.0.12). Rule cases D23.

**Asked by Tony.** "Check and see if there is any easy win we can do to improve metrics." Found by ranking the wrong words on the new 1.8M-word test data (vikey-data).

**Decided.**
- VN mode: a word with 2+ capitals that Telex turned into a rare (under ~3 per million) or invalid syllable is an acronym or a name: keys put back and English (NÁ → NAS, HĐ → HDD, ỐM → OSMO). Vietnamese acronyms where only đ changed stay (VĐV, HĐND, BĐS). A doubled tone key in a capitalized word keeps both presses (ASUS, not AUS).
- EN mode: all-caps words could never convert before, so Caps Lock Vietnamese after any English or unknown word got stuck in English (39% wrong words on an all-caps news test). Now an all-caps word converts when it is inside a run of capitals, the keys are not English, and the syllable is common. A lone HW or CWS stays.
- Before a hyphen, a rare w-word ("Ưi") starts an English compound: Wi-Fi. Vietnamese loanwords with hyphens (nơ-ron, rơ-moóc) stay.
- The simulator's text-to-keys tool now types all-caps words with Caps Lock (modifier keys in capitals too).

**Result.** Tinhte wrong words −52% (5,129 → 2,450), Viblo −9%, VnExpress −12%, all-caps news 64,404 → 2,878; English-only sets unchanged. Average no-switch accuracy over the 10 sets: 99.83%.

## 22. Ties: frequency ratio decides, a clearly English neighbor guards

**Status:** Done 2026-10-28 (1.0.11). Rule cases D22. Tony picked option A.

**Asked by Tony.** Ties (keys that are a real word in both languages) caused most manual switches. Use look-behind/look-forward and the frequency ratio; which works better? Run scenarios.

**Measured** (simulator, manual switches for a switch-by-hand typist / wrong words for a lazy one):

| Variant | Chat | Viblo |
|---|---|---|
| Before (1.0.10) | 166 / 87 | 1,113 / 773 |
| Ratio ≥10x both directions | 64 / 52 | 992 / 909 (worse: "mô tả" → "mô tar") |
| Ratio ≥10x in EN mode only | 76 / 52 | 813 / 728 |
| + "thí" look-behind | 63 / 48 | 808 / 724 (breaks D5.3) |
| **Option A (shipped)** | **94 / 60** | **861 / 729** |

Neighbors as the main signal made things worse (mixed text puts English nouns next to Vietnamese words: "test lại"); the ratio does most of the work.

**Decided (option A).**
- EN mode: an English word converts to its Telex reading when that Vietnamese word is at least 10x more common (cos → có, nos → nó, sex → sẽ, toots → tốt). The old "only if the English word is under ~1 per million" floor is gone.
- Except right after a clearly English word: "the theme of rust", "video call cos" stay English, so D5.3's cases still pass.
- VN mode: a tie leaning English (3x+) right after a clearly English word goes English ("video call thí" → "this"). The Vietnamese floor is kept: tech Vietnamese ("mô tả", "ít", "rút") must not lose to rarer English words.
- Option B (convert ties even inside English text, 63 / 808) was not chosen.

## 21. Pause auto switch for 5 seconds after a manual switch

**Status:** Done 2026-10-28 (1.0.9).

**Asked by Tony.** "When a manual switch is triggered, disable auto switch for 15 seconds, this should be an option, enabled by default."

**Decided.**
- A switch by hand (hotkey, menu bar, main window) turns auto switch off for 5 s (first 15 s; Tony: "make it 5s", 1.0.10); another manual switch restarts it. Afterwards the word being typed is skipped and checking starts with the next word.
- Option "Tạm dừng 5 giây sau khi chuyển tay", on by default (`PauseAutoAfterManual`). App-level only: engine rules and their tests are unchanged.

## 20. Words after ( and ", and no Backspace undo for multi-word fixes

**Status:** Done 2026-10-28 (1.0.8). Rule cases D20.

**Asked by Tony.**
- "(thí create " didn't change "thí" into "this".
- "thí create " became "this create ", then Backspace (to add an s) turned it back into "thí". "Maybe we shouldn't do the backspace to undo thing when there are more than 1 word involved?"

**Decided.**
- An opening ( [ " or ' starts a whole word, like a space does (both modes). "(thí create" → "(this create". Still not after . _ - / (identifiers, paths).
- Backspace right after an auto-fix undoes it only when the fix changed the last word alone. When look-back also fixed earlier words, Backspace just deletes a character.

## 19. Doubled tone key: Telex result by default, the dictionary decides at the end of the word

**Status:** Done 2026-10-28 (1.0.7). Rule cases D19. Replaces D8's "keep both letters" default.

**Asked by Tony.** "v e r r" showed "vẻ" then "verr". "Make it show 'ver' by default and only show 'verr' if it's a valid English word... the majority of people would have muscle memory from previous apps."

**Decided.**
- Pressing the same tone key again still switches to English. On screen: the Telex result ("ver"), unless the keys with both presses are already an English word ("err", "boss", "less").
- At the end of the word the dictionary decides both ways: "terible" → "terrible", "sory" → "sorry", "mixx" → "mix". If both or neither spelling is English, the word stays as shown. Backspace right after undoes the fix.
- Trade-off: an unknown name with a double letter typed in VN mode keeps one letter ("Nassr" → "Nasr"). In exchange, Vietnamese typos with a doubled tone key give the familiar Telex result ("bijj" → "bij"); wrong words on the chat-with-typos test went from 22 to 7.

## 18. "mĩ" + x gives "mix", not "mixx"

**Status:** Done 2026-10-28 (1.0.5). Rule cases D18.

**Asked by Tony.** In VN mode "let's mix" gave "mĩ" (a tie: mĩ is Vietnamese). Pressing x again by muscle memory gave "mixx". Tony picked option A.

**Decided.**
- After a doubled tone key switches to English, the end of that word decides: if the word with both presses is not English but the word with one press is, the extra press goes ("mixx" → "mix", "mixxer" → "mixer"). When both presses make a word they stay (err, boss, tomorrow). Words still being typed are never touched. Backspace right after puts "mixx" back.
- A key Telex drops when undoing a doubled vowel ("eee" → "ee") now counts for the English check: seemed, cheese, defense. Three of the same vowel alone stays Telex ("aaa" → "aa").
- Option B (the previous English word as a hint for ties) was not chosen.
- Tie count, typing the most common English words in VN mode: 27 of the top 1,000 stay Vietnamese (this/thí, there/thể, see/sê, did/đi...). Those are real syllables and stay Vietnamese without other signals.

## 17. Doubled tone key restores every key; no vanishing text

**Status:** Done 2026-10-28 (1.0.5). Rule cases D17.

**Found while counting tie cases for Tony's "let's mĩ" report.**
- A doubled tone key that switches to English now puts back the whole word as typed. Before, only the tone was undone: "tômrrow", "surprrise", "êpss". Now: tomorrow, surprise, express. "err" still keeps both letters (D8).
- A w-vowel (ư) that Telex turns back into u is written as a letter. Before, an empty character was sent and the text vanished ("were" + n).
- A word that has no marks left but where Telex ate a key is put back if the typed keys are English. This only helps when the engine still has every key; the "eee" undo drops one ("seemed" → "semed"), which is still open.

**Open with Tony:** how to resolve ties like mix/mĩ (end-of-word dictionary fix for doubled keys, and/or the previous English word as a hint).

## 16. Auto update

**Status:** Done 2026-10-28 (1.0.4).

**Asked by Tony.** "Add auto update and check for update feature to the app."

**Decided.**
- Sparkle 2. Feed at vikey.app/appcast.xml, written by `release.sh`. Updates are EdDSA-signed; the private key is `~/.secrets/vikey_sparkle_ed25519`, the public key is in Info.plist.
- On by default: checks daily, downloads in the background, installs when Vikey quits (or after the user confirms). The "Tự động cập nhật" toggle in the main window turns it off.
- Menu: "Kiểm tra cập nhật…" checks now and shows Sparkle's window (in Vietnamese).
- Tested end to end with a local 1.0.5 test build: automatic install on quit, and the manual check → install → relaunch.

## 15. Per-site memory off by default, (?) on every option, debug menu hidden

**Status:** Done 2026-10-28.

**Asked by Tony.** "Disable 'nhớ riêng cho từng trang web' by default, upon enable, ask for the permission right away. Add (?) icon and tooltip to explain each option." Then: "hide the Nhật ký gõ menu, normal people aren't supposed to see that."

**Decided.**
- Per-site memory starts off. Turning it on reads the active tab of every running supported browser once, which shows macOS's Automation prompt right away. If the user refuses, the option turns back off and an alert links to System Settings › Automation.
- Every setting in the main window has a (?) after its title: tooltip on hover, popover on click.
- The Gỡ lỗi (typing log) menu is not in the menu bar icon's menu. It lives in the Vikey app menu (shown while a Vikey window is open), always visible there (Tony 2026-10-28).

## 14. Quotes and apostrophes end a word

**Status:** Done 2026-10-28. Rule cases D14 in `rules.tsv`.

**Asked by Tony.** "trang ưeb" (typed inside double quotes) and "doén't" didn't change automatically.

**Decided.**
- A closing `"` or `)` ends a word like a comma does, in both modes: an English word typed in Vietnamese mode is put back before it.
- An apostrophe also ends the word. For contractions, the stem without its final n counts as English: does+n't, is+n't, was+n't. So "doesn't" switches to English at the apostrophe.
- A Vietnamese word in single quotes stays Vietnamese ('tiếng', 'nhà').

## 13. Catch "thí makes" one word sooner

**Status:** Done 2026-10-28. In the engine and on the website demo.

**Asked by Tony.** "Can we detect this sooner? Ideally correct at 'thí makes'. Currently I type 'thí makes' and 'thí' is still not changed."

**Rule (D13).** A word that looks the same in both modes but is English-only ("makes"), right after an ambiguous word that stayed Vietnamese ("thí", which reads as "this"), means the pair is English. Vikey switches at "makes" and fixes "thí" → "this".
- It fires only when the immediately preceding word is ambiguous and its English reading is at least AS_PLAIN_MARGIN = 15 points (about 30×) more common than its Vietnamese one. "this" vs "thí" clears that by 20.

**Measured**

| Setting | Chat: wrong words with no manual switching | Viblo: manual switches |
|---|---|---|
| Margin 0 | 0.49% → 0.42% | 16 more (Vietnamese words before a tech term like "… token", "… file") |
| Margin 15 (chosen) | 0.49% → 0.46% | 2,116 → 2,117 |

**Bug found and fixed.** After leaving Vietnamese mode at a comma, the engine's "the next word starts after punctuation" flag survived the round trip through English mode. So the first word after switching back was not checked ("em đang tét"). `vAutoSwitchForget()` now resets it.

**Rule lock.** D13 cases were added. The golden diffs were reviewed:
- six chat lines improve: "call thí Sunday" → "this Sunday", "há tô many changes" → "has too many changes";
- one line keeps the rare "bừ" typed as keys.

**Website demo examples (Tony: "too many server examples, be creative").** Five scenes, each one broken by plain Telex:
- merge → mẻge;
- issue → isue;
- "this makes sense" → "thí makes sến";
- version → veión;
- test → tét. (Tony: every scene is English typed while in Vietnamese mode, since that is the convincing case.) Highlights appear as soon as a word diverges.

**Known.** "pair" in Vietnamese mode stays "pải", because the subtitle word list contains the misspelling "pải" (of "phải") with a low score. The fix for later is cleaning typo-like rare syllables out of the Vietnamese list.

---

## 12. Typing log is an opt-in debug feature; credits in the About window

**Status:** Done 2026-10-28 and released as 1.0.1.

**Asked by Tony.**
- "Nhật ký gõ is only a debug feature. Make it opt-in from the app and remove mention of it on the website."
- Move the credit line from the main window into the standard macOS About window.

**Decisions**
- **Typing log off by default.** With the log off, nothing is counted or recorded: every log entry point returns immediately, so the log costs nothing on the typing path.
- **Where it lives now:** the menu bar submenu "Gỡ lỗi" → "Ghi nhật ký gõ" turns it on. "Xem nhật ký gõ…" opens the window and is disabled while logging is off. The main window no longer has a log row.
- **Website:** every mention is removed (changelog, FAQ, privacy). The README documents it under "Debug typing log".
- **Credits:** the main window footer is gone. The standard About panel ("Giới thiệu Vikey" in the menu) shows Tony Dinh, the OpenKey/Tuyen Mai credit, GPL-3.0, and links to vikey.app and GitHub. The Info.plist copyright is "© 2026 Tony Dinh".
- **Repo:** the GitHub repo's website is set to https://vikey.app.

---

## 11. vikey.app, open-source release, compact window

**Status:** Done 2026-10-28. Vikey 1.0.0 is public at https://vikey.app and on GitHub Releases.

**Asked by Tony.**
- Buy vikey.app and make a free, open-source website like clipaudit.app.
- Add the author Tony Dinh, while still crediting OpenKey by Tuyen Mai.
- Opening the app while it runs shows the main window.
- Make the main window smaller and drop the sidebar.
- Put the history in its own window.

**Decisions**

- **Domain.** vikey.app was registered on the Cloudflare dedicated account ($8.20; renews at $14.20).
  - Hosting: Pages project "vikey".
  - www redirects to the apex with a 301.
  - Always-HTTPS is on.
- **Website.** Vietnamese, ClipAudit's layout and style, indigo.
  - **Home hero:** the typing demo is replayed from frames that the real engine produced key by key (`harness frames`), so the site shows exactly what Vikey does.
  - **Screenshots:** 2x, taken on a BetterDisplay HiDPI virtual screen.
  - **Pages:** download (DMG plus checksum), changelog, FAQ, privacy.
  - **Copy review:** reviewed by a subagent (avoid-AI-writing and copywriting rules). That fixed two factual errors: keystroke logging is on by default, and Firefox has no per-site memory.
  - **Phones:** the download button leads to /download, which explains that Vikey is for Mac.
- **Release.** `Tools/release/release.sh <version>` runs:
  1. bump the version;
  2. the engine rule lock;
  3. a universal build (arm64 + x86_64);
  4. sign and notarize the app;
  5. build the DMG (signed, notarized, stapled);
  6. site/downloads and release.json;
  7. the GitHub Release;
  8. deploy.
- **App.**
  - **Window:** one compact window (460 points wide): a language switch on top, then hotkey, auto-switch, typing settings and launch at login, with an author/credit footer.
  - **History:** the typing history opens in its own window.
  - **Reopening:** reopening Vikey shows the main window, and opening it by hand also shows the window.
- **Bug found while taking screenshots.** Letting the window track the SwiftUI content size live crashed AppKit's constraint pass on a HiDPI (Retina) screen. The window is now sized once from the content.

---

## 10. Vikey: new name, new app, fewer features

**Status:** In progress 2026-10-28.

**Asked by Tony.**
- "I want to make this official. Let's call this project Vikey."
- Rename everything, including the repo and package name.
- Rebuild the UI in modern SwiftUI.
- Remove VNI.
- Remove the old, irrelevant features.

He approved Otto's suggested cuts and added: "gõ tắt: remove".

**Decisions**

- **Name.**
  - Repo: tonyminibot/Vikey.
  - App: Vikey.
  - Bundle ID: com.devuap.vikey.
  - Core: VikeyCore.
  - Credit: "based on OpenKey by Tuyen Mai" is kept in the About screen, the file headers and the README, as GPL-3.0 requires.
- **Kept**
  - Telex, with modern or classic tone placement.
  - Spell check, and restore-if-invalid.
  - Switch hotkey and beep.
  - Smart switch per app and per website.
  - Auto-switch and the typing log.
  - Browser autocomplete workarounds, built in.
  - Non-QWERTY layouts (Dvorak etc.), now detected automatically.
  - Turning off for other input languages, now automatic and checked once per input-source change instead of on every keystroke.
  - Launch at login.
- **Removed**
  - VNI, Simple Telex 1 and 2 (Simple Telex came back as a setting in D56).
  - Code tables other than Unicode (TCVN3, VNI Windows, Unicode compound, CP1258) and remembering a code table per app.
  - The convert tool.
  - Quick Telex (cc → ch…) and quick start/end consonants.
  - Z/F/W/J consonants.
  - Auto-capitalizing the first letter.
  - Macros (gõ tắt).
  - The Ctrl key turning spell check off, and ⌘ turning the app off.
  - Send-keys-one-by-one mode, and the alternative Chromium fix.
  - The update checker (it pointed to upstream).
  - Dock icon and show-on-startup options.
  - Windows and Linux code.
  - The old AppKit/storyboard app.
- **New app.**
  - SwiftUI menu bar app (MenuBarExtra, macOS 13+), generated with xcodegen.
  - Settings window: General, Typing, Auto switch, About.
  - Accessibility onboarding that starts Vikey as soon as permission is granted.
  - Typing-log window and hotkey recorder.
  - SMAppService for launch at login.
  - OpenKey's settings, per-app/per-site memory and typing log are migrated on first launch.
- **Restore-if-invalid is on by default (it was off in OpenKey).** Measured with the simulator:
  - chat: 0.95 → 0.84 manual switches per 100 words;
  - Viblo: 1.02 → 0.79;
  - chat with typos: 1.18 → 0.96;
  - Viblo with typos: 1.37 → 0.95;
  - wrong words drop too.

  The test harness now runs with the app's real defaults. The Telex lock changes were reviewed: only invalid "syllables" from the subtitle list (âi, bãt, bĩan…) are now kept as typed; no real syllable changed. D9's expected text became the typed keys, while the language still stays Vietnamese. A D10 rule was added.
- **Bug fixed along the way.** An app without a bundle ID no longer makes the per-app memory save to the previous app.

**Verified**
- The rule-lock suite was unchanged after every removal step: 51/51 rules, identical snapshots and metrics.
- The new app passes the live typing suite.

---

## 9. Typos must not flip the language

**Status:** Fixed 2026-10-28 and sent as OpenKey-2.0.4-typo.

**Reported by Tony.** He meant "lắm" but typed "lanwm": "I guess this will happen a lot."

**Findings**

- **EN mode, fixed inside the word:** fine. "lan⌫wsm" converts to "lắm", because the word buffer follows Backspace.
- **VN mode, a typo at the word end:** the "unknown foreign word" rule from decision 8 restored "lănm" to "lanwm" and switched to English. Backspace undo recovers it, but it's wrong.

**Decision.** A word that uses Vietnamese-only modifiers (â ê ô ă ơ ư đ) is never treated as an unknown foreign word. It's a Vietnamese typo and stays in Vietnamese mode. Foreign words like TestFlight and analytics only pick up tone marks, so they still restore.

**Measured.** The simulator now injects realistic typos into 3% of Vietnamese words: a neighbouring key, a dropped letter or a doubled letter.

| Corpus with typos | No guard | Guard |
|---|---|---|
| Chat: manual switches per 100 words | 1.32 | 1.20 |
| Chat: wrong words | 0.33% | 0.13% |
| Viblo: manual switches per 100 words | 1.60 | 1.39 |
| Viblo: wrong words | 0.55% | 0.24% |

On typo-free text the guard costs a little (chat 0.95 → 1.04), but real typing has typos.

**Refined 2026-10-28.** The rule-lock suite caught a conflict: the guard broke D8.2, because "analytics" Telexes to "ânlytics" and the â looked like a Vietnamese mark. The guard now applies only to words whose keys have a single vowel group, since no Vietnamese syllable has more. Result: all 49 rule cases pass; clean chat is back to 0.95 manual switches per 100 words, Viblo 1.02, chat with typos 1.18, Viblo with typos 1.37.

**Also in this build.** The typing log records which shortcut ended a word (⌥⌫, ⌘A…) instead of just "shortcut".

---

## 8. Tuning with a mixed-text typing simulation

**Status:** Built 2026-10-28 and sent as OpenKey-2.0.4-lookback. The double-key change was approved by Tony the same day and shipped in OpenKey-2.0.4-err.

**Reported by Tony.** "I typed 'this makes' in VN mode and got 'thí makes'. Can we correct thí → this?" Then: "Run the engine over a large mixed Vietnamese/English text, key by key, to see how close it gets without manual switching."

**Method.** `Tools/autoswitch/sim` types two corpora into the real engine and mirrors the macOS layer:
- 21k words of chat-style messages;
- 269k words of Viblo tech articles.

Vietnamese words are typed in Telex with varied tone positions: 60% at the end, 25% after the vowel, 15% after the vowel cluster. Two user models:
- **Ideal:** switches by hand only when the next word would come out wrong. Metric: manual switches per 100 words.
- **Lazy:** never switches. Metric: wrong words, and the share of messages typed perfectly.

**Results, manual switches per 100 words (chat / Viblo)**

| Build | Chat | Viblo |
|---|---|---|
| Auto-switch off | 11.98 | 5.41 |
| First auto-switch build | 1.82 | 1.41 |
| This build | 1.51 | 1.12 |
| This build + keep both letters on the double key | 0.95 | 1.00 |

With no manual switching at all, wrong words go 9.8% → 1.2% (chat) and 3.2% → 1.0% (Viblo). With the double-key change, 94% of chat messages are typed perfectly.

### Decisions

- **Look-back.** When a word confidently switches the language, the 1–2 words just before it are fixed too, if they were ambiguous and joined by single spaces. "this makes sense" no longer leaves "thí"; "check mail cos theer" becomes "check mail có thể". Words that look the same in both modes ("makes", "cho") are passed through. It helps most when nobody switches by hand.
- **Unknown foreign words.** In VN mode, a word that is neither a real syllable nor in the English list, and fails the engine's own spelling rules, is restored and switches to English: TestFlight, analytics, workflow, postgresql. Without the spelling-rule check, rare real syllables like riệt and chịt were wrongly restored. Minimum 3 letters.
- **Undo moved to Backspace.** This changes decision 5. The simulation showed the hotkey can't mean both "undo that fix" and "the next word is in the other language": after "wifi" was correctly fixed, pressing the hotkey to type "tốt" reverted it to "ừii". Backspace right after an auto-fix now restores the original word and language, like macOS autocorrect. The hotkey is always a plain switch.
- **"cors gif" in EN mode now becomes "có gì".** Look-back judges the common Vietnamese phrase more likely.
- **Decided by Tony, 2026-10-28: keep both letters on a double key ("let's switch to 'err', I want to try it out").** Doubled-letter English words (issue, miss, sorry, pass, office, error, password, traffic) cause about a third of the remaining manual switches, because Telex drops one letter. Keeping both letters when the double key switches to English cuts chat manual switches from 1.51 to 0.95 per 100 words. This replaces rule 1's earlier "er" output: when the doubled tone key switches to English, it now outputs both letters (err, issue, sorry). Default on, controlled by `AS_DOUBLE_KEEP`.
- **Bug fixed along the way.** Look-back history is now cleared when the language changes from the menu and on every app switch, so a rewrite can never use stale word lengths.

---

## 7. Typing log to measure and tune auto-switch

**Status:** Built 2026-10-28 and sent as OpenKey-2.0.4-log. Tony uses it and sends the log for tuning.

**User story.** As the user, I want Vikey to record how often I switch language by hand versus how often it switches for me, plus what I typed, so the engine can be tuned. The goal is the fewest manual switches.

**What is recorded.** The log is `~/Library/Application Support/Vikey/typing-log.jsonl`, one JSON object per line. The file is user-only (0600) and rotates at 20 MB, keeping one old file.

| Event | When | Fields |
|---|---|---|
| word | each word | raw keys as typed (⌫ = Backspace), starting language, the key that ended it, app, website |
| auto | automatic switch | rule: vi_word_to_en, en_word_to_vi or double_key |
| manual | switch hotkey or menu | — |
| undo | the switch hotkey pressed right after an automatic switch, with no other key in between | counted separately from manual |
| context | smart switch changed the language for an app or website | — |

- Words are logged as raw keys, not output. Replaying the keys through the test harness reproduces exactly what the engine did, so new rules can be evaluated on real typing.
- Daily counters cover words (VN or EN), manual switches, each auto rule, undos and context switches.

**Privacy.** Switch counts are always kept. Word keys are never recorded:
- while macOS secure input is on (password fields);
- in password managers: 1Password, Bitwarden, Apple Passwords, Keychain Access, LastPass, Dashlane, Enpass, KeePassXC, NordPass;
- in terminals: Terminal, iTerm2, Warp, Ghostty, Alacritty, kitty, WezTerm, Hyper, Termius. sudo and ssh passwords there are typed without secure input.

Word recording can be turned off in the log window. Nothing leaves the Mac; Tony sends the file himself.

**Screen.** The status menu gets "Nhật ký gõ...". The window shows:
- counts for today, 7 days and all time, including manual switches per 100 words;
- the latest 300 events, with manual switches and undos in orange;
- the word-recording checkbox;
- Refresh, Open log file and Clear log buttons.

**Performance.** Each key appends one character to an in-memory buffer. Each word creates one JSON line in memory. The file is written on a background queue every 10 s or every 8 KB.

**Analysis.** `Tools/autoswitch/analyze_log.py` prints the counts and every manual switch or undo with the words around it. Those are the cases to fix.

---

## 6. Auto-switch must not touch programming words

**Status:** Fixed 2026-10-28 and sent as OpenKey-2.0.4-autoswitch2.

**Reported by Tony.** "I typed `var`, a popular short form for variable. We probably need to handle a lot of things for developers too."

**Cause.** "var" isn't in the English dictionary, and its Telex replay "vả" is a real Vietnamese word, so English mode converted it.

**Decisions**

- **Programming words count as English.** About 250 terms were added: keywords, CLI tools, file types and common short forms like var, const, def, args, ctx, deps, refs, uri. Identifier words that collided with Vietnamese were also added. These were found by splitting camelCase/snake_case identifiers across real projects and replaying each through the engine, then reviewed by hand. Only that reviewed list is in the repo.
- **Identifier guard.** Only a word that starts after a space, Enter, Tab, click or shortcut is ever checked. Text after . _ - / ( and similar is never touched, so obj.dir, os.path, my_var and image.gif stay as typed. This applies in both modes.
- **Not added.** "gif" and "conf" type the very common Vietnamese "gì" and "còn". Those win in English mode when the word stands alone. File names are covered by the guard.
- **Vietnamese mode keeps Vietnamese.** I tried letting programming words beat moderately common Vietnamese words in VN mode, and rejected it: it broke "mô tả" (tar) and "vả lại" (var). In VN mode, "var" stays "vả".
- **Known limit, from Tony's double-key rule.** In VN mode, err, error and less lose one letter (er, eror, les) and switch to English, because Telex consumes the doubled key.

**Result.** In English mode, 0 of the ~250 programming words are changed except "gif", which is deliberate. All regression cases and live cases pass.

---

## 5. Auto-switch language from correction behavior

**Status:** Built 2026-10-28 and sent as OpenKey-2.0.4-autoswitch. Tony is testing.

**User story.** As someone who types Vietnamese and English all day, I want Vikey to notice when I'm typing in the wrong mode and switch for me, so I rarely press the switch hotkey.

### Tony's rules (2026-10-28)

1. **Double modifier key, VN → EN.** Pressing a tone or mark key twice inside a word (e r r) switches to English immediately. The output stays the normal Telex result ("er", not "err"). Telex has a good reason for that.
2. **Word end in VN mode, VN → EN.** If the raw keys, exactly as typed and in that order, are an English word, and the Vietnamese result isn't a real syllable, restore the keys and switch.
   - m-o-r-e means "more", so it switches.
   - m-o-e-r is meant as the odd Vietnamese "mỏe", so it stays.
3. **Word end in EN mode, EN → VN.** If the raw keys aren't English, and replaying them through the engine's own Telex rules gives a real Vietnamese syllable, convert and switch.
   - Modifiers can sit anywhere: "truowfng", "truongwf" and "truwownfg" all give "trường".
   - Plain words that look the same in both languages never trigger.
4. **Scope.** Good enough for day-to-day use, not every edge case.
5. **Performance.** Nothing we add may affect the app's performance.

### Decisions made while building (Otto)

- **Word lists.** Two lists are compiled into the binary, so nothing is loaded or parsed at runtime:
  - about 6.9k real Vietnamese syllables;
  - about 41k English words, with subtitle frequencies (FrequencyWords, CC BY-SA 4.0).
  - The English list keeps only words the macOS spell checker accepts, lowercase or capitalized. It also gets chat shorthand without apostrophes (dont, hes, im, lol) and developer words (rust, json, repo).
- **When both readings are real words.** The keys could be an English word and also give a real syllable (box → bõ, last → lát). Then the current language is kept unless both of these hold:
  - its word is rare: under about 10 per million for Vietnamese, or under about 1 per million for English;
  - the other reading is at least 10x more common.
- **What that means in practice:**
  - In VN mode, "best", "must", "test", "most", "rest", "list", "now", "how", "his", "is", "of" all restore to English.
  - "last" and "lost" stay "lát" and "lót", because those are common Vietnamese words. The point is to protect tô, thí, á, sê.
  - In EN mode, real English words like "theme", "rust", "sex", "car" and "been" are never converted.
- **Acronyms and identifiers.** A word with 2 or more capital letters is never converted (USA, EST, camelCase).
- **Word ends.** The checks run on space, comma, period, semicolon, Enter, ? and !. They don't run on arrows, Tab, Esc, clicks, shortcuts or apostrophes, so "hasn" in "hasn't" is never checked.
- **Undo.** Pressing the switch hotkey right after an auto-fix puts the original word and language back. Any other key or click cancels the undo. Text undo works in the Unicode code table; in other code tables the hotkey only switches the language.
- **Smart switch memory.** An auto-switch behaves like the hotkey, so it updates the per-app and per-site memory.
- **Setting.** The menu item "Tự chuyển Anh/Việt khi gõ" is on by default. It works in the Telex family only; it's disabled for VNI.
- **Performance, measured.**
  - Nothing runs per letter.
  - At a word end in EN mode, the check is a replay of up to 30 keys plus two binary searches, about 1 µs.
  - In VN mode, the check is two binary searches.
  - No allocation, file access or system calls on the typing path.
- **Platform.** macOS only. The Windows build would need `vAutoSwitchByTyping` defined.

### Tests

- `Tools/autoswitch/test`: 28 regression cases on the real engine, plus a bulk accuracy test.
- Live tests on the real app with synthetic keystrokes: 18 cases in a native text view and 8 in a Chrome textarea, all passing, including undo.

---

## 4. Learned (neural-network) switching

**Status:** Rejected (2026-10-27)

**User story.** As a user, I want a model trained on my own behavior to predict the right language for each new context, so manual switches go down over time.

**Decision.** Tony decided it's overkill and dropped it. Nothing was built.

---

## 3. Smart switch remembers the language per website

**Status:** Shipped in build OpenKey-2.0.4-site, commit e68b851 (2026-10-27)

**User story.** As a browser user, I want smart switch to remember my language per website, not just per browser. Then github.com stays English and facebook.com stays Vietnamese.

**How it works**

- Supported browsers: Chrome (stable, beta, canary), Brave, Edge, Arc, Vivaldi, Chromium, Opera, Safari and Safari Technology Preview.
- The memory key becomes `bundle|host`. A leading "www." is dropped.
- The tab URL is read with AppleScript, off the main thread.
  - It is polled every second while a browser is frontmost.
  - It is re-checked shortly after a click or a ⌘/⌃ key.
- A new site keeps the current language until I switch there.
- Non-web tabs (new tab, chrome://) use the browser's app-level memory.
- The setting is `SmartSwitchPerSite` and defaults to on.
- The first use of each browser asks for Automation permission.

**Not supported.** Firefox has no AppleScript tab URL.

**Related fix.** The restored language value is now masked to the language bit, so it works when "remember code table" is on.

---

## 2. Take the good parts of upstream PR #333

**Status:** Shipped, commit c126604 (2026-10-27)

**User story.** As a user, I want the reliability fixes from upstream PR #333, but not its behavior changes.

**Taken**

- Input-source over-release and leak fix.
- No longer tapping mouse-drag events.
- Tagging the app's own synthesized events.

**Skipped.** The forced-English app list. That is a behavior change Tony didn't ask for.

---

## 1. The app stops typing until restarted

**Status:** Shipped, commit d755b50. Tony is testing on his MacBook for a few weeks from 2026-10-27.

**User story.** As a daily user, I want Vikey to keep working without quitting and reopening it.

**Cause.** macOS disables the event tap when it times out or on user input (a slow Mac, secure input), and the app never turned it back on.

**Fix.** Re-enable the tap when macOS reports it was disabled. A 2-second watchdog also re-enables it if needed.

### Literal restoration after Telex transformations

At a word boundary, restore the complete typed spelling when it is a
recognized English word, including cases where the shortened spelling is
also recognized. Internal case boundaries and separated vowel groups can
provide structural evidence for an unlisted literal. Explicit Telex
modifiers continue to protect unlisted Vietnamese syllables.

Restoration based solely on the new unlisted-literal evidence applies to
the current word. Existing spelling-based restoration retains its lookback
behavior.
