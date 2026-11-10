#include "game/Board.h"
#include "game/Bosses.h"
#include "game/Enemies.h"
#include "game/Gfx.h"
#include "game/Weapons.h"
#include "AudioSystem.h"
#include "FontRenderer.h"
#include "TextureManager.h"
#include "WorldRenderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace HeavyWeapon {

static constexpr double kDegToRad = 0.017453292520882225;   // the constant the original uses

// Sexy::Rand(); only distribution matters here, not the exact sequence.
static int Rand() { return std::rand() & 0x7fffffff; }

// Upgrade curve used by speed, power and rapid fire: (n+1)^(2/ln(n+6)).
// f(0) = 1, rising ever more slowly.
static double UpgradeCurve(int n) {
    return std::pow(n + 1.0, 2.0 / std::log(n + 6.0));
}

static Texture* Img(const char* name) { return TextureManager::Get(name); }

template <typename T, typename Pred>
static void EraseIf(std::vector<T>& v, Pred pred) {
    v.erase(std::remove_if(v.begin(), v.end(), pred), v.end());
}

// -----------------------------------------------------------------------------
// Construction (Board ctor 0x4191d0)
// -----------------------------------------------------------------------------

Board::Board(AppState& app, const LevelDef* level, const std::vector<CraftDef>& craftById, bool survival)
    : mApp(app), mLevel(level), mCraftById(craftById), mSurvival(survival) {
    if (level) mLength = level->length;
    if (mApp.up[UP_SHIELD] == 0) mApp.up[UP_SHIELD] = 1;   // every level starts with one shield hit
}

Board::~Board() {
    AudioSystem::StopAllLoops();
}

const CraftDef& Board::CraftStats(int type) const {
    static const CraftDef kNone;
    return (type > 0 && type < (int)mCraftById.size()) ? mCraftById[type] : kNone;
}

const LevelDef* Board::WaveLevel() const {
    if (mSurvival && mSurvLevels && !mSurvLevels->empty()) {
        return &(*mSurvLevels)[std::min((size_t)Tier(), mSurvLevels->size() - 1)];
    }
    return mLevel;
}

int Board::Tier() const {
    return mSurvival ? mProgress / 12000 : mApp.mission;
}

// -----------------------------------------------------------------------------
// Update
// -----------------------------------------------------------------------------

void Board::Update() {
    UpdateF();

    if (mRespawn < 1) {
        mDeployed = true;
        UpdateOrbs();
        // Level progress halts at the end of the level until the boss is beaten;
        // survival mode runs at double rate and never ends.
        if (mProgress != mLength || mSurvival) ++mProgress;
        if (mSurvival) {
            ++mProgress;
            mApp.score = mProgress / 20;   // survival scores by time: 10 points per second
        }
    } else {
        // Deploy countdown (+0x80): 300 ticks at the start of a level, 400 after a death.
        // lives counts the tanks in reserve; deploying one uses a life.
        if (mRespawn == 300 && mDeployed) {
            if (mApp.lives < 1) {
                AudioSystem::PlaySoundId(SND_V_GAMEOVER, 0);
                ShowMessage("GAME OVER");
            } else {
                AudioSystem::PlaySoundId(SND_V_GETREADY, 0);
                ShowMessage("GET READY");
            }
        }
        if (mRespawn > 1) --mRespawn;
        if (mRespawn == 1) {
            if (mApp.lives == 0) {
                mGameOver = true;
            } else {
                --mApp.lives;
                mRespawn = 0;
            }
        }
    }

    if (!mSurvival) {
        // The boss arrives as the level runs out (0x41b690 switch on mission % 9).
        if (mProgress == mLength - 1 && !mBoss) SpawnBoss();
        // Safety net if no boss spawned (e.g. progress jumped past length - 1): once the
        // sky is clear the post-boss sequence runs as if the boss had been beaten.
        if (mProgress == mLength && !mBoss && !mDeath && mCrafts.empty()) BossDefeated();
        // A mile sign passes every 5000 ticks.
        if (mProgress > 0 && mProgress < mLength && mProgress % 5000 == 0) --mMile;
    }
    if (mMile < 0) --mMile;
    if (mMile < -0x2c1) mMile = 0;

    for (auto& m : mMessages) {
        for (auto& z : m.z) z -= 0.3;
    }
    EraseIf(mMessages, [](const BigMessage& m) { return m.z.empty() || m.z.back() < -80.0; });

    ++mApp.tick;
    if (mNukeFlash != 0.0) mNukeFlash -= 0.75;
    if (mNukeFlash < 0.0) mNukeFlash = 0.0;
    if (mShieldFlash > 0.0) mShieldFlash -= 0.3;

    // Near the end of a level the progress counter waits until the sky is clear.
    if (!mSurvival && mProgress == mLength - 600) {
        for (const auto& c : mCrafts) {
            if (!c->friendly) {
                --mProgress;
                break;
            }
        }
    }
    if (mCrateCooldown != 0) --mCrateCooldown;
    if (mNukeCooldown != 0) --mNukeCooldown;
    if (mShieldCooldown != 0) --mShieldCooldown;
    if (mPupTimer != 0) --mPupTimer;
    if (mNoNukeMsg != 0) --mNoNukeMsg;

    // White flash doubles as the screen shake (0x40fe00).
    if (mShake > 0) {
        int amp = mShake / 16 + 1;
        mShakeX = Rand() % amp - mShake / 32;
        mShakeY = Rand() % amp - mShake / 32;
        mShake -= 2;
        if (mShake < 1) mShake = mShakeX = mShakeY = 0;
    }

    // Same order as Board::Update (0x41b690).
    if (mDeath) UpdateBossDeath();
    for (auto& c : mCrafts) {                       // 0x410ec0
        if (mRespawn != 0 && c.get() != mBoss) c->vx *= 1.01;   // enemies speed away while you are down
        if (c.get() == mBoss && mBoss->hittable) mBossBarHidden = false;
        if (!c->dead) c->Update();
    }
    for (auto& p : mPendingParts) mCrafts.push_back(std::move(p));
    mPendingParts.clear();
    UpdateBullets();                                // 0x41add0
    for (auto& f : mFlak) f->Update();              // 0x410cf0
    EraseIf(mFlak, [](const std::unique_ptr<FlakBurst>& f) { return f->dead; });
    for (auto& h : mHazards) {                      // 0x4112a0
        if (!h->dead) h->Update();
    }
    UpdateExplosions();                             // 0x4111a0
    UpdateParticles();                              // 0x42ba60
    UpdatePowerUps();                               // 0x42c2b0
    for (auto& p : mPopups) {                       // 0x414a70
        --p.life;
        if (p.rainbow) p.hue = (p.hue + 16) & 0xff;
    }
    for (size_t i = 0; i < mPopups.size(); ++i) {   // overlapping popups push each other up
        for (size_t j = 0; j < mPopups.size(); ++j) {
            if (i != j && std::abs(mPopups[i].x - mPopups[j].x) < 60 &&
                std::abs((mPopups[i].y + mPopups[i].life / 2) - (mPopups[j].y + mPopups[j].life / 2)) < 20 && j > i) {
                mPopups[j].y -= 2;
            }
        }
    }
    EraseIf(mPopups, [](const Popup& p) { return p.life <= 0; });
    UpdateTracks();                                 // 0x4116b0
    UpdateMushrooms();                              // 0x411800
    UpdateLaser();                                  // 0x418a50

    // Megalaser beam: lit while fire is held in megalaser mode, and 10 ticks after.
    if (mBeam != 0) --mBeam;
    if (mFiring && mMegaTime != 0) mBeam = 10;
    if (mMegaTime != 0) mWaveDelay = 0;            // waves keep coming while it lasts
    if (mBeam != 0) UpdateMegalaser();
    AudioSystem::SetLoop(SND_MEGALASER, mBeam != 0 && mRespawn == 0, Pan(mTankX));

    UpdateCrates();                                 // 0x411930
    UpdateCasings();                                // 0x411980
    UpdateMuzzleFlashes();                          // 0x411b40

    if (mDeaths != 0 && mDeaths < 25) ++mDeaths;   // tank flash animation (+0x1c8)

    if (mRespawn == 0) {
        UpdateTank();
        if (mRespawn == 0 && (mProgress < mLength - 800 || mSurvival)) UpdateSpawner();
    }

    // Removals, and hazards created during this tick.
    for (auto& c : mCrafts) {
        if (c->dead) c->OnRemoved();
    }
    EraseIf(mCrafts, [](const std::unique_ptr<Craft>& c) { return c->dead; });
    EraseIf(mHazards, [](const std::unique_ptr<Hazard>& h) { return h->dead; });
    for (auto& h : mNewHazards) mHazards.push_back(std::move(h));
    mNewHazards.clear();
}

