// Enemy craft, translated from the original craft classes (see game/Enemies.h).
// Each class notes its constructor address; behaviour constants are the original's.

#include "game/Enemies.h"
#include "game/Board.h"
#include "game/Gfx.h"
#include "AudioSystem.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace HeavyWeapon {

static int Rand() { return std::rand() & 0x7fffffff; }
static Texture* Img(const char* name) { return TextureManager::Get(name); }

// -----------------------------------------------------------------------------
// Craft base (0x431550)
// -----------------------------------------------------------------------------

Craft::Craft(Board& board, int t, const char* image) : b(board), type(t) {
    img = Img(image);
    b.CountCraft(1, 0, 0);
    const CraftDef& def = b.CraftStats(type);
    hp = maxHp = def.armor;
    points = def.points;
}

void Craft::Update() {
    flash -= 0x18;
    if (flash < 0) flash = 0;
}

void Craft::UpdateF() {
    x += vx;
}

void Craft::Draw() {
    if (flash != 0) {
        Gfx::SetColorizeImages(true);
        Gfx::SetColor(255, 255 - flash, 255 - flash);
    }
    Gfx::DrawSprite(img, (int)x, (int)y, true, (int)frame, 0, mirror);
    Gfx::SetColorizeImages(false);
}

// 0x431330 + 0x431480 + AddScore: the common "destroyed" sequence.
void Craft::Die() {
    b.CountCraft(0, 1, friendly ? 1 : 0);
    // 1 in 20 kills drops a supply crate (crates arrive with the power-up port).
    if (Rand() % 20 == 0 && b.CanDropCrate() && y <= 280.0 && x >= -320.0 && x < 320.0) {
        b.DropCrate((int)x, (int)y);
    }
    Explode();
    b.AddScore(points, (int)x, (int)y);
}

void Craft::Explode() {
    int size = (int)((std::max(W(), H()) + 20) * 1.25);
    b.SpawnExplosion(x, y, size, size, vx, vy);
}

// 0x431650: random side, unless friendly units are on the board.
void Craft::PickSide() {
    if (b.FriendlyCount() == 0) mirror = (Rand() & 1) != 0;
}

void Craft::EnterFromSide(int yRange, double baseSpeed, int spread) {
    int h = H();
    y = (double)(Rand() % std::max(1, yRange - h) + 50 + h / 2);
    PickSide();
    double speed = (double)(Rand() % spread) * 0.01 + baseSpeed;
    if (!mirror) {
        x = (double)(-320 - W() / 2);
        vx = speed;
    } else {
        x = (double)(W() / 2 + 320);
        vx = -speed;
    }
}

// Bomb release point: below the craft's centre.
static int BombY(const Craft& c) { return (int)(c.H() / 2 + c.y); }

// -----------------------------------------------------------------------------
// Simple bombers / fighters
// -----------------------------------------------------------------------------

