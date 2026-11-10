#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "Constants.h"

namespace HeavyWeapon {

// Craft definition from craft.xml
struct CraftDef {
    int id = 0;          // 1-based position in craft.xml; the original's craft type ID
    std::string name;
    std::string desc;
    std::string arms;
    int points = 0;
    int armor = 1;
};

// Boss component definitions from bosses.xml
struct BossTurretDef {
    int armor = 80;
    int fireInterval = 150;
    float speed = 0.1f;
    bool stationary = true;
};

struct BossLauncherDef {
    int armor = 100;
    int fireInterval = 200;
    float speed = 0.75f;
};

struct BossLevelDef {
    int armor = 500;
    int score = 10000;
    int turretArmor = 200;
    int fireInterval = 80;
    std::vector<BossTurretDef> turrets;
    std::vector<BossLauncherDef> launchers;
    int dishDown = 800;
    int dishUp = 400;
    int dishMeteors = 15;
    bool longChain = false;     // Wrecker
    double throwSpeed = 0.04;   // Ape: throw animation speed
    double jumpX = 1.35, jumpY = 4.0, jumpGravity = 0.06;
    int handArmor = 300, handFire = 20;   // Eye
    double speed = 3.0;                   // Head
    int chargeDelay = 500, bombOn = 30, bombOff = 30, bombFreq = 6;
    int boulders = 8;                     // Worm
    int armArmor = 750;                   // Robot
    double downAccel = 0.04, downMax = 3.0, upAccel = 0.2, upMax = 3.0;
    int bombFire = 200, laserFire = 50, smallLauncherFire = 120, bigLauncherFire = 160;   // Final
    struct Jump { int depth = 1100; double jump = 4.0, gravity = 0.02, lateral = 1.0; } wormJump, wormJumpFast;
};

struct BossDef {
    std::string type; // Helicopter, Battleship, Rainer, etc.
    std::string info;
    std::vector<BossLevelDef> levels; // Level 1, Level 2, Level 3
};

// Wave entry from waves.xml
struct WaveCraftEntry {
    std::string craftId;
    int quantity = 1;
};

struct WaveDef {
    int length = 1000;
    std::vector<WaveCraftEntry> craftList;
};

// Mission definition from levels.xml
struct IntelDef {
    std::string text;
};

struct LevelDef {
    std::string name;
    int length = 10000;
    std::vector<IntelDef> intelList;
    std::string bgTheme; // e.g. "frigistan", "blastnya", "petrovakia"
    std::vector<WaveDef> waves;
};

// Background animation definition from Anims.xml
struct AnimDelayDef {
    int frame = 0;
    float speed = 0.0f;
};

struct AnimDef {
    std::string name;
    std::string type; // "looping", "pingpong"
    int frames = 1;
    float speed = 0.1f;
    int plane = 1; // 4=Sky, 3=Far BG, 2=Mid BG, 1=Ground
    int offset = 640;
    int y = 350;
    float mx = 0.0f;
    bool nuke = false;
    bool rare = false;
    std::vector<AnimDelayDef> delays;
};

// Runtime active animation instance
struct ActiveAnim {
    AnimDef def;
    float currentFrame = 0.0f;
    float worldX = 0.0f;
    float y = 0.0f;
    bool nuked = false;
    bool visible = true;
};

} // namespace HeavyWeapon