// Scrolling (0x4112f0): the ground moves mScrollSpeed px/tick, the other planes at
// fixed fractions of it.
void Board::UpdateF() {
    if (mGasX >= 10.0) {
        if (!mBoss) {
            mScrollSpeed += 0.01;
            if (mScrollSpeed > 1.0) mScrollSpeed = 1.0;
        } else {
            mScrollSpeed *= 0.99;   // the battle comes to a halt
        }
    } else {
        mScrollSpeed = mGasX * 0.1;
    }

    float s = (float)mScrollSpeed;
    mSkyX -= s * 0.1f;
    if (mSkyX < -640.0f) mSkyX += 640.0f;
    mBg2X -= s * 0.2f;
    if (mBg2X < -960.0f) mBg2X += 960.0f;
    mBgX -= s * 0.3f;
    if (mBgX < -960.0f) mBgX += 960.0f;
    mGroundX -= s;
    if (mGroundX < -640.0f) mGroundX += 640.0f;
    if (mGasX < 1000.0) mGasX -= s;

    // Craters scroll with the ground and fade out (0x4112f0).
    for (auto& c : mCraters) {
        --c.life;
        c.x -= s;
    }
    int craterW = Img("crater") ? Img("crater")->GetCelWidth() : 0;
    EraseIf(mCraters, [craterW](const Crater& c) { return c.x < -320 - craterW / 2 || c.life < 0; });

    WorldRenderer::Tick(s);

    // Ground glows left by shells hitting the ground (0x411eb0).
    for (auto& g : mGlows) {
        g.x -= s;
        g.alpha -= (g.type == 0) ? 3 : 5;
    }
    EraseIf(mGlows, [](const GroundGlow& g) { return g.alpha <= 0; });

    for (auto& c : mCrafts) c->UpdateF();
}

// Tank movement, aiming and the cannon (0x412dc0).
void Board::UpdateTank() {
    if (mProgress > mLength && !mSurvival) {
        // Level over: the tank drives to the centre and stops firing.
        mFiring = false;
        mMouseX = 320;
        mMouseY = 320;
    }

    if (mShieldFlash > 0.0) mShieldCooldown = mSurvival ? 500 : mLength / 8;
    // Megalaser mode drains one tick per tick, warning near the end (0x410860).
    if (mMegaTime != 0) {
        --mMegaTime;
        if (mMegaTime < 500 && mMegaTime % 100 == 0) AudioSystem::PlaySoundId(SND_MEGALASER_OUT, 0);
        if (mMegaTime == 0) mBeam = 0;
    }

    mCycle += 0.1;
    if (mCycle >= 10.0) mCycle -= 10.0;

    // Aim at the cursor, clamped to the upper half-circle.
    const int mouseY = mMouseY;
    const int dxTarget = mMouseX - 320;
    double angle = std::atan2((double)dxTarget - mTankX, (double)(mTankY - mouseY - 12)) * 57.29577951;
    angle = std::clamp(angle, -90.0, 90.0);
    mGunCel = (angle + 94.0) * (1.0 / 9.0);

    const double maxSpeed = UpgradeCurve(mApp.up[UP_SPEED]) * 0.25 + 1.75;
    const double aimDeg = angle + 90.0;          // 0 = left, 90 = up, 180 = right

    // Speed follows the aim direction and fades out as the cursor nears the tank.
    double speed = (aimDeg * (1.0 / 9.0) - 10.0) * 0.5;
    double dist;
    if (mouseY < mTankY) {
        dist = std::sqrt(std::pow((double)(mouseY - mTankY), 2.0) + std::pow(dxTarget - mTankX, 2.0));
    } else {
        dist = std::abs((int)(dxTarget - mTankX));
    }
    if (dist < 40.0) {
        speed = 0.0;
    } else if (dist < 80.0) {
        speed = (dist - 40.0) * speed * 0.025;
    }
    if (mDriveOverride) {
        speed = mDriveAxis * maxSpeed;
        dist = 80.0;
    }
    speed = std::clamp(speed, -maxSpeed, maxSpeed);

    mTread += speed * (1.0 / 3.0) + mScrollSpeed * 0.3;
    if (mTread > 9.0) mTread -= 9.0;
    if (mTread < 0.0) mTread += 9.0;

    if ((mMouseX > 29 && dist >= 80.0) || mTankX >= -290.0 || mDriveOverride) {
        mTankX += speed;
        if (mTankX < -290.0 && speed < 0.0) mTankX = -290.0;
        if (mTankX > 290.0) mTankX = 290.0;
    } else {
        mTankX += 2.0;    // drive in from off-screen after spawning
    }

    for (int& c : mCooldown) {
        if (c != 0) --c;
    }

    if (!mFiring || mApp.noWeapons || mMegaTime != 0) return;

    // --- main cannon ---------------------------------------------------------------
    if (mCooldown[0] == 0) {
        const double spread = mApp.up[UP_SPREAD];
        const double power = UpgradeCurve(mApp.up[UP_POWER]);
        const double rate = UpgradeCurve(mApp.up[UP_RATE]) - 1.0;
        mCooldown[0] = (int)(spread + spread + (15.0 - rate));

        // Cross-fade between the five cannon sounds as gun power rises.
        double t = std::min((power - 1.0) * (1.0 / 6.0), 1.0) * 4.0;
        int lo = (int)std::floor(t);
        int hi = (int)std::ceil(t);
        if (lo == hi) {
            AudioSystem::PlaySoundId(SND_TANKFIRE + lo, Pan(mTankX));
        } else {
            static const float kVol[5] = { 0.70f, 0.60f, 0.60f, 0.80f, 0.70f };
            AudioSystem::PlaySoundId(SND_TANKFIRE + lo, Pan(mTankX), (float)((1.0 - (t - lo)) * kVol[lo]));
            if (hi < 5) {
                AudioSystem::PlaySoundId(SND_TANKFIRE + hi, Pan(mTankX), (float)((1.0 - (hi - t)) * kVol[hi]));
            }
        }

        // Muzzle flash at the barrel tip, travelling with the tank.
        double rad = aimDeg * kDegToRad;
        mFlashes.push_back({ mTankX - std::cos(rad) * 36.0, (mTankY - 12) - std::sin(rad) * 36.0, speed, 0.0, 5.0 });

        // spread+1 shells, 10 degrees apart, centred on the aim.
        for (int i = 0; i <= (int)spread; ++i) {
            FireShell(power, 0, (int)(i * 10 - spread * 5.0));
        }

        // Ejected casing.
        mCasings.push_back({ mTankX - 5.0, (double)(mTankY - 10), speed - 1.5, -1.5 });
    }

    // --- armory weapons, each on its own cooldown -------------------------------------
    if (mApp.up[UP_STATIC] != 0 && mCooldown[6] == 0) {
        mCooldown[6] = 50;
        FireStatic(*this);
    }
    if (mApp.up[UP_HOMING] != 0 && mCooldown[2] == 0) {
        mCooldown[2] = 85 - mApp.up[UP_HOMING] * 15;
        FireHomingMissile(*this);
    }
    if (mApp.up[UP_FLAK] != 0 && mCooldown[5] == 0) {
        mCooldown[5] = 50;
        mFlak.push_back(std::make_unique<FlakBurst>(*this, (double)(mMouseX - 320), (double)mMouseY));
    }
    if (mApp.up[UP_LASER] != 0 && mCooldown[3] == 0) {
        mCooldown[3] = 100;
        AudioSystem::PlaySoundId(SND_TANKLASER, Pan(mTankX));
    }
    if (mApp.up[UP_ROCKETS] != 0 && mCooldown[4] == 0) {
        mCooldown[4] = 40;
        int n = mApp.up[UP_ROCKETS];
        for (int i = 0; i < n; ++i) {
            int side = (90 / n) * i;
            if (n > 1) side += (n == 3 ? -8 : 0) - 22;
            FireRocket(*this, mGunCel, side);
        }
        AudioSystem::PlaySoundId(SND_SMALLMISSILE, Pan(mTankX));
    }
}

