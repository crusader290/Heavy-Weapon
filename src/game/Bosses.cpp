// Bosses. Each boss is a craft that arrives as the level runs out (Board::Update spawns
// it at progress == length-1 by mission % 9) plus craft "parts" riding on it. Stats come
// from bosses.xml, Level = mission / 9 + 1 (0x43f910).

#include "game/Bosses.h"
#include "game/Board.h"
#include "game/Enemies.h"
#include "game/Gfx.h"
#include "AudioSystem.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace HeavyWeapon {

static int Rand() { return std::rand() & 0x7fffffff; }

// Sub-folder boss images are not in the game's image table, so their grids come from the
// boss constructors (0x42d500 calls).
static Texture* Grid(const char* path, int cols, int rows = 1) {
    return TextureManager::LoadGrid(path, cols, rows);
}

static const BossLevelDef* Stats(Board& b, const char* type) {
    const BossDef* def = b.BossStats(type);
    if (!def || def->levels.empty()) return nullptr;
    size_t level = std::min((size_t)(b.App().mission / 9), def->levels.size() - 1);
    return &def->levels[level];
}

namespace {

// ---------------------------------------------------------------------------------
// Helicopter "Twinblade" (0x440300): hovers over the middle with two missile launchers,
// plus two sweeping energy-cannon turrets from its second encounter on.
// ---------------------------------------------------------------------------------

struct BossCopter;

// A part that rides on its boss at a fixed offset and becomes hittable with it.
struct BossPart : Craft {
    Craft& boss;
    double ox, oy;
    int fireDelay, timer;
    BossPart(Board& bd, Craft& owner, Texture* image, double hp_, double ox_, double oy_, int fire)
        : Craft(bd, 0, "pupcopter"), boss(owner), ox(ox_), oy(oy_), fireDelay(fire), timer(fire) {
        img = image;
        hp = maxHp = hp_;
        points = 0;
        hittable = false;
        b.CountCraft(-1, 0, 0);     // parts do not count toward the kill percentage
    }
    bool Follow() {
        if (b.Progress() < b.Length()) return false;
        Craft::Update();
        x = (int)boss.x + ox;
        y = (int)boss.y + oy;
        vx = boss.vx;
        vy = boss.vy;
        hittable = boss.hittable;
        return true;
    }
    void UpdateF() override {}
    void Die() override {
        Craft::Die();
        b.CountCraft(0, -1, 0);
    }
    void Draw() override {
        if (b.Progress() >= b.Length()) Craft::Draw();
    }
};

// Missile launcher (0x440780).
struct CopterLauncher : BossPart {
    double speed;
    CopterLauncher(Board& bd, Craft& owner, const BossLauncherDef& d, double ox_)
        : BossPart(bd, owner, Grid("Images/hugecopter/launcher", 1), d.armor, ox_, 45.0, d.fireInterval), speed(d.speed) {}
    void Update() override {
        if (!Follow() || !boss.hittable) return;
        timer = b.TankDead() ? fireDelay : timer - 1;
        if (x > -300.0 && x < 300.0 && timer < 1 && boss.y > 50.0 && b.NukeFlash() == 0.0 && !b.BossDying()) {
            FireMissile(b, (int)x, (int)y, vx, vy, speed);
            timer = fireDelay;
        }
    }
};

// Sweeping energy cannon (0x440a20).
struct CopterTurret : BossPart {
    double sweep;
    CopterTurret(Board& bd, Craft& owner, const BossTurretDef& d, double ox_, double sweep_)
        : BossPart(bd, owner, Grid("Images/hugecopter/turret", 21), d.armor, ox_, 33.0, d.fireInterval), sweep(sweep_) {
        frame = 10.0;
    }
    void Update() override {
        if (!Follow()) return;
        frame += sweep;
        int cols = img ? img->cols : 21;
        if (frame >= cols) {
            sweep = -0.1;
            frame = cols - 1;
        } else if (frame < 0.0) {
            sweep = 0.1;
            frame = 0.0;
        }
        timer = b.TankDead() ? fireDelay : timer - 1;
        if (timer < 1 && boss.y > 50.0 && !b.TankDead() && b.NukeFlash() == 0.0 && !b.BossDying()) {
            double a = frame / (cols * 0.6369426751592356) + 0.7854;
            b.FireEnemyShot((int)(x - std::cos(a) * 30.0), (int)(std::sin(a) * 30.0 + y), a, 0.0);
            timer = fireDelay;
        }
    }
};

struct BossCopter : Craft {
    Texture* blades;
    double bladeFrame = 0.0;
    BossCopter(Board& bd) : Craft(bd, 0, "pupcopter") {
        img = Grid("Images/hugecopter/hugecopter", 1);
        blades = Grid("Images/hugecopter/copterblades", 5);
        const BossLevelDef* st = Stats(b, "Helicopter");
        hp = maxHp = st ? st->armor : 300;
        points = st ? st->score : 10000;
        boss = true;
        persistent = true;
        hittable = false;
        x = 0.0;
        y = -(double)H();
        if (st) {
            BossLauncherDef ld = st->launchers.empty() ? BossLauncherDef() : st->launchers[0];
            b.AddPart(std::make_unique<CopterLauncher>(b, *this, ld, -75.0));
            b.AddPart(std::make_unique<CopterLauncher>(b, *this, ld, 75.0));
            if (b.App().mission >= 1 && !st->turrets.empty()) {
                b.AddPart(std::make_unique<CopterTurret>(b, *this, st->turrets[0], -122.0, -0.1));
                b.AddPart(std::make_unique<CopterTurret>(b, *this, st->turrets[0], 122.0, 0.1));
            }
        }
    }
    ~BossCopter() override { AudioSystem::SetLoop(SND_BOSSCOPTER, false); }
    void UpdateF() override {}
    void Update() override {
        Craft::Update();
        bladeFrame += 0.4;
        if (bladeFrame >= 5.0) bladeFrame -= 5.0;
        x += vx;
        y += vy;
        // Descend to the hover band, then drift around the middle.
        if (y >= 140.0) {
            hittable = true;
            vy += (y <= 200.0) ? (double)(Rand() % 20) * 0.001 - 0.01 : -0.01;
        } else {
            vy += 0.01;
        }
        if (x < -120.0) vx += 0.01;
        else if (x > 120.0) vx -= 0.01;
        else vx += (double)(Rand() % 20) * 0.001 - 0.01;
        vx = std::clamp(vx, -0.5, 0.5);
        vy = std::clamp(vy, -0.5, 0.5);
        AudioSystem::SetLoop(SND_BOSSCOPTER, true, b.Pan(x * 2.0));
    }
    void Draw() override {
        if (b.Progress() < b.Length()) return;
        if (flash != 0) {
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(255, 255 - flash, 255 - flash);
        }
        Gfx::DrawSprite(img, (int)x, (int)y, true, 0, 0, mirror);
        Gfx::DrawSprite(blades, (int)(x - 122.0), (int)(y - 68.0), true, (int)bladeFrame);
        Gfx::DrawSprite(blades, (int)(x + 122.0), (int)(y - 68.0), true, (int)bladeFrame);
        Gfx::SetColorizeImages(false);
    }
    // Slot 4 (0x43f800): the score, then the death sequence takes over.
    void Die() override {
        if (b.BossDying()) return;
        b.CountCraft(0, 1, 0);
        b.AddScore(points, (int)x, (int)y);
        b.StartBossDeath();
    }
};

// ---------------------------------------------------------------------------------
// Battleship (0x439e60): sails in from the right with four sweeping gun turrets and
// launches homing missiles from the stern.
// ---------------------------------------------------------------------------------

// Gun turret (0x43a1c0). Guns 0/1 sweep the front arc, guns 2/3 the rear arc.
struct BattleGun : BossPart {
    int index;
    double sweep, sweepSpeed;
    BattleGun(Board& bd, Craft& owner, int idx, double hp_, const BossTurretDef& d, double ox_, double oy_)
        : BossPart(bd, owner, Grid("Images/battleship/gun", 21), hp_, ox_, oy_, d.fireInterval),
          index(idx), sweep(d.speed), sweepSpeed(d.speed) {
        frame = 10.0;
    }
    void Update() override {
        if (b.Progress() < b.Length()) return;
        Craft::Update();
        x = (int)boss.x + ox;
        y = (int)boss.y + oy;
        vx = boss.vx;
        vy = boss.vy;
        hittable = true;
        int cols = img ? img->cols : 21;
        frame += sweep;
        if (index < 2) {
            if (frame >= cols) { frame = cols - 1; sweep = -sweepSpeed; }
            else if (frame < 7.0) { frame = 7.0; sweep = sweepSpeed; }
        } else {
            if (frame >= cols / 2 + 4) { frame = cols / 2 + 3; sweep = -sweepSpeed; }
            else if (frame < 0.0) { frame = 0.0; sweep = sweepSpeed; }
        }
        timer = b.TankDead() ? fireDelay : timer - 1;
        if (timer < 1 && boss.y > 50.0 && !b.TankDead() && b.NukeFlash() == 0.0 && !b.BossDying()) {
            double a = frame / (cols * 0.6369426751592356) + 0.7854;
            // Twin barrels: one shrapnel shell from each (the volley count is not recoverable
            // from the decompiled loop; two matches the double-barrel sprite).
            for (int k = 0; k < 13; k += 12) {
                Board::Bullet s;
                s.x = (frame - 10.0) * 2.0 + x - k;
                s.y = y - 17.0;
                s.vx = std::cos(a) * -3.75;
                s.vy = std::sin(a) * -3.75;
                s.gravity = 0.05;
                s.enemy = true;
                s.img = TextureManager::Get("bombfrag");
                b.Bullets().push_back(s);
                b.AddMuzzleFlash(s.x, s.y, 0.0, 0.0);
                b.SpawnParticles(TextureManager::Get("smoke"), s.x, s.y - 20.0, -0.5, 0.0, 4, 0.5, -0.01, -0.5, 100, true, -1);
            }
            AudioSystem::PlaySoundId(SND_ENEMYTANKGUN, b.Pan(x));
            timer = fireDelay;
        }
    }
};

struct Battleship : Craft {
    Texture* hull;
    BattleGun* guns[4] = {};
    int launchTimer, launchDelay;
    Battleship(Board& bd) : Craft(bd, 0, "pupcopter") {
        img = Grid("Images/battleship/main", 1);
        hull = Grid("Images/battleship/hull", 1);
        const BossLevelDef* st = Stats(b, "Battleship");
        hp = maxHp = st ? st->armor : 800;
        points = 25000;   // the constructor overrides the XML score
        boss = true;
        persistent = true;
        hittable = false;
        x = 770.0;
        y = 260.0;
        vx = -1.0;
        launchDelay = launchTimer = (st && !st->launchers.empty()) ? st->launchers[0].fireInterval : 250;
        static const double kOffset[4][2] = { { -219.0, 51.0 }, { -306.0, 47.0 }, { 219.0, 49.0 }, { 306.0, 43.0 } };
        for (int i = 0; i < 4; ++i) {
            BossTurretDef td = (st && i < (int)st->turrets.size()) ? st->turrets[i] : BossTurretDef();
            auto g = std::make_unique<BattleGun>(b, *this, i, st ? st->turretArmor : 200, td, kOffset[i][0], kOffset[i][1]);
            guns[i] = g.get();
            b.AddPart(std::move(g));
        }
    }
    bool GunAlive(int i) {
        for (auto& c : b.Crafts()) {
            if (c.get() == guns[i] && !c->dead) return true;
        }
        return false;
    }
    void UpdateF() override {}
    void Update() override {
        Craft::Update();
        x += vx;
        // Sail in and stop near the middle; with both bow guns gone, press on to the left.
        if (!GunAlive(0) && !GunAlive(1)) {
            if (x <= -50.0) vx = std::min(vx + 0.005, 0.0);
            else vx -= 0.005;
        } else if (x < 240.0) {
            vx = std::min(vx + 0.005, 0.0);
        }
        vx = std::clamp(vx, -1.0, 1.0);
        if (vx == 0.0) hittable = true;

        if (launchTimer != 0) --launchTimer;
        if (b.TankDead()) launchTimer = launchDelay;
        if (vx == 0.0 && launchTimer == 0 && b.NukeFlash() == 0.0 && !b.BossDying()) {
            FireMissile(b, (int)(x >= 0.0 ? x - 120.0 : x + 120.0), 300, 0.0, 0.0, 0.75);
            launchTimer = launchDelay;
        }
    }
    void Draw() override {
        if (b.Progress() < b.Length()) return;
        Gfx::DrawSprite(hull, (int)x, (int)(y + 89.0), true);
        Craft::Draw();
    }
    void Die() override {
        if (b.BossDying()) return;
        b.CountCraft(0, 1, 0);
        b.AddScore(points, (int)x, (int)y);
        b.StartBossDeath();
    }
};

// ---------------------------------------------------------------------------------
// Rainer war blimp (0x441c30): drifts over the battlefield dropping roto-mines, and
// lowers a tractor-beam dish that calls down meteor showers.
// ---------------------------------------------------------------------------------

// Roto-mine (0x447a30): falls while steering toward the tank. Shootable.
struct AirMine : Hazard {
    AirMine(Board& bd, double x_, double y_, double vx_, double vy_) : Hazard(bd, "airmine", x_, y_, vx_, vy_, false) {
        hp = 20.0;
        points = 40;
    }
    void Update() override {
        x += vx;
        y += vy;
        if (vy < 3.0) vy += 0.02;
        double steer = 0.1 - std::cos(y * 0.0032710416666666665) * 0.1;
        vx += (b.TankX() <= x) ? -steer : steer;
        vx = std::clamp(vx, -3.0, 3.0);
        frame += 0.5;
        if (frame >= 5.0) frame -= 5.0;
        CheckGroundAndTank();
    }
};

// Meteor (0x449510): streaks in from high above at a steep angle. Cannot be shot.
struct Meteor : Hazard {
    Meteor(Board& bd) : Hazard(bd, "meteorite", 0, 0, 0, 0, false) {
        int cx = Rand() % 640 - 320;
        double a = (double)(Rand() % 800) * 0.001 + 1.5701 - 0.4;
        double d = (double)(Rand() % 1000) + 500.0;
        double sp = (double)(Rand() % 100) * 0.01 + 3.0;
        active = false;
        x = d * std::cos(a) + cx;
        y = 460.0 - std::sin(a) * d;
        vx = -(std::cos(a) * sp);
        vy = std::sin(a) * sp;
    }
    void Update() override {
        x += vx;
        y += vy;
        if (y >= 0.0 && y - vy < 0.0) AudioSystem::PlaySoundId(SND_METEOR, b.Pan(x));
        if (b.App().tick % 3 == 0) b.SpawnParticles(img, x, y, 0.0, 0.0, 1, 0.0, 0.0, 0.0, 60, true, -1);
        CheckGroundAndTank();
    }
};

struct Rainer : Craft {
    Texture* dish;
    Texture* prop;
    Texture* beam;
    double propFrame = 0.0;     // +0x80
    double dishAngle = 0.0;     // +0x88 0 = stowed, pi/2 = lowered
    bool beamOn = false;        // +0x90
    bool entered = false;       // +0x48
    int dishTimer, dishUp, dishDown, meteors;
    int mineTimer = 0, mineDelay;
    Rainer(Board& bd) : Craft(bd, 0, "pupcopter") {
        img = Grid("Images/rainer/rainer", 1);
        dish = Grid("Images/rainer/dish", 1);
        prop = Grid("Images/rainer/prop", 8);
        beam = Grid("Images/rainer/beam", 1);
        const BossLevelDef* st = Stats(b, "Rainer");
        hp = maxHp = st ? st->armor : 2700;
        points = st ? st->score : 30000;
        mineDelay = st ? st->fireInterval : 80;
        dishUp = st ? st->dishUp : 400;
        dishDown = dishTimer = st ? st->dishDown : 800;
        meteors = st ? st->dishMeteors : 15;
        boss = true;
        persistent = true;
        hittable = false;   // +0x48, set once the blimp has drifted in
        x = 520.0;
        y = 200.0;
        vx = -0.75;
        vy = 0.0;
    }
    ~Rainer() override { AudioSystem::SetLoop(SND_TRACTORBEAM, false); }
    void UpdateF() override {}
    void Update() override {
        if (b.Progress() < b.Length()) return;
        Craft::Update();
        if (x >= 20.0 || entered) {
            // Wander, pulled back toward the centre and a cruising height of 200.
            vx += ((double)(Rand() % 100) - 50.0) * 0.0005;
            vx -= x * 0.00025;
            vy += (200.0 - y) * 0.0005 + ((double)(Rand() % 100) - 50.0) * 0.00025;
            vx = std::clamp(vx, -0.75, 0.75);
            vy = std::clamp(vy, -0.25, 0.25);
        } else {
            entered = true;
        }
        hittable = entered;
        x += vx;
        y += vy;
        propFrame += 0.5;
        if (propFrame >= 8.0) propFrame -= 8.0;

        if (entered) {
            if (!beamOn || b.TankDead()) {
                AudioSystem::SetLoop(SND_TRACTORBEAM, false);
                beamOn = false;
                if (dishAngle > 0.0) dishAngle -= 0.01;
                if (dishTimer-- < 0) {
                    beamOn = true;
                    dishTimer = dishUp;
                }
            } else {
                if (dishAngle < 1.5701) dishAngle += 0.01;
                if (b.NukeFlash() == 0.0 && !b.BossDying()) {
                    if (dishTimer-- < 0) {
                        AudioSystem::SetLoop(SND_TRACTORBEAM, false);
                        beamOn = false;
                        dishTimer = dishDown;
                        for (int i = 0; i < meteors; ++i) b.AddHazard(std::make_unique<Meteor>(b));
                    } else if (dishAngle > 1.56) {
                        AudioSystem::SetLoop(SND_TRACTORBEAM, true, b.Pan(x));
                    }
                }
            }
        }

        // Roto-mines drop only while the dish is stowed.
        if (b.TankDead() || !entered || dishAngle > 0.02) mineTimer = mineDelay;
        else --mineTimer;
        if (mineTimer < 1 && b.NukeFlash() == 0.0 && !b.BossDying()) {
            b.AddHazard(std::make_unique<AirMine>(b, x, y - 115.0, (double)(Rand() % 400) * 0.01 - 2.0, -1.0));
            AudioSystem::PlaySoundId(SND_AIRMINE, b.Pan(x));
            mineTimer = mineDelay;
        }
        if (b.BossDying()) AudioSystem::SetLoop(SND_TRACTORBEAM, false);
    }
    void Tint() {
        if (flash != 0) {
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(255, 255 - flash, 255 - flash);
        }
    }
    void Draw() override {
        if (b.Progress() < b.Length()) return;
        Tint();
        Gfx::DrawSprite(dish, (int)x, (int)(y - 80.0 - std::sin(dishAngle) * 44.0), true);
        Gfx::SetColorizeImages(false);
        Craft::Draw();
        Tint();
        Gfx::DrawSprite(prop, (int)(x - 139.0), (int)(y - 96.0), true, (int)propFrame, 0, true);
        Gfx::DrawSprite(prop, (int)(x + 140.0), (int)(y - 96.0), true, (int)propFrame, 0, false);
        Gfx::SetColorizeImages(false);
        if (beamOn && dishAngle > 1.56 && !b.BossDying()) {
            // The beam texture scrolls upward through a 162px window.
            Gfx::SetDrawMode(1);
            int sy = 37 - (b.App().tick * 3) % 38;
            Gfx::DrawImageRect(beam, (int)(x - 25.0), (int)(y - 299.0), 50, 162, 0, sy, 50, 162);
            Gfx::SetDrawMode(0);
        }
    }
    void Die() override {
        if (b.BossDying()) return;
        b.CountCraft(0, 1, 0);
        b.AddScore(points, (int)x, (int)y);
        b.StartBossDeath();
    }
};

// ---------------------------------------------------------------------------------
// War Wrecker (0x447210): a crane truck that rolls in from the right and swings a
// wrecking ball on a chain. The tower is the hit box.
// ---------------------------------------------------------------------------------

// Boulder (0x447e00) thrown up when the ball smashes into the ground.
struct Boulder : Hazard {
    bool dim;
    Boulder(Board& bd, double x_, double y_, double vx_, double vy_, bool dim_)
        : Hazard(bd, "boulder", x_, y_, vx_, vy_, false), dim(dim_) {
        frame = (double)(Rand() % 5);
        hp = 20.0;
        points = 0;
    }
    void Update() override {
        x += vx;
        y += vy;
        if (vy < 2.0) vy += 0.05;
        if (OffScreenX()) { Remove(); return; }
        if (y > 460.0) {
            b.SpawnExplosion(x, y, 90, 90, 0.0, -1.0);
            RemoveWithCrater();
            return;
        }
        if (!b.TankDead() && b.HitsTank(img, (int)x, (int)y, (int)frame, 0, mirror)) {
            b.SpawnExplosion(x, y, 90, 90, 0.0, -1.0);
            b.KillTank();
            Remove();
            return;
        }
        BaseUpdate();
    }
    void Draw() override {
        if (dim) {
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(128, 128 - flash / 2, 128 - flash / 2);
        } else if (flash != 0) {
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(255, 255 - flash, 255 - flash);
        }
        Gfx::DrawSprite(img, (int)x, (int)y, true, (int)frame, 0, mirror);
        Gfx::SetColorizeImages(false);
    }
};

struct Wrecker : Craft {
    Texture *body, *wheel, *link, *ball, *shadow;
    double truckX = 640.0, truckV = -1.0, wheelFrame = 0.0;   // +0x90 +0x98 +0x88
    double swingA = 1.5708, swingB = 1.5708;                   // +0xa0 +0xa8
    double ballX = 0.0, ballY = 250.0, ballVx = 0.0, ballVy = 0.0; // +0xb0..+0xc8
    double chain = 200.0;                                      // +0xf0
    int maxChain;                                              // +0x10c
    bool entered = false, landed = false;                      // +0x48 +0xf8
    bool winch = false;                                        // +0x104
    bool loose = false;                                        // +0x108 ball cut free on death
    double lx = 0, ly = 0, lvx = 0, lvy = 0;
    Wrecker(Board& bd) : Craft(bd, 0, "pupcopter") {
        img = Grid("Images/wrecker/tower", 1);
        body = Grid("Images/wrecker/body", 1);
        wheel = Grid("Images/wrecker/wheel", 12);
        link = Grid("Images/wrecker/link", 1);
        ball = Grid("Images/wrecker/ball", 1);
        shadow = Grid("Images/wrecker/shadow", 1);
        const BossLevelDef* st = Stats(b, "Wrecker");
        hp = maxHp = st ? st->armor : 2500;
        points = st ? st->score : 40000;
        maxChain = (st && st->longChain) ? 0x170 : 0x160;
        boss = true;
        persistent = true;
        hittable = false;   // +0x48, set once the truck has parked
        x = 0.0;
        y = 190.0;
        vx = vy = 0.0;
    }
    ~Wrecker() override {
        AudioSystem::SetLoop(SND_DIESEL, false);
        AudioSystem::SetLoop(SND_CHAIN, false);
    }
    void SetWinch(bool on) {
        if (on != winch) AudioSystem::SetLoop(SND_CHAIN, on, b.Pan(x));
        winch = on;
    }
    void UpdateF() override {}
    void Update() override {
        if (b.Progress() < b.Length()) return;
        Craft::Update();
        if (loose) {
            lx += lvx;
            ly += lvy;
            lvy += 0.1;
            chain = std::max(chain - 2.0, 0.0);
        }
        if (!b.BossDying()) {
            swingA += 0.005;
            swingB += 0.015;
        }
        if (!entered) {
            truckX += truckV;
            wheelFrame -= truckV * -0.4;
            if (wheelFrame < 0.0) wheelFrame += 12.0;
            if (wheelFrame >= 12.0) wheelFrame -= 12.0;
            AudioSystem::SetLoop(SND_DIESEL, true, b.Pan(truckX));
            if (truckX < 20.0) {
                truckV = truckX * -0.05;
                if (truckX < 0.5) {
                    entered = hittable = true;
                    AudioSystem::SetLoop(SND_DIESEL, false);
                    AudioSystem::PlaySoundId(SND_AIRBRAKE, b.Pan(truckX));
                }
            }
        }
        if (swingA >= 6.28318) swingA -= 6.28318;
        if (swingB >= 6.28318) swingB -= 6.28318;
        x = (std::cos(swingA) + std::cos(swingB)) * 100.0 + truckX;

        // Pendulum: integrate, then pull the ball back onto the chain's length.
        ballX += ballVx;
        ballY += ballVy + 0.5;
        double dx = ballX - x, dy = ballY - 50.0;
        double k = chain / std::sqrt(dx * dx + dy * dy);
        ballX = dx * k + x;
        ballY = dy * k + 50.0;
        ballVx += (x - ballX) * 0.00025;
        ballVy = ((chain + 50.0) - ballY) * 0.01;

        if (!b.BossDying() && !b.TankDead() && b.HitsTank(ball, (int)ballX, (int)ballY, 0, 0, false)) b.KillTank();
        if (b.BossDying()) return;

        // On the long chain (mission 13) the ball smashes the ground and throws boulders.
        if (ballY >= 410.0) {
            if ((double)maxChain == chain && b.App().mission == 12 && !landed) {
                for (int i = 0; i < 10; ++i) {
                    double bvy = (double)(Rand() % 200) * 0.01 - 5.0;
                    double bvx = ballVx * 0.25 + (double)(Rand() % 600) * 0.01 - 3.0;
                    double bx = (double)(Rand() % 80) + ballX - 40.0;
                    b.AddHazard(std::make_unique<Boulder>(b, bx, 460.0, bvx, bvy, true));
                }
                b.SpawnParticles(TextureManager::Get("sanddust"), ballX, 460.0, ballVx * 0.25, -1.0, 20, 2.0, -0.01, -0.5, 200, true, -1);
                b.SpawnParticles(TextureManager::Get("rock"), ballX, 460.0, ballVx * 0.25, -2.0, 10, 1.0, 0.05, 3.0, 200, false, -1);
                landed = true;
                b.Shake(128);
                AudioSystem::PlaySoundId(SND_BIGTHUD, b.Pan(ballX));
            }
        } else {
            landed = false;
        }

        // Lower the ball while the tank is in play; wind it back up otherwise.
        if (!b.TankDead() && entered) {
            if (chain < maxChain) {
                chain += 0.5;
                SetWinch(true);
            } else {
                SetWinch(false);
            }
        } else if (b.App().lives >= 1) {
            if (chain > 200.0) {
                chain -= 0.5;
                SetWinch(true);
            } else {
                SetWinch(false);
            }
        }
    }
    void Draw() override {
        if (b.Progress() < b.Length()) return;
        if (flash != 0) {
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(255, 255 - flash, 255 - flash);
        }
        Gfx::DrawSprite(body, (int)truckX, (int)(y + 181.0), true);
        Gfx::DrawSprite(img, (int)x, (int)y, true);
        int a = (int)(ballY - 200.0);
        if (a > 0) {
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(255, 255, 255, std::min(a, 255));
            Gfx::DrawSprite(shadow, (int)ballX, 465, true);
            Gfx::SetColorizeImages(false);
            if (flash != 0) {
                Gfx::SetColorizeImages(true);
                Gfx::SetColor(255, 255 - flash, 255 - flash);
            }
        }
        for (double off : { -215.0, 215.0, -133.0, 133.0 })
            Gfx::DrawSprite(wheel, (int)(truckX + off), (int)(y + 225.0), true, (int)wheelFrame);
        Gfx::SetColorizeImages(false);
        // Chain links from the ball back up to the pulley at (x, 50).
        double t = std::atan2(ballX - x, ballY - 50.0);
        double sx = std::cos(t - 1.570795) * 16.0, sy = std::sin(t - 1.570795) * 16.0;
        for (double cx = ballX, cy = ballY; cy > 48.0; cx -= sx, cy += sy)
            Gfx::DrawSprite(link, (int)cx, (int)cy, true);
        Gfx::DrawSprite(ball, (int)(loose ? lx : ballX), (int)(loose ? ly : ballY), true);
        Gfx::SetColor(0xc0, 0xc0, 0xc0);
        Gfx::FillRect((int)(x - 12.0), 35, 24, 10);
    }
    void Die() override {
        if (b.BossDying()) return;
        b.CountCraft(0, 1, 0);
        b.AddScore(points, (int)x, (int)y);
        b.StartBossDeath();
        loose = true;
        lx = ballX; ly = ballY; lvx = ballVx; lvy = ballVy;
        SetWinch(false);
    }
};

// ---------------------------------------------------------------------------------
// Kommie Kong (0x438be0): a giant ape that throws bursting rockets three at a time,
// then leaps to the other side of the screen and shakes the ground when it lands.
// ---------------------------------------------------------------------------------

// Bursting rocket (0x448130): flies level and bursts into shrapnel above the tank.
struct BurstRocket : Hazard {
    int fragments;
    BurstRocket(Board& bd, double x_, double y_, double vx_, bool mirror_, int n)
        : Hazard(bd, "burstrocket", x_, y_, vx_, 0.0, mirror_), fragments(n) {
        hp = 80.0;
        points = 100;
    }
    void Update() override {
        x += vx;
        frame += 0.2;
        if (frame >= 8.0) frame = 0.0;
        if (OffScreenX()) { Remove(); return; }
        if (x < b.TankX() + 20.0 && x > b.TankX() - 20.0 && !b.TankDead()) {
            b.SpawnExplosion(x, y, 140, 70, vx, vy);
            for (double a = 1.5708; a < 7.8539; a += 6.282 / fragments) {   // 0x449fa0
                Board::Bullet s;
                s.x = x;
                s.y = y;
                s.vx = std::cos(a) * 3.0;
                s.vy = std::sin(a) * -3.0;
                s.gravity = 0.05;
                s.enemy = true;
                s.img = TextureManager::Get("bombfrag");
                b.Bullets().push_back(s);
            }
            Remove();
            return;
        }
        BaseUpdate();
    }
};

struct Ape : Craft {
    Texture* shadow;
    int wait = 100;             // +0x74
    int thrown = 0;             // +0x78 rockets in this volley
    int crouch = 0;             // +0x7c
    bool throwing = false;      // +0x80
    int shake = 0;              // +0x84
    double jumpX, jumpY, gravity, throwSpeed;
    bool entered = false;
    Ape(Board& bd) : Craft(bd, 0, "pupcopter") {
        img = Grid("Images/ape/ape", 9);
        shadow = Grid("Images/ape/shadow", 1);
        const BossLevelDef* st = Stats(b, "Ape");
        hp = maxHp = st ? st->armor : 2500;
        points = st ? st->score : 50000;
        throwSpeed = st ? st->throwSpeed : 0.04;
        jumpX = st ? st->jumpX : 1.35;
        jumpY = st ? st->jumpY : 4.0;
        gravity = st ? st->jumpGravity : 0.06;
        boss = true;
        persistent = true;
        mirror = true;          // facing left
        hittable = false;       // +0x48, set once the scroll has stopped
        x = 380.0;
        y = 315.0;
        vx = vy = 0.0;
        frame = 0.0;
    }
    int Facing() const { return mirror ? 1 : 0; }
    void UpdateF() override {}
    void Update() override {
        if (b.Progress() < b.Length()) return;
        Craft::Update();
        // Walk in with the scenery until the scroll has eased to a stop.
        if (b.ScrollSpeed() >= 0.1) {
            x -= b.ScrollSpeed() * 1.35;
            return;
        }
        entered = hittable = true;
        x += vx;
        if (b.TankDead()) thrown = 3;
        if (wait == 0) {
            if (!throwing || b.BossDying()) {
                if (crouch == 0) {
                    vy = std::min(vy + gravity, jumpY);
                    if (vy > 0.5) frame = 8.0;
                    y += vy;
                    if (y > 315.0) {
                        if (!b.BossDying()) {
                            shake = 0x80;
                            AudioSystem::PlaySoundId(SND_BIGTHUD, b.Pan(x));
                        }
                        y = 315.0;
                        vx = 0.0;
                        frame = 0.0;
                        if (std::abs((int)x) < 100) {
                            crouch = 50;     // landed mid-screen: jump again
                            frame = 6.0;
                        } else {
                            wait = 1;
                            if (!b.BossDying()) mirror = !mirror;
                        }
                    }
                } else if (--crouch == 0) {
                    frame = 7.0;
                    vy = -jumpY;
                    vx = -(double)(Facing() * 2 - 1) * jumpX;
                }
            } else {
                double prev = frame;
                frame += throwSpeed;
                if (frame > 5.9) {
                    frame = 0.0;
                    ++thrown;
                    wait = 1;
                }
                if (frame >= 4.0 && prev < 4.0 && !b.BossDying())
                    b.AddHazard(std::make_unique<BurstRocket>(b, x, y - 120.0, (double)(Facing() * -6 + 3), mirror, 6));
            }
        } else if (--wait == 0) {
            if (thrown == 3) {
                crouch = 50;
                frame = 6.0;
                thrown = 0;
                throwing = false;
            } else {
                throwing = true;
            }
        }
        if (shake != 0) {
            b.Shake(shake);
            shake -= 2;
        }
        // Hold the tank's deployment until the ape has moved clear of the drop zone.
        if (b.RespawnTimer() == 2 && ((thrown == 3 && Facing() == 1) || (Facing() == 0 && x < 0.0)))
            b.SetRespawnTimer(3);
        if (!b.BossDying() && !b.TankDead() && b.HitsTank(img, (int)x, (int)y, (int)frame, 0, mirror)) b.KillTank();
    }
    void Draw() override {
        if (b.Progress() < b.Length()) return;
        Gfx::SetColorizeImages(true);
        Gfx::SetColor(255, 255, 255, std::clamp((int)(255.0 - (315.0 - y)), 0, 255));
        Gfx::DrawSprite(shadow, (int)(Facing() * 40 - 20 + x), 455, true);
        Gfx::SetColorizeImages(false);
        Craft::Draw();
    }
    void Die() override {
        if (b.BossDying()) return;
        b.CountCraft(0, 1, 0);
        b.AddScore(points, (int)x, (int)y);
        b.StartBossDeath();
    }
};

// ---------------------------------------------------------------------------------
// Eyebot (0x43ba90): a floating eye with six jointed arms. The eye can only be hit while
// open; while closed, a hand charges up and calls down a lightning bolt.
// ---------------------------------------------------------------------------------

struct Eyebot;

// Hand (0x43c000): rides on the tip of its arm; the boss draws it.
struct EyeHand : BossPart {
    Eyebot& eye;
    int index;
    EyeHand(Board& bd, Eyebot& owner, Craft& asCraft, int idx, double hp_, Texture* image)
        : BossPart(bd, asCraft, image, hp_, 0.0, 0.0, 0), eye(owner), index(idx) {}
    void Update() override;
    void Draw() override {}
    void Die() override;
};

struct Eyebot : Craft {
    static constexpr int kArms = 6, kJoints = 8;
    static constexpr int kBase[kArms][2] = { { -54, -54 }, { -76, 0 }, { -54, 54 }, { 54, -54 }, { 76, 0 }, { 54, 54 } };
    Texture *body, *arm, *glow, *hand, *bolt, *handGlow;
    double pt[kArms][kJoints][2];   // +0xc8 arm joints; the last one carries the hand
    double reach[kArms][2];         // +0x3c8 where each hand is heading, relative to its base
    double hv[kArms][2];            // +0x3d8 hand velocity
    EyeHand* hands[kArms] = {};     // +0x3e8
    double tx = 0.0, ty = 190.0;    // +0x88 +0x90 roaming target
    int glowLevel = 0;              // +0x98
    int fireTimer, fireDelay;       // +0x9c +0xa0
    int charging = -1;              // +0xa8 hand charging a bolt
    int handTimer, handDelay;       // +0xac +0xb0
    int boltTimer = 0;              // +0xb4
    double charge = 0.0;            // +0xb8
    int handsLeft = kArms;          // +0xc0
    int cycle = 499;                // +0xc4 eye open/closed phase
    Eyebot(Board& bd) : Craft(bd, 0, "pupcopter") {
        img = Grid("Images/eye/eye", 6);
        body = Grid("Images/eye/body", 1);
        arm = Grid("Images/eye/arm", 1);
        glow = Grid("Images/eye/glow", 1);
        hand = Grid("Images/eye/hand", 1);
        bolt = Grid("Images/eye/bolt", 1);
        handGlow = Grid("Images/eye/handglow", 1);
        const BossLevelDef* st = Stats(b, "Eye");
        hp = maxHp = st ? st->armor : 1500;
        points = st ? st->score : 50000;
        fireDelay = fireTimer = st ? st->fireInterval : 60;
        handDelay = handTimer = st ? st->handFire : 20;
        double handHp = st ? st->handArmor : 300;
        boss = true;
        persistent = true;
        hittable = false;
        x = 0.0;
        y = -100.0;
        for (int i = 0; i < kArms; ++i) {
            int side = i / 3;
            reach[i][0] = side * 192 - 96 + kBase[i][0];
            reach[i][1] = kBase[i][1];
            hv[i][0] = hv[i][1] = 0.0;
            auto h = std::make_unique<EyeHand>(b, *this, *this, i, handHp, hand);
            hands[i] = h.get();
            b.AddPart(std::move(h));
            for (int j = 0; j < kJoints; ++j) {
                pt[i][j][0] = kBase[i][0] + j * (side * 24 - 12) + x;
                pt[i][j][1] = kBase[i][1] + y;
            }
        }
    }
    static void Constrain(double* p, const double* to) {
        double dx = p[0] - to[0], dy = p[1] - to[1];
        double d = std::sqrt(dx * dx + dy * dy);
        if ((int)d != 12 && d > 0.0) {
            p[0] = dx * (12.0 / d) + to[0];
            p[1] = dy * (12.0 / d) + to[1];
        }
    }
    void UpdateF() override {}
    void Update() override {
        if (b.Progress() < b.Length()) return;
        Craft::Update();
        if (y > 160.0) ++cycle;
        if ((cycle % 1000 < 500 || b.BossDying()) && handsLeft > 0) {
            frame = std::max(frame - 0.2, 0.0);
        } else {
            frame = std::min(frame + 0.2, 5.0);
        }
        hittable = frame > 0.0;
        glowLevel = hittable ? (int)((std::cos((b.App().tick % 63) * 0.1) + 1.0) * frame * 0.2 * 127.0) : 0;

        if (b.App().tick % 50 == 0) {
            tx = (double)(Rand() % 400) - 200.0;
            ty = (double)(Rand() % 20) + 180.0;
            for (int i = 0; i < kArms; ++i) {
                reach[i][0] = ((double)(Rand() % 40) - 20.0) + kBase[i][0];
                reach[i][1] = ((double)(Rand() % 40) - 20.0) + kBase[i][1];
            }
        }
        if (x < tx && vx < 1.5) vx += 0.02;
        if (tx < x && vx > -1.5) vx -= 0.02;
        if (y < ty && vy < 0.5) vy += 0.01;
        if (ty < y && vy > -0.5) vy -= 0.01;
        x += vx;
        y += vy;

        // Arms: steer each hand toward its target, then pull the chain of joints after it
        // and back to the shoulder (12px links).
        for (int i = 0; i < kArms; ++i) {
            double* tip = pt[i][kJoints - 1];
            double gx = kBase[i][0] + reach[i][0] + x, gy = kBase[i][1] + reach[i][1] + y;
            if (tip[0] < gx && hv[i][0] < 1.0) hv[i][0] += 0.01;
            if (gx < tip[0] && hv[i][0] > -1.0) hv[i][0] -= 0.01;
            if (tip[1] < gy && hv[i][1] < 1.0) hv[i][1] += 0.01;
            if (gy < tip[1] && hv[i][1] > -1.0) hv[i][1] -= 0.01;
            tip[0] += hv[i][0] + vx;
            tip[1] += hv[i][1] + vy;
            for (int j = kJoints - 2; j >= 0; --j) Constrain(pt[i][j], pt[i][j + 1]);
        }
        for (int i = 0; i < kArms; ++i) {
            pt[i][0][0] = kBase[i][0] + x;
            pt[i][0][1] = kBase[i][1] + y;
            for (int j = 1; j < kJoints; ++j) Constrain(pt[i][j], pt[i][j - 1]);
        }

        // Energy cannon: fires from the fully open eye.
        if (b.TankDead() || b.NukeFlash() != 0.0 || y < 160.0) fireTimer = fireDelay;
        else if (fireTimer != 0) --fireTimer;
        if (fireTimer == 0 && hittable) {
            if (frame == 5.0 && !b.BossDying()) {
                double a = (double)(Rand() % 157) * 0.01 + 3.926;
                Board::Bullet s;
                s.x = x;
                s.y = y;
                s.vx = std::cos(a) * -3.0;
                s.vy = std::sin(a) * -3.0;
                s.enemy = true;
                s.img = TextureManager::Get("bombfrag");
                b.Bullets().push_back(s);
                fireTimer = fireDelay;
                AudioSystem::PlaySoundId(SND_ENERGYFIRE, b.Pan(x));
            }
        } else if (!hittable) {
            // Eye closed: pick a hand to charge a lightning bolt.
            if (charging == -1 && y > 160.0) {
                if (!b.TankDead() && b.NukeFlash() == 0.0) {
                    if (handTimer != 0) --handTimer;
                } else {
                    handTimer = handDelay;
                }
                if (handTimer == 0 && !b.BossDying()) {
                    int live[kArms], n = 0;
                    for (int i = 0; i < kArms; ++i)
                        if (hands[i] && hands[i]->y > 80.0) live[n++] = i;
                    if (n != 0) {
                        charging = live[Rand() % n];
                        handTimer = handDelay;
                        AudioSystem::PlaySoundId(SND_BOLTCHARGE, b.Pan(hands[charging]->x));
                    }
                }
            }
        }
        if (charging >= 0) {
            if (b.NukeFlash() == 0.0) {
                if (charge < 255.0 && boltTimer == 0) {
                    charge += 2.5;
                    if (charge >= 255.0) {
                        charge = 255.0;
                        boltTimer = 50;
                        AudioSystem::PlaySoundId(SND_THUNDER, b.Pan(hands[charging] ? hands[charging]->x : x));
                    }
                }
            } else {
                charging = -1;
                boltTimer = 0;
                charge = 0.0;
            }
        }
        if (boltTimer > 0) {
            EyeHand* h = charging >= 0 ? hands[charging] : nullptr;
            if (h && !b.TankDead() && h->x < b.TankX() + 40.0 && b.TankX() - 40.0 < h->x) b.KillTank();
            if (--boltTimer == 0) {
                charge = 0.0;
                charging = -1;
            }
        }
    }
    void HandLost(int i) {
        if (charging == i) {
            handTimer = handDelay;
            charging = -1;
            boltTimer = 0;
            charge = 0.0;
        }
        hands[i] = nullptr;
        --handsLeft;
    }
    void Draw() override {
        if (b.Progress() < b.Length()) return;
        for (int i = 0; i < kArms; ++i) {
            for (int j = kJoints - 1; j >= 0; --j) Gfx::DrawSprite(arm, (int)pt[i][j][0], (int)pt[i][j][1], true);
            EyeHand* h = hands[i];
            if (!h) continue;
            if (h->flash != 0) {
                Gfx::SetColorizeImages(true);
                Gfx::SetColor(255, 255 - h->flash, 255 - h->flash);
            }
            Gfx::DrawSprite(hand, (int)h->x, (int)h->y, true);
            if (charge > 0.0 && charging == i) {
                Gfx::SetDrawMode(1);
                Gfx::SetColorizeImages(true);
                Gfx::SetColor((int)charge, (int)charge, (int)charge);
                Gfx::DrawSprite(handGlow, (int)h->x, (int)h->y, true);
                Gfx::SetDrawMode(0);
            }
            Gfx::SetColorizeImages(false);
        }
        Gfx::DrawSprite(body, (int)x, (int)y, true);
        if (hittable) {
            Craft::Draw();
            Gfx::SetDrawMode(1);
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(glowLevel, glowLevel, glowLevel);
            Gfx::DrawSprite(glow, (int)x, (int)y, true);
            Gfx::SetDrawMode(0);
            Gfx::SetColorizeImages(false);
        }
        if (boltTimer != 0 && charging >= 0 && hands[charging]) {
            EyeHand* h = hands[charging];
            int c = (int)((b.App().tick % 10) * 25.5);
            Gfx::SetDrawMode(1);
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(c, c, c);
            Gfx::DrawSprite(bolt, (int)(h->x - 30.0), (int)(h->y - 22.0), false);
            Gfx::FillRect(-320, 0, 640, 480);   // lightning flash
            Gfx::SetColorizeImages(false);
            Gfx::SetDrawMode(0);
        }
    }
    void Die() override {
        if (b.BossDying()) return;
        b.CountCraft(0, 1, 0);
        b.AddScore(points, (int)x, (int)y);
        b.StartBossDeath();
    }
};

void EyeHand::Update() {
    if (b.Progress() < b.Length()) return;
    Craft::Update();
    x = eye.pt[index][Eyebot::kJoints - 1][0];
    y = eye.pt[index][Eyebot::kJoints - 1][1];
    if (eye.hittable) hittable = true;
}

void EyeHand::Die() {
    BossPart::Die();
    eye.HandLost(index);
}

// ---------------------------------------------------------------------------------
// Bustczar (0x43f310): a giant head that rests at one side firing homing missiles, then
// charges across the screen carpet-bombing.
// ---------------------------------------------------------------------------------

// Head missile (0x448750): a slower homing missile that locks its heading once aimed.
struct HeadMissile : Hazard {
    double turn;            // +0x70
    bool locked = false;    // +0x68
    HeadMissile(Board& bd, double x_, double y_, bool mirror_) : Hazard(bd, "headmissile", x_, y_, 0, 0, false) {
        hp = 20.0;
        points = 50;
        turn = (double)(Rand() % 150) * 0.001 + 0.05;
        frame = mirror_ ? 3.0 : 17.0;
        AudioSystem::PlaySoundId(SND_MISSILE, b.Pan(x));
        b.SpawnParticles(TextureManager::Get("smoke"), x, y, 0.0, 0.0, 4, 1.0, -0.01, -0.5, 40, true, -1);
    }
    void Update() override {
        static constexpr double kStep = 17.143 * 0.017453292520882225;
        double a = (frame - 10.0) * kStep;
        vx = std::cos(a + 1.5705) * 3.0;
        vy = std::sin(a + 1.5705) * 3.0;
        x -= vx;
        y += vy;
        b.SpawnParticles(TextureManager::Get("spark"), vx * 5.0 + x, y - vy * 5.0, vx * 0.5, vy * -0.5, 1, 1.0, 0, 0, 30, true, -1);
        if (!locked) {
            double target = std::atan2(b.TankX() - x, (double)b.TankY() - y);
            if (a <= target) {
                frame += turn;
                if (target <= turn * kStep + a) locked = true;
            } else {
                frame -= turn;
                if (a - turn * kStep <= target) locked = true;
            }
        }
        CheckGroundAndTank();
    }
};

struct Head : Craft {
    double tx = 0.0, ty = 160.0;    // +0x98 +0xa0
    double maxSpeed;                // +0xb0
    bool charging = false;          // +0xac
    int delay = 500;                // +0xa8
    int launch[2], launchDelay[2];  // +0x70 +0x78
    int bombCycle = 0, bombOn, bombOff, bombFreq;   // +0x84 +0x88 +0x8c +0x90
    Head(Board& bd) : Craft(bd, 0, "pupcopter") {
        img = Grid("Images/head/head", 1);
        const BossLevelDef* st = Stats(b, "Head");
        hp = maxHp = st ? st->armor : 2800;
        points = st ? st->score : 60000;
        maxSpeed = st ? st->speed : 3.0;
        for (int i = 0; i < 2; ++i)
            launchDelay[i] = launch[i] = (st && i < (int)st->launchers.size()) ? st->launchers[i].fireInterval : 100 + 50 * i;
        bombOn = st ? st->bombOn : 30;
        bombOff = st ? st->bombOff : 30;
        bombFreq = std::max(1, st ? st->bombFreq : 6);
        // The XML charge delay is loaded (+0xb8) but the original always waits 500 ticks.
        boss = true;
        persistent = true;
        mirror = false;
        x = -440.0;
        y = 160.0;
        vx = vy = 0.0;
    }
    void UpdateF() override {}
    void Update() override {
        if (b.Progress() < b.Length()) return;
        Craft::Update();
        x += vx;
        y += vy;
        vx = (tx - x) * 0.02;
        vy = (ty - y) * 0.02;
        double sp = std::sqrt(vx * vx + vy * vy);
        if (sp > maxSpeed) {
            vx *= maxSpeed / sp;
            vy *= maxSpeed / sp;
        }
        if (b.BossDying()) {
            tx = 0.0;
            ty = 200.0;
            return;
        }
        if (!charging) {
            tx = mirror ? 220.0 : -220.0;
            ty = 160.0;
            if (sp < 0.1) {
                if (delay == 0) {
                    charging = true;
                    ty = 220.0;
                    bombCycle = bombOn + bombOff;
                    tx = mirror ? 440.0 : -440.0;
                } else {
                    --delay;
                }
            }
        } else {
            tx += mirror ? -3.0 : 3.0;
            if (x < -520.0 || x > 520.0) {
                charging = false;
                mirror = !mirror;
                delay = 500;
            }
        }
        if (x > -300.0 && x < 300.0 && b.NukeFlash() == 0.0 && !charging) {
            static const int kMuzzle[2] = { 76, 104 };
            for (int i = 0; i < 2; ++i) {
                if (b.TankDead()) launch[i] = launchDelay[i];
                else --launch[i];
                if (launch[i] < 1) {
                    double mx = x + kMuzzle[i] - (mirror ? 2 * kMuzzle[i] : 0);
                    b.AddHazard(std::make_unique<HeadMissile>(b, mx, y - 40.0, mirror));
                    launch[i] = launchDelay[i];
                }
            }
        }
        if (charging && b.NukeFlash() == 0.0) {
            if (bombCycle == 0) bombCycle = bombOn + bombOff;
            else --bombCycle;
            if (bombCycle < bombOn && b.App().tick % bombFreq == 0)
                DropIronBomb(b, (int)((double)(Rand() % 40) + x - 20.0), (int)(y + 135.0), 0.0, 0.0, mirror);
        }
    }
    void Draw() override {
        if (b.Progress() >= b.Length()) Craft::Draw();
    }
    void Die() override {
        if (b.BossDying()) return;
        b.CountCraft(0, 1, 0);
        b.AddScore(points, (int)x, (int)y);
        b.StartBossDeath();
    }
};

// ---------------------------------------------------------------------------------
// Mechworm (0x445800): a segmented worm that leaps out of the sand under the tank,
// throwing up boulders. Three turrets on its back drop armoured bombs; once they are
// gone it jumps faster and fires homing missiles.
// ---------------------------------------------------------------------------------

struct Mechworm;

// Back turret (0x445b90): rides on segment 9, 19 or 29; the worm draws it.
struct WormTurret : BossPart {
    Mechworm& worm;
    int index;
    WormTurret(Board& bd, Mechworm& owner, Craft& asCraft, int idx, double hp_, Texture* image, int fire)
        : BossPart(bd, asCraft, image, hp_, 0.0, 0.0, fire), worm(owner), index(idx) {
        hittable = true;
    }
    void Update() override;
    void Draw() override {}
    void Die() override;
};

struct Mechworm : Craft {
    static constexpr int kSegs = 30;
    Texture *rib, *turretImg, *sand;
    double sx[kSegs], sy[kSegs];    // +0x88 / +0x178, segment 0 is the head
    WormTurret* turrets[3] = {};    // +0x7c
    BossLevelDef::Jump normal, fast, cur;
    bool fastMode = false;          // +0x2e4
    int fireTimer, fireDelay;       // +0x2c8 +0x2a8
    int turretFire = 60;            // +0x268
    int boulders;                   // +0x2ac
    int sandAlpha = 0, sandDelta = 0, sandX = 0;   // +0x2dc +0x2e0 +0x2d8
    Mechworm(Board& bd) : Craft(bd, 0, "pupcopter") {
        img = Grid("Images/worm/head", 10);
        rib = Grid("Images/worm/rib", 4);
        turretImg = Grid("Images/worm/turret", 1);
        sand = Grid("Images/worm/sand", 4);
        const BossLevelDef* st = Stats(b, "Worm");
        hp = maxHp = st ? st->armor : 2000;
        points = st ? st->score : 50000;
        fireDelay = fireTimer = st ? st->fireInterval : 40;
        boulders = st ? st->boulders : 8;
        if (st) {
            normal = st->wormJump;
            fast = st->wormJumpFast;
            if (!st->turrets.empty()) turretFire = st->turrets[0].fireInterval;
        }
        cur = normal;
        int turretHp = (st && !st->turrets.empty()) ? st->turrets[0].armor : 500;
        boss = true;
        persistent = true;
        hittable = true;
        for (int i = 0; i < 3; ++i) {
            auto t = std::make_unique<WormTurret>(b, *this, *this, i, turretHp, turretImg, turretFire);
            turrets[i] = t.get();
            b.AddPart(std::move(t));
        }
        Jump(true);
    }
    // 0x4441b0: dive back in and line up the next leap.
    void Jump(bool first) {
        double nx;
        if (fastMode) nx = b.TankX();
        else if (first) nx = -200.0;
        else nx = (double)((Rand() & 1) * 2 - 1) * ((double)(Rand() % 160) + 100.0);
        if (b.TankX() < -300.0) nx = 200.0;
        for (int i = 0; i < kSegs; ++i) {
            sx[i] = nx;
            sy[i] = cur.depth;
        }
        x = nx;
        y = cur.depth;
        vy = -cur.jump;
    }
    void UpdateF() override {}
    void Update() override {
        if (b.Progress() < b.Length()) return;
        Craft::Update();
        bool anyTurret = turrets[0] || turrets[1] || turrets[2];
        if (anyTurret) {
            cur = normal;
            fastMode = false;
        } else {
            cur = fast;
            fastMode = true;
            if (b.TankDead()) fireTimer = fireDelay;
            else --fireTimer;
            if (y < 280.0 && fireTimer < 1 && b.NukeFlash() == 0.0 && !b.BossDying()) {
                FireMissile(b, (int)x, (int)y, vx, vy, 1.0);
                fireTimer = fireDelay;
            }
        }
        if (!b.BossDying()) {
            frame += 0.3;
            if (frame >= 10.0) frame -= 10.0;
            sy[0] += vy;
            if (sy[0] >= 520.0) {
                // Underground: steer toward the tank.
                vx = std::clamp((b.TankX() - sx[0]) * 0.01, -cur.lateral, cur.lateral);
            } else {
                sx[0] += vx;
                vy = std::min(vy + cur.gravity, cur.jump);
            }
        } else {
            if (b.App().tick % 50 == 0) {
                vx = (double)(Rand() % 200) * 0.01 - 1.0 - sx[0] * 0.0015625;
                vy = ((double)(Rand() % 200) * 0.01 - 1.0) - (sy[0] - 240.0) * 0.0020833333333333333;
            }
            sx[0] += vx;
            sy[0] += vy;
        }
        x = sx[0];
        y = sy[0];
        hittable = sy[0] < 480.0;
        if (sy[kSegs - 1] > 795.0 && sy[0] > cur.depth) Jump(false);
        for (int i = 1; i < kSegs; ++i) {
            sx[i] -= (sx[i] - sx[i - 1]) * 0.125;
            sy[i] -= (sy[i] - sy[i - 1]) * 0.125;
        }

        // Sand plume while the body crosses the surface.
        double tail = sy[kSegs - 1];
        if ((sy[0] < 460.0 && tail >= 460.0) || (sy[0] > 460.0 && tail <= 460.0) || (sy[0] > 500.0 && vy < 0.0)) {
            if (sandDelta != 8) {
                sandX = (int)sx[0];
                sandDelta = 8;
                AudioSystem::PlaySoundId(SND_EARTHQUAKE, b.Pan(sx[0]));
            }
        } else {
            sandDelta = -2;
        }
        if (sy[0] > 460.0 && sy[0] + vy < 460.0) {
            for (int i = 0; i < boulders; ++i) {
                double bvy = (double)(Rand() % 200) * 0.01 - 5.0;
                double bvx = ((double)(Rand() % 400) * 0.01 + vx) - 2.0;
                double bx = ((double)(Rand() % 80) + x) - 40.0;
                b.AddHazard(std::make_unique<Boulder>(b, bx, 460.0, bvx, bvy, false));
            }
        }
        sandAlpha += sandDelta;
        if (sandAlpha >= 256) {
            sandAlpha = 255;
            if (b.App().tick % 5 == 0)
                b.SpawnParticles(TextureManager::Get("sanddust"), (double)(Rand() % 100 - 50 + sandX), 500.0, 0.0, -1.0, 1, 0.5, -0.01, -1.0, 120, true, -1);
        } else if (sandAlpha < 0) {
            sandAlpha = 0;
        }

        // Every segment is deadly. (The original's head test passes the wrong
        // coordinates and never hits; segment 0 covers the head anyway.)
        if (!b.BossDying() && !b.TankDead()) {
            for (int i = 0; i < kSegs; ++i) {
                if (b.HitsTank(rib, (int)sx[i], (int)sy[i], 0, 0, false)) {
                    b.KillTank();
                    break;
                }
            }
        }
        if (b.RespawnTimer() == 2 && (y < 460.0 || tail < 460.0)) b.SetRespawnTimer(3);
    }
    void TurretLost(int i) { turrets[i] = nullptr; }
    void Draw() override {
        if (b.Progress() < b.Length()) return;
        for (int i = kSegs - 1; i >= 0; --i) Gfx::DrawSprite(rib, (int)sx[i], (int)sy[i], true, i % 4);
        for (WormTurret* t : turrets) {
            if (!t) continue;
            if (t->flash != 0) {
                Gfx::SetColorizeImages(true);
                Gfx::SetColor(255, 255 - t->flash, 255 - t->flash);
            }
            Gfx::DrawSprite(turretImg, (int)t->x, (int)t->y, true, (int)t->frame);
            Gfx::SetColorizeImages(false);
        }
        Craft::Draw();
        if (sandAlpha != 0) {
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(255, 255, 255, sandAlpha);
            Gfx::DrawSprite(sand, sandX, 460, true, (b.App().tick / 8) % 4);
            Gfx::SetColorizeImages(false);
        }
    }
    void Die() override {
        if (b.BossDying()) return;
        b.CountCraft(0, 1, 0);
        b.AddScore(points, (int)x, (int)y);
        b.StartBossDeath();
    }
};

void WormTurret::Update() {
    if (b.Progress() < b.Length()) return;
    Craft::Update();
    int seg = 9 + index * 10;
    vx = worm.sx[seg] - x;
    vy = worm.sy[seg] - y;
    x = worm.sx[seg];
    y = worm.sy[seg];
    if (y >= 460.0) return;
    timer = b.TankDead() ? worm.turretFire : timer - 1;
    if (timer > 0 || y >= 280.0 || b.NukeFlash() != 0.0 || vy >= 1.0 || b.BossDying()) return;
    timer = worm.turretFire;
    double bvx = std::clamp((b.TankX() - x) * 0.0125, -3.0, 3.0) + (double)(Rand() % 200) * 0.01 - 1.0;
    DropIronBomb(b, (int)x, (int)y, bvx, vy, bvx < 0.0);
}

void WormTurret::Die() {
    BossPart::Die();
    worm.TurretLost(index);
}

// ---------------------------------------------------------------------------------
// X-Bot (0x443210): a giant robot that hops toward the tank and crushes it, with two
// shoulder launchers and two armoured arms. Once both arms are gone its eye fires
// twin laser beams.
// ---------------------------------------------------------------------------------

// Eye laser (0x4438f0): a tapered beam that streaks from the eye to the ground.
struct EyeBeam : Hazard {
    double tx, ty;          // +0x78 +0x80 tail
    double angle;           // +0x98
    bool landed = false;    // +0xa0
    EyeBeam(Board& bd, double x_, double y_) : Hazard(bd, "rock", x_, y_, 0, 0, false), tx(x_), ty(y_) {
        active = false;
        angle = std::atan2(x_ - b.TankX(), (double)b.TankY() - y_) + 1.57;
        vx = std::cos(angle) * 4.0;
        vy = std::sin(angle) * 4.0;
        AudioSystem::PlaySoundId(SND_ROBOTLASER, b.Pan(x));
    }
    void Update() override {
        double len = std::sqrt((x - tx) * (x - tx) + (y - ty) * (y - ty));
        bool tail = true;
        if (y >= 460.0) {
            landed = true;  // (the original also leaves a scorch mark here; not ported)
        } else {
            x += vx;
            y += vy;
            tail = len > 50.0;
        }
        if (tail) {
            tx += vx;
            ty += vy;
        }
        if (tx < -320.0 || tx > 320.0 || ty > 460.0) { Remove(); return; }
        if (!b.TankDead() && b.HitsTank(img, (int)x, (int)y, 0, 0, false)) {
            b.KillTank();
            Remove();
            return;
        }
        BaseUpdate();
    }
    void Draw() override {
        Gfx::SetDrawMode(1);
        double a = angle + 1.57, c = std::cos(a), sn = std::sin(a);
        for (int w = 2; w > 0; --w) {
            double hw = 4 * w + 1, tw = w + 1;
            int xs[4] = { (int)(x + hw * c), (int)(x - hw * c), (int)(tx - tw * c), (int)(tx + tw * c) };
            int ys[4] = { (int)(y + hw * sn), (int)(y - hw * sn), (int)(ty - tw * sn), (int)(ty + tw * sn) };
            if (w == 2) Gfx::SetColor(0x80, 0x20, 0x40);
            else Gfx::SetColor(255, 255, 255);
            Gfx::FillPolygon(xs, ys);
        }
        Gfx::SetDrawMode(0);
    }
};

struct Robot;

// Arm (0x443720) or shoulder launcher (0x443d00); both ride at fixed offsets.
struct RobotPart : BossPart {
    Robot& robot;
    int index;
    bool launcher;
    RobotPart(Board& bd, Robot& owner, Craft& asCraft, int idx, bool isLauncher, double hp_, Texture* image, int fire)
        : BossPart(bd, asCraft, image, hp_, idx == 0 ? (isLauncher ? -52.0 : -59.0) : (isLauncher ? 52.0 : 59.0),
                   isLauncher ? -26.0 : 60.0, fire),
          robot(owner), index(idx), launcher(isLauncher) {
        frame = idx;
        hittable = true;
    }
    void Update() override {
        if (!Follow()) return;
        hittable = true;
        if (!launcher) return;
        timer = b.TankDead() ? fireDelay : timer - 1;
        if (x > -300.0 && x < 300.0 && timer < 1 && b.NukeFlash() == 0.0) {
            FireMissile(b, (int)x, (int)y, vx, vy);
            timer = fireDelay;
        }
    }
    void Die() override;
};

struct Robot : Craft {
    Texture *waist, *foot, *panel, *eyeGlow;
    RobotPart* arms[2] = {};        // +0x88
    double footY = 0.0, prevFoot = 0.0;    // +0x98 +0xa0
    double panelFrame = 0.0;        // +0xa8
    int eyeTimer, eyeDelay;         // +0xb0 +0xb4
    double downAccel, downMax, upAccel, upMax;
    int shake = 0;                  // +0xf0
    Robot(Board& bd) : Craft(bd, 0, "pupcopter") {
        img = Grid("Images/robot/body", 1);
        waist = Grid("Images/robot/waist", 1);
        foot = Grid("Images/robot/foot", 1);
        Texture* armImg = Grid("Images/robot/arms", 2);
        Texture* launcherImg = Grid("Images/robot/launchers", 2);
        panel = Grid("Images/robot/panel", 4);
        eyeGlow = Grid("Images/robot/eyeglow", 1);
        const BossLevelDef* st = Stats(b, "Robot");
        hp = maxHp = st ? st->armor : 2200;
        points = st ? st->score : 70000;
        eyeDelay = eyeTimer = st ? st->fireInterval : 300;
        downAccel = st ? st->downAccel : 0.04;
        downMax = st ? st->downMax : 3.0;
        upAccel = st ? st->upAccel : 0.2;
        upMax = st ? st->upMax : 3.0;
        int armHp = st ? st->armArmor : 750;
        int lHp = (st && !st->launchers.empty()) ? st->launchers[0].armor : 700;
        int lFire = (st && !st->launchers.empty()) ? st->launchers[0].fireInterval : 200;
        boss = true;
        persistent = true;
        hittable = true;
        x = 0.0;
        y = -240.0;
        vx = vy = 0.0;
        for (int i = 0; i < 2; ++i) {
            auto a = std::make_unique<RobotPart>(b, *this, *this, i, false, armHp, armImg, 0);
            arms[i] = a.get();
            b.AddPart(std::move(a));
        }
        for (int i = 0; i < 2; ++i) b.AddPart(std::make_unique<RobotPart>(b, *this, *this, i, true, lHp, launcherImg, lFire));
    }
    bool Armless() const { return !arms[0] && !arms[1]; }
    void UpdateF() override {}
    void Update() override {
        if (b.Progress() < b.Length()) return;
        Craft::Update();
        panelFrame += 0.1;
        if (panelFrame >= 4.0) panelFrame -= 4.0;
        if (y >= 210.0) {
            if (!b.BossDying()) vy = std::max(vy - upAccel, -upMax);   // spring up
        } else {
            vy = std::min(vy + downAccel, downMax);                    // fall
        }
        // The foot stays planted until the body lifts it; landing shakes the ground.
        footY = std::max(footY, y + 208.0);
        if (footY >= 410.0) footY = 410.0;
        if (footY == 410.0 && prevFoot < 410.0) {
            AudioSystem::PlaySoundId(SND_ROBOTSMASH, b.Pan(x));
            shake = 0x80;
        }
        footY = std::min(footY, y + 208.0);
        if (prevFoot == 410.0 && footY < 410.0) {
            // Lift-off: hop toward the tank.
            double target;
            if (b.TankDead() || b.TankX() < -300.0) target = 240.0;
            else target = std::clamp(b.TankX(), -240.0, 240.0);
            vx = std::clamp((target - x) * 0.02 * ((double)(Rand() % 1000) * 0.0005 + 0.5), -1.5, 1.5);
        }
        prevFoot = footY;
        if (!b.BossDying() && !b.TankDead() && b.HitsTank(foot, (int)x, (int)footY, 0, 0, false)) b.KillTank();
        if (footY < 410.0) x += vx;
        if (y < 260.0) y += vy;

        if (!b.TankDead() && Armless()) --eyeTimer;
        else eyeTimer = eyeDelay;
        if (x > -300.0 && x < 300.0 && eyeTimer < 1 && b.NukeFlash() == 0.0 && !b.BossDying()) {
            b.AddHazard(std::make_unique<EyeBeam>(b, x - 10.0, y - 55.0));
            b.AddHazard(std::make_unique<EyeBeam>(b, x + 10.0, y - 55.0));
            eyeTimer = eyeDelay;
        }
        if (shake != 0) {
            b.Shake(shake);
            shake -= 2;
        }
    }
    void ArmLost(int i) { arms[i] = nullptr; }
    void Draw() override {
        if (b.Progress() < b.Length()) return;
        Craft::Draw();
        Gfx::DrawSprite(panel, (int)x, (int)(y + 37.0), true, (int)panelFrame);
        if (flash != 0) {
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(255, 255 - flash, 255 - flash);
        }
        Gfx::DrawSprite(waist, (int)x, (int)(y + 126.0), true);
        Gfx::DrawSprite(foot, (int)x, (int)footY, true);
        if (eyeTimer > 0 && Armless()) {
            int g = (int)(255.0 - (255.0 / eyeDelay) * eyeTimer);
            Gfx::SetDrawMode(1);
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(g, g, g);
            Gfx::DrawSprite(eyeGlow, (int)x, (int)(y - 55.0), true);
            Gfx::SetDrawMode(0);
        }
        Gfx::SetColorizeImages(false);
    }
    void Die() override {
        if (b.BossDying()) return;
        b.CountCraft(0, 1, 0);
        b.AddScore(points, (int)x, (int)y);
        b.StartBossDeath();
    }
};

void RobotPart::Die() {
    BossPart::Die();
    if (!launcher) robot.ArmLost(index);
}

// ---------------------------------------------------------------------------------
// Secret Weapon (0x43de30), the final boss at mission 18. Three stages, swapped at 66%
// and 33% armour: wing lasers raining beams, then a spinning shrapnel turret with Fat
// Boys, then homing and head missiles.
// ---------------------------------------------------------------------------------

// Wing laser beam (0x44b950): a 40-point trail that falls straight, diagonally or in a
// zigzag; every point of it is deadly.
struct BossLaserBeam : Hazard {
    int pattern, dir;
    double swing;           // +0x70
    bool landed = false;    // +0x7c
    std::vector<int> tx, ty;
    BossLaserBeam(Board& bd, double x_, double y_, int pattern_, int dir_)
        : Hazard(bd, "bombfrag", x_, y_, 0, 0, false), pattern(pattern_), dir(dir_), swing(dir_ * 0.1) {
        active = false;
        points = 500;
    }
    void Update() override {
        if (pattern == 0) {
            y += 3.5;
        } else if (pattern == 1) {
            x += dir * 3;
            y += 3.0;
        } else {
            x += vx;
            vx += swing;
            if (vx < -5.0) swing = 0.075;
            if (vx > 5.0) swing = -0.075;
            y += 2.0;
        }
        if (y > 460.0) landed = true;   // (the original also leaves a scorch mark; not ported)
        if (!ty.empty() && ty.front() >= 461) { Remove(); return; }
        tx.push_back((int)x);
        ty.push_back((int)y);
        if (tx.size() > 40) {
            tx.erase(tx.begin());
            ty.erase(ty.begin());
        }
        if (!b.TankDead()) {
            for (size_t i = 0; i < tx.size(); ++i) {
                if (b.HitsTank(img, tx[i], ty[i], 0, 0, false)) {
                    b.KillTank();
                    break;
                }
            }
        }
    }
    void Draw() override {
        Gfx::SetDrawMode(1);
        for (size_t i = 0; i < tx.size(); ++i)
            if (ty[i] < 460) Gfx::DrawSprite(img, tx[i], ty[i], true);
        Gfx::SetDrawMode(0);
    }
};

struct FinalBoss : Craft {
    Texture *body[3], *turret, *laser, *laserGlow;
    int stage = 0;                  // +0x88
    int timer = 80;                 // +0x8c
    double tx = 0.0, ty = 160.0;    // +0x90 +0x98
    int smallTimer, bigTimer, bombTimer;    // +0xa0 +0xa4 +0xa8
    int smallDelay, bigDelay, bombDelay, turretDelay;
    double laserPhase = 0.0;        // +0xb8
    int lastPattern = 2;            // +0xcc
    double anim = 0.0;              // +0xd0 laser frame / turret angle
    double prevHp;                  // +0xd8
    FinalBoss(Board& bd) : Craft(bd, 0, "pupcopter") {
        body[0] = Grid("Images/finalboss/body1", 1);
        body[1] = Grid("Images/finalboss/body2", 1);
        body[2] = Grid("Images/finalboss/body3", 1);
        turret = Grid("Images/finalboss/turret", 16);
        laser = Grid("Images/finalboss/laser", 5);
        laserGlow = Grid("Images/finalboss/laserglow", 5);
        img = body[0];
        const BossLevelDef* st = Stats(b, "Final");
        hp = maxHp = prevHp = st ? st->armor : 15000;
        points = st ? st->score : 140000;
        smallDelay = smallTimer = st ? st->smallLauncherFire : 120;
        bigDelay = bigTimer = st ? st->bigLauncherFire : 160;
        bombDelay = bombTimer = st ? st->bombFire : 200;
        turretDelay = (st && !st->turrets.empty()) ? st->turrets[0].fireInterval : 60;
        boss = true;
        persistent = true;
        hittable = false;
        x = 0.0;
        y = -140.0;
        vx = 0.0;
        vy = 0.5;
    }
    void Flames(double ox, double oy, double dvx) {
        b.SpawnParticles(TextureManager::Get("meteorite"), x + ox, y + oy, vx + dvx, vy + 2.0, 1, 0.5, 0, 0, 20, true, -1);
    }
    // 0x43c510: blow off the outer hull.
    void StageBlast() {
        b.SpawnExplosion(x, y, (int)((img ? img->GetCelWidth() : 300) * 1.5), (int)((img ? img->GetCelHeight() : 200) * 1.5), 0.0, 0.0, 0.02);
        b.Shake(0x80);
        for (int i = 0; i < 6; ++i)
            b.SpawnParticles(TextureManager::Get("bigdebris"), x, y, 0.0, 0.0, 2, 2.0, 0.02, 4.0, 300, false, i);
    }
    void UpdateF() override {}
    void Update() override {
        if (b.Progress() < b.Length()) return;
        bool live = !b.TankDead() && hittable;
        if (!hittable) {
            // Descend into view first.
            if (y > 150.0) hittable = true;
        } else if (stage == 0) {
            if (std::abs(x - tx) < 10.0 && std::abs(y - ty) < 10.0) {
                if (!b.TankDead()) --timer;
                else timer = 80;
                if (timer < 80 && b.NukeFlash() == 0.0) {
                    if (laserPhase == 0.0) AudioSystem::PlaySoundId(SND_BOSSLASER, b.Pan(x));
                    if (laserPhase > 1.5 && laserPhase < 1.54) {
                        int p;
                        do p = Rand() % 3; while (p == lastPattern);
                        lastPattern = p;
                        b.AddHazard(std::make_unique<BossLaserBeam>(b, x - 156.0, y + 10.0, p, 1));
                        b.AddHazard(std::make_unique<BossLaserBeam>(b, x + 157.0, y + 10.0, p, -1));
                        AudioSystem::PlaySoundId(SND_BOSSLASERBLAST, b.Pan(x));
                    }
                }
                if ((timer < 80 || (laserPhase > 0.0 && b.TankDead())) &&
                    (b.NukeFlash() == 0.0 || laserPhase != 0.0) && laserPhase < 3.141592653589793) {
                    anim += std::sin(laserPhase) * 0.5;
                    if (anim >= 5.0) anim -= 5.0;
                    laserPhase = std::min(laserPhase + 0.04, 3.141592653589793);
                }
                if (timer == 0) {
                    do {
                        tx = (double)(Rand() % 200) - 100.0;
                        ty = (double)(Rand() % 40) + 140.0;
                    } while (std::sqrt((ty - y) * (ty - y) + (tx - x) * (tx - x)) < 60.0);
                    laserPhase = 0.0;
                    timer = 80;
                }
            }
            double dx = (tx - x) * 0.2, dy = (ty - y) * 0.2;
            if (vx < dx) vx += 0.1;
            if (dx < vx) vx -= 0.1;
            if (vy < dy) vy += 0.03;
            if (dy < vy) vy -= 0.03;
            if (std::abs(dx) < std::abs(vx)) vx = dx;
            if (std::abs(dy) < std::abs(vy)) vy = dy;
        } else {
            if (b.App().tick % 50 == 0) {
                tx = (double)(Rand() % 400) - 200.0;
                ty = (double)((stage + 16) * 10) + ((double)(Rand() % 40) - 20.0);
            }
            if (x < tx && vx < 2.0) vx += 0.04;
            if (tx < x && vx > -2.0) vx -= 0.04;
            if (y < ty && vy < 0.5) vy += 0.01;
            if (ty < y && vy > -0.5) vy -= 0.01;
        }

        if (prevHp > maxHp * 0.66 && hp <= maxHp * 0.66) {
            StageBlast();
            stage = 1;
            img = body[1];
            anim = 0.0;
            timer = turretDelay;
        } else if (prevHp > maxHp * 0.33 && hp <= maxHp * 0.33) {
            StageBlast();
            stage = 2;
            img = body[2];
        }
        prevHp = hp;
        x += vx;
        y += vy;
        bool even = (b.App().tick & 1) == 0;
        bool canFire = b.NukeFlash() == 0.0 && !b.BossDying();
        live = !b.TankDead() && hittable;
        if (stage == 0) {
            if (even) {
                static const double kJets[6][2] = { { 92, 105 }, { -91, 105 }, { 71, 109 }, { -70, 109 }, { 42, 110 }, { -41, 110 } };
                for (auto& j : kJets) Flames(j[0], j[1], 0.0);
            }
        } else if (stage == 1) {
            anim += 0.2;
            if (anim >= 16.0) anim -= 16.0;
            if (even) {
                Flames(98.0, 20.0, 0.0);
                Flames(-97.0, 20.0, 0.0);
            }
            if (live) {
                --timer;
                --bombTimer;
            } else {
                timer = turretDelay;
                bombTimer = bombDelay;
            }
            if (canFire) {
                if (bombTimer < 1) {
                    DropFatBoy(b, (int)x, (int)(y + 100.0), vx, vy, false, true);
                    bombTimer = bombDelay;
                }
                if (timer < 1) {
                    for (double k = 0.0; k <= 3.141; k += 3.141) {
                        double a = anim * 0.19630938358853553 + k;
                        Board::Bullet s;
                        s.x = std::cos(a) * 45.0 + x;
                        s.y = (y - 84.0) - std::sin(a) * 45.0;
                        s.vx = std::cos(a) * 2.5;
                        s.vy = std::sin(a) * -2.5;
                        s.gravity = 0.1;
                        s.enemy = true;
                        s.img = TextureManager::Get("bombfrag");
                        b.Bullets().push_back(s);
                        b.AddMuzzleFlash(s.x, s.y, vx, vy);
                    }
                    timer = turretDelay;
                }
            }
        } else {
            if (even) {
                Flames(75.0, 64.0, 0.5);
                Flames(-74.0, 64.0, -0.5);
            }
            if (live) {
                --smallTimer;
                --bigTimer;
            } else {
                smallTimer = smallDelay;
                bigTimer = bigDelay;
            }
            if (canFire && smallTimer < 1) {
                FireMissile(b, (int)(x - 84.0), (int)(y + 3.0), vx, vy, 1.0);
                FireMissile(b, (int)(x + 85.0), (int)(y + 3.0), vx, vy, 1.0);
                smallTimer = smallDelay;
            }
            if (canFire && bigTimer < 1) {
                b.AddHazard(std::make_unique<HeadMissile>(b, x - 52.0, y - 52.0, true));
                b.AddHazard(std::make_unique<HeadMissile>(b, x + 53.0, y - 52.0, false));
                bigTimer = bigDelay;
            }
        }
        Craft::Update();
    }
    void Draw() override {
        if (b.Progress() < b.Length()) return;
        Craft::Draw();
        if (flash != 0) {
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(255, 255 - flash, 255 - flash);
        }
        if (stage == 0) {
            Gfx::DrawSprite(laser, (int)x - 156, (int)y - 15, true, (int)anim, 0, false);
            Gfx::DrawSprite(laser, (int)x + 157, (int)y - 15, true, (int)anim, 0, true);
            if (laserPhase > 0.0) {
                int g = (int)(std::sin(laserPhase) * 255.0);
                Gfx::SetColorizeImages(true);
                Gfx::SetDrawMode(1);
                Gfx::SetColor(g, g, g);
                Gfx::DrawSprite(laserGlow, (int)x - 156, (int)y - 15, true, (int)anim, 0, false);
                Gfx::DrawSprite(laserGlow, (int)x + 157, (int)y - 15, true, (int)anim, 0, true);
                Gfx::SetDrawMode(0);
            }
        } else if (stage == 1) {
            Gfx::DrawSprite(turret, (int)x, (int)y - 84, true, (int)anim);
        }
        Gfx::SetColorizeImages(false);
    }
    void Die() override {
        if (b.BossDying()) return;
        b.CountCraft(0, 1, 0);
        b.AddScore(points, (int)x, (int)y);
        b.StartBossDeath();
    }
};

} // namespace

const char* BossSpriteFolder(int mission) {
    static const char* kFolders[9] = { "hugecopter", "battleship", "Rainer", "wrecker", "ape",
                                       "Eye", "Head", "Worm", "robot" };
    if (mission == 18) return "FinalBoss";
    return kFolders[((mission % 9) + 9) % 9];
}

std::unique_ptr<Craft> CreateBoss(Board& b, int mission) {
    switch (mission % 9) {
        case 0:
            if (mission == 18) return std::make_unique<FinalBoss>(b);
            return std::make_unique<BossCopter>(b);
        case 1: return std::make_unique<Battleship>(b);
        case 2: return std::make_unique<Rainer>(b);
        case 3: return std::make_unique<Wrecker>(b);
        case 4: return std::make_unique<Ape>(b);
        case 5: return std::make_unique<Eyebot>(b);
        case 6: return std::make_unique<Head>(b);
        case 7: return std::make_unique<Mechworm>(b);
        case 8: return std::make_unique<Robot>(b);
        default: return nullptr;
    }
}

} // namespace HeavyWeapon
