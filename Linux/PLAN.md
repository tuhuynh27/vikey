# Vikey for Linux — plan and status

Tony, 2026-10-29: "now build the Linux version" (after Windows shipped). Parity target: the Mac app.

## How it works on Linux

Linux has no global keyboard hook that works under Wayland, so Vikey is an **IBus input engine**
(`ibus-engine-vikey`), the way every Vietnamese IME on Linux works (ibus-unikey, ibus-bamboo). IBus delivers
the key events of the focused app to the engine and the engine commits text back, in every toolkit, on X11
and Wayland, GNOME and KDE. Same `Engine/` C++ as macOS/Windows/iOS; the platform layer is `Linux/ibus/`.

| macOS feature | Linux |
|---|---|
| Telex, tone placement, spelling, restore-if-invalid | same engine (`Linux/check.sh` runs the engine lock on CI, x64 + arm64) |
| Auto switch EN/VI, doubled key, look-back, Backspace undo, 5 s pause | `Linux/ibus/Core.cpp`, a port of the Windows/macOS core with an output sink |
| Switch hotkey (⌥Z) | Alt+Z inside the engine (modifier-only chords tracked from modifier key events); IBus' own input-source key also works |
| Switch sound ("sparkle": up into Vietnamese, down into English) | the Mac's `.caf` files as `.wav` (`Linux/sounds/`, `/usr/share/vikey/sounds`), played in the engine with GStreamer `playbin` (no process start per switch); system bell if GStreamer fails |
| Undo then type on (D32), digits start a word (D33), look forward (D35), DD/acronyms/all-caps (D39, D41, D42) | `Core.cpp` follows `VikeyCore.mm`; live cases in `Linux/Tests/cases-linux.tsv` |
| Emoji picker (hotkey, search EN/VI/Telex, Frequently Used, sections, arrows, Enter, Esc) | `vikey-settings --emoji` (`Linux/settings/Emoji.cpp`, a port of `EmojiPicker.swift` + `EmojiSearch.swift`), started by the engine on the hotkey. Borderless, centred, above other windows (X11/XWayland: Wayland gives clients no positions). The choice goes back through the command file (`emoji 🐴`) and the engine commits it when the app has the focus again. The picker runs with GTK's simple input method, so its search reads raw keys like the Mac panel. Emoji newer than the colour emoji font are left out (fontconfig check) |
| Onboarding (Telex, permission, practice, hotkey, emoji) | `vikey-settings --guide` (first run and the panel's "Hướng dẫn…"); the Accessibility step is "Thêm Vikey vào nguồn nhập" with a live check of `org.gnome.desktop.input-sources` and a button to Settings › Keyboard (no check outside GNOME: the step never blocks there) |
| Sparkle update check | "Kiểm tra cập nhật…" and a daily check from the engine when "Tự động cập nhật" is on: `vikey.app/release.json` → `linux.version`; a dialog, or a notification with "Tải về" → vikey.app/download#linux. The package is installed by the user (no apt/dnf repo yet) |
| Ghi nhận | About › Details › Ghi nhận (the Mac list without Sparkle) |
| Crash reports (ask on the next launch, "Luôn gửi, không hỏi lại") | the engine's signal handler (`Linux/ibus/Crash.cpp`: SIGSEGV/BUS/ABRT/FPE/ILL on an alternate stack, async-signal-safe) writes `~/.local/state/vikey/crashes/vikey-YYYYMMDD-HHMMSS.txt` (signal, fault address, backtrace as module+offset, version; no typed text) and re-raises, so IBus starts the engine again; 8 s after that start the engine runs `vikey-settings --crash-report`, which sends silently (`CrashReportsAuto`, also a switch "Tự gửi báo cáo lỗi") or asks with a desktop notification (three actions; an Adwaita dialog with the checkbox when there is no notification server, or `--dialog`). Home folder → `~`, curl POST to vikey.app/api/crash. Self-test: `echo crash-selftest > ~/.config/vikey/command` (or `crash-selftest abort`), or `VIKEY_CRASH_SELFTEST=segv\|abort` when run by hand |
| Per-app memory | X11 `_NET_ACTIVE_WINDOW` WM_CLASS; on Wayland only XWayland apps (Wayland gives engines no app identity) |
| Per-website memory | not available: Linux has no accessibility path to a browser's address bar that works across browsers and Wayland (the Mac reads it with Apple Events, Windows with UI Automation). It would need a browser extension talking to the engine; the row is left out of the window |
| Menu bar icon V/E + menu | IBus panel properties: V/E symbol, Tiếng Việt toggle, Tự chuyển, Mở Vikey…, Hướng dẫn…, Kiểm tra cập nhật…, Giới thiệu Vikey (+ Xem nhật ký gõ… while the typing log is on, the Mac's debug menu). No "Thoát": IBus owns the engine's life |
| Settings window, typing log, About | `vikey-settings` (GTK4 + libadwaita), 1:1 rows with the Mac window; writes `~/.config/vikey/settings.json`, the engine watches it |
| Launch at login | IBus starts the engine with the session (row in the window is informational) |
| Auto update | a real switch (`AutoUpdate`): daily check + notification; the install itself is a package (apt/dnf repo later) |
| Launch at login | shown as a switch that mirrors whether Vikey is an input source (IBus starts it with the session); set up in the guide |
| Typing log | same JSONL at `~/.local/share/vikey/typing-log.jsonl`; terminals/password managers excluded by WM_CLASS |
| Output | no pre-edit: text is committed straight (like Mac/Win). Delete via `delete_surrounding_text` when the client supports it, else forwarded BackSpace keys |

## Phases

| # | Phase | Status |
|---|---|---|
| 0 | Ubuntu 24.04 VM "Vikey-Linux" in VMPal | done (needed host disk cleanup) |
| 1 | Engine lock on Linux CI | done (ubuntu-latest + ubuntu-24.04-arm) |
| 2 | IBus engine | done: live suite 14/14 in the VM (GNOME Wayland) from the installed .deb |
| 3 | Settings app / typing log / About | done (screenshots sent to Tony) |
| 4 | Packages: .deb + .rpm (CPack), Arch .pkg.tar.zst from the .deb, CI upload to the GitHub release, `release.sh --linux` | done; .deb installed and tested in the VM (prefix /usr) |
| 5 | QA: `Linux/Tests/livetest.py` 14/14 on GNOME (Wayland session, XWayland test window), package install | done in the dev VM; clean-VM install not done (no disk) |
| 6 | Site: Linux card on /download, home button (behind `<!-- linux -->` until release.json has `linux`) | written |
| 7 | Parity with the Mac app as of 1.0.23 (D44): core D32–D42, switch sound, emoji picker, guide, update check, Ghi nhận | done in the dev VM: live 25/25 |
| later | Fcitx5 engine, apt repo for updates, browser extension for per-site memory | |

## What can't match the Mac (and why)

- **Per-website memory**: see the table; needs a browser extension.
- **Picker position on pure Wayland**: the picker is an X11 (XWayland) window so it can centre itself and stay above; a native Wayland client can't choose its position or keep itself on top.
- **Auto install**: Linux packages need root; Vikey tells you about a new version and opens the download page instead of installing it (an apt/dnf repo would let the system updater do it).
- **"Mở Vikey khi đăng nhập"**: not a choice on Linux; IBus starts the engine while Vikey is an input source.
- **Tips**: the (?) shows a popover on click and a tooltip on hover, as on the Mac; one tip adds "Trên Wayland chỉ với ứng dụng X11" (per-app memory), one says the update is a package (auto update).

## Gotchas learned (also in memory)

- IBus drops the global engine when the last client goes away: tests run `ibus engine vikey` first.
- GTK resets the IM context right after inserting a passthrough key: the engine ignores a `reset` within 300 ms of its own output, or the auto-switch look-back is wiped after every word.
- Settings are a shared JSON file: stats go to `stats.json` (engine only), settings are re-read before each write, language switches go through the one-shot `~/.config/vikey/command` file.
- The IBus component bakes the libexec path: the default install prefix is `/usr`, like the packages.
- The emoji picker's grid is one drawing area the size of the view, scrolled by hand: a widget as tall as the whole list (~10k px at 2x) is over the GPU's texture size and draws nothing.
- XWayland on a 2x screen: X positions are device pixels, GDK sizes are not (multiply by the surface scale); GTK rewrites WM hints and `_NET_WM_STATE` at map, so "above" is a client message to the window manager.
- A window started by the engine has no startup id: mutter only focuses it after a `_NET_ACTIVE_WINDOW` request with source "pager".
- `pkill -f vikey-settings` also kills the shell that runs it (its command line contains the name): use `pkill -x`.
