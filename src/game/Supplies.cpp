// Board systems around supplies and the big weapons: megalaser parts (crates), the
// power-up helicopter's power-ups, nukes and their mushroom cloud, the megalaser and the
// tank laser. Translated from the functions noted on each.

#include "game/Board.h"
#include "game/Enemies.h"
#include "game/Gfx.h"
#include "AudioSystem.h"
#include "WorldRenderer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace HeavyWeapon {

static int Rand() { return std::rand() & 0x7fffffff; }
static Texture* Img(const char* name) { return TextureManager::Get(name); }

template <typename T, typename Pred>
static void EraseIf(std::vector<T>& v, Pred pred) {
    v.erase(std::remove_if(v.begin(), v.end(), pred), v.end());
}

// Power-up names, caps and how fast they unlock with the mission number (table at
// VA 0x52bb18: char name[32], int max, ..., double weight). Weight 0 = always offered.
struct PowerUpInfo { const char* message; int max; double weight; int sound; };
static const PowerUpInfo kPowerUps[6] = {
    { "NUKE ACQUIRED",   3,     0.0, SND_POWERUP },
    { "SHIELD UPGRADED", 3,     0.0, SND_SHIELDUP },
    { "SPEED INCREASED", 10000, 1.0, SND_SPEEDUP },
    { "GUN POWER UP",    10000, 1.0, SND_GUNPOWERUP },
    { "RAPID FIRE",      10000, 1.0, SND_GUNPOWERUP },
    { "SPREAD SHOT",     4,     0.5, SND_GUNPOWERUP },
};
static const char* kWeaponNames[6] = {
    "DEFENSE ORBS", "HOMING MISSILE", "LASER", "ROCKETS", "FLAK CANNON", "THUNDERSTRIKE"
};

void Board::AddMessage(const std::string& text, int x, int y, bool rainbow, bool red) {
    mPopups.push_back({ text, (double)x, (double)y, 255, red, rainbow, 0 });
}

// -----------------------------------------------------------------------------
// Megalaser parts (0x42da00, 0x42db00)
// -----------------------------------------------------------------------------

bool Board::CanDropCrate() const {
    return (mProgress < mLength - 4000 || mSurvival) && mCrateCooldown == 0 && mMegaTime == 0;
}

void Board::DropCrate(int x, int y) {
    // Drifts toward the middle of the screen as it falls.
    int drift = (int)(((long long)x * 0x77777777) >> 32) - x;
    mCrates.push_back({ (double)x, (double)y, (double)((drift >> 6) - (drift >> 31)), 0.0, (double)mMegaParts, false });
    mCrateCooldown = mSurvival ? 250 : 500;   // survival drops supplies twice as often
}

void Board::UpdateCrates() {
    for (auto& c : mCrates) {
        if (!c.collected) {
            c.x += c.vx;
            c.y += c.vy;
            c.vx *= 0.99;
            c.vy = std::min(c.vy + 0.02, 1.5);
            if (c.y > 460.0) {
                AudioSystem::PlaySoundId(SND_STAR, Pan(c.x));
                SpawnExplosion(c.x, c.y, 50, 50, c.vx, c.vy);
                SpawnCraters((int)c.x, 0);
                c.frame = -1;   // remove
                continue;
            }
            if (mRespawn == 0 && PixelHit(Img("crates"), (int)c.x, (int)c.y, (int)c.frame, 0, false,
                                          Img("tank"), (int)mTankX, mTankY, (int)mTread, 0, false)) {
                AudioSystem::PlaySoundId(SND_STARSMASH, Pan(c.x));
                char msg[64];
                std::snprintf(msg, sizeof msg, "MEGALASER %i%% COMPLETE", (mMegaParts + 1) * 25);
                AddMessage(msg, (int)mTankX, mTankY, true);
                c.collected = true;
            }
        } else {
            // Fly up to the megalaser slots on the status bar.
            c.x += c.vx;
            c.y += c.vy;
            double dx = 160.0 - c.x, dy = 20.0 - c.y;
            double d = std::sqrt(dx * dx + dy * dy);
            c.vx = dx;
            c.vy = dy;
            if (d > 8.0) {
                c.vx = dx * 8.0 / d;
                c.vy = dy * 8.0 / d;
            }
            if (c.y < 21.0) {
                AudioSystem::PlaySoundId(SND_MEGAUP1 + mMegaParts, 0);
                ++mMegaParts;
                if (mMegaParts == 4) {
                    mMegaParts = 0;
                    mMegaTime = 3000;
                    AudioSystem::PlaySoundId(SND_V_MEGALASER, 0);
                } else {
                    AddScore(mMegaParts * 1000, (int)c.x, (int)c.y);
                }
                c.frame = -1;
            }
        }
    }
    EraseIf(mCrates, [](const Crate& c) { return c.frame < 0; });
}

