// Crash reports on Linux (the Mac's CrashReporter.swift). The engine writes a small text report when it crashes
// (signal, fault address, backtrace as module+offset, version: never typed text), IBus starts it again, and on that
// start the engine asks vikey-settings --crash-report to send it (CrashReportsAuto) or to ask the user first.
#pragma once
#include <string>
#include <vector>
namespace vikey { namespace crash {
std::string dir();                        // $XDG_STATE_HOME/vikey/crashes (~/.local/state/vikey/crashes)
/// Install the signal handler (SIGSEGV/SIGBUS/SIGABRT/SIGFPE/SIGILL) on an alternate stack. Call early in main.
void install(const char* version);
/// Reports from the last 3 days newer than the handled watermark (CrashReportsSeenUntil), newest first.
std::vector<std::string> pending();
/// Crash on purpose (self-test): "abort" -> SIGABRT, anything else -> SIGSEGV.
[[noreturn]] void selfTest(const std::string& kind);
}}
