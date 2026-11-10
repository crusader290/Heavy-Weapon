#include "InputManager.h"
#include "Constants.h"
#include "Renderer.h"
#include "core/Perf.h"
#include <cmath>
#include <algorithm>

namespace HeavyWeapon {

InputState InputManager::sState;
SDL_GameController* InputManager::sController = nullptr;
bool InputManager::sPrevFireCannon = false;
bool InputManager::sPrevFireNuke = false;
bool InputManager::sPrevFireMegalaser = false;
bool InputManager::sPrevPause = false;
bool InputManager::sPrevConfirm = false;
bool InputManager::sPrevCancel = false;
bool InputManager::sPrevMusic = false;
bool InputManager::sPrevUp = false;
bool InputManager::sPrevDown = false;
bool InputManager::sPrevLeft = false;
bool InputManager::sPrevRight = false;
bool InputManager::sPrevAltFire = false;
bool InputManager::sPrevTouch = false;
bool InputManager::sMouseRightPulse = false;
bool InputManager::sMouseDown = false;
SDL_FingerID InputManager::sFingers[InputManager::kMaxFingers];
int InputManager::sNumFingers = 0;
Uint32 InputManager::sLastFingerEventMs = 0;
bool InputManager::sStuckLogged = false;

// Only the front (direct) touch panel counts. On Vita SDL registers the rear touchpad as a second,
// indirect touch device; its events must never drive the tank.
static bool IsFrontTouch(SDL_TouchID id) {
    SDL_TouchDeviceType t = SDL_GetTouchDeviceType(id);
    return t == SDL_TOUCH_DEVICE_DIRECT || t == SDL_TOUCH_DEVICE_INVALID;
}

static SDL_TouchID FrontTouchDevice(bool& found) {
    int n = SDL_GetNumTouchDevices();
    for (int i = 0; i < n; ++i) {
        SDL_TouchID id = SDL_GetTouchDevice(i);
        if (SDL_GetTouchDeviceType(id) == SDL_TOUCH_DEVICE_DIRECT) { found = true; return id; }
    }
    found = false;
    return 0;
}

void InputManager::ResetTouch() {
    sNumFingers = 0;
    sState.fingerDown = false;
    sState.touchDown = sMouseDown;
}

void InputManager::Init() {
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            sController = SDL_GameControllerOpen(i);
            if (sController) break;
        }
    }
}

void InputManager::Shutdown() {
    if (sController) {
        SDL_GameControllerClose(sController);
        sController = nullptr;
    }
}

