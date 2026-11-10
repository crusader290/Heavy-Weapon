// Enemy ordnance (bombs, missiles), translated from the original hazard classes.

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
static constexpr double kDeg = 0.017453292520882225;

// -----------------------------------------------------------------------------
// Hazard base (0x447760)
// -----------------------------------------------------------------------------

Hazard::Hazard(Board& board, const char* image, double x_, double y_, double vx_, double vy_, bool mirror_)
    : b(board), x(x_), y(y_), vx(vx_), vy(vy_), mirror(mirror_), img(Img(image)) {}

void Hazard::Draw() {
    if (flash != 0) {
        Gfx::SetColorizeImages(true);
        Gfx::SetColor(255, 255 - flash, 255 - flash);
    }
    Gfx::DrawSprite(img, (int)x, (int)y, true, (int)frame, 0, mirror);
    Gfx::SetColorizeImages(false);
}

void Hazard::BaseUpdate() {
    flash -= 0x18;
    if (flash < 0) flash = 0;
    if (!b.TankDead() && b.ShieldUp() && b.HitsShield(img, (int)x, (int)y, (int)frame, 0, mirror)) {
        int h = img ? img->GetCelHeight() : 0;
        int w = img ? img->GetCelWidth() : 0;
        b.SpawnExplosion(x, y, w + 40, h + 40, 0.0, -1.0);
        b.AbsorbShieldHit();
        Remove();
    }
}

void Hazard::RemoveWithCrater() {
    b.SpawnCraters((int)x, 0);
    Remove();
}

bool Hazard::OffScreenX() const {
    int w = img ? img->GetCelWidth() : 0;
    return x < -320 - w / 2 || x > w / 2 + 320;
}

// Common bomb tail: a 70x70 blast and a crater on the ground, or on the tank.
bool Hazard::CheckGroundAndTank() {
    if (OffScreenX()) {
        Remove();
        return true;
    }
    if (y > 460.0) {
        b.SpawnExplosion(x, y, 70, 70, 0.0, -1.0);
        RemoveWithCrater();
        return true;
    }
    if (!b.TankDead() && b.HitsTank(img, (int)x, (int)y, (int)frame, 0, mirror)) {
        b.SpawnExplosion(x, y, 70, 70, 0.0, -1.0);
        b.KillTank();
    }
    BaseUpdate();
    return dead;
}

