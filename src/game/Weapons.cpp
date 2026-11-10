// The tank's armory weapons: homing missiles (0x449830), rockets (0x449c80), flak
// (0x44a220) and Thunderstrike (0x44aa10). Missiles, rockets and bolts are hazard-list
// objects that cannot be shot; flak bursts live in their own list.

#include "game/Weapons.h"
#include "game/Board.h"
#include "game/Enemies.h"
#include "game/Gfx.h"
#include "AudioSystem.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace HeavyWeapon {

static int Rand() { return std::rand() & 0x7fffffff; }
static Texture* Img(const char* name) { return TextureManager::Get(name); }

// Shared "player projectile hits a craft" block: damage, then either sparks or a kill.
static bool HitCraft(Board& b, const Texture* img, double x, double y, int frame, double damage, bool skipFriendly) {
    for (auto& c : b.Crafts()) {
        if (c->dead || !c->hittable || (skipFriendly && c->friendly)) continue;
        if (!Board::PixelHit(img, (int)x, (int)y, frame, 0, false, c->img, (int)c->x, (int)c->y, (int)c->frame, 0, c->mirror)) continue;
        c->flash = 0xff;
        c->hp -= damage;
        if (c->hp >= 0.0) {
            AudioSystem::PlaySoundId(SND_SMALLEXPLODE, b.Pan(x));
            b.SpawnParticles(Img("spark"), x, y, c->vx, c->vy, (int)(10.0 * b.App().detail), 2.0, 0.05, 3.0, 30, true, -1);
        } else {
            c->Die();
            c->Remove();
        }
        return true;
    }
    return false;
}

namespace {

struct HomingMissile : Hazard {
    HomingMissile(Board& bd, double x_, double y_) : Hazard(bd, "missile", x_, y_, 0, 0, false) {
        hittable = false;
        frame = 5.0;   // straight up
        AudioSystem::PlaySoundId(SND_SMALLMISSILE, b.Pan(x));
        b.SpawnParticles(Img("smoke"), x, y, 0.0, 0.0, 4, 1.0, -0.01, -0.5, 40, true, -1);
    }
    Craft* Target() {
        Craft* best = nullptr;
        double bestD = 100000.0;
        for (auto& c : b.Crafts()) {
            if (c->dead || c->friendly || !c->hittable || c->y >= y + 1.0) continue;
            double d = std::sqrt(std::pow(c->y - y, 2.0) + std::pow(c->x - x, 2.0));
            if (d < bestD) {
                bestD = d;
                best = c.get();
            }
        }
        return best;
    }
    void Update() override {
        double a = frame * 0.3141690229343387;
        vx = std::cos(a) * 4.0;
        vy = std::sin(a) * 4.0;
        x += vx;
        y -= vy;
        b.SpawnParticles(Img("spark"), x - vx * 5.0, vy * 5.0 + y, vx * 0.5, vy * -0.5, 1, 1.0, 0, 0, 30, true, -1);
        if (Craft* t = Target()) {
            double ta = std::atan2(x - t->x, y - t->y) + 1.5705;
            if (a < ta) frame += 0.1;
            if (ta < a) frame -= 0.1;
            frame = std::clamp(frame, 0.0, 10.0);
        }
        if (HitCraft(b, img, x, y, (int)frame, 15.0, true) || OffScreenX() || y < 0.0) Remove();
    }
};

struct Rocket : Hazard {
    Rocket(Board& bd, double tankX, int tankY, double gunCel, int side) : Hazard(bd, "rocket", 0, 0, 0, 0, false) {
        hittable = false;
        frame = gunCel;
        double a = (gunCel * 9.0 - 4.0) * 0.017453292520882225;
        x = (tankX - std::cos(a) * 12.0) - std::cos(a + 1.5707963268794) * side;
        y = ((tankY - 12) - std::sin(a) * 12.0) - std::sin(a + 1.5707963268794) * side;
        vx = std::cos(a) * -4.0;
        vy = std::sin(a) * 4.0;
        b.SpawnParticles(Img("smoke"), x, y, vx * -0.5, vy * 0.5, 4, 1.0, -0.01, -0.5, 40, true, -1);
    }
    void Update() override {
        x += vx;
        y -= vy;
        b.SpawnParticles(Img("spark"), x - vx * 5.0, vy * 5.0 + y, vx * 0.5, vy * -0.5, 1, 1.0, 0, 0, 30, true, -1);
        if (HitCraft(b, img, x, y, (int)frame, 6.0, false) || OffScreenX() || y < 0.0) Remove();
    }
};

// Thunderstrike: a bolt with a 10-point trail that jumps between targets.
struct StaticBolt : Hazard {
    double tx[10], ty[10];
    Craft* target = nullptr;
    Craft* lastTarget = nullptr;
    int strikes;
    StaticBolt(Board& bd) : Hazard(bd, "bolt", bd.TankX(), bd.TankY() - 4, 0, 0, false) {
        hittable = false;
        for (int i = 0; i < 10; ++i) {
            tx[i] = b.TankX();
            ty[i] = b.TankY() - 4;
        }
        strikes = b.App().up[UP_STATIC] * 2;
        AudioSystem::PlaySoundId(SND_STATICSHOT, b.Pan(x));
        target = Pick();
    }
    Craft* Pick() {
        Craft* best = nullptr;
        double bestD = 100000.0;
        for (auto& c : b.Crafts()) {
            if (c->dead || c->friendly || !c->hittable || c.get() == lastTarget || c->y >= 480.0) continue;
            double d = std::sqrt(std::pow(c->x - tx[0], 2.0) + std::pow(c->y - ty[0], 2.0));
            if (d < bestD) {
                bestD = d;
                best = c.get();
            }
        }
        if (best) lastTarget = best;
        return best;
    }
    bool TargetAlive() {
        for (auto& c : b.Crafts()) {
            if (c.get() == target && !c->dead) return true;
        }
        return false;
    }
    void Update() override {
        if (!target || !TargetAlive()) target = Pick();
        if (strikes == 0 || tx[0] < -420.0 || tx[0] > 420.0 || ty[0] < -100.0 || ty[0] > 580.0) {
            Remove();
            return;
        }
        for (int i = 9; i > 0; --i) {
            tx[i] = tx[i - 1];
            ty[i] = ty[i - 1];
        }
        tx[1] += (double)(Rand() % 2000) * 0.01 - 10.0;
        ty[1] += (double)(Rand() % 2000) * 0.01 - 10.0;
        double gx = target ? target->x : 0.0, gy = target ? target->y : -120.0;
        double dx = gx - tx[0], dy = gy - ty[0];
        double d = std::sqrt(dx * dx + dy * dy);
        if (d > 20.0) {
            dx *= 20.0 / d;
            dy *= 20.0 / d;
        }
        tx[0] += dx;
        ty[0] += dy;
        x = tx[0];
        y = ty[0];
        if (target && tx[0] == target->x && ty[0] == target->y) {
            target->hp -= b.App().up[UP_STATIC];
            target->flash = 0xff;
            b.SpawnParticles(Img("staticspark"), target->x, target->y, target->vx, target->vy,
                             (int)(5.0 * b.App().detail), 3.0, 0, 0, 30, true, -1);
            AudioSystem::PlaySoundId(SND_STATICHIT, b.Pan(target->x));
            if (target->hp < 0.0) {
                target->Die();
                target->Remove();
            }
            target = Pick();
            --strikes;
        }
    }
    void Draw() override {
        Gfx::SetDrawMode(1);
        Gfx::SetColorizeImages(true);
        for (int i = 0; i < 9; ++i) {
            double v = 255.0;
            for (double t = 0.0; t < 0.9; t += 0.1) {
                int c = std::max((int)v, 0);
                Gfx::SetColor(c, c, c);
                Gfx::DrawSprite(img, (int)((tx[i + 1] - tx[i]) * t + tx[i]), (int)((ty[i + 1] - ty[i]) * t + ty[i]),
                                true, std::max(4 - i / 2, 0));
                v -= 2.55;
            }
        }
        Gfx::SetColorizeImages(false);
        Gfx::SetDrawMode(0);
    }
};

} // namespace

