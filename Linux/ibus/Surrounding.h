// How many characters delete_surrounding_text may remove before the cursor (#32). GNOME's mutter checks the delete
// against the text the app last reported (it never adds what we committed since) and aborts the whole session on a
// delete past its start (g_assert before >= start). cursor: the app's last reported cursor, -1 when none since focus_in.
#pragma once
#include <algorithm>
#include <cstring>
inline int surroundingDeleteCount(int n, int cursor) { return std::max(0, std::min(n, cursor)); }
// Only GNOME on Wayland needs it (mutter relays the delete). Elsewhere (X11, KDE) the app deletes on its real text,
// which a lagging report would under-count ("dd" typed fast at the start of a field: "dđ"). The engine inherits the
// session's environment from ibus-daemon. Arguments: XDG_SESSION_TYPE and XDG_CURRENT_DESKTOP ("ubuntu:GNOME").
// One that's unset doesn't rule GNOME Wayland out: clamping by mistake costs a rare "dđ", not clamping a crash.
inline bool surroundingClampNeeded(const char* sessionType, const char* desktop) {
    bool notWayland = sessionType && *sessionType && strcmp(sessionType, "wayland");
    bool notGnome = desktop && *desktop && !strstr(desktop, "GNOME");
    return !notWayland && !notGnome;
}