// Defense orbs (0x418770): circle the tank, destroying ordnance and bullets they touch.
void Board::UpdateOrbs() {
    int n = mApp.up[UP_ORBS];
    mOrbAngle += 10.0 - (n + n);
    if (mOrbAngle > 360.0) mOrbAngle -= 360.0;
    Texture* orb = Img("orb");
    for (int i = 0; i < n && i < 3; ++i) {
        mOrbFlash[i] = std::max(0.0, mOrbFlash[i] - 0.02);
        int offset = (n == 2) ? i * 180 : i * 120;
        double a = (offset + mOrbAngle) * kDegToRad;
        mOrbX[i] = std::cos(a) * 50.0 + mTankX;
        mOrbY[i] = std::sin(a) * 40.0 + mTankY;
        if (HitHazard(orb, (int)mOrbX[i], (int)mOrbY[i], 0, 0, 1000.0)) {
            mOrbFlash[i] = 1.0;
            AudioSystem::PlaySoundId(SND_ORBHIT, Pan(mOrbX[i]));
        }
        EraseIf(mBullets, [&](const Bullet& b) {
            if (!b.enemy || !PixelHit(orb, (int)mOrbX[i], (int)mOrbY[i], 0, 0, false, b.img, (int)b.x, (int)b.y, b.col, b.row, false)) return false;
            mOrbFlash[i] = 1.0;
            AddScore(10, (int)b.x, (int)b.y);
            SpawnExplosion(mOrbX[i], mOrbY[i], 50, 50, 0.0, 0.0);
            AudioSystem::PlaySoundId(SND_ORBHIT, Pan(mOrbX[i]));
            return true;
        });
    }
}

// Tread marks (0x4116b0): the tank lays a track that scrolls away with the ground.
void Board::UpdateTracks() {
    bool active = false;
    for (auto& t : mTracks) {
        t.x0 -= mScrollSpeed;
        if (t.x0 < -323.0) t.x0 += 4.0;
        if (!t.active) {
            t.x1 -= mScrollSpeed;
        } else {
            t.x0 = std::min(t.x0, mTankX - 20.0);
            t.x1 = (t.x1 >= mTankX + 20.0) ? t.x1 - mScrollSpeed : mTankX + 20.0;
            active = true;
        }
    }
    EraseIf(mTracks, [](const Track& t) { return !t.active && t.x1 < -319.0; });
    if (!active && mRespawn == 0) mTracks.push_back({ mTankX, mTankX, true });
}

// FUN_00410d40: one cannon shell, angleOffset in degrees from the barrel.
void Board::FireShell(double power, int sideOffset, int angleOffset) {
    Bullet b;
    b.img = Img("bullets");
    b.enemy = false;
    b.gravity = 0.0;

    int col = (int)(angleOffset / 9 + mGunCel);
    if (col > 20) col -= 20;
    if (col < 0) col += 20;
    b.col = col;

    double base = (mGunCel * 9.0 - 4.0) * kDegToRad;
    double side = base + 1.5707963268794;
    b.x = (mTankX - std::cos(base) * 36.0) - std::cos(side) * sideOffset;
    b.y = ((mTankY - 12) - std::sin(base) * 36.0) - std::sin(side) * sideOffset;
    double dir = angleOffset * kDegToRad + base;
    b.vx = std::cos(dir) * -5.0;
    b.vy = std::sin(dir) * -5.0;
    b.damage = power + 1.0;
    b.row = std::min((int)power, 4);
    mBullets.push_back(b);
}