// -----------------------------------------------------------------------------
// Power-ups (0x41aa90 pick, 0x42c1b0 drop, 0x42c2b0 update)
// -----------------------------------------------------------------------------

int Board::PickPowerUp() {
    std::vector<int> candidates;
    const double tier = Tier();
    for (int i = 0; i < 6; ++i) {
        int level = mApp.up[i];
        bool unlocked = level <= tier * kPowerUps[i].weight || kPowerUps[i].weight <= 0.0;
        if (!unlocked || level == kPowerUps[i].max) continue;
        if (i == UP_NUKES && !(mNukeCooldown == 0 && (Rand() & 1) == 0)) continue;
        if (i == UP_SHIELD && mShieldCooldown != 0) continue;
        candidates.push_back(i);
    }
    if (mSurvival) {
        // Survival also hands out armory weapons, one level per tier.
        int owned = 0;
        for (int i = UP_ORBS; i <= UP_STATIC; ++i) owned += mApp.up[i];
        if (owned < Tier()) {
            for (int i = UP_ORBS; i <= UP_STATIC; ++i) {
                if (mApp.up[i] < 3) candidates.push_back(i);
            }
        }
    }
    if (candidates.empty()) return -1;
    return candidates[Rand() % candidates.size()];
}

void Board::DropPowerUp(double x, double y, double vx, double vy, int type) {
    mPowerUps.push_back({ type, x, y, vx, vy });
}

void Board::UpdatePowerUps() {
    Texture* pupImg = Img("powerups");
    int w = pupImg ? pupImg->GetCelWidth() : 0;
    for (auto& p : mPowerUps) {
        p.x += p.vx;
        p.vx *= 0.99;
        if (p.y >= 455.0) {
            // On the ground it slides off with the scenery.
            if (mRespawn == 0) {
                p.vx = p.vx * 0.99 - 0.003;
                if (p.vx < -1.0) p.vx = -1.0;
            }
            p.y = 455.0;
        } else {
            p.y += p.vy;
            p.vy = std::min(p.vy + 0.07, 3.0);
        }
        if (p.x < -320 - w) {
            p.type = -1;
            continue;
        }
        if (mRespawn != 0) continue;
        if (!PixelHit(pupImg, (int)p.x, (int)p.y, p.type, 0, false, Img("tank"), (int)mTankX, mTankY, (int)mTread, 0, false)) continue;

        int t = p.type;
        p.type = -1;
        if (t >= UP_ORBS) {
            if (mApp.up[t] < 3) {
                ++mApp.up[t];
                AddMessage(kWeaponNames[t - UP_ORBS], (int)mTankX, mTankY - 13, true);
                AudioSystem::PlaySoundId(SND_UPGRADE, Pan(mTankX));
            }
            continue;
        }
        if (mApp.up[t] < kPowerUps[t].max) {
            ++mApp.up[t];
            AddMessage(kPowerUps[t].message, (int)mTankX, mTankY - 13, true);
        }
        AudioSystem::PlaySoundId(kPowerUps[t].sound, Pan(mTankX));
        if (t == UP_SHIELD) mShieldPulse = 255.0;
    }
    EraseIf(mPowerUps, [](const PowerUp& p) { return p.type < 0; });
    if (mShieldPulse > 0.0) mShieldPulse = std::max(0.0, mShieldPulse - 4.0);
}

