// surroundingDeleteCount (#32): a delete never reaches past what the app reported before the cursor; only on GNOME Wayland.
// usage: surrounding_test   (exit 1 on a failure; Linux/check.sh runs it)
#include "Surrounding.h"
#include <cstdio>
int main() {
    struct { int n, cursor, want; const char* what; } t[] = {
        {1, 3, 1, "vie + e: the field reports vie"},
        {3, 3, 3, "a whole reported word"},
        {2, 0, 0, "a field that clears itself (xterm.js) reports nothing before the cursor"},
        {3, 1, 1, "the field reports less than the word"},
        {1, -1, 0, "no report since focus_in"},
        {0, 5, 0, "nothing to delete"},
    };
    int fails = 0;
    for (auto& c : t) { int got = surroundingDeleteCount(c.n, c.cursor); if (got != c.want) { fails++; printf("FAIL %s: delete %d, cursor %d -> %d, expected %d\n", c.what, c.n, c.cursor, got, c.want); } }
    struct { const char *type, *desktop; bool want; } s[] = {
        {"wayland", "GNOME", true}, {"wayland", "ubuntu:GNOME", true}, {"x11", "ubuntu:GNOME", false},
        {"wayland", "KDE", false}, {nullptr, "GNOME", true}, {"wayland", nullptr, true}, {nullptr, nullptr, true},
        {nullptr, "KDE", false}, {"x11", nullptr, false},
    };
    for (auto& c : s) if (surroundingClampNeeded(c.type, c.desktop) != c.want) { fails++; printf("FAIL clamp needed: %s, %s\n", c.type ? c.type : "(unset)", c.desktop ? c.desktop : "(unset)"); }
    printf(fails ? "surrounding delete: FAIL\n" : "surrounding delete: ok\n");
    return fails ? 1 : 0;
}
