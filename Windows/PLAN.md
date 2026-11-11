# Vikey for Windows — plan (v0, 2026-10-29, for Tony's review)

## 0. What I found

**VMPal** (vmpal.com, by the TablePlus team): VM manager for Apple Silicon, needs macOS 26 (this mini runs 26.3). macOS/Linux guests on Apple's Virtualization framework; **Windows 11 on its own engine** (GPU-accelerated, TPM 2.0, up to 5K). Free download, **no limits in the free tier today**; individual licence $69 (early-bird $34.50, one-time, per Mac). Built for agents: a `vmpal` CLI (`list`, `create`, `start/stop`, `exec "VM" -- cmd`, `cp`, `ssh`, `ui`) plus an **MCP server** ("Add to Claude Code") that lets me see the VM screen, click, type and run commands. Guest tools for Windows ship inside the app (`VMPalTools.exe`, `VMPalMCP.exe`, `Install VMPal Tools.cmd`). Installed: VMPal 0.110 in /Applications (not launched yet — first launch, Windows download and the "Add to Claude Code" step need one GUI session).

**Host resources:** 24 GB RAM, 57 GB free disk. A Windows 11 ARM64 VM needs ~30–40 GB; OK but I should run the disk cleanup first and cap the VM at 48 GB / 8 GB RAM.

**Code base:** the engine is already platform-neutral C++ (`Engine/Engine.cpp` 2.2k lines, `Driver.{h,cpp}` = EN word buffer + auto switch + undo + pause, verified letter-for-letter against the simulator on 6.7M letters, used by iOS). The macOS layer is `App/Core/VikeyCore.mm` (1.2k lines: CGEvent tap, smart switch per app/site, auto switch, typing log, browser-autocomplete fix, Dvorak/layout compat, other-input-source detection) + SwiftUI app. Vikey's git history still contains OpenKey's Windows port (`Sources/OpenKey/win32`, commit bac3e84): Win32 C++, `WH_KEYBOARD_LL` + `WH_MOUSE_LL` hooks, `SendInput` with `KEYEVENTF_UNICODE`, tray icon, registry prefs, run-at-login, Chromium address-bar workaround, "run as admin for games". That is the proven way Vietnamese IMEs work on Windows (UniKey/EVKey do the same) and the direct counterpart of our event tap.

**Toolchain facts:** Windows 11 ARM64 is what runs natively on Apple Silicon; x64 apps run through emulation. GitHub Actions has free `windows-11-arm` and `windows-latest` (x64) runners for public repos, so **release builds come from CI, not the VM**; the VM is for development and real-user testing. Code signing: Azure Trusted Signing is $9.99/mo but individuals must be US/Canada-based; a Vietnamese company would need an OV certificate (~$200–400/yr, Sectigo/SSL.com, hardware or cloud key). Unsigned builds work but trigger SmartScreen "unknown publisher" on first run.

## 1. Parity target (what "100%" means)