// Shells and enemy bullets (0x41add0).
void Board::UpdateBullets() {
    for (auto& c : mCrafts) c->DeflectBullets();

    for (auto& b : mBullets) {
        if (b.vy < 3.0) b.vy += b.gravity;
        b.x += b.vx;
        b.y += b.vy;
    }
    EraseIf(mBullets, [this](const Bullet& b) {
        if (b.x < -320.0 || b.x > 320.0 || b.y < 0.0) return true;
        if (b.y > 460.0) {
            mGlows.push_back({ (double)(int)b.x, 255, b.enemy ? 0 : 1 });
            ++mApp.missedShots;
            return true;
        }
        return false;
    });

    EraseIf(mBullets, [this](const Bullet& b) {
        if (!b.enemy) {
            // Shell vs craft: the first craft it overlaps takes the damage.
            for (auto& c : mCrafts) {
                if (c->dead || !c->hittable) continue;
                if (!PixelHit(b.img, (int)b.x, (int)b.y, b.col, b.row, false,
                              c->img, (int)c->x, (int)c->y, (int)c->frame, 0, c->mirror)) continue;
                c->flash = 0xff;
                c->hp -= b.damage;
                if (c->hp >= 0.0) {
                    AudioSystem::PlaySoundId(SND_BULLETHIT, Pan(b.x));
                    SpawnParticles(Img("spark"), b.x, b.y, c->vx, c->vy, (int)(10.0 * mApp.detail), 2.0, 0.05, 3.0, 30, true, -1);
                } else {
                    c->Die();
                    c->Remove();
                }
                return true;
            }
            // Shell vs bombs and missiles.
            return HitHazard(b.img, (int)b.x, (int)b.y, b.col, 0, b.damage);
        }
        if (mRespawn != 0) return false;
        // Enemy bullet vs shield, then vs the tank.
        if (ShieldUp() && HitsShield(b.img, (int)b.x, (int)b.y, b.col, b.row, false)) {
            AbsorbShieldHit();
            return true;
        }
        if (HitsTank(b.img, (int)b.x, (int)b.y, b.col, b.row, false)) {
            KillTank();
            return true;
        }
        return false;
    });
}

void Board::AbsorbShieldHit() {
    if (mShieldFlash <= 0.0) {
        --mApp.up[UP_SHIELD];
        if (mApp.up[UP_SHIELD] < 0) mApp.up[UP_SHIELD] = 0;
        AudioSystem::PlaySoundId(SND_SHIELDDOWN, Pan(mTankX));
    }
    ShieldSpark();
}

void Board::ShieldSpark() {
    mShieldFlash = 9.0;
    AudioSystem::PlaySoundId(SND_SPARKING, Pan(mTankX));
}

// 0x417390: a shot hits the first overlapping hazard.
bool Board::HitHazard(const Texture* img, int x, int y, int col, int row, double damage) {
    for (auto& h : mHazards) {
        if (h->dead || !h->hittable || !h->active || h->behind) continue;
        if (!PixelHit(img, x, y, col, row, false, h->img, (int)h->x, (int)h->y, (int)h->frame, 0, h->mirror)) continue;
        h->hp -= damage;
        h->flash = 0xff;
        if (h->hp < 0.0) {
            AddScore(h->points, (int)h->x, (int)h->y);
            int hw = h->img ? h->img->GetCelWidth() : 0;
            int hh = h->img ? h->img->GetCelHeight() : 0;
            SpawnExplosion(h->x, h->y, hw + 40, hh + 40, h->vx, h->vy - 0.5);
            h->Remove();
        } else {
            AudioSystem::PlaySoundId(SND_BULLETHIT, x * 3);
            SpawnParticles(Img("spark"), x, y, h->vx, h->vy, (int)(10.0 * mApp.detail), 2.0, 0.05, 3.0, 30, true, -1);
        }
        return true;
    }
    return false;
}

// 0x42d750: bounding boxes, then the solid pixels of both cels.
bool Board::PixelHit(const Texture* a, int ax, int ay, int acol, int arow, bool amirror,
                     const Texture* b, int bx, int by, int bcol, int brow, bool bmirror, bool centered) {
    if (!a || !b) return false;
    const int aw = a->GetCelWidth(), ah = a->GetCelHeight();
    const int bw = b->GetCelWidth(), bh = b->GetCelHeight();
    if (centered) {
        ax -= aw / 2;
        ay -= ah / 2;
        bx -= bw / 2;
        by -= bh / 2;
    }
    if (!(ax < bx + bw && ay < by + bh && bx < ax + aw && by < ay + ah)) return false;
    if (a->alpha.empty() || b->alpha.empty()) return true;

    const int x0 = std::max(ax, bx), x1 = std::min(ax + aw, bx + bw);
    const int y0 = std::max(ay, by), y1 = std::min(ay + ah, by + bh);
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            int lax = px - ax, lbx = px - bx;
            if (amirror) lax = aw - 1 - lax;
            if (bmirror) lbx = bw - 1 - lbx;
            size_t ia = (size_t)(arow * ah + (py - ay)) * a->width + (acol * aw + lax);
            size_t ib = (size_t)(brow * bh + (py - by)) * b->width + (bcol * bw + lbx);
            if (ia < a->alpha.size() && ib < b->alpha.size() && a->alpha[ia] && b->alpha[ib]) return true;
        }
    }
    return false;
}

bool Board::HitsTank(const Texture* img, int x, int y, int col, int row, bool mirror) const {
    return PixelHit(img, x, y, col, row, mirror, Img("tank"), (int)mTankX, mTankY, (int)mTread, 0, false);
}

bool Board::HitsShield(const Texture* img, int x, int y, int col, int row, bool mirror) const {
    return PixelHit(img, x, y, col, row, mirror, Img("shield"), (int)mTankX, mTankY + 2, (int)mCycle, 0, false);
}

void Board::UpdateCasings() {
    for (auto& c : mCasings) {
        c.x += c.vx;
        c.y += c.vy;
        c.vx *= 0.99;
        c.vy += 0.06;
        if (c.vy > 2.0) c.vy = 2.0;
    }
    EraseIf(mCasings, [](const Casing& c) { return c.x < -325.0 || c.y > 485.0; });
}

void Board::UpdateMuzzleFlashes() {
    for (auto& f : mFlashes) {
        f.x += f.vx;
        f.y += f.vy;
        f.life -= 1.0;
    }
    EraseIf(mFlashes, [](const MuzzleFlash& f) { return f.life < 1.0; });
}

void Board::UpdateExplosions() {
    for (auto& e : mExplosions) {
        e.x += e.vx;
        e.y += e.vy;
        e.vx *= 0.99;
        e.vy *= 0.99;
        e.frame += e.speed;
    }
    EraseIf(mExplosions, [](const Explosion& e) { return (int)e.frame >= 20; });
}

void Board::UpdateParticles() {
    for (auto& p : mParticles) {
        p.x += p.vx;
        p.y += p.vy;
        if (p.accel > 0.0 ? p.vy < p.maxV : p.vy > p.maxV) p.vy += p.accel;
        p.frame += p.frameSpeed;
    }
    EraseIf(mParticles, [](const Particle& p) { return !p.img || (int)p.frame >= p.img->cols; });
}