void InputManager::Update() {
    // Reset one-frame pulses
    sState.fireCannonPressed = false;
    sState.fireNukePressed = false;
    sState.fireMegalaserPressed = false;
    sState.altFirePressed = false;
    sState.pausePressed = false;
    sState.confirmPressed = false;
    sState.cancelPressed = false;
    sState.musicTogglePressed = false;
    sState.upPressed = false;
    sState.downPressed = false;
    sState.leftPressed = false;
    sState.rightPressed = false;
    sState.touchPressed = false;
    sState.touchReleased = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            sState.quitRequested = true;
        } else if (event.type == SDL_CONTROLLERDEVICEADDED && !sController) {
            sController = SDL_GameControllerOpen(event.cdevice.which);
        } else if (event.type == SDL_CONTROLLERDEVICEREMOVED && sController) {
            SDL_GameControllerClose(sController);
            sController = nullptr;
        } else if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION) {
            if (!IsFrontTouch(event.tfinger.touchId)) continue;
            sLastFingerEventMs = SDL_GetTicks();
            // Finger coordinates are normalised over the touch panel, which covers the display.
            Renderer::DisplayToLogical(event.tfinger.x * DISPLAY_WIDTH, event.tfinger.y * DISPLAY_HEIGHT,
                                       sState.touchX, sState.touchY);
            if (event.type == SDL_FINGERDOWN) {
                bool known = false;
                for (int i = 0; i < sNumFingers; ++i) known |= (sFingers[i] == event.tfinger.fingerId);
                if (!known && sNumFingers < kMaxFingers) sFingers[sNumFingers++] = event.tfinger.fingerId;
                sState.touchPressed = true;
            }
        } else if (event.type == SDL_FINGERUP) {
            if (!IsFrontTouch(event.tfinger.touchId)) continue;
            sLastFingerEventMs = SDL_GetTicks();
            for (int i = 0; i < sNumFingers; ++i) {
                if (sFingers[i] == event.tfinger.fingerId) { sFingers[i] = sFingers[--sNumFingers]; break; }
            }
            sState.touchReleased = true;
#ifndef __vita__
        } else if (event.type == SDL_MOUSEMOTION && event.motion.which != SDL_TOUCH_MOUSEID) {
            Renderer::WindowToLogical((float)event.motion.x, (float)event.motion.y, sState.touchX, sState.touchY);
            sState.pointerAim = true;
        } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.which != SDL_TOUCH_MOUSEID) {
            Renderer::WindowToLogical((float)event.button.x, (float)event.button.y, sState.touchX, sState.touchY);
            sState.pointerAim = true;
            if (event.button.button == SDL_BUTTON_LEFT) {
                sMouseDown = true;
                sState.touchPressed = true;
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                sMouseRightPulse = true;
            }
        } else if (event.type == SDL_MOUSEBUTTONUP && event.button.which != SDL_TOUCH_MOUSEID &&
                   event.button.button == SDL_BUTTON_LEFT) {
            sMouseDown = false;
            sState.touchReleased = true;
#endif
        }
    }

    // Self-heal: if SDL reports no fingers on the front panel, any tracked ids are stale (lost UP).
    if (sNumFingers > 0) {
        bool found = false;
        SDL_TouchID front = FrontTouchDevice(found);
        if (found && SDL_GetNumTouchFingers(front) == 0) sNumFingers = 0;
    }
    sState.fingerDown = sNumFingers > 0;
    sState.touchDown = sState.fingerDown || sMouseDown;

    // Regression canary (HW_PERF builds): a finger held with no events at all for 10 s.
    if (!sState.fingerDown) {
        sStuckLogged = false;
    } else if (!sStuckLogged && SDL_GetTicks() - sLastFingerEventMs > 10000) {
        sStuckLogged = true;
        PERF_LOG("touchDown stuck >10 s with no finger events (fingers=%d)", sNumFingers);
    }

    float moveX = 0.0f;
    float aimX = 0.0f;
    float aimY = 0.0f;
    bool btnCannon = false;
    bool btnNuke = false;
    bool btnLaser = false;
    bool btnAltFire = false;
    bool btnPause = false;
    bool btnConfirm = false;
    bool btnCancel = false;
    bool btnMusic = false;
    bool btnUp = false;
    bool btnDown = false;
    bool btnLeft = false;
    bool btnRight = false;

    if (sController) {
        // Left Stick Movement
        int16_t rawMoveX = SDL_GameControllerGetAxis(sController, SDL_CONTROLLER_AXIS_LEFTX);
        float normMoveX = (float)rawMoveX / 32767.0f;
        if (std::abs(normMoveX) > 0.2f) {
            moveX = normMoveX;
        }

        // D-Pad buttons
        btnUp = SDL_GameControllerGetButton(sController, SDL_CONTROLLER_BUTTON_DPAD_UP);
        btnDown = SDL_GameControllerGetButton(sController, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        btnLeft = SDL_GameControllerGetButton(sController, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        btnRight = SDL_GameControllerGetButton(sController, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

        if (btnLeft) {
            moveX = -1.0f;
        } else if (btnRight) {
            moveX = 1.0f;
        }

        // Right Stick Aiming
        int16_t rawAimX = SDL_GameControllerGetAxis(sController, SDL_CONTROLLER_AXIS_RIGHTX);
        int16_t rawAimY = SDL_GameControllerGetAxis(sController, SDL_CONTROLLER_AXIS_RIGHTY);
        float normAimX = (float)rawAimX / 32767.0f;
        float normAimY = (float)rawAimY / 32767.0f;

        float aimMagSq = normAimX * normAimX + normAimY * normAimY;
        if (aimMagSq > (0.25f * 0.25f)) {
            aimX = normAimX;
            aimY = normAimY;
            sState.hasAimInput = true;

            // Compute angle in degrees: 0 is right, -90 is up, -180 is left
            float rad = std::atan2(aimY, aimX);
            float deg = rad * 180.0f / 3.14159265f;

            // Clamp aiming angle so it remains above the ground plane
            if (deg > 0.0f && deg <= 90.0f) deg = 0.0f;       // clamp down-right to flat right
            if (deg > 90.0f && deg <= 180.0f) deg = -180.0f;  // clamp down-left to flat left

            sState.aimAngleDegrees = deg;

            // Twin-stick auto-fire threshold
            if (aimMagSq > (0.45f * 0.45f)) {
                btnCannon = true;
            }
        }

        // Action Buttons
        btnCannon |= SDL_GameControllerGetButton(sController, SDL_CONTROLLER_BUTTON_A);
        btnCannon |= SDL_GameControllerGetButton(sController, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);

        btnNuke = SDL_GameControllerGetButton(sController, SDL_CONTROLLER_BUTTON_Y) ||
                  SDL_GameControllerGetButton(sController, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);

        btnLaser = SDL_GameControllerGetButton(sController, SDL_CONTROLLER_BUTTON_X);
        btnAltFire = SDL_GameControllerGetButton(sController, SDL_CONTROLLER_BUTTON_Y);
        btnPause = SDL_GameControllerGetButton(sController, SDL_CONTROLLER_BUTTON_START);
        btnConfirm = SDL_GameControllerGetButton(sController, SDL_CONTROLLER_BUTTON_A);
        btnCancel = SDL_GameControllerGetButton(sController, SDL_CONTROLLER_BUTTON_B);
        btnMusic = SDL_GameControllerGetButton(sController, SDL_CONTROLLER_BUTTON_BACK);   // SELECT
    }

#ifndef __vita__
    // Desktop keyboard + mouse. Mouse aims (hover) and the left button fires, as on PC.
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    bool kLeft = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A];
    bool kRight = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];
    if (kLeft != kRight) moveX = kLeft ? -1.0f : 1.0f;
    btnLeft |= kLeft;
    btnRight |= kRight;
    btnUp |= keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
    btnDown |= keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
    btnCannon |= keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_J] || sState.touchDown;
    btnNuke |= keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_N] || sMouseRightPulse;
    btnLaser |= keys[SDL_SCANCODE_C] || keys[SDL_SCANCODE_M];
    btnConfirm |= keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE];
    btnCancel |= keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_BACKSPACE];
    btnPause |= keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_P];
    btnMusic |= keys[SDL_SCANCODE_TAB];
    sMouseRightPulse = false;

    // A gamepad or keyboard aim overrides the mouse until it moves again.
    if (std::abs(aimX) > 0.0f || std::abs(aimY) > 0.0f) sState.pointerAim = false;