| macOS feature (DECISIONS) | Windows counterpart | Notes |
|---|---|---|
| **Status 2026-10-29** | Phases 0–5 done; live suite 14/14 on ARM64 and x64 (emulated); update flow verified; code signing: Certum OSS cloud cert ordered (Tony pays), signing step added when it lands | |
| **Status 2026-10-30 (parity pass, D44)** | Platform core caught up with the Mac (D32 undo-then-continue, D33 digits, D35 look-forward; D39/D41/D42 come from the shared engine); sparkle switch sound; emoji picker; onboarding v2; tray "Hướng dẫn…" / "Gửi góp ý…"; About links + Acknowledgements; windows centred on the mouse's screen. Live suite 23/23 (ARM64 VM) | |
| Switch sound "sparkle" (D31) | SwitchToVietnamese/English.caf → WAV (afconvert LEI16), embedded RCDATA, `PlaySound(SND_MEMORY\|SND_ASYNC)` | `Windows/App/Sounds/` |
| Emoji picker (EmojiPicker.swift, EmojiSearch.swift) | `Windows/App/Emoji.cpp`: borderless WS_EX_NOACTIVATE popup drawn with Direct2D + DirectWrite (colour Segoe UI Emoji), keys reach it through the keyboard hook (the app in front keeps its caret), click outside closes, Enter types via SendInput KEYEVENTF_UNICODE; Emoji.json embedded; Telex reader + scoring ported 1:1; prefs EmojiPicker / EmojiHotkey / EmojiSkinTone / EmojiUsage | country flags and tag-sequence flags are left out (Segoe UI Emoji draws them as letters) |
| Crash reports (CrashReporter.swift) | `Windows/App/CrashReporter.cpp`: unhandled-exception filter + std::terminate/abort/pure call/invalid parameter write a text report (exception, module+offset per frame via RtlVirtualUnwind, Vikey + Windows version, thread id; no typed text, no minidump) to `%LOCALAPPDATA%\Vikey\Crashes`; next launch asks with a TaskDialog (or sends when "Luôn gửi" was chosen), profile path → `~`, WinHTTP POST to vikey.app/api/crash on its own thread; prefs CrashReportsAuto / CrashReportsSeenUntil / crashEndpoint (test); hidden `--crash-self-test[=terminate]` | upload results in `Crashes\upload.log` |
| Onboarding (OnboardingView.swift) | 4 steps (no Accessibility step on Windows): Telex table, practice fields, hotkey + sound, emoji (optional); tray "Hướng dẫn…" | |
| Telex, modern/classic tone, spell check, restore-if-invalid | same engine, unchanged | compiled into the app; `check.sh` lock runs on Windows CI too |
| Auto switch EN/VI (D5–D29), doubled key, look-back, Backspace undo, pause after manual | `Engine/Driver` already does all of it | the Windows layer just feeds keys and applies `VKEdit` |
| Switch hotkey (key+modifiers or modifiers alone, beep) | LL hook sees modifier-only chords | default ⌥Z → Alt+Z |
| Per-app memory | foreground process exe name (OpenKey code) | |
| Per-website memory (Chrome/Safari/Arc/Brave/Edge/Vivaldi/Opera) | UI Automation reads the address bar of Chrome/Edge/Brave/Vivaldi/Opera/Firefox | Firefox *gains* support on Windows; Safari/Arc n/a |
| Dvorak/other layouts, step aside for other input languages | `GetKeyboardLayout` + `ToUnicode`; ignore when IME language ≠ en/vi | |
| Menu-bar icon V/E, compact settings window, onboarding (Accessibility) | tray icon V/E, settings window, first-run page (no permission needed on Windows; "run as admin" optional for games/elevated apps) | |
| Typing log (opt-in, privacy exclusions: password managers, terminals, secure input) | same file format; exclusions by process name (1Password, Bitwarden, KeePassXC, Windows Terminal, PuTTY…) + skip when a password edit control has focus (UIA `IsPassword`) | |
| Auto update (Sparkle, appcast on vikey.app) | own updater (`Windows/App/Updater.cpp`): same appcast, Windows items `sparkle:os="windows" vikey:arch=…`, same EdDSA key (TweetNaCl verify), daily check, install on quit/launch | one appcast + `release.sh --windows` |
| Launch at login | HKCU Run key (or Task Scheduler when elevated) | |
| Migration from OpenKey settings | read `HKCU\SOFTWARE\TuyenMai\OpenKey` | |
| Browser autocomplete workaround | OpenKey's Chromium fix (already in the old code) | |
| Website demo frames, release pipeline, notarized DMG | `release.sh` gains a Windows lane: CI builds ARM64 + x64, signs (if cert), uploads `Vikey-Setup.exe` + `.zip`, updates appcast | |
| iOS keyboard, fat-finger, auto-correct strip | out of scope (mobile only) | |

Explicit non-goals: VNI, code tables, macros (already cut on macOS).

## 2. Architecture

```
Engine/            shared C++ (unchanged)            ─┐
Engine/Driver      shared C++ (unchanged)             ├─ static lib "VikeyCore" (CMake)
Windows/Core/      Win32 layer in C++: LL keyboard+mouse hooks, SendInput, ─┘
                   foreground app/URL (UIA), layout, hotkey, typing log, prefs (registry/JSON)
Windows/App/       tray + windows. Choice below.
Windows/Tests/     unit tests (engine lock = existing harness built with MSVC) + live test (sends keys to Notepad/Edge via SendInput, reads back via UIA)
.github/workflows/windows.yml   build ARM64 + x64, run tests, package, sign (optional), release
```

