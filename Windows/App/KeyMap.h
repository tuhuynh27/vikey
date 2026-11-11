// Windows virtual-key codes -> the engine's key codes (Engine/platforms/mac.h).
// The engine is compiled with the macOS key codes on every platform; this is the only place
// Windows codes are translated. Letters use VK_A..VK_Z, which already follow the active
// layout (Dvorak etc.), so no extra layout compatibility step is needed for letters.
#pragma once
#include <windows.h>
#include "Engine.h"

namespace vikey {

inline Uint16 engineKey(DWORD vk) {
    switch (vk) {
        case 'A': return KEY_A; case 'B': return KEY_B; case 'C': return KEY_C; case 'D': return KEY_D; case 'E': return KEY_E;
        case 'F': return KEY_F; case 'G': return KEY_G; case 'H': return KEY_H; case 'I': return KEY_I; case 'J': return KEY_J;
        case 'K': return KEY_K; case 'L': return KEY_L; case 'M': return KEY_M; case 'N': return KEY_N; case 'O': return KEY_O;
        case 'P': return KEY_P; case 'Q': return KEY_Q; case 'R': return KEY_R; case 'S': return KEY_S; case 'T': return KEY_T;
        case 'U': return KEY_U; case 'V': return KEY_V; case 'W': return KEY_W; case 'X': return KEY_X; case 'Y': return KEY_Y;
        case 'Z': return KEY_Z;
        case '0': return KEY_0; case '1': return KEY_1; case '2': return KEY_2; case '3': return KEY_3; case '4': return KEY_4;
        case '5': return KEY_5; case '6': return KEY_6; case '7': return KEY_7; case '8': return KEY_8; case '9': return KEY_9;
        // numpad digits (Num Lock on) are digits like the top row: they type the same character, and digits after a
        // space start a word of their own ("5trawm" -> "5trăm", D33)
        case VK_NUMPAD0: return KEY_0; case VK_NUMPAD1: return KEY_1; case VK_NUMPAD2: return KEY_2; case VK_NUMPAD3: return KEY_3; case VK_NUMPAD4: return KEY_4;
        case VK_NUMPAD5: return KEY_5; case VK_NUMPAD6: return KEY_6; case VK_NUMPAD7: return KEY_7; case VK_NUMPAD8: return KEY_8; case VK_NUMPAD9: return KEY_9;
        case VK_SPACE: return KEY_SPACE;
        case VK_RETURN: return KEY_RETURN;
        case VK_BACK: return KEY_DELETE;       // Backspace is the engine's "delete"
        case VK_TAB: return KEY_TAB;
        case VK_ESCAPE: return KEY_ESC;
        case VK_LEFT: return KEY_LEFT; case VK_RIGHT: return KEY_RIGHT; case VK_UP: return KEY_UP; case VK_DOWN: return KEY_DOWN;
        case VK_OEM_1: return KEY_SEMICOLON;   // ;:
        case VK_OEM_2: return KEY_SLASH;       // /?
        case VK_OEM_3: return KEY_BACKQUOTE;   // `~
        case VK_OEM_4: return KEY_LEFT_BRACKET;
        case VK_OEM_5: return KEY_BACK_SLASH;
        case VK_OEM_6: return KEY_RIGHT_BRACKET;
        case VK_OEM_7: return KEY_QUOTE;       // '"
        case VK_OEM_COMMA: return KEY_COMMA;
        case VK_OEM_PERIOD: return KEY_DOT;
        case VK_OEM_MINUS: return KEY_MINUS;
        case VK_OEM_PLUS: return KEY_EQUALS;
        // keys the engine treats as word breaks (it has no code of its own for them): act like Esc
        case VK_INSERT: case VK_HOME: case VK_END: case VK_DELETE: case VK_PRIOR: case VK_NEXT:
        case VK_SNAPSHOT: case VK_PRINT: case VK_SELECT: case VK_HELP: case VK_EXECUTE: case VK_NUMLOCK: case VK_SCROLL:
            return KEY_ESC;
        default: return KEY_EMPTY;
    }
}

/// Keys the engine has no code for that still end the word: the ones typing a character (numpad operators, the
/// ISO key next to left Shift...) and Clear (numpad 5 with Num Lock off). The engine never sees them, so the hook
/// ends the word on them: otherwise the next letter would join the letters typed before ("a", numpad +, "a" gave
/// "aâ"). Not mapped to KEY_ESC, which hotkeys can use.
inline bool unmodeledKeyEndsWord(DWORD vk) {
    switch (vk) {
        case VK_DECIMAL: case VK_ADD: case VK_SUBTRACT: case VK_MULTIPLY: case VK_DIVIDE: case VK_SEPARATOR:
        case VK_OEM_102: case VK_OEM_8: case VK_CLEAR:
            return true;
        default: return false;
    }
}

/// The virtual key that types this engine key on a US layout (for re-sending a control key).
inline WORD vkOf(Uint16 key) {
    switch (key) {
        case KEY_RETURN: case KEY_ENTER: return VK_RETURN;
        case KEY_TAB: return VK_TAB;
        case KEY_ESC: return VK_ESCAPE;
        case KEY_SPACE: return VK_SPACE;
        case KEY_LEFT: return VK_LEFT; case KEY_RIGHT: return VK_RIGHT; case KEY_UP: return VK_UP; case KEY_DOWN: return VK_DOWN;
        default: return 0;
    }
}

} // namespace vikey