#endif

    sState.moveAxisX = moveX;
    sState.aimAxisX = aimX;
    sState.aimAxisY = aimY;
    sState.fireCannon = btnCannon;

    // Edge triggers
    if (btnCannon && !sPrevFireCannon) sState.fireCannonPressed = true;
    if (btnNuke && !sPrevFireNuke) sState.fireNukePressed = true;
    if (btnLaser && !sPrevFireMegalaser) sState.fireMegalaserPressed = true;
    if (btnAltFire && !sPrevAltFire) sState.altFirePressed = true;
    if (btnPause && !sPrevPause) sState.pausePressed = true;
    if (btnConfirm && !sPrevConfirm) sState.confirmPressed = true;
    if (btnCancel && !sPrevCancel) sState.cancelPressed = true;
    if (btnMusic && !sPrevMusic) sState.musicTogglePressed = true;
    if (btnUp && !sPrevUp) sState.upPressed = true;
    if (btnDown && !sPrevDown) sState.downPressed = true;
    if (btnLeft && !sPrevLeft) sState.leftPressed = true;
    if (btnRight && !sPrevRight) sState.rightPressed = true;

    sPrevFireCannon = btnCannon;
    sPrevFireNuke = btnNuke;
    sPrevFireMegalaser = btnLaser;
    sPrevAltFire = btnAltFire;
    sPrevPause = btnPause;
    sPrevConfirm = btnConfirm;
    sPrevCancel = btnCancel;
    sPrevMusic = btnMusic;
    sPrevUp = btnUp;
    sPrevDown = btnDown;
    sPrevLeft = btnLeft;
    sPrevRight = btnRight;
}

} // namespace HeavyWeapon