void Board::SpawnParticles(const Texture* img, double x, double y, double vx, double vy, int count,
                           double randSpeed, double accel, double maxV, int life, bool fade, int frame) {
    if (!img) return;
    for (int i = 0; i < count; ++i) {
        double r = 0.0;
        if (randSpeed > 0.0) r = (Rand() % std::max(1, (int)(randSpeed * 100.0))) * 0.01;
        double a = (Rand() % 3600) * 0.0017453292520882224;
        Particle p;
        p.img = img;
        p.x = x;
        p.y = y;
        p.vx = std::cos(a) * r + vx;
        p.vy = std::sin(a) * r + vy;
        p.accel = accel;
        p.maxV = maxV;
        p.fade = fade;
        if (frame < 0) {
            p.frame = 0.0;
            p.frameSpeed = (double)img->cols / life;
        } else {
            p.frame = frame;
            p.frameSpeed = 0.0;
        }
        mParticles.push_back(p);
    }
}

void Board::SpawnExplosion(double x, double y, int w, int h, double vx, double vy, double speed) {
    // The original computes a size-dependent frame speed and then overwrites it with 0.4.
    (void)speed;
    mExplosions.push_back({ x, y, vx, vy, w, h, 0.0, 0.4 });

    int count = std::min((int)(std::min((w + h) / 6, 100) * mApp.detail), 100);
    if (count > 0) {
        SpawnParticles(Img("spark"), x, y, vx * 0.5, vy, count, 3.0, 0.05, 3.0, 100, true, -1);
        if (count > 1) {
            SpawnParticles(Img("smoke"), x, y, vx * 0.5, vy, count / 2, 0.5, -0.01, -0.5, 200, true, -1);
        }
    }
    if (w + h > 140) AudioSystem::PlaySoundId(SND_BIGEXPLODE, Pan(x));
    AudioSystem::PlaySoundId(SND_SMALLEXPLODE, Pan(x));
}

// 0x4138e0, after the boss's death animation: everything in the air is destroyed and
// the refuelling stop rolls in.
void Board::SpawnBoss() {
    auto boss = CreateBoss(*this, mApp.mission);
    if (!boss) return;
    mBoss = boss.get();
    mBossBarHidden = true;
    mCrafts.push_back(std::move(boss));
    for (auto& p : mPendingParts) mCrafts.push_back(std::move(p));
    mPendingParts.clear();
}

void Board::AddPart(std::unique_ptr<Craft> part) {
    mPendingParts.push_back(std::move(part));
}

const BossDef* Board::BossStats(const std::string& type) const {
    if (!mBossDefs) return nullptr;
    auto it = mBossDefs->find(type);
    return it == mBossDefs->end() ? nullptr : &it->second;
}

// Boss death (0x41bf70 inside 0x4138e0): big explosions all over the wreck, every
// 11-20 ticks; after the first the sky is cleared, after twenty it is over.
void Board::UpdateBossDeath() {
    if (!mBoss) return;
    if (++mDeathTimer > 20) {
        mDeathTimer = Rand() % 10;
        ++mDeathCount;
        int w = mBoss->W(), h = mBoss->H();
        for (int tries = 0; tries < 50; ++tries) {
            int px = (int)(Rand() % std::max(1, w) - w / 2 + mBoss->x);
            int py = (int)(Rand() % std::max(1, h) - h / 2 + mBoss->y);
            if (PixelHit(mBoss->img, (int)mBoss->x, (int)mBoss->y, 0, 0, mBoss->mirror, Img("bullets"), px, py, 0, 0, false)) {
                SpawnExplosion(px, py, 160, 160, 0.0, 0.0);
                break;
            }
        }
    }
    if (mDeathCount == 1) {
        for (auto& hz : mHazards) hz->Remove();
        EraseIf(mBullets, [](const Bullet& b) { return b.enemy; });
    }
    if (mDeathCount > 20) {
        mDeath = false;
        BossDefeated();
    }
}

void Board::BossDefeated() {
    Craft* boss = mBoss;
    mBoss = nullptr;
    for (auto& c : mCrafts) {
        if (c.get() == boss) {
            c->persistent = false;   // already scored when it died
            c->Remove();
        } else if (!c->dead) {
            c->Die();
            c->Remove();
        }
    }
    mShake = 254;
    mNukeFlash = 255.0;
    EraseIf(mBullets, [](const Bullet& b) { return b.enemy; });
    for (auto& h : mHazards) h->Remove();
    mWaves.clear();
    AudioSystem::PlaySoundId(SND_BOSSBLAST, 0);
    mGasX = 480.0;
    ++mProgress;
    mGasName = Rand() & 3;
}

void Board::ShowMessage(const std::string& text) {
    BigMessage m;
    m.text = text;
    for (size_t i = 0; i < text.size(); ++i) m.z.push_back(9.0 + 2.0 * i);
    mMessages.push_back(m);
}

// Big centred messages (0x41cae0): each letter zooms in from depth and fades in,
// holds, then fades out.
void Board::DrawMessages() {
    const char* font = "RubberStampLET42";
    for (const auto& m : mMessages) {
        float total = FontRenderer::GetStringWidth(font, m.text);
        float x = 320.0f - total / 2.0f;
        for (size_t i = 0; i < m.text.size(); ++i) {
            std::string ch(1, m.text[i]);
            float w = FontRenderer::GetStringWidth(font, ch);
            double z = m.z[i];
            if (z <= 8.0) {
                double fade = std::max(z * 32.0, 0.0);
                if (z <= -70.0) fade = std::min((z + 70.0) * -50.0, 255.0);
                float alpha = (float)(255.0 - fade) / 255.0f;
                float scale = (float)(std::max(z, 1.0) * 0.25 + 0.75);
                float cx = x + w / 2.0f;
                float cy = 240.0f;
                float h = FontRenderer::GetStringHeight(font) * scale;
                FontRenderer::DrawString(font, ch, cx - w * scale / 2.0f, cy - h / 2.0f,
                                         { 1.0f, 1.0f, 1.0f, std::clamp(alpha, 0.0f, 1.0f) }, scale);
            }
            x += w;
        }
    }
}

// 0x411540: a row of 2n+1 craters, with smoke and debris.
void Board::SpawnCraters(int x, int n) {
    for (int i = -n; i <= n; ++i) {
        double cx = x + i * 20;
        mCraters.push_back({ cx, 1000, Rand() % 5 });
        SpawnParticles(Img("smoke"), cx, 460.0, -1.0, -1.0, (int)(10.0 * mApp.detail), 1.0, -0.01, -0.5, 100, true, -1);
        SpawnParticles(Img("rock"), cx, 460.0, -1.0, -2.0, (int)(20.0 * mApp.detail), 1.0, 0.05, 3.0, 200, false, -1);
    }
}

// 0x417230: score plus a floating popup (none in survival, which scores by time).
void Board::AddScore(int points, int x, int y) {
    if (points == 0 || mSurvival) return;
    mApp.score = std::max(0, mApp.score + points);
    mPopups.push_back({ std::to_string(points), (double)x, (double)y, 255, points < 0, false, 0 });
}

// 0x411850: an aimed enemy energy shot.
void Board::FireEnemyShot(int x, int y, double angle, double speed) {
    if (mRespawn != 0) return;
    if (speed == 0.0) speed = 2.5;
    Bullet b;
    b.x = x;
    b.y = y;
    b.vx = -(std::cos(angle) * speed);
    b.vy = std::sin(angle) * speed;
    b.enemy = true;
    b.img = Img("bombfrag");
    mBullets.push_back(b);
    AudioSystem::PlaySoundId(SND_ENEMYFIRE, Pan(x));
}