namespace {

// Dumb bomb (0x4481c0); the iron bomb (0x448f30) is the armoured version.
struct DumbBomb : Hazard {
    DumbBomb(Board& bd, const char* im, double x_, double y_, double vx_, double vy_, bool m)
        : Hazard(bd, im, x_, y_, vx_, vy_, m) {
        AudioSystem::PlaySoundId(SND_BOMBFALL, b.Pan(x));
    }
    void Update() override {
        x += vx;
        y += vy;
        vx *= 0.995;
        if (vy < 2.0) vy += 0.05;
        if (frame < 9.0) frame += 0.1;      // tips nose-down
        CheckGroundAndTank();
    }
};

// Frag bomb (0x448680): splits into shrapnel on the ground (0x44a330).
struct FragBomb : DumbBomb {
    FragBomb(Board& bd, double x_, double y_, double vx_, double vy_, bool m)
        : DumbBomb(bd, "fragbomb", x_, y_, vx_, vy_, m) { points = 15; }
    void Update() override {
        x += vx;
        y += vy;
        vx *= 0.995;
        if (vy < 2.0) vy += 0.05;
        if (frame < 9.0) frame += 0.1;
        if (!OffScreenX() && y > 460.0) {
            for (double a = 0.79; a <= 2.37; a += 0.52) {
                Board::Bullet s;
                s.x = x;
                s.y = 460.0;
                s.vx = std::cos(a) * 4.0;
                s.vy = std::sin(a) * -4.0;
                s.gravity = 0.05;
                s.enemy = true;
                s.img = Img("bombfrag");
                b.Bullets().push_back(s);
            }
        }
        CheckGroundAndTank();
    }
};

// Laser-guided bomb (0x4491c0): starts level, then steers toward the tank.
struct LaserGuidedBomb : Hazard {
    double angle;   // +0x68
    LaserGuidedBomb(Board& bd, double x_, double y_, double vx_, double vy_, bool fromRight)
        : Hazard(bd, "lgb", x_, y_, vx_, vy_, false) {
        points = 15;
        angle = fromRight ? -90.0 : 90.0;
        frame = fromRight ? 0.0 : 20.0;
        AudioSystem::PlaySoundId(SND_BOMBFALL, b.Pan(x));
    }
    void Update() override {
        x += vx;
        y += vy;
        vx *= 0.95;
        if (vy < 1.0) vy += 0.05;
        if (angle >= 45.0) angle -= 2.0;
        if (angle <= -45.0) angle += 2.0;
        if (std::abs((int)angle) < 45) {
            if (x < b.TankX() - 20.0) angle += 2.0;
            if (b.TankX() + 20.0 < x) angle -= 2.0;
            double a = (angle - 90.0) * kDeg;
            x += std::cos(a) + std::cos(a);
            y -= std::sin(a);
        }
        frame = (angle + 90.0) * (1.0 / 9.0);
        CheckGroundAndTank();
    }
};

// Fatboy (0x448290): a small nuke. If it lands (or touches the tank) the flash grows
// for 30 ticks and then destroys the tank wherever it is. Shoot it down first.
struct FatBoy : Hazard {
    bool exploding = false;     // +0x68
    double flashScale = 0.2;    // +0x70
    FatBoy(Board& bd, double x_, double y_, double vx_, double vy_, bool m, bool tilted)
        : Hazard(bd, "fatboy", x_, y_, vx_, vy_, m) {
        hp = 60.0;
        points = 200;
        frame = tilted ? 9.0 : 0.0;
        AudioSystem::PlaySoundId(SND_BOMBFALL, b.Pan(x));
    }
    void Detonate() {
        exploding = true;
        active = false;
        b.SetNukeFlash(128.0);
        b.SpawnExplosion(x, y, 200, 200, 0.0, 0.0);
    }
    void Update() override {
        if (exploding) {
            flashScale += 0.2;
            if (flashScale >= 6.0) {
                b.KillTank();
                Remove();
            }
            return;
        }
        x += vx;
        y += vy;
        vx *= 0.995;
        if (vy < 2.0) vy += 0.05;
        if (frame < 9.0) frame += 0.1;
        if (OffScreenX()) {
            Remove();
            return;
        }
        if (y > 460.0) {
            Detonate();
            b.SpawnCraters((int)x, 0);
            return;
        }
        if (!b.TankDead() &&
            (b.HitsTank(img, (int)x, (int)y, (int)frame, 0, mirror) || b.HitsShield(img, (int)x, (int)y, (int)frame, 0, mirror))) {
            Detonate();
            b.KillTank();
            return;
        }
        BaseUpdate();
    }
    void Draw() override {
        if (!exploding) Hazard::Draw();
        Texture* f = Img("fatboyflash");
        if (!f) return;
        int w = (int)(f->width * flashScale * 0.4);
        int h = (int)(f->height * flashScale);
        int v = std::min((int)(512.0 - flashScale * 84.0), 255);
        Gfx::SetDrawMode(1);
        Gfx::SetColorizeImages(true);
        Gfx::SetColor(v, v, v);
        Gfx::DrawImageRect(f, (int)(x - w / 2), (int)(y - h / 2), w, h, 0, 0, f->width, f->height);
        Gfx::SetColorizeImages(false);
        Gfx::SetDrawMode(0);
    }
};

// RPG (0x449930): an arcing rocket from the truck.
struct Rpg : Hazard {
    double arc;     // +0x68 frame step
    Rpg(Board& bd, double x_, double y_, bool m, double arc_) : Hazard(bd, "rpg", x_, y_, 0, 0, m), arc(arc_) {
        points = 20;
        AudioSystem::PlaySoundId(SND_MISSILE, b.Pan(x));
        b.SpawnParticles(Img("smoke"), x, y, 0.0, 0.0, 4, 1.0, -0.01, -0.5, 40, true, -1);
    }
    void Update() override {
        frame += arc;
        if (frame > 9.0) frame = 9.0;
        double a = (30.0 - frame * 6.6) * kDeg;
        vx = std::cos(a) * (mirror ? -3.0 : 3.0);
        vy = std::sin(a) * -3.0;
        x += vx;
        y += vy;
        b.SpawnParticles(Img("spark"), x - vx * 5.0, y - vy * 5.0, vx * 0.5, vy * 0.5, 1, 1.0, 0, 0, 30, true, -1);
        CheckGroundAndTank();
    }
};

// Homing missile (0x448b20): turns toward the tank until it lines up.
struct Missile : Hazard {
    double turn;            // +0x70
    double speedScale;      // +0x78
    bool locked = false;    // +0x68
    Missile(Board& bd, double x_, double y_, double vx_, double vy_, double scale)
        : Hazard(bd, "hellfire", x_, y_, vx_, vy_, false), speedScale(scale) {
        points = 20;
        turn = ((double)(Rand() % 150) * 0.001 + 0.1) * speedScale;
        if (b.TankX() <= x) {
            frame = 0.0;
            x -= 20.0;
        } else {
            frame = 20.0;
            x += 20.0;
        }
        AudioSystem::PlaySoundId(SND_MISSILE, b.Pan(x));
        b.SpawnParticles(Img("smoke"), x, y, vx, vy, 4, 1.0, -0.01, -0.5, 40, true, -1);
    }
    void Update() override {
        double a = (frame - 10.0) * 17.143 * kDeg;
        double sp = speedScale * 4.0;
        vx = std::cos(a + 1.5705) * sp;
        vy = std::sin(a + 1.5705) * sp;
        x -= vx;
        y += vy;
        b.SpawnParticles(Img("spark"), vx * 5.0 + x, y - vy * 5.0, vx * 0.5, vy * -0.5, 1, 1.0, 0, 0, 30, true, -1);
        if (!locked) {
            double target = std::atan2(b.TankX() - x, (double)b.TankY() - y);
            if (a <= target) {
                frame += turn;
                if (target <= turn * 17.143 * kDeg + a) locked = true;
            } else {
                frame -= turn;
                if (a - turn * 17.143 * kDeg <= target) locked = true;
            }
            frame = std::clamp(frame, 0.0, 20.0);
        }
        CheckGroundAndTank();
    }
};

// BIGMISSILE (0x431d20): rises from behind the scenery, then dives.
struct BigMissile : Hazard {
    bool rising = true;     // +0x38 (row of the sprite)
    bool fizzle = false;    // +0x68 tank died: missiles leaving the top are dropped
    BigMissile(Board& bd, double x_) : Hazard(bd, "bigmissile", x_, 460.0, 0, 0, false) {
        int tier = std::min(b.Tier(), 14);
        hp = std::min((double)((tier + 1) * 7), 40.0);
        active = false;
        behind = true;
        points = 500;
        AudioSystem::PlaySoundId(SND_AIRRAID, 0);
    }
    void Update() override {
        if (b.TankDead()) fizzle = true;
        y += rising ? -1.5 : 3.0;
        int h = img ? img->GetCelHeight() : 0;
        if (y < -h && rising) {
            rising = false;
            active = true;
            behind = false;
            if (fizzle) {
                Remove();
                return;
            }
        }
        frame += 0.15;
        if (frame >= 10.0) frame -= 10.0;
        int w = img ? img->GetCelWidth() : 0;
        if (y > 460.0) {
            b.SpawnExplosion(x, y, w + 40, h + 40, 0.0, -1.0);
            RemoveWithCrater();
            return;
        }
        if (!b.TankDead() && !rising && b.HitsTank(img, (int)x, (int)y, (int)frame, 0, false)) {
            b.SpawnExplosion(x, y, w + 40, h + 40, 0.0, -1.0);
            b.KillTank();
            Remove();
            return;
        }
        BaseUpdate();
    }
    void Draw() override {
        if (flash != 0) {
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(255, 255 - flash, 255 - flash);
        }
        Gfx::DrawSprite(img, (int)x, (int)y, true, (int)frame, rising ? 1 : 0);
        Gfx::SetColorizeImages(false);
    }
};

} // namespace