UI layer options:
- **A. Pure C++/Win32 with modern styling** (OpenKey's approach, plus WinUI-like look via DarkMode/Mica + a hand-built settings dialog). Smallest binary (~2 MB), zero runtime, one language, easiest to test from a VM. Looks "fine", not "SwiftUI-pretty".
- **B. C# WPF/WinUI 3 app + C++ core DLL (P/Invoke).** Prettier settings/onboarding, but needs .NET runtime (self-contained ~70 MB) and two toolchains; WinUI 3 on ARM64 in a VM has rough edges.

Recommendation: **A**, with the window rebuilt to match the macOS layout (language switch on top, hotkey, auto-switch, typing, launch at login, credit footer). Parity is about behavior; the macOS app is deliberately plain stock UI anyway.

## 3. Phases

| # | Phase | Deliverable | How it's verified |
|---|---|---|---|
| 0 | VM environment (½ day) | VMPal first run; Windows 11 ARM64 VM "Vikey-Dev" (8 GB RAM, 48 GB disk); guest tools; `vmpal` CLI + MCP wired into this session; shared folder `~/Work/Vikey` ↔ `Z:\`; snapshot "clean"; VS 2022 Build Tools (MSVC ARM64 + x64), CMake, Ninja, Git, Notepad/Edge/Chrome for live tests | `vmpal exec Vikey-Dev -- cl /?` works; screenshot via MCP |
| 1 | Engine on Windows (1 day) | CMake builds `Engine` + `Driver` + the test harness/sim with MSVC; `check.sh` equivalent (`check.ps1`) passes: rule cases, golden snapshots, Telex lock, driver parity, perf | same numbers as macOS |
| 2 | Core: hook + output (1–2 days) | tray app that types Vietnamese in Notepad, Edge, Word, Terminal; Backspace/arrow/click handling; Telex + auto switch + undo via `Driver`; hotkey; per-app memory; layout compat; run-at-login | live test harness: synthetic keys → read back text; cases ported from the macOS live suite (18 native + 8 browser) |
| 3 | Per-site memory + browser fixes (1 day) | UIA URL reading for Chromium browsers + Firefox; autocomplete fix | browser live cases |
| 4 | App parity (1–2 days) | settings window, onboarding/first run, typing-log window, migration, about/credits, icons, Vietnamese strings | screenshots side by side with macOS |
| 5 | Update + packaging + CI (1 day) | own updater in `Updater.cpp` (same appcast + EdDSA key as Sparkle; WinSparkle dropped: no ARM64 build, extra DLL); Inno Setup per-user installer + portable zip; GitHub Actions ARM64/x64 build+test, `package` job on tags; `release.sh --windows` | CI green; install → update from a local appcast works in the VM |
| 6 | QA + ship (1 day) | full live suite on a clean snapshot (ARM64) and on x64 emulation; Tony tests on a real Windows machine if he has one; vikey.app gets a Windows download page | Tony sign-off |

Total ≈ 6–8 working days of my time; I'll report per phase with screenshots/recordings from the VM.

## 4. Testing strategy (so you never first-test)

- Engine: existing rule lock + golden + Telex lock + perf, built and run on Windows in CI on every push.
- Live: a Windows port of `live_vikey.py`/TypeBox: a tiny test window (or Notepad) receives `SendInput` keystrokes from the test runner while the Vikey hook is active, then UIA reads the text back; cases = DECISIONS examples (more, trường, this makes sense, err/mix, undo, hotkey, per-app memory).
- Browser: Edge/Chrome on the VM, URL memory and address-bar fix.
- Visual: MCP screenshots of every window; I compare against macOS screenshots.
- Clean-install QA from the "clean" snapshot before each release.

## 5. Decisions needed from you

1. **UI stack:** A (C++/Win32, recommended) or B (C#/WinUI 3)?
2. **Code signing:** none for v1 (SmartScreen warning, like most indie Windows tools) / OV certificate for Typing Technology (~$200–400/yr, needs company docs) / try Azure Trusted Signing with the company (may be rejected outside US/CA).
3. **Targets:** ARM64 + x64 both (recommended; x64 is what most Vietnamese Windows PCs run) — note I can only test x64 under emulation in the VM.
4. **VMPal licence:** free tier is unlimited today; buy the $34.50 licence now (your card) or wait until they limit it?
5. **Distribution:** vikey.app download page + GitHub Releases only (recommended), or also Microsoft Store (needs a $19 dev account, MSIX packaging, and LL hooks are restricted in Store apps — not recommended)?
6. **Admin mode:** OpenKey ran elevated by default so it works in games/elevated apps. Proposal: not elevated by default, with a "Run as administrator" toggle.

## 6. Risks

- Windows on ARM in a VM: some x64-only apps (older games) can't be tested here; acceptable.
- UIA URL reading differs per browser and breaks on browser updates; mitigated by falling back to per-app memory.
- VMPal is new (0.110) — if the Windows engine misbehaves, fallback is UTM/QEMU (free) with the same VM image.
- Disk: 57 GB free; cleanup first, VM capped at 48 GB.