// -----------------------------------------------------------------------------
// Flak (0x44a220): a cluster of bursts around the cursor.
// -----------------------------------------------------------------------------

FlakBurst::FlakBurst(Board& board, double cx, double cy)
    : b(board), centerX(cx), centerY(cy), px((int)cx), py((int)cy) {
    bursts = (b.App().up[UP_FLAK] + 1) * 5;
    radius = b.App().up[UP_FLAK] * 8 + 8;
    AudioSystem::PlaySoundId(SND_FLAK, (int)cx);
}

void FlakBurst::Update() {
    if (frame == 0.0) {
        Texture* flash = Img("flakflash");
        for (auto& c : b.Crafts()) {
            if (c->dead || !c->hittable) continue;
            if (!Board::PixelHit(flash, px, py, 0, 0, false, c->img, (int)c->x, (int)c->y, (int)c->frame, 0, c->mirror)) continue;
            c->flash = 0xff;
            c->hp -= 0.75;
            if (c->hp >= 0.0) {
                AudioSystem::PlaySoundId(SND_BULLETHIT, px);
            } else {
                c->Die();
                c->Remove();
            }
        }
        b.FlakHitHazards(flash, px, py);
        b.SpawnParticles(Img("smoke"), px, py, -1.0, 0.0, 1, 0.0, -0.01, -0.5, 120, true, -1);
    }
    frame += 1.0;
    if (frame > 4.0) {
        frame = 0.0;
        if (++count > bursts) {
            dead = true;
            return;
        }
        double a = (double)(Rand() % 6282) * 0.001;
        double r = (double)(Rand() % std::max(1, radius));
        px = (int)(std::cos(a) * r + centerX);
        py = (int)(std::sin(a) * r + centerY);
    }
}

void FlakBurst::Draw() {
    Gfx::SetDrawMode(1);
    Gfx::DrawSprite(Img("flakflash"), px, py, true, (int)frame);
    Gfx::SetDrawMode(0);
}

// -----------------------------------------------------------------------------
// Spawners used by Board::UpdateTank
// -----------------------------------------------------------------------------

void FireHomingMissile(Board& b) {
    b.AddHazard(std::make_unique<HomingMissile>(b, b.TankX(), (double)b.TankY()));
}

void FireRocket(Board& b, double gunCel, int side) {
    b.AddHazard(std::make_unique<Rocket>(b, b.TankX(), b.TankY(), gunCel, side));
}

void FireStatic(Board& b) {
    b.AddHazard(std::make_unique<StaticBolt>(b));
}

} // namespace HeavyWeapon