// -----------------------------------------------------------------------------
// Nuke (0x41a160) and its mushroom cloud (0x4282e0, 0x428ac0, 0x428770)
// -----------------------------------------------------------------------------

void Board::FireNuke() {
    if (mApp.up[UP_NUKES] <= 0 || mRespawn != 0 || mNukeFlash != 0.0) {
        AudioSystem::PlaySoundId(SND_DENIED, 0);
        if (mApp.up[UP_NUKES] == 0 && mRespawn == 0) mNoNukeMsg = 250;
        return;
    }
    --mApp.up[UP_NUKES];
    mShake = 254;
    mNukeFlash = 255.0;
    WorldRenderer::TriggerNuke();

    // Every enemy craft in the air is destroyed; during a boss fight everything instead
    // takes maxHP / (mission + 5) damage.
    for (auto& c : mCrafts) {
        if (c->dead || c->friendly || !c->hittable) continue;
        if (c->y - c->H() / 2 < 460.0) {
            if (!mBoss) {
                c->Die();
                c->Remove();
            } else {
                c->hp -= c->maxHp / (mApp.mission + 5);
                if (c->hp < 0.0) {
                    c->Die();
                    c->Remove();
                }
            }
        }
    }
    // Enemy bullets are worth 10 each, hazards their own value.
    EraseIf(mBullets, [this](const Bullet& b) {
        if (!b.enemy) return false;
        AddScore(10, (int)b.x, (int)b.y);
        return true;
    });
    for (auto& h : mHazards) {
        if (h->dead || !h->hittable) continue;
        AddScore(h->points, (int)h->x, (int)h->y);
        h->Remove();
    }
    mWaves.clear();
    mWaveDelay = 0;
    mNukeCooldown = mSurvival ? 600 : mLength / 6;
    AudioSystem::PlaySoundId(SND_NUKEBLAST, 0);
    SpawnMushroom();
}

void Board::SpawnMushroom() {
    Mushroom m;
    // Stem.
    for (int y = 128; y < 360; y += 64) {
        m.smoke.push_back({ (double)((Rand() & 31) - 16), (double)y, 0.0, -1.5, Rand() % 3, 160.0 });
    }
    // Cap: two rings of smoke, then two of smoke-with-fire.
    for (double r = 2.0; r > 0.0; r -= 1.0) {
        for (double a = 3.141; a < 9.423; a += 3.0 - r) {
            double c = std::cos(a), s = std::sin(a);
            m.smoke.push_back({ c * r * 5.0 + (Rand() & 15) - 8.0, (r * 5.0 * 0.25 * s + (Rand() & 15) - 8.0) - 40.0,
                                c * r * (1.0 / 3.0), s * r * 0.25 - 1.0, Rand() % 3, (double)(int)(223.0 - s * 32.0) });
        }
    }
    for (double r = 2.0; r > 0.0; r -= 1.0) {
        for (double a = 3.141; a < 9.423; a += 3.0 - r) {
            double c = std::cos(a), s = std::sin(a);
            CloudPuff p = { c * r * 10.0 + (Rand() & 15) - 8.0, r * 10.0 * 0.25 * s + (Rand() & 15) - 8.0,
                            c * r * 0.25, s * r * (1.0 / 7.0) - 0.3, Rand() % 3, (double)(int)(223.0 - s * 32.0) };
            m.smoke.push_back(p);
            p.frame = Rand() % 3;
            p.life = (double)((Rand() & 127) + 0x17f);
            m.fire.push_back(p);
        }
    }
    mMushrooms.push_back(m);
}

void Board::UpdateMushrooms() {
    for (auto& m : mMushrooms) {
        m.x -= mScrollSpeed * 0.3;
        m.alpha = std::max(0.0, m.alpha - 1.0);
        for (auto& p : m.smoke) {
            p.x += p.vx;
            p.y += p.vy;
            p.vx *= 0.99;
            p.vy *= 0.99;
        }
        for (auto& p : m.fire) {
            p.x += p.vx;
            p.y += p.vy;
            p.vx *= 0.99;
            p.vy *= 0.99;
            p.life = std::max(0.0, p.life - 0.75);
        }
    }
    EraseIf(mMushrooms, [](const Mushroom& m) { return m.x < -520.0 || m.alpha == 0.0; });
}

