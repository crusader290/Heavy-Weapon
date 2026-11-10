#pragma once

#include <cstdint>
#include <string>

namespace HeavyWeapon {

// Logical game space. Heavy Weapon is a 640x480 game and every asset is authored for
// it; the whole game renders into a 640x480 offscreen target that is scaled to the
// physical display at the end of the frame (see Renderer).
constexpr int SCREEN_WIDTH = 640;
constexpr int SCREEN_HEIGHT = 480;

// Physical PS Vita display
constexpr int DISPLAY_WIDTH = 960;
constexpr int DISPLAY_HEIGHT = 544;

constexpr float TARGET_FPS = 60.0f;
constexpr float FIXED_DT = 1.0f / TARGET_FPS;

// PopCap logic ticks at 100 Hz: Anims.xml speeds are "per program cycle, 1 = 100fps".
constexpr float POPCAP_TICKS_PER_SEC = 100.0f;

// Playfield layout (provisional until confirmed against the decompiled draw code).
// The 640x60 ground strip fills the bottom of the screen and the 30px status bar runs
// along the top. The 300px background planes are bottom-aligned to the screen: with
// that placement every theme's plane art ends just below the ground strip's grass edge
// and the Anims.xml props (absolute y) stand on the plane's terrain.
constexpr float STATUSBAR_Y = 0.0f;
constexpr float GROUND_PLANE_Y = 420.0f;   // top of the ground strip image
constexpr float BG_PLANE_Y = SCREEN_HEIGHT - 300.0f;
constexpr float GROUND_Y = 432.0f;         // walkable surface (below the strip's grass edge)
constexpr float TANK_DEFAULT_Y = 412.0f;   // tank chassis sprite centre

// Tank limits
constexpr float TANK_MIN_X = 40.0f;
constexpr float TANK_MAX_X = SCREEN_WIDTH - 40.0f;
constexpr float TANK_SPEED = 200.0f; // pixels per second

// Gameplay constants
constexpr int MAX_LIVES = 5;
constexpr int INITIAL_LIVES = 3;
constexpr int INITIAL_NUKES = 1;
constexpr int MAX_NUKES = 3;
constexpr int NUM_CAMPAIGN_MISSIONS = 19;

// Purely visual scenery animations from Images/Anims/Anims.xml (igloos, snowmen...). They have
// no gameplay effect; off saves their texture loads and per-frame work. Set true to restore.
constexpr bool kDecorativeAnims = false;

// Armory weapons, in the order of the armory slots and upgrades.png
// (app +0x980..+0x994 in the original; see UpgradeSlot in game/AppState.h).
enum WeaponType {
    WEAPON_ORBS = 0,
    WEAPON_HOMING,
    WEAPON_LASER,
    WEAPON_ROCKETS,
    WEAPON_FLAK,
    WEAPON_THUNDERSTRIKE,
    WEAPON_COUNT
};

// Game state machine
enum GameState {
    STATE_BOOT = 0,
    STATE_TITLE,
    STATE_MISSION_SELECT,
    STATE_PLAYING,
    STATE_ARMORY,
    STATE_PAUSED,
    STATE_GAMEOVER,
    STATE_VICTORY,
    STATE_DEBRIEF,
    STATE_OPTIONS,
    STATE_HELP
};

} // namespace HeavyWeapon
