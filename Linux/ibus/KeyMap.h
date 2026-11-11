// X keysyms (what IBus hands the engine) -> the engine's key codes (Engine/platforms/mac.h) + shift.
// Letters follow the active layout already (keysym 'a' is wherever the layout puts it).
#pragma once
#include <ibus.h>
#include "Engine.h"
namespace vikey {
struct MappedKey { Uint16 key; bool shift; };
inline MappedKey engineKey(guint keyval) {
    if (keyval >= IBUS_KEY_a && keyval <= IBUS_KEY_z) { static const Uint16 L[] = {KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z}; return {L[keyval - IBUS_KEY_a], false}; }
    if (keyval >= IBUS_KEY_A && keyval <= IBUS_KEY_Z) { static const Uint16 L[] = {KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z}; return {L[keyval - IBUS_KEY_A], true}; }
    switch (keyval) {
        case IBUS_KEY_0: return {KEY_0, false}; case IBUS_KEY_1: return {KEY_1, false}; case IBUS_KEY_2: return {KEY_2, false}; case IBUS_KEY_3: return {KEY_3, false}; case IBUS_KEY_4: return {KEY_4, false};
        case IBUS_KEY_5: return {KEY_5, false}; case IBUS_KEY_6: return {KEY_6, false}; case IBUS_KEY_7: return {KEY_7, false}; case IBUS_KEY_8: return {KEY_8, false}; case IBUS_KEY_9: return {KEY_9, false};
        case IBUS_KEY_parenright: return {KEY_0, true}; case IBUS_KEY_exclam: return {KEY_1, true}; case IBUS_KEY_at: return {KEY_2, true}; case IBUS_KEY_numbersign: return {KEY_3, true}; case IBUS_KEY_dollar: return {KEY_4, true};
        case IBUS_KEY_percent: return {KEY_5, true}; case IBUS_KEY_asciicircum: return {KEY_6, true}; case IBUS_KEY_ampersand: return {KEY_7, true}; case IBUS_KEY_asterisk: return {KEY_8, true}; case IBUS_KEY_parenleft: return {KEY_9, true};
        // numpad digits (Num Lock on) are digits like the top row ("5trawm" -> "5trăm" works the same, D33)
        case IBUS_KEY_KP_0: return {KEY_0, false}; case IBUS_KEY_KP_1: return {KEY_1, false}; case IBUS_KEY_KP_2: return {KEY_2, false}; case IBUS_KEY_KP_3: return {KEY_3, false}; case IBUS_KEY_KP_4: return {KEY_4, false};
        case IBUS_KEY_KP_5: return {KEY_5, false}; case IBUS_KEY_KP_6: return {KEY_6, false}; case IBUS_KEY_KP_7: return {KEY_7, false}; case IBUS_KEY_KP_8: return {KEY_8, false}; case IBUS_KEY_KP_9: return {KEY_9, false};
        case IBUS_KEY_space: return {KEY_SPACE, false};
        case IBUS_KEY_Return: return {KEY_RETURN, false}; case IBUS_KEY_KP_Enter: return {KEY_ENTER, false};
        case IBUS_KEY_BackSpace: return {KEY_DELETE, false};
        case IBUS_KEY_Tab: case IBUS_KEY_ISO_Left_Tab: return {KEY_TAB, false};
        case IBUS_KEY_Escape: return {KEY_ESC, false};
        case IBUS_KEY_Left: return {KEY_LEFT, false}; case IBUS_KEY_Right: return {KEY_RIGHT, false}; case IBUS_KEY_Up: return {KEY_UP, false}; case IBUS_KEY_Down: return {KEY_DOWN, false};
        case IBUS_KEY_semicolon: return {KEY_SEMICOLON, false}; case IBUS_KEY_colon: return {KEY_SEMICOLON, true};
        case IBUS_KEY_slash: return {KEY_SLASH, false}; case IBUS_KEY_question: return {KEY_SLASH, true};
        case IBUS_KEY_grave: return {KEY_BACKQUOTE, false}; case IBUS_KEY_asciitilde: return {KEY_BACKQUOTE, true};
        case IBUS_KEY_bracketleft: return {KEY_LEFT_BRACKET, false}; case IBUS_KEY_braceleft: return {KEY_LEFT_BRACKET, true};
        case IBUS_KEY_bracketright: return {KEY_RIGHT_BRACKET, false}; case IBUS_KEY_braceright: return {KEY_RIGHT_BRACKET, true};
        case IBUS_KEY_backslash: return {KEY_BACK_SLASH, false}; case IBUS_KEY_bar: return {KEY_BACK_SLASH, true};
        case IBUS_KEY_apostrophe: return {KEY_QUOTE, false}; case IBUS_KEY_quotedbl: return {KEY_QUOTE, true};
        case IBUS_KEY_comma: return {KEY_COMMA, false}; case IBUS_KEY_less: return {KEY_COMMA, true};
        case IBUS_KEY_period: return {KEY_DOT, false}; case IBUS_KEY_greater: return {KEY_DOT, true};
        case IBUS_KEY_minus: return {KEY_MINUS, false}; case IBUS_KEY_underscore: return {KEY_MINUS, true};
        case IBUS_KEY_equal: return {KEY_EQUALS, false}; case IBUS_KEY_plus: return {KEY_EQUALS, true};
        // word breaks the engine has no code for: act like Esc
        case IBUS_KEY_Insert: case IBUS_KEY_Home: case IBUS_KEY_End: case IBUS_KEY_Delete: case IBUS_KEY_Page_Up: case IBUS_KEY_Page_Down:
        case IBUS_KEY_Print: case IBUS_KEY_Num_Lock: case IBUS_KEY_Scroll_Lock: case IBUS_KEY_KP_Home: case IBUS_KEY_KP_End: case IBUS_KEY_KP_Delete:
            return {KEY_ESC, false};
        default: return {KEY_EMPTY, false};
    }
}
inline bool isModifierKeysym(guint k) {
    return k == IBUS_KEY_Shift_L || k == IBUS_KEY_Shift_R || k == IBUS_KEY_Control_L || k == IBUS_KEY_Control_R || k == IBUS_KEY_Alt_L || k == IBUS_KEY_Alt_R ||
           k == IBUS_KEY_Meta_L || k == IBUS_KEY_Meta_R || k == IBUS_KEY_Super_L || k == IBUS_KEY_Super_R || k == IBUS_KEY_Caps_Lock || k == IBUS_KEY_ISO_Level3_Shift;
}
/// The keysym that types this engine key (for re-sending a control key after a restore).
inline guint keysymOf(Uint16 key) {
    switch (key) { case KEY_RETURN: return IBUS_KEY_Return; case KEY_ENTER: return IBUS_KEY_KP_Enter; case KEY_TAB: return IBUS_KEY_Tab; case KEY_ESC: return IBUS_KEY_Escape;
                   case KEY_SPACE: return IBUS_KEY_space; case KEY_LEFT: return IBUS_KEY_Left; case KEY_RIGHT: return IBUS_KEY_Right; case KEY_UP: return IBUS_KEY_Up; case KEY_DOWN: return IBUS_KEY_Down; default: return 0; }
}
}