void Board::DrawMushrooms() {
    // Smoke tint per theme (table at VA 0x561a90).
    static const double kTint[11][3] = {
        { 0.73, 0.87, 1.0 }, { 0.85, 0.9, 1.0 }, { 1.0, 0.8, 0.85 }, { 0.85, 1.0, 0.95 },
        { 0.7, 0.8, 1.0 },   { 0.2, 0.35, 0.5 }, { 0.85, 0.9, 1.0 }, { 0.85, 0.9, 1.0 },
        { 1.0, 0.9, 0.8 },   { 0.8, 0.85, 0.9 }, { 0.8, 0.7, 0.65 },
    };
    int theme = mSurvival ? 10 : (mApp.mission == 18 ? 9 : mApp.mission % 9);
    for (const auto& m : mMushrooms) {
        int a = std::min((int)m.alpha, 255);
        Gfx::SetColorizeImages(true);
        for (const auto& p : m.smoke) {
            Gfx::SetColor((int)(p.life * kTint[theme][0]), (int)(p.life * kTint[theme][1]), (int)(p.life * kTint[theme][2]), a);
            Gfx::DrawSprite(Img("mushsmoke"), (int)(m.x + p.x), (int)(p.y + 240.0), true, p.frame);
        }
        Gfx::SetDrawMode(1);
        for (const auto& p : m.fire) {
            if (p.life == 0.0) continue;
            Gfx::SetColor(std::min((int)p.life, 255), std::min((int)(p.life * 0.5), 255), std::min((int)(p.life * 0.25), 255));
            Gfx::DrawSprite(Img("mushfire"), (int)(m.x + p.x), (int)(p.y + 240.0), true, p.frame);
        }
        Gfx::SetDrawMode(0);
        Gfx::SetColorizeImages(false);
    }
}

// -----------------------------------------------------------------------------
// Megalaser (0x418c90) and tank laser (0x418a50)
// -----------------------------------------------------------------------------

void Board::UpdateMegalaser() {
    int aimY = std::min(mMouseY, mTankY - 12);
    mBeamAngle = std::atan2((double)(mMouseX - 320) - mTankX, (double)((mTankY - aimY) - 12)) - 1.57;
    mBeamLength = 35.0;
    Texture* beam = Img("beam");
    do {
        double px = std::cos(mBeamAngle) * mBeamLength + mTankX;
        double py = (mTankY - 12) + std::sin(mBeamAngle) * mBeamLength;
        if (px < -320.0 || px > 320.0 || py < 0.0) break;
        for (auto& c : mCrafts) {
            if (c->dead || c->friendly || !c->hittable) continue;
            if (PixelHit(beam, (int)px, (int)py, 0, 0, false, c->img, (int)c->x, (int)c->y, (int)c->frame, 0, c->mirror)) {
                c->Die();
                c->Remove();
            }
        }
        HitHazard(beam, (int)px, (int)py, 0, 0, 10000.0);
        EraseIf(mBullets, [&](const Bullet& b) {
            if (!b.enemy) return false;
            if (!PixelHit(beam, (int)px, (int)py, 0, 0, false, b.img, (int)b.x, (int)b.y, b.col, b.row, false)) return false;
            AddScore(10, (int)b.x, (int)b.y);
            return true;
        });
        mBeamLength += 5.0;
    } while (mBeamLength < 800.0);
}

