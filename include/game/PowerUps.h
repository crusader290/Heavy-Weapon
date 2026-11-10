#pragma once

// The original six power-ups (table at VA 0x52bb18: char name[32], int max, ...,
// double weight). Mods may tweak them (hw.tweak_powerup), so the table is mutable.

#include <string>

namespace HeavyWeapon {

struct PowerUpInfo {
    std::string message;   // popup on pickup
    int max;               // level cap
    double weight;         // unlocks while level <= tier * weight; 0 = always offered
    int sound;             // SoundId
    double chance = 1.0;   // relative drop weight among the candidates (mods; 1 = original)
};

// i = UP_NUKES .. UP_SPREAD
PowerUpInfo& OriginalPowerUp(int i);

} // namespace HeavyWeapon
