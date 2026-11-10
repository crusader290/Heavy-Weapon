#pragma once

// Enemy craft and enemy ordnance, translated from the original classes.
//
// Craft (base ctor 0x431550, list Board+0x1f4): vtable slot 1 Draw, 2 Update, 3 UpdateF
// (movement), 4 Die, 5 Explode, 8 SetDirection. Craft type IDs 1..21 follow craft.xml.
// Hazard (base ctor 0x447760, list Board+0x214): bombs and missiles; shootable, and
// they hit the tank or its shield.

#include "TextureManager.h"
#include <memory>

namespace HeavyWeapon {

class Board;

enum CraftType {
    CRAFT_PROPFIGHTER = 1, CRAFT_SMALLJET, CRAFT_BOMBER, CRAFT_JETFIGHTER, CRAFT_TRUCK,
    CRAFT_BIGBOMBER, CRAFT_SMALLCOPTER, CRAFT_MEDCOPTER, CRAFT_BIGCOPTER, CRAFT_DELTABOMBER,
    CRAFT_DELTAJET, CRAFT_BIGMISSILE, CRAFT_SUPERBOMBER, CRAFT_FATBOMBER, CRAFT_BLIMP,
    CRAFT_SATELLITE, CRAFT_STRAFER, CRAFT_ENEMYTANK, CRAFT_DOZER, CRAFT_DEFLECTOR, CRAFT_CRUISE
};

class Craft {
public:
    Craft(Board& board, int type, const char* image);
    virtual ~Craft() = default;

    virtual void Update();          // slot 2; the base fades the hit flash
    virtual void UpdateF();         // slot 3; x += vx
    virtual void Draw();            // slot 1
    virtual void Die();             // slot 4: score, explosion, maybe a crate
    virtual void OnRemoved() {}     // clean-up when leaving the board (edge blocks etc.)
    virtual void DeflectBullets() {}

    void Explode();                 // slot 5
    // A boss stays on the board through its death sequence (the original skips removing
    // Board+0x2e4); Board::BossDefeated clears `persistent` and removes it.
    void Remove() { if (!persistent) dead = true; }

    int W() const { return img ? img->GetCelWidth() : 0; }
    int H() const { return img ? img->GetCelHeight() : 0; }
    bool OffScreen() const { return x < -320 - W() / 2 || x > W() / 2 + 320; }
    bool OnScreen300() const { return x > -300.0 && x < 300.0; }

    Board& b;
    int type;
    double x = 0, y = 0, vx = 0, vy = 0;   // +0x08 +0x10 +0x18 +0x20
    double frame = 0;                       // +0x28
    bool mirror = false;                    // +0x30 entered from the right
    int flash = 0;                          // +0x34 red hit flash
    bool friendly = false;                  // +0x38
    bool hittable = true;                   // +0x48
    double hp = 1;                          // +0x58
    double maxHp = 1;                       // +0x40
    bool boss = false;
    bool persistent = false;
    const Texture* img = nullptr;           // +0x60
    int points = 0;                         // +0x64
    bool dead = false;
    int cooldown = 0;                       // +0x70

protected:
    void PickSide();                        // slot 8 (0x431650)
    // Common spawn: random height in the sky band, enter from the chosen side at a
    // random speed of base + (rand % spread) / 100.
    void EnterFromSide(int yRange, double baseSpeed, int spread);
};

std::unique_ptr<Craft> CreateCraft(Board& board, int type);
std::unique_ptr<Craft> CreatePupCopter(Board& board, int powerUpType);   // 0x437450
void SpawnBigMissiles(Board& board);       // craft type 12 spawns a row (0x40fed0 case 0xc)

class Hazard {
public:
    Hazard(Board& board, const char* image, double x, double y, double vx, double vy, bool mirror);
    virtual ~Hazard() = default;

    virtual void Update() = 0;
    virtual void Draw();            // 0x4476b0

    // 0x447890: hit flash fade and shield contact (costs a shield point).
    void BaseUpdate();
    void Remove() { dead = true; }
    void RemoveWithCrater();        // 0x447860
    bool OffScreenX() const;
    bool CheckGroundAndTank();      // common bomb tail: ground blast / tank hit; true if removed

    Board& b;
    double x, y, vx, vy;            // +0x10 +0x18 +0x20 +0x28
    double frame = 0;               // +0x30
    bool mirror;                    // +0x40
    bool hittable = true;           // +0x41
    double hp = 0;                  // +0x48
    int flash = 0;                  // +0x50
    bool active = true;             // +0x54
    bool behind = false;            // +0x55 drawn behind the background planes
    int points = 10;                // +0x08
    const Texture* img;             // +0x5c
    bool dead = false;
};

// Bomb spawners used by craft (0x4104b0 dumb bomb, 0x410540 iron bomb, 0x4105d0 missile).
void DropDumbBomb(Board& b, int x, int y, double vx, double vy, bool mirror);
void DropIronBomb(Board& b, int x, int y, double vx, double vy, bool mirror);
void DropFragBomb(Board& b, int x, int y, double vx, double vy, bool mirror);
void DropLaserGuidedBomb(Board& b, int x, int y, double vx, double vy, bool fromRight);
void DropFatBoy(Board& b, int x, int y, double vx, double vy, bool mirror, bool tilted = false);
void FireRpg(Board& b, double x, double y, bool mirror, double arc);
void FireMissile(Board& b, int x, int y, double vx, double vy, double speedScale = 1.0);

} // namespace HeavyWeapon
