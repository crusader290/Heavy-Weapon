#pragma once

// Persistent player state that the original keeps on its app object (offsets in the
// original HeavyWeaponApp noted for reference).

namespace HeavyWeapon {

// Upgrade slots, app +0x968 + 4*i. Power-up crate type i increments slot i.
enum UpgradeSlot {
    UP_NUKES = 0,   // +0x968  max 3
    UP_SHIELD,      // +0x96c  max 3, hits absorbed
    UP_SPEED,       // +0x970
    UP_POWER,       // +0x974
    UP_RATE,        // +0x978  "rapid fire"
    UP_SPREAD,      // +0x97c  max 4, extra cannon shells; also the gun sprite row
    UP_ORBS,        // +0x980  armory weapons, max 3 each
    UP_HOMING,      // +0x984
    UP_LASER,       // +0x988
    UP_ROCKETS,     // +0x98c
    UP_FLAK,        // +0x990
    UP_STATIC,      // +0x994  "Thunderstrike"
    UP_COUNT
};

struct AppState {
    int mission = 0;            // +0x960 (0..18)
    int lives = 3;              // +0x964
    int up[UP_COUNT] = {};      // +0x968..+0x994
    int score = 0;
    int tick = 0;               // +0x414, global 100 Hz counter
    bool noWeapons = false;     // +0x958, set during scripted sequences (hides attachments, no fire)
    double detail = 1.0;        // +0xb28 particle detail (options), scales debris counts
    int missedShots = 0;        // +0xb1c statistics
};

} // namespace HeavyWeapon
