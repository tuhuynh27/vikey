// Crash reports, the Windows side of App/Model/CrashReporter.swift.
// At crash time a small text report (exception, module+offset of the fault and of each stack frame, Vikey and Windows
// versions, thread id) is written to %LOCALAPPDATA%\Vikey\Crashes\Vikey-YYYY-MM-DD-HHMMSS.txt. No typed text, no
// clipboard, no window titles, no minidump. On the next launch Vikey asks whether to send it (or sends it when the
// user chose "always") to vikey.app/api/crash. The profile folder (C:\Users\<name>) is reduced to "~" before sending.
#pragma once
#include <windows.h>
namespace vikey { namespace crash {
void install();               // as early as possible: unhandled exceptions, std::terminate, abort, pure call, invalid parameter
void selfTestIfAsked();       // hidden: "--crash-self-test" crashes with an access violation, "--crash-self-test=terminate" via std::terminate
void checkAtLaunch();         // after the tray is up: one question (on its own thread) for the newest unhandled report
}}