void Board::AddHazard(std::unique_ptr<Hazard> h) {
    mNewHazards.push_back(std::move(h));
}


// -----------------------------------------------------------------------------
// Waves (0x41b340, 0x41a5b0, 0x40fed0)
// -----------------------------------------------------------------------------

void Board::UpdateSpawner() {
    if (mNukeFlash != 0.0) return;

    // Now and then a friendly helicopter brings a power-up.
    if (Rand() % (mSurvival ? 75 : 150) == 0 && mPupTimer == 0 && mFriendlies == 0 && mMegaTime == 0 && mPowerUps.empty()) {
        mPupTimer = mSurvival ? 300 : 1000;
        int type = PickPowerUp();
        if (type >= 0) mCrafts.push_back(CreatePupCopter(*this, type));
    }

    // Active waves spawn each of their craft types every length/qty ticks.
    for (auto& w : mWaves) {
        for (size_t i = 0; i < w.types.size(); ++i) {
            int every = w.qty[i] > 0 ? w.length / w.qty[i] : 0;
            if (every > 0 && w.counter > 0 && w.counter % every == 0) SpawnCraft(w.types[i]);
        }
        if (w.counter != 0) {
            --w.counter;
            mWaveDelay = 500;
        }
    }
    EraseIf(mWaves, [](const Wave& w) { return w.counter == 0; });

    if (mWaveDelay == 0) {
        // A random wave of the level; mission 1 always opens with its first wave.
        int index = 0;
        const LevelDef* src = WaveLevel();
        int count = src ? (int)src->waves.size() : 0;
        if (!(mApp.mission == 0 && mProgress < 2000) && count > 0) index = Rand() % count;
        StartWave(Tier(), index);
    } else {
        --mWaveDelay;
    }
    // With only friendly units left, the next wave comes sooner.
    if ((int)mCrafts.size() == mFriendlies && mWaveDelay > 100) mWaveDelay = 100;
}

void Board::StartWave(int tier, int index) {
    const LevelDef* src = WaveLevel();
    if (!src || index >= (int)src->waves.size()) return;
    (void)tier;
    const WaveDef& def = src->waves[index];
    Wave w;
    w.counter = w.length = def.length;
    for (const auto& e : def.craftList) {
        int type = 0;
        for (const auto& c : mCraftById) {
            if (c.name == e.craftId) type = c.id;
        }
        if (type == 0) continue;
        w.types.push_back(type);
        w.qty.push_back(e.quantity);
    }
    mWaves.push_back(w);
}

void Board::SpawnCraft(int type) {
    if (type == CRAFT_BIGMISSILE) {
        SpawnBigMissiles(*this);
        return;
    }
    // Only one ground unit per edge, and one dozer at a time.
    if ((type == CRAFT_TRUCK || type == CRAFT_ENEMYTANK) && (mBlockLeft || mBlockRight)) return;
    if (type == CRAFT_DOZER && mDozerPresent) return;
    auto c = CreateCraft(*this, type);
    if (c) mCrafts.push_back(std::move(c));
}

// Tank destroyed (0x41a880).
void Board::KillTank() {
    if (mRespawn != 0 || mGod) return;
    mRespawn = 400;
    ++mDeaths;
    mDeathX = (int)mTankX;
    SpawnExplosion(mTankX, mTankY - 5, 300, 200, 0.0, 0.0);
    SpawnParticles(Img("tankflame"), mTankX, mTankY, 0.0, -2.0, 60, 3.0, 0.03, 3.0, 200, true, -1);
    AudioSystem::PlaySoundId(SND_TANKEXPLODE, Pan(mTankX));
    SpawnCraters((int)mTankX, 1);

    mTankX = -380.0;
    mTankY = 443;
    mTread = 0.0;
    mGunCel = 0.0;
    for (int& c : mCooldown) c = 0;
    mApp.up[UP_NUKES] = 0;
    mApp.up[UP_SHIELD] = 1;
    mMegaParts = 0;
    mMegaTime = 0;
    mBeam = 0;
    mCrates.clear();
    mWaves.clear();
    for (auto& t : mTracks) t.active = false;
}

// -----------------------------------------------------------------------------
// Draw (0x415530)
// -----------------------------------------------------------------------------