namespace {

// PROPFIGHTER (0x434430): flies across, no weapons.
struct PropFighter : Craft {
    PropFighter(Board& bd) : Craft(bd, CRAFT_PROPFIGHTER, "propfighter") { EnterFromSide(230, 1.0, 200); }
    void Update() override {
        Craft::Update();
        frame += 0.4;
        if (frame >= 4.0) frame -= 4.0;
        if (OffScreen()) Remove();
    }
};

// Shared "drop something now and then while on screen" pattern.
struct Dropper : Craft {
    int chance, delay;
    bool needTankAlive;
    Dropper(Board& bd, int t, const char* im, int chance_, int delay_, bool needAlive)
        : Craft(bd, t, im), chance(chance_), delay(delay_), needTankAlive(needAlive) {}
    virtual void Drop() = 0;
    void Update() override {
        Craft::Update();
        if (cooldown > 0) --cooldown;
        bool alive = !needTankAlive || !b.TankDead();
        if (OnScreen300() && alive && (chance <= 1 || Rand() % chance == 0) && cooldown == 0 && !b.App().noWeapons) {
            Drop();
            cooldown = delay;
        }
        if (OffScreen()) Remove();
    }
};

// SMALLJET (0x434690): dumb bombs, 1/80 per tick, 200 tick cooldown.
struct SmallJet : Dropper {
    SmallJet(Board& bd) : Dropper(bd, CRAFT_SMALLJET, "smalljet", 80, 200, false) { EnterFromSide(230, 1.0, 200); }
    void Drop() override { DropDumbBomb(b, (int)x, BombY(*this), vx, vy, mirror); }
};

// BOMBER (0x4324f0): dumb bombs, 1/40, 100.
struct Bomber : Dropper {
    Bomber(Board& bd) : Dropper(bd, CRAFT_BOMBER, "bomber", 40, 100, true) { EnterFromSide(230, 1.0, 200); }
    void Drop() override { DropDumbBomb(b, (int)x, BombY(*this), vx, vy, mirror); }
};

// JETFIGHTER (0x433e90): laser-guided bombs, 1/40, 150.
struct JetFighter : Dropper {
    JetFighter(Board& bd) : Dropper(bd, CRAFT_JETFIGHTER, "jetfighter", 40, 150, true) { EnterFromSide(230, 2.0, 200); }
    void Drop() override { DropLaserGuidedBomb(b, (int)x, BombY(*this), vx, vy, mirror); }
};

// DELTABOMBER (0x432f50): frag bombs, 1/40, 100.
struct DeltaBomber : Dropper {
    DeltaBomber(Board& bd) : Dropper(bd, CRAFT_DELTABOMBER, "deltabomber", 40, 100, true) { EnterFromSide(230, 2.0, 200); }
    void Drop() override { DropFragBomb(b, (int)x, BombY(*this), vx, vy, mirror); }
};

// DELTAJET (0x433260): iron bombs, 1/40, 75.
struct DeltaJet : Dropper {
    DeltaJet(Board& bd) : Dropper(bd, CRAFT_DELTAJET, "deltajet", 40, 75, true) { EnterFromSide(230, 2.0, 200); }
    void Drop() override { DropIronBomb(b, (int)x, BombY(*this), vx, vy, mirror); }
};

// SUPERBOMBER (0x4353b0): an iron bomb every 50 ticks while on screen.
struct SuperBomber : Dropper {
    SuperBomber(Board& bd) : Dropper(bd, CRAFT_SUPERBOMBER, "superbomber", 1, 50, false) { EnterFromSide(230, 1.0, 150); }
    void Drop() override { DropIronBomb(b, (int)x, BombY(*this), vx, vy, mirror); }
};

// FATBOMBER (0x433b90): a fatboy every 150 ticks, flies in the upper half.
struct FatBomber : Dropper {
    FatBomber(Board& bd) : Dropper(bd, CRAFT_FATBOMBER, "fatbomber", 1, 150, false) {
        int h = H();
        y = (double)(Rand() % std::max(1, (230 - h) / 2) + 50 + h / 2);
        PickSide();
        double speed = (double)(Rand() % 100) * 0.01 + 1.0;
        x = mirror ? (double)(W() / 2 + 320) : (double)(-320 - W() / 2);
        vx = mirror ? -speed : speed;
    }
    void Drop() override { DropFatBoy(b, (int)x, BombY(*this), vx, 0.0, mirror); }
};

// BIGBOMBER (0x431730): bursts of 3-4 dumb bombs, 20 ticks apart.
struct BigBomber : Craft {
    int burst = 0, burstTimer = 0;
    BigBomber(Board& bd) : Craft(bd, CRAFT_BIGBOMBER, "bigbomber") { EnterFromSide(230, 1.0, 150); }
    void Update() override {
        Craft::Update();
        if (cooldown > 0) --cooldown;
        if (burstTimer > 0) --burstTimer;
        // The original tests vx (not x) against +-300 here, so the burst can start anywhere.
        if (vx > -300.0 && vx < 300.0 && !b.TankDead() && Rand() % 20 == 0 && cooldown == 0) {
            burstTimer = 0;
            burst = (Rand() & 1) + 3;
            cooldown = 200;
        }
        if (OnScreen300() && !b.TankDead() && burst != 0 && burstTimer == 0) {
            DropDumbBomb(b, (int)x, BombY(*this), vx, vy, mirror);
            burstTimer = 20;
            --burst;
        }
        if (OffScreen()) Remove();
    }
};

// BLIMP (0x4320c0): slow, four propellers, bombs from both ends, bursts into shrapnel.
struct Blimp : Craft {
    int cooldown2 = 0;
    double prop = 0;
    Blimp(Board& bd) : Craft(bd, CRAFT_BLIMP, "blimp") { EnterFromSide(230, 0.5, 100); }
    void Update() override {
        Craft::Update();
        if (cooldown > 0) --cooldown;
        if (cooldown2 > 0) --cooldown2;
        prop += 0.4;
        if (prop >= 4.0) prop -= 4.0;
        if (OnScreen300()) {
            int r = Rand();
            if (r % 40 == 0 && cooldown == 0) {
                DropDumbBomb(b, (int)(x + 60.0), BombY(*this), vx, vy, mirror);
                cooldown = 50;
            }
            if (r % 40 == 0 && cooldown2 == 0) {
                DropDumbBomb(b, (int)(x - 60.0), BombY(*this), vx, vy, mirror);
                cooldown2 = 50;
            }
        }
        if (OffScreen()) Remove();
    }
    void Draw() override {
        Craft::Draw();
        Texture* p = Img("blimpprop");
        int f = (int)prop;
        if (!mirror) {
            Gfx::DrawSprite(p, (int)(x + 28.0), (int)(y + 21.0), true, f);
            Gfx::DrawSprite(p, (int)(x - 88.0), (int)(y + 20.0), true, f);
        }
        Gfx::DrawSprite(p, (int)(x - 27.0), (int)(y + 21.0), true, f);
        Gfx::DrawSprite(p, (int)(x + 89.0), (int)(y + 20.0), true, f);
    }
    void Die() override {
        Craft::Die();
        // Shrapnel burst (0x435f50).
        for (int i = 0; i < 10; ++i) {
            double a = (double)(Rand() % 3141) * 0.001;
            double s = (double)(Rand() % 15) * 0.1 + 1.0;
            Board::Bullet bt;
            bt.x = 100.0 * std::cos(a) + x;
            bt.y = y - std::sin(a) * 30.0;
            bt.vx = std::cos(a) * s;
            bt.vy = -(std::sin(a) * s);
            bt.gravity = 0.05;
            bt.enemy = true;
            bt.img = Img("bombfrag");
            b.Bullets().push_back(bt);
        }
    }
};

// -----------------------------------------------------------------------------
// Helicopters
// -----------------------------------------------------------------------------

// SMALLCOPTER (0x434c80): crosses the screen firing aimed energy shots.
struct SmallCopter : Craft {
    int muzzle = 0;
    SmallCopter(Board& bd) : Craft(bd, CRAFT_SMALLCOPTER, "smallcopter") { EnterFromSide(180, 1.0, 150); }
    void Update() override {
        Craft::Update();
        if (cooldown > 0) --cooldown;
        frame += 0.4;
        if (frame >= 5.0) frame -= 5.0;
        if (muzzle != 0) --muzzle;
        if (OnScreen300() && Rand() % 20 == 0 && cooldown == 0 && !b.App().noWeapons) {
            double a = std::atan2(b.TankX() - x, (double)b.TankY() - y) + 1.5705;
            b.FireEnemyShot((int)x, (int)(H() / 2 + y), a, 0.0);
            cooldown = 150;
            muzzle = 5;
        }
        if (OffScreen()) Remove();
    }
    void Draw() override {
        Craft::Draw();
        if (muzzle != 0) {
            Gfx::SetDrawMode(1);
            Gfx::DrawSprite(Img("muzzleflash"), (int)x, (int)(H() / 2 + y), true, muzzle - 1);
            Gfx::SetDrawMode(0);
        }
    }
};

// MEDCOPTER (0x4341a0) / BIGCOPTER (0x431a70): hover over the middle, dodge shells and
// fire missiles. The frame is the tilt, and the tilt sets the horizontal speed.
struct HoverCopter : Craft {
    double rotor = 0;
    int maxTilt;          // 6 for the medium, 8 for the big copter
    double tiltStep;      // 0.15 / 0.1
    double climb;         // 0.02 / 0.05 when outside the 100..200 band
    int fireDelay;        // 125 / 75
    const char* rotorImg;
    HoverCopter(Board& bd, int t, const char* im, int maxT, double step, double cl, int delay, const char* rot)
        : Craft(bd, t, im), maxTilt(maxT), tiltStep(step), climb(cl), fireDelay(delay), rotorImg(rot) {
        int h = H();
        y = (double)(Rand() % std::max(1, 230 - h) + 50 + h / 2);
        frame = maxTilt / 2;
        x = (Rand() & 1) == 0 ? (double)(-320 - W() / 2) : (double)(W() / 2 + 320);
    }
    void UpdateF() override {
        x += (frame - maxTilt / 2) * (maxTilt == 6 ? 0.5 : (1.0 / 3.0));
        y += vy;
    }
    void Update() override {
        Craft::Update();
        Texture* rt = Img(rotorImg);
        rotor += 0.5;
        if (rt && rotor >= rt->cols) rotor -= rt->cols;
        if (cooldown > 0) --cooldown;

        if (!b.TankDead() && (!b.NearLevelEnd(700) || b.Survival())) {
            // Dodge player shells within 200 px.
            double sumX = 0.0, sumY = 0.0;
            int n = 0;
            for (const auto& s : b.Bullets()) {
                if (s.enemy) continue;
                if (std::sqrt(std::pow(s.x - x, 2.0) + std::pow(s.y - y, 2.0)) < 200.0) {
                    sumX += s.x;
                    sumY += s.y;
                    ++n;
                }
            }
            if (n > 0 && x > -160.0 && x < 160.0 && y > 100.0 && y < 200.0) {
                // As in the original, only the y sum is averaged.
                double avgY = sumY / n;
                if (x < sumX - 10.0) frame -= tiltStep;
                if (sumX + 10.0 < x) frame += tiltStep;
                if (y < avgY) vy -= 0.02;
                if (avgY < y) vy += 0.02;
            }
            if (x < -100.0) frame += tiltStep;
            if (x > 100.0) frame -= tiltStep;
        } else {
            // Leave towards the nearer edge.
            if (x < 0.0) frame -= tiltStep;
            else frame += tiltStep;
        }
        frame = std::clamp(frame, 0.0, (double)maxTilt);

        if (Rand() % 10 == 0) vy += (double)(Rand() % 3) * 0.05 - 0.05;
        if (y < 100.0) vy += climb;
        if (y > 200.0) vy -= climb;
        vy = std::clamp(vy, -0.5, 0.5);

        if (OnScreen300() && Rand() % 30 == 0 && cooldown == 0) {
            FireMissile(b, (int)x, BombY(*this), vx, vy);
            cooldown = fireDelay;
        }
        if (OffScreen()) Remove();
    }
    void Draw() override {
        if (flash != 0) {
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(255, 255 - flash, 255 - flash);
        }
        Gfx::DrawSprite(img, (int)x, (int)y, true, (int)frame);
        if (maxTilt == 6) {
            Gfx::DrawSprite(Img(rotorImg), (int)x, (int)(y - 22.0), true, (int)rotor);
        } else {
            Gfx::DrawSprite(Img(rotorImg), (int)(x - W() / 2), (int)(y - H() / 2), false, (int)rotor);
        }
        Gfx::SetColorizeImages(false);
    }
};

// -----------------------------------------------------------------------------
// Ground units
// -----------------------------------------------------------------------------

// Common ground behaviour of the truck and enemy tank: arrive from the side away from
// the player, brake on screen, leave when the tank dies or the level is ending.
struct GroundUnit : Craft {
    GroundUnit(Board& bd, int t, const char* im, double groundY) : Craft(bd, t, im) {
        y = groundY;
        if (b.TankX() < 0.0) {
            mirror = true;
            vx = -1.0;
            x = (double)(W() / 2 + 320);
            b.SetEdgeBlock(false, true);
        } else {
            mirror = false;
            vx = 1.0;
            x = (double)(-320 - W() / 2);
            b.SetEdgeBlock(true, false);
        }
    }
    void Drive() {
        if (!b.TankDead() && (!b.NearLevelEnd(700) || b.Survival())) {
            if (x > -320.0 && x < 320.0) vx *= 0.98;
        } else {
            vx += mirror ? 0.02 : -0.02;
            vx = std::clamp(vx, -1.0, 1.0);
        }
        frame += vx * (1.0 / 3.0) + 0.3;
        if (frame >= 10.0) frame -= 10.0;
        if (frame < 0.0) frame += 9.0;
    }
    void OnRemoved() override { b.SetEdgeBlock(false, false); }
    void Die() override {
        Craft::Die();
        b.SetEdgeBlock(false, false);
    }
};

// TRUCK (0x435650): fires RPGs.
struct Truck : GroundUnit {
    Truck(Board& bd) : GroundUnit(bd, CRAFT_TRUCK, "truck", 445.0) {}
    void Update() override {
        Craft::Update();
        Drive();
        if (cooldown > 0) --cooldown;
        if (cooldown == 0 && !b.TankDead() && x > -320.0 && x < 320.0 && Rand() % 20 == 0) {
            cooldown = 100;
            FireRpg(b, x, y - 25.0, mirror, (double)(Rand() % 100 + 50) * 0.001);
        }
        if (OffScreen()) {
            OnRemoved();
            Remove();
        }
    }
    void Draw() override {
        Gfx::DrawSprite(Img("enemytankshadow"), (int)x, 465, true);
        Craft::Draw();
    }
};

// ENEMYTANK (0x433960): sweeping gun lobbing shrapnel shells.
struct EnemyTank : GroundUnit {
    double gun = 7.0, gunSpeed = 0.03;
    EnemyTank(Board& bd) : GroundUnit(bd, CRAFT_ENEMYTANK, "enemytank", 440.0) { cooldown = 120; }
    void Update() override {
        Craft::Update();
        Drive();
        if (cooldown > 0) --cooldown;
        gun += gunSpeed;
        if (gun >= 8.0) { gunSpeed = -0.04; gun = 7.0; }
        if (gun < 0.0) { gunSpeed = 0.04; gun = 0.0; }
        if (cooldown == 0 && !b.TankDead() && x > -320.0 && x < 320.0) {
            double a = gun * 0.1571;
            if (mirror) a = 3.1416 - a;
            Board::Bullet bt;
            bt.x = std::cos(a) * 24.0 + (mirror ? -18 : 18) + x;
            bt.y = (y - 5.0) - std::sin(a) * 24.0;
            bt.vx = (gun * 0.5 + 2.0) * std::cos(a);
            bt.vy = -((gun * 0.5 + 2.0) * std::sin(a));
            bt.gravity = 0.05;
            bt.enemy = true;
            bt.img = Img("bombfrag");
            b.Bullets().push_back(bt);
            cooldown = 120;
            AudioSystem::PlaySoundId(SND_ENEMYTANKGUN, b.Pan(x));
            b.AddMuzzleFlash(bt.x, bt.y, vx, 0.0);
        }
        if (OffScreen()) {
            OnRemoved();
            Remove();
        }
    }
    void Draw() override {
        Gfx::DrawSprite(Img("enemytankshadow"), (int)x, 465, true);
        Gfx::DrawSprite(Img("enemygun"), (int)x, 423, true, (int)gun, mirror ? 1 : 0);
        Craft::Draw();
    }
};

// DOZER (0x433520): rams the tank; shooting it pushes it back.
struct Dozer : Craft {
    Dozer(Board& bd) : Craft(bd, CRAFT_DOZER, "dozer") {
        y = 425.0;
        if (b.TankX() < 0.0) {
            mirror = true;
            x = (double)(W() / 2 + 320);
        } else {
            mirror = false;
            x = (double)(-320 - W() / 2);
        }
        vx = 0.0;
        b.SetDozerPresent(true);
    }
    void Update() override {
        Craft::Update();
        if (!b.TankDead() && b.HitsTank(img, (int)x, (int)y, (int)frame, 0, mirror)) b.KillTank();

        double dir = mirror ? 1.0 : -1.0;   // the original's (mirror*2 - 1)
        if (!b.TankDead() && (!b.NearLevelEnd(700) || b.Survival())) {
            if (flash == 0) {
                vx = std::clamp(vx - dir * 0.02, -1.5, 1.5);
            } else if (std::abs((int)x) + W() / 2 <= 0x13f) {
                vx += dir * 0.02;
                if (vx < -0.5 && !mirror) vx = -0.5;
                if (vx > 0.5 && mirror) vx = 0.5;
            }
        } else {
            vx = std::clamp(vx + (mirror ? 0.03 : -0.03), -5.0, 5.0);
        }
        frame -= vx * (1.0 / 3.0) + 0.3;
        if (frame >= 10.0) frame -= 10.0;
        if (frame < 0.0) frame += 9.0;
        if (OffScreen()) {
            b.SetDozerPresent(false);
            Remove();
        }
    }
    void Die() override {
        Craft::Die();
        b.SetDozerPresent(false);
    }
    void Draw() override {
        Gfx::DrawSprite(Img("dozershadow"), (int)x, 460, true);
        Craft::Draw();
    }
};

// -----------------------------------------------------------------------------
// Special craft
// -----------------------------------------------------------------------------

// STRAFER (0x435020): dives in from above on a curve, firing along its heading.
struct Strafer : Craft {
    double speed;
    Strafer(Board& bd) : Craft(bd, CRAFT_STRAFER, "strafer") {
        int h = H();
        y = (double)(h / 2) + (double)(Rand() % std::max(1, 160 - h)) - 80.0;
        mirror = (Rand() & 1) != 0;
        x = mirror ? 460.0 : -460.0;
        speed = (double)(Rand() % 50) * 0.02 + 2.0;
        AudioSystem::PlaySoundId(SND_JETDIVE, b.Pan(x));
    }
    void UpdateF() override {
        x += vx;
        y += vy;
    }
    void Update() override {
        Craft::Update();
        double a = 0.7854 - frame * 0.08726612678022899;
        double c = std::cos(a), s = std::sin(a);
        vx = c * speed * (mirror ? -1.0 : 1.0);
        vy = s * speed;
        if ((int)y > 180) frame += 0.08;
        if (frame > 9.0) frame = 9.0;
        if (cooldown != 0) --cooldown;
        if (x > W() / 2 - 320 && x < 320 - W() / 2 && cooldown == 0 && !b.TankDead() && frame < 6.0) {
            double shotSpeed = speed + 1.5;
            int sy = (int)(s * 40.0 + y);
            if (!mirror) {
                b.FireEnemyShot((int)(c * 40.0 + x), sy, 3.142 - a, shotSpeed);
            } else {
                b.FireEnemyShot((int)(x - c * 40.0), sy, a, shotSpeed);
            }
            cooldown = 75;
        }
        if (x < -460.0 || x > 460.0) Remove();
    }
};

// SATELLITE (0x434950): hunts the tank and fires a ground laser.
struct Satellite : Craft {
    int laser = 0, beep = 0, beep2 = 0, spark = 0;
    double sparkFrame = 0;
    Satellite(Board& bd) : Craft(bd, CRAFT_SATELLITE, "satellite") {
        cooldown = 200;
        int h = H();
        y = (double)(Rand() % std::max(1, 230 - h) + 50 + h / 2);
        x = (Rand() & 1) == 0 ? (double)(-320 - W() / 2) : (double)(W() / 2 + 320);
    }
    void UpdateF() override {
        x += vx;
        y += vy;
    }
    void Update() override {
        Craft::Update();
        if (cooldown > 0) --cooldown;
        if (laser > 0) --laser;
        if (beep != 0) --beep;
        if (beep2 != 0) --beep2;
        frame += 0.2;
        if (frame >= 10.0) frame -= 10.0;
        sparkFrame += 0.5;
        if (sparkFrame >= 10.0) sparkFrame -= 10.0;

        if (!b.TankDead() && (!b.NearLevelEnd(700) || b.Survival())) {
            double sumX = 0.0, sumY = 0.0;
            int n = 0;
            for (const auto& s : b.Bullets()) {
                if (s.enemy) continue;
                if (std::sqrt(std::pow(s.y - y, 2.0) + std::pow(s.x - x, 2.0)) < 150.0) {
                    sumX += s.x;
                    sumY += s.y;
                    ++n;
                }
            }
            if (n > 0 && x > -160.0 && x < 160.0 && y > 100.0 && y < 200.0) {
                double avgY = sumY / n;
                if (x < sumX) vx -= 0.1;
                if (sumX < x) vx += 0.1;
                if (y < avgY) vy -= 0.03;
                if (avgY < y) vy += 0.03;
            }
            if (x < b.TankX()) vx += 0.02;
            if (x > b.TankX()) vx -= 0.02;
        } else {
            vx += (x < 0.0) ? -0.03 : 0.03;
        }
        if (Rand() % 80 == 0) vy += (double)(Rand() & 3) * 0.1 - 0.8;
        if (y < 100.0) vy += 0.1;
        if (y > 200.0) vy -= 0.1;
        vx = std::clamp(vx, -2.0, 2.0);
        vy = std::clamp(vy, -1.0, 1.0);

        if (OnScreen300() && Rand() % 40 == 0 && cooldown == 0 && !b.TankDead()) {
            cooldown = 400;
            laser = 200;
            AudioSystem::PlaySoundId(SND_LASERPOWERUP, b.Pan(x));
        }
        if (laser == 100) AudioSystem::PlaySoundId(SND_SATLASER, b.Pan(x));
        if (laser > 0 && laser < 100 && !b.TankDead()) {
            if (b.TankX() - 50.0 < x && x < b.TankX() + 50.0 && b.App().up[UP_SHIELD] != 0) {
                b.App().up[UP_SHIELD] = 0;   // the beam burns away the whole shield
                b.ShieldSpark();
            }
            if (b.TankX() - 32.0 < x && x < b.TankX() + 32.0) b.KillTank();
        }

        // Electronic chatter.
        if (Rand() % 100 == 0 && beep2 == 0) {
            AudioSystem::PlaySoundId(SND_SAT1 + Rand() % 7, b.Pan(x));
            beep2 = 100;
        } else if ((Rand() & 3) == 0 && beep == 0) {
            int k = Rand() & 1;
            AudioSystem::PlaySoundId(SND_SHORTKEY + k, b.Pan(x));
            beep = k ? 22 : 12;
        }

        if (++spark > 3) {
            spark = 0;
            b.SpawnParticles(Img("spark"), x + 22.0, y + 28.0, vx + 2.0, vy + 1.0, 1, 1.0, 0, 0, 30, true, -1);
            b.SpawnParticles(Img("spark"), x - 22.0, y + 28.0, vx - 2.0, vy + 1.0, 1, 1.0, 0, 0, 30, true, -1);
        }
        if (OffScreen()) Remove();
    }
    void Draw() override {
        Craft::Draw();
        Gfx::SetDrawMode(1);
        if (laser > 0 && laser < 100) {
            // Beam: a bright core with fading edges, from the emitter to the ground.
            Gfx::SetColorizeImages(true);
            for (int i = 0, c = 255; c >= 0; ++i, c -= 64) {
                Gfx::SetColor(0, c, 0);
                Gfx::FillRect((int)(x + i), (int)(y + 48.0), 1, 460 - (int)(y + 48.0));
                Gfx::FillRect((int)(x - i), (int)(y + 48.0), 1, 460 - (int)(y + 48.0));
            }
            Gfx::SetColorizeImages(false);
            int burn = (b.App().tick % 40) / 4;
            Gfx::DrawSprite(Img("laserburn"), (int)x, 460, true, burn);
        }
        if (laser != 0) {
            Gfx::DrawSprite(Img("satlaser"), (int)x, (int)(y + 47.0), true, (int)sparkFrame);
        }
        Gfx::SetDrawMode(0);
    }
};

// DEFLECTOR (0x432c80): a shield bubble that turns shells into shrapnel aimed back.
struct Deflector : Craft {
    int shieldHp = 60, shieldFlash = 0;
    Deflector(Board& bd) : Craft(bd, CRAFT_DEFLECTOR, "deflector") {
        int h = H();
        y = (double)(Rand() % std::max(1, (230 - h) / 2) + 50 + h / 2);
        PickSide();
        double speed = (double)(Rand() % 100) * 0.01 + 2.0;
        x = mirror ? (double)(W() / 2 + 320) : (double)(-320 - W() / 2);
        vx = mirror ? -speed : speed;
    }
    void DeflectBullets() override {
        if (shieldHp <= 0) return;
        for (auto& s : b.Bullets()) {
            if (s.enemy) continue;
            double dy = s.y - y, dx = s.x - x;
            double d = std::sqrt(std::pow(dx, 2.0) + std::pow(dy, 2.0));
            if (d <= 60.0) {
                double a = std::atan2(dx, dy);
                s.enemy = true;
                double nx = std::cos(-a - 1.57), ny = std::sin(-a - 1.57);
                double dot = ny * s.vy + nx * s.vx;
                s.vx -= dot * nx * 1.5;
                s.vy -= dot * ny * 1.5;
                s.img = Img("bombfrag");
                s.col = 0;
                s.row = 0;
                shieldFlash = 255;
                shieldHp = (int)(shieldHp - s.damage);
                AudioSystem::PlaySoundId(SND_DEFLECT, b.Pan(s.x));
            }
        }
    }
    void Update() override {
        Craft::Update();
        DeflectBullets();
        if (cooldown > 0) --cooldown;
        if (shieldFlash != 0) shieldFlash -= 8;
        if (shieldFlash < 0) shieldFlash = 0;
        if (OnScreen300() && !b.TankDead() && Rand() % 40 == 0 && cooldown == 0) {
            FireMissile(b, (int)x, (int)y, vx, vy);
            cooldown = 75;
        }
        if (OffScreen()) Remove();
    }
    void Draw() override {
        Craft::Draw();
        if (shieldHp > 0) {
            Gfx::SetDrawMode(1);
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(255, 0, 128);
            Texture* sh = Img("deflectshield");
            Gfx::DrawSprite(sh, (int)x, (int)y, true, (b.App().tick / 4) & 3);
            if (shieldFlash != 0) {
                Gfx::SetColor(shieldFlash, shieldFlash, shieldFlash);
                Gfx::DrawSprite(sh, (int)x, (int)y, true, 0);
            }
            Gfx::SetColorizeImages(false);
            Gfx::SetDrawMode(0);
        }
    }
};

// CRUISE (0x4327b0): skims across, then dives on the tank.
struct Cruise : Craft {
    Cruise(Board& bd) : Craft(bd, CRAFT_CRUISE, "cruise") {
        int h = H();
        y = (double)(Rand() % std::max(1, (230 - h) / 2) + 50 + h / 2);
        mirror = (Rand() & 1) != 0;
        vx = mirror ? -5.0 : 5.0;
        x = mirror ? (double)(W() / 2 + 320) : (double)(-320 - W() / 2);
        b.CountCraft(-1, 0, 0);   // cruise missiles do not count toward the kill percentage
    }
    void Die() override {
        Craft::Die();
        b.CountCraft(0, -1, 0);
    }
    void UpdateF() override {
        x += vx;
        y += vy;
    }
    void Update() override {
        Craft::Update();
        if (OffScreen()) {
            Remove();
            return;
        }
        if (frame > 0.0) {
            frame += 0.2;
            if (frame > 7.0) frame = 7.0;
            vx = (7.0 - frame) * (5.0 / 7.0) * (mirror ? -1.0 : 1.0);
            vy = frame * (5.0 / 7.0);
        }
        if (frame == 0.0 && std::abs((int)(x - b.TankX())) < 90) frame += 0.2;

        if (y <= (double)(470 - H() / 2)) {
            bool hit = false;
            if (!b.TankDead() && b.ShieldUp() && b.HitsShield(img, (int)x, (int)y, (int)frame, 0, mirror)) {
                b.AbsorbShieldHit();
                hit = true;
            } else if (!b.TankDead() && b.HitsTank(img, (int)x, (int)y, (int)frame, 0, mirror)) {
                b.KillTank();
                hit = true;
            }
            if (!hit) return;
        } else {
            vy = 0.0;
            b.SpawnCraters((int)x, 0);
        }
        Explode();
        Remove();
    }
};

// The white power-up helicopter (0x437450): an ally. It drops its power-up near the
// middle of the screen; shooting it costs points.
struct PupCopter : Craft {
    int powerUp;
    bool dropped = false;
    double rotor = 0.0, maxHp;
    int dontShoot = 0, pain = 0, dropX;
    PupCopter(Board& bd, int type_) : Craft(bd, 0, "pupcopter"), powerUp(type_) {
        int tier = std::min(b.Tier(), 14);
        hp = maxHp = (double)((tier * 5 + 50) * 2);
        points = -1500;
        friendly = true;
        int h = H();
        y = (double)(Rand() % std::max(1, 180 - h) + 50 + h / 2);
        mirror = (Rand() & 1) != 0;
        dropX = Rand() % 200 - 100;
        double speed = (double)(Rand() % 50) * 0.01 + 2.0;
        x = mirror ? 520.0 : -520.0;
        vx = mirror ? -speed : speed;
        b.AddFriendly(1);
        b.CountCraft(-1, 0, 0);
    }
    ~PupCopter() override { AudioSystem::SetLoop(SND_PUPCOPTER, false); }
    void Update() override {
        rotor += 0.5;
        if (rotor >= 7.0) rotor -= 7.0;
        if (dontShoot > 0) dontShoot = std::max(0, dontShoot - 5);
        if (std::abs((int)(x - dropX)) < 5 && !dropped && !b.App().noWeapons) {
            b.DropPowerUp(x, y, vx, vy, powerUp);
            dropped = true;
        }
        AudioSystem::SetLoop(SND_PUPCOPTER, true, b.Pan(x));
        if (flash == 0xff) {
            dontShoot = 255;
            static const double kThreshold[3] = { 0.75, 0.5, 0.25 };
            if (pain < 3 && hp < maxHp * kThreshold[pain]) {
                AudioSystem::PlaySoundId(SND_FRIENDLY1 + pain, b.Pan(x));
                ++pain;
            }
        }
        Craft::Update();
        if (x < -520.0 || x > 520.0) Remove();
    }
    void OnRemoved() override { b.AddFriendly(-1); }
    void Die() override {
        AudioSystem::PlaySoundId(SND_FRIENDLYDIE, b.Pan(x));
        Craft::Die();
        b.CountCraft(0, -1, 0);
    }
    void Draw() override {
        Craft::Draw();
        Texture* r = Img("puprotor");
        int f = (int)rotor;
        if (!mirror) {
            Gfx::DrawSprite(r, (int)(x - 47.0), (int)(y - 24.0), true, f);
            Gfx::DrawSprite(r, (int)(x + 27.0), (int)(y - 18.0), true, f);
        } else {
            Gfx::DrawSprite(r, (int)(x - 27.0), (int)(y - 18.0), true, f);
            Gfx::DrawSprite(r, (int)(x + 47.0), (int)(y - 24.0), true, f);
        }
        if (dontShoot != 0) {
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(255, 255, 255, dontShoot);
            Gfx::DrawSprite(Img("dontshoot"), (int)x, (int)y, true);
            Gfx::SetColorizeImages(false);
        }
    }
};

} // namespace

