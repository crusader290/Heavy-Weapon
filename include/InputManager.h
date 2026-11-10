#pragma once

#include <SDL3/SDL.h>

namespace HeavyWeapon {

struct InputState {
    // Movement
    float moveAxisX = 0.0f; // -1.0 to 1.0 (Left stick / D-Pad)

    // Navigation buttons
    bool upPressed = false;
    bool downPressed = false;
    bool leftPressed = false;
    bool rightPressed = false;

    // Aiming
    bool hasAimInput = false;
    float aimAngleDegrees = -90.0f; // -90 is straight up, -180 is left, 0 is right
    float aimAxisX = 0.0f;
    float aimAxisY = 0.0f;

    // Actions
    bool fireCannon = false;
    bool fireCannonPressed = false;
    bool fireNukePressed = false;
    bool fireMegalaserPressed = false;
    bool altFirePressed = false;
    bool pausePressed = false;
    bool confirmPressed = false;
    bool cancelPressed = false;
    bool musicTogglePressed = false;   // Back/Select on a gamepad, Tab on the keyboard

    // Window close / app exit requested
    bool quitRequested = false;

    // Touch screen or mouse. Coordinates are in logical 640x480 space.
    // pointerAim is set while a mouse is aiming the turret (hover, like the PC game).
    bool pointerAim = false;
    bool touchDown = false;     // any touch-screen finger down, or the left mouse button
    bool fingerDown = false;    // touchDown caused by a real finger (not the mouse)
    bool touchPressed = false;
    bool touchReleased = false;
    float touchX = 0.0f;
    float touchY = 0.0f;
};

class InputManager {
public:
    static void Init();
    static void Shutdown();
    static void Update();

    static const InputState& GetState() { return sState; }

    // Forget all tracked fingers (call on state changes). A finger still held down is ignored
    // until it lifts and touches again.
    static void ResetTouch();

    // A PlayStation (PS3/PS4/PS5) controller is the active gamepad.
    static bool PlayStationPadConnected();

private:
    static InputState sState;
    static SDL_Gamepad* sController;
    static bool sPrevFireCannon;
    static bool sPrevFireNuke;
    static bool sPrevFireMegalaser;
    static bool sPrevPause;
    static bool sPrevConfirm;
    static bool sPrevCancel;
    static bool sPrevMusic;
    static bool sPrevUp;
    static bool sPrevDown;
    static bool sPrevLeft;
    static bool sPrevRight;
    static bool sPrevAltFire;
    static bool sPrevTouch;
    static bool sMouseRightPulse;
    static bool sMouseDown;
    // Active touch-screen finger ids (SDL_FingerID); touchDown is derived from this set.
    static const int kMaxFingers = 10;
    static SDL_FingerID sFingers[kMaxFingers];
    static int sNumFingers;
    static Uint64 sLastFingerEventMs;
    static bool sStuckLogged;
};

} // namespace HeavyWeapon
