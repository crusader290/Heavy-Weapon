#pragma once

// The in-game playfield: a translation of the original game's Board widget
// (constructor 0x4191d0, Update 0x41b690, UpdateF 0x419e30, Draw 0x415530).
//
// World coordinates follow the original: x is centred (0 = middle of the screen, the
// tank roams -290..290) and everything is drawn with a +320 translation; y is screen y.
// Update() is one 100 Hz game tick.

#include "game/AppState.h"
#include "DataModels.h"
#include "Renderer.h"
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace HeavyWeapon {

class Craft;
class Hazard;
struct FlakBurst;

class Board {
public:
    Board(AppState& app, const LevelDef* level, const std::vector<CraftDef>& craftById, bool survival);
    ~Board();

    void Update();      // one 100 Hz tick
    void Draw();

    // Input, in 640x480 screen coordinates (original MouseMove/MouseDown/MouseUp).
    void SetTarget(int x, int y) { mMouseX = x; mMouseY = y; }
    void SetFiring(bool on) { mFiring = on; }
    void FireNuke();

    // Twin-stick drive override for gamepads: -1..1 replaces the cursor-derived speed.
    void SetDriveOverride(bool active, float axis) { mDriveOverride = active; mDriveAxis = axis; }

    bool IsGameOver() const { return mGameOver; }
    bool IsLevelComplete() const { return !mSurvival && mProgress >= mLength + 500; }
    // Debriefing statistics (+0x328 kills, +0x32c craft spawned, +0x340 friendlies lost).
    int Kills() const { return mKills; }
    int Spawned() const { return mSpawned; }
    int FriendlyKills() const { return mFriendlyKills; }
    void CountCraft(int spawned, int killed, int friendlyKilled) {
        mSpawned += spawned;
        mKills += killed;
        mFriendlyKills += friendlyKilled;
    }
    int Progress() const { return mProgress; }
    int Length() const { return mLength; }
    void SetProgress(int p) { mProgress = std::max(0, mSurvival ? p : std::min(p, mLength)); }
    // Survival draws its waves from survivalN.xml: one LevelDef per tier.
    void SetSurvivalLevels(const std::vector<LevelDef>* levels) { mSurvLevels = levels; }
    // Survival time in whole seconds (progress runs at 2 per tick, 100 ticks per second).
    int SurvivalSeconds() const { return mProgress / 200; }
    double TankX() const { return mTankX; }
    int TankY() const { return mTankY; }

    // --- services used by crafts and hazards (original Board helpers) --------------
    struct Bullet {                 // list +0x1e4, shared by shells and enemy bullets
        double x, y, vx, vy;
        double gravity = 0.0;       // +0x20
        double damage = 1.0;        // +0x28
        bool enemy = false;         // +0x30
        const Texture* img = nullptr; // +0x34
        int col = 0, row = 0;       // +0x38, +0x3c
    };

    AppState& App() { return mApp; }
    const CraftDef& CraftStats(int type) const;
    bool TankDead() const { return mRespawn != 0; }
    int RespawnTimer() const { return mRespawn; }
    void SetRespawnTimer(int t) { mRespawn = t; }
    bool NearLevelEnd(int margin) const { return !mSurvival && mProgress >= mLength - margin; }
    bool Survival() const { return mSurvival; }
    const LevelDef* WaveLevel() const;  // level whose waves are drawn from
    int Tier() const;               // mission index, or progress/12000 in survival
    double ScrollSpeed() const { return mScrollSpeed; }
    double TankTread() const { return mTread; }
    bool ShieldUp() const { return mApp.up[UP_SHIELD] != 0 || mShieldFlash > 0.0; }
    double TankCycle() const { return mCycle; }
    std::vector<Bullet>& Bullets() { return mBullets; }
    std::vector<std::unique_ptr<Craft>>& Crafts() { return mCrafts; }
    int FriendlyCount() const { return mFriendlies; }

    void KillTank();                                                          // 0x41a880
    void AbsorbShieldHit();      // shield takes a hit: one point (unless still flashing), flash, sparks
    void ShieldSpark();          // flash + sparks only
    void AddMuzzleFlash(double x, double y, double vx, double vy) { mFlashes.push_back({ x, y, vx, vy, 5.0 }); }
    void SpawnExplosion(double x, double y, int w, int h, double vx, double vy, double speed = 0.4); // 0x410f40
    void SpawnParticles(const Texture* img, double x, double y, double vx, double vy, int count,
                        double randSpeed, double accel, double maxV, int life, bool fade, int frame); // 0x42b900
    void SpawnCraters(int x, int n);                                          // 0x411540
    void AddScore(int points, int x, int y);                                  // 0x417230
    void FireEnemyShot(int x, int y, double angle, double speed);             // 0x411850
    void AddHazard(std::unique_ptr<Hazard> h);
    void SetGodMode(bool on) { mGod = on; }
    void Shake(int amount) { mShake = std::max(mShake, amount); }
    void SetNukeFlash(double v) { mNukeFlash = v; }
    void SetEdgeBlock(bool left, bool right) { mBlockLeft = left; mBlockRight = right; }
    void SetDozerPresent(bool on) { mDozerPresent = on; }
    bool CanDropCrate() const;
    void DropCrate(int x, int y);                                             // 0x42da00
    void DropPowerUp(double x, double y, double vx, double vy, int type);     // 0x42c1b0
    void AddFriendly(int d) { mFriendlies += d; }
    void AddMessage(const std::string& text, int x, int y, bool rainbow, bool red = false);  // 0x42c000
    // Bosses
    void SetBossDefs(const std::unordered_map<std::string, BossDef>* defs) { mBossDefs = defs; }
    const BossDef* BossStats(const std::string& type) const;
    void AddPart(std::unique_ptr<Craft> part);
    void StartBossDeath() { mDeath = true; mDeathTimer = 20; mDeathCount = 0; }
    bool BossDying() const { return mDeath; }
    double NukeFlash() const { return mNukeFlash; }
    double TankShieldPulse() const { return mShieldPulse; }
    void FlakHitHazards(const Texture* img, int x, int y) { HitHazard(img, x, y, 0, 0, 0.5); }
    int Pan(double x) const { return (int)(x * 3.0); }

    // Per-pixel sprite overlap test (0x42d750).
    static bool PixelHit(const Texture* a, int ax, int ay, int acol, int arow, bool amirror,
                         const Texture* b, int bx, int by, int bcol, int brow, bool bmirror,
                         bool centered = true);
    bool HitsTank(const Texture* img, int x, int y, int col, int row, bool mirror) const;
    bool HitsShield(const Texture* img, int x, int y, int col, int row, bool mirror) const;

private:
    struct Casing { double x, y, vx, vy; };                 // list +0x284
    struct MuzzleFlash { double x, y, vx, vy, life; };      // list +0x294
    struct GroundGlow { double x; int alpha; int type; };   // list +0x2d4
    struct Crater { double x; int life; int frame; };       // list +0x224
    struct Explosion {                                      // list +0x204
        double x, y, vx, vy;
        int w, h;
        double frame = 0.0, speed = 0.4;
    };
    struct Particle {                                       // particle system +0x2e8
        const Texture* img;
        double x, y, vx, vy, accel, maxV;
        bool fade;
        double frame, frameSpeed;
    };
    struct Popup {                                          // floating text (0x42c000)
        std::string text;
        double x, y;
        int life;
        bool negative;
        bool rainbow;
        int hue;
    };
    struct Crate {                                          // megalaser part, list +0x274
        double x, y, vx, vy;
        double frame;
        bool collected;
    };
    struct PowerUp {                                        // power-up item (0x42c2b0)
        int type;
        double x, y, vx, vy;
    };
    struct CloudPuff { double x, y, vx, vy; int frame; double life; };
    struct Track { double x0, x1; bool active; };             // list +0x254
    struct BigMessage {                                       // 0x41c810 "GET READY" etc.
        std::string text;
        std::vector<double> z;      // per-letter depth: letters zoom in one after another
    };
    struct Mushroom {                                       // nuke cloud (0x4282e0)
        double x = 0.0, alpha = 1024.0;
        std::vector<CloudPuff> smoke, fire;
    };
    struct Wave {                                           // list +0x2b4 (0x41a5b0)
        int counter, length;
        std::vector<int> types;
        std::vector<int> qty;
    };

    AppState& mApp;
    const LevelDef* mLevel;
    const std::vector<LevelDef>* mSurvLevels = nullptr;
    std::vector<CraftDef> mCraftById;
    bool mSurvival;

    // input (+0x60, +0x64, +0x128)
    int mMouseX = 320, mMouseY = 240;
    bool mFiring = false;
    bool mDriveOverride = false;
    float mDriveAxis = 0.0f;

    // tank
    double mTankX = -380.0;     // +0x90
    int mTankY = 443;           // +0x98
    double mTread = 0.0;        // +0x118
    double mGunCel = 10.0;      // +0x120
    double mCycle = 0.0;        // +0x140
    double mShieldFlash = 0.0;  // +0x148
    int mCooldown[7] = {};      // +0xa8..+0xc0
    bool mGod = false;          // testing: KillTank does nothing
    int mRespawn = 300;         // +0x80 the tank deploys when this runs out
    int mDeaths = 0;            // +0x1c8
    int mDeathX = 0;            // +0x1cc
    bool mGameOver = false;
    bool mBlockLeft = false;    // +0x1b8 a ground unit holds the left edge
    bool mBlockRight = false;   // +0x1b9
    bool mDozerPresent = false; // +0x1ba

    // level progress, waves and scrolling
    int mProgress = 0;          // +0x17c
    int mLength = 10000;        // +0x184
    int mWaveDelay = 100;       // +0x1d0
    int mCrateCooldown = 0;     // +0x1ac
    int mNukeCooldown = 0;      // +0x1b0 nuke power-ups are withheld until this runs out
    int mShieldCooldown = 0;    // +0x1b4 same for shields
    int mPupTimer = 1000;       // +0x178 power-up helicopter cooldown
    int mMegaParts = 0;         // +0x1a8 megalaser parts collected (0..3)
    int mMegaTime = 0;          // +0x12c megalaser mode remaining
    int mBeam = 0;              // +0x130 beam lit
    double mBeamAngle = 0.0;    // +0x150
    double mBeamLength = 0.0;   // +0x158
    double mShieldPulse = 0.0;  // +0x160 pickup pulse
    int mShake = 0;             // +0x84 white flash / screen shake
    int mShakeX = 0, mShakeY = 0;
    int mNoNukeMsg = 0;         // +0x1dc
    bool mDeployed = false;     // +0x326 a tank has been deployed this level
    int mMile = 0;              // +0x1c4 mile marker sign position (counts down while shown)
    int mGasName = 0;           // +0x198
    int mKills = 0, mSpawned = 0, mFriendlyKills = 0;
    std::vector<BigMessage> mMessages;
    int mFriendlies = 0;        // +0x1bc
    double mScrollSpeed = 1.0;  // +0x188
    double mGasX = 1000.0;      // +0x190 (gas station; >= 1000 means none)
    float mSkyX = 0, mGroundX = 0, mBgX = 0, mBg2X = 0;   // +0x6c, +0x70, +0x74, +0x78
    double mNukeFlash = 0.0;    // +0x88

    std::vector<Bullet> mBullets;
    std::vector<Casing> mCasings;
    std::vector<MuzzleFlash> mFlashes;
    std::vector<GroundGlow> mGlows;
    std::vector<Crater> mCraters;
    std::vector<Explosion> mExplosions;
    std::vector<Particle> mParticles;
    std::vector<Popup> mPopups;
    std::vector<Crate> mCrates;
    std::vector<PowerUp> mPowerUps;
    std::vector<Mushroom> mMushrooms;
    std::vector<std::unique_ptr<FlakBurst>> mFlak;  // list +0x234
    std::vector<Track> mTracks;
    double mOrbAngle = 0.0;     // +0x110
    double mOrbX[3] = {}, mOrbY[3] = {}, mOrbFlash[3] = {};   // +0xc8 (x, y, flash) * 3
    std::vector<Wave> mWaves;
    std::vector<std::unique_ptr<Craft>> mCrafts;     // list +0x1f4
    std::vector<std::unique_ptr<Craft>> mPendingParts;
    const std::unordered_map<std::string, BossDef>* mBossDefs = nullptr;
    Craft* mBoss = nullptr;     // +0x2e4
    bool mBossBarHidden = true; // +0x1e0 until the boss is in position
    bool mDeath = false;        // +0x300 boss death sequence (0x41bf20)
    int mDeathTimer = 0, mDeathCount = 0;
    std::vector<std::unique_ptr<Hazard>> mHazards;   // list +0x214
    std::vector<std::unique_ptr<Hazard>> mNewHazards;

    // --- update steps (named after the original functions) ------------------------
    void UpdateF();                 // 0x419e30 scrolling
    void UpdateTank();              // 0x412dc0
    void UpdateBullets();           // 0x41add0
    void UpdateCasings();           // 0x411980
    void UpdateMuzzleFlashes();     // 0x411b40
    void UpdateExplosions();        // 0x4111a0
    void UpdateParticles();         // 0x42ba60
    void UpdateSpawner();           // 0x41b340
    void StartWave(int tier, int index);   // 0x41a5b0
    void SpawnCraft(int type);      // 0x40fed0
    bool HitHazard(const Texture* img, int x, int y, int col, int row, double damage);  // 0x417390
    void UpdateCrates();            // 0x42db00
    void UpdatePowerUps();          // 0x42c2b0
    void UpdateMushrooms();         // 0x428ac0
    void UpdateMegalaser();         // 0x418c90
    void UpdateLaser();             // 0x418a50
    int PickPowerUp();              // 0x41aa90
    void SpawnMushroom();           // 0x4282e0
    void DrawMushrooms();           // 0x428770
    void DrawBeams();
    void UpdateOrbs();              // 0x418770
    void BossDefeated();            // 0x4138e0 (final stage)
    void SpawnBoss();
    void UpdateBossDeath();         // 0x4138e0 / 0x41bf70
    void ShowMessage(const std::string& text);
    void DrawMessages();
    void UpdateTracks();            // 0x4116b0

    void FireShell(double power, int sideOffset, int angleOffset);   // 0x410d40

    void DrawTank(int x, int y, int tread);     // 0x401b80
    void DrawHUD();                             // 0x42e220
};

} // namespace HeavyWeapon