void Board::UpdateLaser() {
    // Fires for the last 50 ticks of each 100 tick cycle (cooldown slot +0xb4).
    if (mCooldown[3] <= 0x31) return;
    int aimY = std::min(mMouseY, mTankY - 12);
    mBeamLength = 35.0;
    mBeamAngle = std::atan2((double)(mMouseX - 320) - mTankX, (double)((mTankY - aimY) - 12)) - 1.57;
    Texture* rock = Img("rock");
    while (mBeamLength < 800.0) {
        double r = mBeamLength + 5.0;
        double px = std::cos(mBeamAngle) * r + mTankX;
        double py = (mTankY - 12) + std::sin(mBeamAngle) * r;
        if (px < -330.0 || px > 330.0 || py < 0.0) break;
        bool stop = false;
        for (auto& c : mCrafts) {
            if (c->dead || !c->hittable) continue;
            if (PixelHit(rock, (int)px, (int)py, 0, 0, false, c->img, (int)c->x, (int)c->y, (int)c->frame, 0, c->mirror)) {
                c->flash = 0xff;
                c->hp -= mApp.up[UP_LASER] * 0.2;
                if (c->hp < 0.0) {
                    c->Die();
                    c->Remove();
                }
                stop = true;
                break;
            }
        }
        if (stop || HitHazard(rock, (int)px, (int)py, 0, 0, mApp.up[UP_LASER] * 0.25)) break;
        mBeamLength += 5.0;
    }
}

void Board::DrawBeams() {
    // Tank laser: a glowing two-tone line with a muzzle glare and a burn where it lands.
    if (mCooldown[3] > 0x32 && mCooldown[3] < 100) {
        Gfx::SetDrawMode(1);
        double c = std::cos(mBeamAngle), s = std::sin(mBeamAngle);
        int x0 = (int)(35.0 * c + mTankX), y0 = (int)(s * 35.0 + (mTankY - 12));
        int x1 = (int)(c * mBeamLength + mTankX), y1 = (int)(s * mBeamLength + (mTankY - 12));
        for (int pass = 2; pass > 0; --pass) {
            int half = (mApp.up[UP_LASER] + 1) * pass;
            if (pass == 2) Gfx::SetColor(32, 128, 64);
            else Gfx::SetColor(255, 255, 255);
            Renderer::DrawLine((float)(x0 + Gfx::TransX()), (float)(y0 + Gfx::TransY()),
                               (float)(x1 + Gfx::TransX()), (float)(y1 + Gfx::TransY()),
                               pass == 2 ? Color4f{ 0.13f, 0.5f, 0.25f, 1.0f } : Color4f::White(), (float)half);
        }
        Gfx::DrawSprite(Img("laserglare"), x0, y0, true, mCooldown[3] % 10);
        if (mBeamLength < 800.0 && x1 > -320 && x1 < 320 && y1 > 0) {
            Gfx::DrawSprite(Img("laserburn"), x1, y1, true, (mApp.tick % 40) / 4);
        }
        Gfx::SetDrawMode(0);
    }

    // Megalaser beam.
    if (mBeam != 0) {
        double c = std::cos(mBeamAngle), s = std::sin(mBeamAngle);
        int anim = (mMegaTime / 4) % 5;
        Gfx::DrawSprite(Img("beamfire"), (int)(c * 40.0 + mTankX), (int)((mTankY - 12) + s * 40.0), true, anim);
        for (double d = (double)(20 - (mMegaTime * 2) % 20); d < mBeamLength; d += 20.0) {
            int f = (int)(d * 0.04 + mMegaTime / 4) % 5;
            Gfx::DrawSprite(Img("beamfire"), (int)(c * d + mTankX), (int)((mTankY - 12) + s * d), true, f);
        }
        Gfx::SetDrawMode(1);
        for (double d = 0.0; d < mBeamLength; d += 15.0) {
            Gfx::DrawSprite(Img("beam"), (int)(c * (d + 40.0) + mTankX), (int)((mTankY - 12) + s * (d + 40.0)), true);
        }
        for (double d = (double)(50 - (mMegaTime * 2) % 50); d < mBeamLength; d += 50.0) {
            int f = (int)(d * 0.04 + mMegaTime / 4) % 5;
            Gfx::DrawSprite(Img("beamfringe"), (int)(c * d + mTankX), (int)((mTankY - 12) + s * d), true, f);
        }
        Gfx::DrawSprite(Img("beamfringe"), (int)(c * 40.0 + mTankX), (int)((mTankY - 12) + s * 40.0), true, anim);
        Gfx::SetDrawMode(0);
    }
}

} // namespace HeavyWeapon