// -----------------------------------------------------------------------------
// Spawners
// -----------------------------------------------------------------------------

static bool CanFire(Board& b) { return !b.TankDead(); }

void DropDumbBomb(Board& b, int x, int y, double vx, double vy, bool mirror) {
    if (!CanFire(b)) return;
    b.AddHazard(std::make_unique<DumbBomb>(b, "dumbbomb", x, y, vx, vy, mirror));
}

void DropIronBomb(Board& b, int x, int y, double vx, double vy, bool mirror) {
    if (!CanFire(b)) return;
    auto bomb = std::make_unique<DumbBomb>(b, "ironbomb", x, y, vx, vy, mirror);
    bomb->hp = 10.0;
    bomb->points = 20;
    b.AddHazard(std::move(bomb));
}

void DropFragBomb(Board& b, int x, int y, double vx, double vy, bool mirror) {
    b.AddHazard(std::make_unique<FragBomb>(b, x, y, vx, vy, mirror));
}

void DropLaserGuidedBomb(Board& b, int x, int y, double vx, double vy, bool fromRight) {
    b.AddHazard(std::make_unique<LaserGuidedBomb>(b, x, y, vx, vy, fromRight));
}

void DropFatBoy(Board& b, int x, int y, double vx, double vy, bool mirror, bool tilted) {
    b.AddHazard(std::make_unique<FatBoy>(b, x, y, vx, vy, mirror, tilted));
}

void FireRpg(Board& b, double x, double y, bool mirror, double arc) {
    b.AddHazard(std::make_unique<Rpg>(b, x, y, mirror, arc));
}

void FireMissile(Board& b, int x, int y, double vx, double vy, double speedScale) {
    if (!CanFire(b)) return;
    b.AddHazard(std::make_unique<Missile>(b, x, y, vx, vy, speedScale));
}

// Craft type 12: a row of 2-5 missiles across the screen, more on later missions.
void SpawnBigMissiles(Board& b) {
    int n = std::clamp((int)(b.Tier() * 0.6666666666666666), 2, 5);
    int span = 640 - 640 / n;
    for (int px = -(span / 2); px <= span / 2; px += span / n) {
        b.AddHazard(std::make_unique<BigMissile>(b, (double)px));
    }
}

} // namespace HeavyWeapon