void Board::Draw() {
    Gfx::Reset();
    Gfx::Translate(mShakeX, mShakeY);

    // Nuke flash tints the whole scene red.
    auto applyNukeTint = [this]() {
        if (mNukeFlash != 0.0) {
            int v = (int)(255.0 - mNukeFlash);
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(255, v, v, 255);
        }
    };
    applyNukeTint();

    Gfx::DrawSprite(WorldRenderer::Sky(), (int)mSkyX, 0);
    Gfx::DrawSprite(WorldRenderer::Sky(), (int)(mSkyX + 640.0f), 0);
    WorldRenderer::RenderAnims(4);

    // Ordnance launched from behind the scenery (big missiles rising).
    Gfx::Translate(320, 0);
    for (auto& h : mHazards) {
        if (h->behind) h->Draw();
    }
    Gfx::Translate(-320, 0);

    Gfx::DrawSprite(WorldRenderer::Bg2(), (int)mBg2X, 180);
    Gfx::DrawSprite(WorldRenderer::Bg2(), (int)mBg2X + 960, 180);
    WorldRenderer::RenderAnims(3);

    Gfx::Translate(320, 0);
    DrawMushrooms();
    Gfx::Translate(-320, 0);

    applyNukeTint();
    Gfx::DrawSprite(WorldRenderer::Bg(), (int)mBgX, 180);
    Gfx::DrawSprite(WorldRenderer::Bg(), (int)mBgX + 960, 180);
    WorldRenderer::RenderAnims(2);
    Gfx::SetColorizeImages(false);
    applyNukeTint();

    // Mile sign, every 5000 ticks.
    if (mMile < 0 && !mSurvival) {
        Gfx::DrawSprite(Img("milemarker"), mMile + 640, 350);
        int miles = (mLength - mProgress) / 5000;
        if (miles < 5 && miles >= 0) Gfx::DrawSprite(Img("miletext"), mMile + 650, 372, false, miles);
    }

    Gfx::DrawSprite(WorldRenderer::Ground(), (int)mGroundX, 420);
    Gfx::DrawSprite(WorldRenderer::Ground(), (int)(mGroundX + 640.0f), 420);
    WorldRenderer::RenderAnims(1);

    // Everything below is in world space.
    Gfx::Translate(320, 0);

    // The refuelling stop after the boss.
    static const char* kGasNames[4] = { "NUKE n GO", "GROUND ZERO GAS", "ROD'S FUEL RODS", "NED'S NUKES" };
    if (mGasX < 1000.0) {
        Gfx::DrawSprite(Img("gasstation"), (int)mGasX, 405, true);
        int w = (int)FontRenderer::GetStringWidth("StationFont", kGasNames[mGasName]);
        FontRenderer::DrawStringBaseline("StationFont", kGasNames[mGasName], (int)mGasX - w / 2 + Gfx::TransX(), 350, Color4f::White());
        Gfx::DrawSprite(Img("gassign"), (int)mGasX - 135, 350, true);
    }

    // Tread marks.
    if (Texture* tr = Img("tracks")) {
        for (const auto& t : mTracks) {
            int w = (int)(t.x1 - t.x0);
            if (w > 0 && w < 644) Gfx::DrawImageRect(tr, (int)t.x0, 455, w, 17, 0, 0, w, 17);
        }
    }

    // Craters fade over their last 512 ticks.
    Gfx::SetColorizeImages(true);
    for (const auto& c : mCraters) {
        int a = c.life < 0x200 ? c.life / 2 : 255;
        Gfx::SetColor(255, 255, 255, a);
        Gfx::DrawSprite(Img("crater"), (int)c.x, 460, true, c.frame);
    }
    Gfx::SetColorizeImages(false);

    // Ground glows from spent shells.
    Gfx::SetColorizeImages(true);
    Gfx::SetDrawMode(1);
    for (const auto& g : mGlows) {
        int a = std::min(g.alpha, 255);
        int a2 = std::max(a * 2 - 255, 0);
        if (g.type == 0) Gfx::SetColor(a, a2, a);
        else Gfx::SetColor(a2, a, a);
        Gfx::DrawSprite(Img("bulletglow"), (int)g.x, 460, true, g.type);
    }
    Gfx::SetDrawMode(0);
    Gfx::SetColorizeImages(false);

    for (auto& c : mCrafts) c->Draw();

    if (mRespawn == 0) DrawTank((int)mTankX, mTankY, (int)mTread);

    for (const auto& c : mCasings) {
        Gfx::DrawSprite(Img("casing"), (int)c.x, (int)c.y, true);
    }
    if (mGasX < 1000.0) {
        Gfx::DrawSprite(Img("gaspump"), (int)mGasX - 65, 455, true);
        Gfx::DrawSprite(Img("gaspump"), (int)mGasX + 55, 455, true);
    }

    // Shield hit flash.
    if (mShieldFlash > 0.0) {
        int v = std::clamp((int)(mShieldFlash * 28.3), 0, 255);
        Gfx::SetDrawMode(1);
        Gfx::SetColorizeImages(true);
        Gfx::SetColor(v, v, v, 255);
        Gfx::DrawSprite(Img("shieldzap"), (int)mTankX, mTankY + 2, true, (int)mShieldFlash);
        Gfx::SetColorizeImages(false);
        Gfx::SetDrawMode(0);
    }

    // Particles.
    for (const auto& p : mParticles) {
        if (p.fade) {
            int alpha = (int)(255.0 / ((double)p.img->cols / ((double)p.img->cols - p.frame)));
            Gfx::SetColorizeImages(true);
            Gfx::SetColor(255, 255, 255, std::clamp(alpha, 0, 255));
        }
        Gfx::DrawSprite(p.img, (int)p.x, (int)p.y, true, (int)p.frame);
        Gfx::SetColorizeImages(false);
    }

    // Explosions: 160x160 frames scaled to each explosion's size.
    Texture* explosion = Img("explosion");
    for (const auto& e : mExplosions) {
        int frame = (int)e.frame;
        Gfx::DrawImageRect(explosion, (int)(e.x - e.w / 2), (int)(e.y - e.h / 2), e.w, e.h, frame * 160, 0, 160, 160);
    }

    // Floating score popups (Keypunch16).
    for (const auto& p : mPopups) {
        float w = FontRenderer::GetStringWidth("Keypunch16", p.text);
        int px = (int)(p.x - w / 2);
        px = std::clamp(px, -320, 320 - (int)w);
        Color4f col = p.negative ? Color4f{ 1.0f, 0.0f, 0.0f, p.life / 255.0f } : Color4f{ 1.0f, 1.0f, 1.0f, p.life / 255.0f };
        if (p.rainbow) {
            // HSL(hue, 255, 192) as in the original's colour cycling (0x44d350).
            float h = p.hue / 256.0f * 6.0f, l = 192 / 255.0f, sat = 1.0f;
            float c = (1.0f - std::abs(2.0f * l - 1.0f)) * sat, xx = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
            float r = 0, g = 0, bl = 0;
            if (h < 1) { r = c; g = xx; } else if (h < 2) { r = xx; g = c; } else if (h < 3) { g = c; bl = xx; }
            else if (h < 4) { g = xx; bl = c; } else if (h < 5) { r = xx; bl = c; } else { r = c; bl = xx; }
            float m = l - c / 2.0f;
            col = { r + m, g + m, bl + m, p.life / 255.0f };
        }
        FontRenderer::DrawStringBaseline("Keypunch16", p.text, px + Gfx::TransX(), (int)(p.life / 2 - 128 + p.y), col);
    }

    for (auto& f : mFlak) f->Draw();
    for (const auto& c : mCrates) {
        Gfx::DrawSprite(Img("crates"), (int)c.x, (int)c.y, true, (int)c.frame);
    }
    for (const auto& p : mPowerUps) {
        Gfx::DrawSprite(Img("powerups"), (int)p.x, (int)p.y, true, p.type);
    }
    DrawBeams();

    for (auto& h : mHazards) {
        if (!h->behind) h->Draw();
    }

    // Shells and enemy bullets.
    for (const auto& b : mBullets) {
        Gfx::DrawSprite(b.img, (int)b.x, (int)b.y, true, b.col, b.row);
    }

    // Muzzle flashes (additive).
    Gfx::SetDrawMode(1);
    for (const auto& f : mFlashes) {
        Gfx::DrawSprite(Img("muzzleflash"), (int)f.x, (int)f.y, true, (int)(f.life - 1.0));
    }
    Gfx::SetDrawMode(0);

    // Full-screen nuke flash.
    if (mNukeFlash != 0.0) {
        Gfx::Translate(-320, 0);
        Gfx::SetDrawMode(1);
        Gfx::SetColor((int)mNukeFlash, (int)(mNukeFlash * 0.25), 0, 255);
        Gfx::FillRect(0, 0, 640, 480);
        Gfx::SetDrawMode(0);
        Gfx::Translate(320, 0);
    }

    // White flash.
    if (mShake != 0) {
        Gfx::Translate(-320, 0);
        Gfx::SetDrawMode(1);
        Gfx::SetColor(mShake, mShake, mShake, 255);
        Gfx::FillRect(0, 0, 640, 480);
        Gfx::SetDrawMode(0);
        Gfx::Translate(320, 0);
    }

    // Nuke stock, under the status bar; a blinking reminder when there are none.
    if (mApp.up[UP_NUKES] == 0) {
        if (mNoNukeMsg != 0 && mNoNukeMsg % 30 > 10) {
            FontRenderer::DrawStringBaseline("Outlined", "NO NUKES - COLLECT", -305 + Gfx::TransX(), 55, { 1.0f, 0.0f, 0.0f, 1.0f });
            Gfx::DrawSprite(Img("nukeicon"), -120, 50, true);
        }
    } else if (mProgress <= mLength || mSurvival) {
        for (int i = 0; i < mApp.up[UP_NUKES]; ++i) {
            Gfx::DrawSprite(Img("nukeicon"), -300 + i * 30, 50, true);
        }
    }

    Gfx::Translate(-320, 0);

    // Boss health bar.
    if (mBoss && !mBossBarHidden) {
        FontRenderer::DrawStringBaseline("Outlined", "BOSS", 215 - (int)FontRenderer::GetStringWidth("Outlined", "BOSS") - 4 + Gfx::TransX(), 50, Color4f::White());
        int w = std::max(0, (int)(200.0 / mBoss->maxHp * mBoss->hp));
        if (mBoss->hittable) {
            int r = (int)(std::cos((mApp.tick % 31) * 0.1) * 64.0 + 192.0);
            Gfx::SetColor(r, 0, 0, 255);
        } else {
            Gfx::SetColor(255, 255, 255, 0x50);
        }
        Gfx::FillRect(220, 37, w, 15);
        Gfx::SetColor(0, 0, 0, mBoss->hittable ? 255 : 0x50);
        Gfx::FillRect(220 + w, 37, 200 - w, 15);
    }

    DrawMessages();
    DrawHUD();
}

