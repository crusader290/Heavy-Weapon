#pragma once

#include <string>

// On-screen prompt and help text that names gamepad buttons, in two styles:
//
//   Xbox         A / B / Y / RB / LB / BACK, with keyboard keys where they apply (default)
//   PlayStation  CROSS / CIRCLE / TRIANGLE / R / L / SELECT: the PS Vita port's original
//                wording, kept verbatim so it can be switched back on
//
// The style is chosen with `buttons=xbox|playstation|auto` in settings.ini or `--buttons`
// on the command line; `auto` uses PlayStation names while a PlayStation controller is
// connected. Only the text changes: the button mapping is the same (SDL's south/east/
// north buttons are A/B/Y on an Xbox pad and Cross/Circle/Triangle on a PlayStation pad).

namespace HeavyWeapon {

enum class ButtonStyle { Xbox, PlayStation, Auto };

struct ButtonLabels {
    const char* beginPrompt;      // mission briefing
    const char* pauseHint;        // pause overlay
    const char* pauseMusicHint;   // appended to the music on/off line
    const char* optionsHint;      // options screen
    const char* helpFire;         // help screen, gamepad section: key column
    const char* helpNuke;
    const char* helpPause;
    const char* helpMusic;
    const char* helpMenu;
    const char* helpBackHint;     // help screen footer
    const char* gameOverPrompt;
};

inline const ButtonLabels& GetButtonLabels(ButtonStyle style) {
    static const ButtonLabels kXbox = {
        "PRESS ENTER OR A TO BEGIN",
        "P / START: RESUME    BACKSPACE / B: QUIT TO TITLE",
        "   (TAB / BACK)",
        "ENTER/CLICK: CHANGE    ESC: BACK",
        "A / RB",
        "Y / LB",
        "START",
        "BACK",
        "A / B",
        "ANY BUTTON OR CLICK: BACK",
        "PRESS ENTER TO RETURN TO HEADQUARTERS",
    };
    // The PS Vita port's text, unchanged.
    static const ButtonLabels kPlayStation = {
        "PRESS CROSS TO BEGIN",
        "START: RESUME    CIRCLE: RETIRE TO TITLE",
        "   (SELECT)",
        "CROSS: CHANGE    CIRCLE: BACK    OR TAP",
        "CROSS (A) / R",
        "TRIANGLE (Y) / L",
        "START",
        "SELECT",
        "CROSS / CIRCLE",
        "ANY BUTTON OR TAP: BACK",
        "PRESS CROSS TO RETURN TO HEADQUARTERS",
    };
    return style == ButtonStyle::PlayStation ? kPlayStation : kXbox;
}

// "xbox" / "playstation" (or "ps") / "auto"; false if the name is unknown.
inline bool ParseButtonStyle(const std::string& name, ButtonStyle& out) {
    if (name == "xbox") out = ButtonStyle::Xbox;
    else if (name == "playstation" || name == "ps") out = ButtonStyle::PlayStation;
    else if (name == "auto") out = ButtonStyle::Auto;
    else return false;
    return true;
}

inline const char* ButtonStyleName(ButtonStyle style) {
    switch (style) {
        case ButtonStyle::PlayStation: return "playstation";
        case ButtonStyle::Auto: return "auto";
        default: return "xbox";
    }
}

} // namespace HeavyWeapon