std::unique_ptr<Craft> CreatePupCopter(Board& b, int type) {
    return std::make_unique<PupCopter>(b, type);
}

// -----------------------------------------------------------------------------
// Factory (0x40fed0)
// -----------------------------------------------------------------------------

std::unique_ptr<Craft> CreateCraft(Board& b, int type) {
    switch (type) {
        case CRAFT_PROPFIGHTER: return std::make_unique<PropFighter>(b);
        case CRAFT_SMALLJET:    return std::make_unique<SmallJet>(b);
        case CRAFT_BOMBER:      return std::make_unique<Bomber>(b);
        case CRAFT_JETFIGHTER:  return std::make_unique<JetFighter>(b);
        case CRAFT_TRUCK:       return std::make_unique<Truck>(b);
        case CRAFT_BIGBOMBER:   return std::make_unique<BigBomber>(b);
        case CRAFT_SMALLCOPTER: return std::make_unique<SmallCopter>(b);
        case CRAFT_MEDCOPTER:   return std::make_unique<HoverCopter>(b, CRAFT_MEDCOPTER, "medcopter", 6, 0.15, 0.02, 125, "medrotor");
        case CRAFT_BIGCOPTER:   return std::make_unique<HoverCopter>(b, CRAFT_BIGCOPTER, "bigcopter", 8, 0.1, 0.05, 75, "rotors");
        case CRAFT_DELTABOMBER: return std::make_unique<DeltaBomber>(b);
        case CRAFT_DELTAJET:    return std::make_unique<DeltaJet>(b);
        case CRAFT_SUPERBOMBER: return std::make_unique<SuperBomber>(b);
        case CRAFT_FATBOMBER:   return std::make_unique<FatBomber>(b);
        case CRAFT_BLIMP:       return std::make_unique<Blimp>(b);
        case CRAFT_SATELLITE:   return std::make_unique<Satellite>(b);
        case CRAFT_STRAFER:     return std::make_unique<Strafer>(b);
        case CRAFT_ENEMYTANK:   return std::make_unique<EnemyTank>(b);
        case CRAFT_DOZER:       return std::make_unique<Dozer>(b);
        case CRAFT_DEFLECTOR:   return std::make_unique<Deflector>(b);
        case CRAFT_CRUISE:      return std::make_unique<Cruise>(b);
        default:                return nullptr;
    }
}

} // namespace HeavyWeapon