// The tank and its weapon attachments (0x401b80, 0x4011b0).
void Board::DrawTank(int x, int y, int tread) {
    const int gunCol = std::clamp((int)mGunCel, 0, 20);
    Gfx::DrawSprite(Img("tankshadow"), x, y + 22, true);
    Gfx::DrawSprite(Img("tank"), x, y, true, tread);
    Gfx::DrawSprite(Img("gun"), x - 40, y - 52, false, gunCol, mApp.up[UP_SPREAD]);
    if (mApp.noWeapons) return;

    if (mApp.up[UP_LASER] > 0) Gfx::DrawSprite(Img("lasers"), x - 40, y - 42, false, gunCol, mApp.up[UP_LASER] - 1);
    if (mApp.up[UP_FLAK] > 0) Gfx::DrawSprite(Img("flakguns"), x - 40, y - 52, false, gunCol, mApp.up[UP_FLAK] - 1);
    if (mApp.up[UP_HOMING] > 0) Gfx::DrawSprite(Img("homing"), x, y - 2, true, mApp.up[UP_HOMING] - 1);
    if (mApp.up[UP_STATIC] > 0) Gfx::DrawSprite(Img("staticstrike"), x, y + 4, true, 0, mApp.up[UP_STATIC] - 1);

    // Rocket pods fan out along the barrel.
    int n = mApp.up[UP_ROCKETS];
    if (n > 0) {
        double a = (mGunCel * 9.0 - 4.0) * kDegToRad;
        double side = a + 1.5707963268794;
        double bx = x - std::cos(a) * 12.0;
        double by = (y - 12) - std::sin(a) * 12.0;
        for (int i = 0; i < n; ++i) {
            int off = (90 / n) * i;
            if (n > 1) off += (n == 3 ? -8 : 0) - 22;
            Gfx::DrawSprite(Img("rocketpod"), (int)(bx - std::cos(side) * off), (int)(by - std::sin(side) * off), true, gunCol);
        }
    }

    // Defense orbs.
    for (int i = 0; i < mApp.up[UP_ORBS] && i < 3; ++i) {
        Gfx::DrawSprite(Img("orb"), (int)mOrbX[i], (int)mOrbY[i], true);
        if (mOrbFlash[i] > 0.0) {
            int v = (int)(mOrbFlash[i] * 255.0);
            Gfx::SetColorizeImages(true);
            Gfx::SetDrawMode(1);
            Gfx::SetColor(v, v, v);
            Gfx::DrawSprite(Img("orb"), (int)mOrbX[i], (int)mOrbY[i], true, 1);
            Gfx::SetDrawMode(0);
            Gfx::SetColorizeImages(false);
        }
    }

    // Shield glow: orange / yellow / green for 1 / 2 / 3 hits.
    int shield = mApp.up[UP_SHIELD];
    if (shield >= 1 && shield <= 3) {
        static const int kShieldColor[3][3] = { { 255, 64, 0 }, { 255, 255, 0 }, { 0, 255, 0 } };
        Gfx::SetColorizeImages(true);
        Gfx::SetColor(kShieldColor[shield - 1][0], kShieldColor[shield - 1][1], kShieldColor[shield - 1][2], 128);
        Gfx::SetDrawMode(1);
        Gfx::DrawSprite(Img("shield"), x, y + 2, true, (mApp.tick % 100) / 10);
        Gfx::SetDrawMode(0);
        Gfx::SetColorizeImages(false);
    }
}

// Status bar (0x42e220).
void Board::DrawHUD() {
    Gfx::DrawSprite(Img("statusbar"), 0, 0);

    const Color4f green = { 0.0f, 1.0f, 0.0f, 1.0f };
    if (!mSurvival) {
        int icons = std::min(mApp.lives, 2);
        for (int i = 0; i < icons; ++i) {
            Gfx::DrawSprite(Img("tankicon"), 117 + i * 22, 7);
        }
    } else {
        Gfx::DrawSprite(Img("statuscovers"), 108, 2);
    }
    FontRenderer::DrawStringBaseline("Computer", std::to_string(mApp.score), 10, 20, green);

    // Megalaser: collected parts, or the draining meter while it is active.
    if (mMegaTime == 0) {
        for (int i = 0; i < mMegaParts; ++i) Gfx::DrawSprite(Img("megalaser"), 438 + i * 20, 7, false, i);
    } else if (Texture* meter = Img("megameter")) {
        int w = (int)(mMegaTime * 0.029333333333333333);
        int pulse = (mMegaTime * 5 & 0x7f) * 4;
        int g = pulse < 0x100 ? 0xff - (pulse & 0xff) : (pulse & 0xff);
        Gfx::SetColorizeImages(true);
        Gfx::SetColor(255, g, 0);
        Gfx::DrawImageRect(meter, 436, 6, w, 18, 0, 0, w, 18);
        Gfx::SetColorizeImages(false);
    }

    // Level progress on the theme's mini-map.
    if (!mSurvival) {
        Gfx::DrawSprite(WorldRenderer::MiniMap(), 196, 6);
        double frac = mLength > 0 ? (double)std::min(mProgress, mLength) / mLength : 0.0;
        Gfx::DrawSprite(Img("mappointer"), (int)(119.0 * frac + 205.0), 11, true);
    } else {
        int secs = SurvivalSeconds();
        char buf[32];
        snprintf(buf, sizeof(buf), "SURVIVAL  %d:%02d", secs / 60, secs % 60);
        FontRenderer::DrawStringBaseline("Computer", buf, 209, 20, green);
    }
}

} // namespace HeavyWeapon
