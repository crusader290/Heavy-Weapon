// Mod support: discovery, asset overrides, the sandboxed Lua state and the `hw` API.
// The API reference for mod authors is docs/MODDING.md; keep the two in sync.

#include "ModSystem.h"
#include "AudioSystem.h"
#include "Constants.h"
#include "FontRenderer.h"
#include "InputManager.h"
#include "TextureManager.h"
#include "Vfs.h"
#include "game/Board.h"
#include "game/Enemies.h"
#include "game/Gfx.h"
#include "game/PowerUps.h"
#include "game/Weapons.h"
#include <SDL3/SDL.h>
#include <lua.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>

namespace HeavyWeapon {

// Board internals the API needs (friend of Board).
class ModBridge {
public:
    static AppState& App(Board& b) { return b.mApp; }
    static void ShowMessage(Board& b, const std::string& t) { b.ShowMessage(t); }
    static void FireShell(Board& b, double power, int side, int angle) { b.FireShell(power, side, angle); }
    static double GunCel(Board& b) { return b.mGunCel; }
    static int& MegaTime(Board& b) { return b.mMegaTime; }
    static int& Invulnerable(Board& b) { return b.mModInvuln; }
    static int Respawn(Board& b) { return b.mRespawn; }
    static bool Survival(Board& b) { return b.mSurvival; }
    static double Progress(Board& b) { return b.mLength > 0 ? std::min(1.0, (double)b.mProgress / b.mLength) : 0.0; }
};

namespace {

// ---- state ----------------------------------------------------------------------------

struct Callback {
    int ref = LUA_NOREF;
    std::string mod;
};

struct PickupDef {
    std::string id, mod, message, image, sound;
    int cols = 0, frame = 0;   // cols 0: the image's own cel layout
    double chance = 1.0;
    int tier = 0;                 // drops from this tier (mission index / survival tier) on
    int max = -1;                 // per run; -1 = unlimited
    int collected = 0;
    bool campaign = true, survival = true;
    Callback onPickup;
    Texture* tex = nullptr;
};

struct AbilityDef {
    std::string id, mod, name, keyLabel;
    std::vector<SDL_Scancode> keys;
    std::vector<SDL_GamepadButton> buttons;
    std::vector<SDL_GamepadAxis> axes;   // triggers
    int startCharges = -1;               // -1 = unlimited
    int charges = -1;
    int maxCharges = -1;
    int cooldown = 0, cooldownLeft = 0;  // ticks (100 per second)
    bool wasDown = false;
    bool hud = true;
    Callback onUse;
};

struct Hook {
    std::string event;
    Callback cb;
};

struct Timer {
    int ticks;
    Callback cb;
};

lua_State* L = nullptr;
std::vector<std::string> sLoaded;
std::vector<PickupDef> sPickups;
std::vector<AbilityDef> sAbilities;
std::vector<Hook> sHooks;
std::vector<Timer> sTimers;
std::string sCurrentMod;     // mod whose code is running (owner of callbacks it registers)
Board* sBoard = nullptr;
int sErrors = 0;
int sBudget = 0;
FILE* sLog = nullptr;

const char* const kSlotNames[UP_COUNT] = {
    "nukes", "shield", "speed", "power", "rapid", "spread",
    "orbs", "homing", "laser", "rockets", "flak", "thunderstrike",
};

void Log(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "[Mods] %s\n", buf);
    if (!sLog) sLog = std::fopen("mods.log", "w");
    if (sLog) {
        std::fprintf(sLog, "%s\n", buf);
        std::fflush(sLog);
    }
}

int SlotIndex(const std::string& name) {
    for (int i = 0; i < UP_COUNT; ++i) {
        if (name == kSlotNames[i]) return i;
    }
    return -1;
}

// ---- Lua plumbing ------------------------------------------------------------------------

// Instruction budget: the count hook fires every 100k instructions; 200 of them (20M
// instructions) in one call means a runaway script.
void CountHook(lua_State* l, lua_Debug*) {
    if (++sBudget > 200) luaL_error(l, "script ran too long (endless loop?)");
}

int Traceback(lua_State* l) {
    const char* msg = lua_tostring(l, 1);
    luaL_traceback(l, l, msg ? msg : "(error object is not a string)", 1);
    return 1;
}

// Calls the callback with the `nargs` values on top of the stack; leaves `nres` results.
bool Call(const Callback& cb, int nargs, int nres) {
    if (!L) return false;
    if (cb.ref == LUA_NOREF) {
        lua_pop(L, nargs);
        return false;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, cb.ref);
    lua_insert(L, -(nargs + 1));
    lua_pushcfunction(L, Traceback);
    lua_insert(L, -(nargs + 2));
    const int errIdx = lua_gettop(L) - nargs - 1;
    const std::string prevMod = sCurrentMod;
    const int prevBudget = sBudget;
    sCurrentMod = cb.mod;
    sBudget = 0;
    const int status = lua_pcall(L, nargs, nres, errIdx);
    sCurrentMod = prevMod;
    sBudget = prevBudget;
    if (status != LUA_OK) {
        ++sErrors;
        Log("%s: %s", cb.mod.c_str(), lua_tostring(L, -1));
        lua_pop(L, 2);   // message + traceback function
        return false;
    }
    lua_remove(L, errIdx);
    return true;
}

Callback MakeCallback(lua_State* l, int idx) {
    Callback cb;
    if (lua_isfunction(l, idx)) {
        lua_pushvalue(l, idx);
        cb.ref = luaL_ref(l, LUA_REGISTRYINDEX);
        cb.mod = sCurrentMod;
    }
    return cb;
}

void Release(Callback& cb) {
    if (L && cb.ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, cb.ref);
    cb.ref = LUA_NOREF;
}

std::string FieldStr(lua_State* l, int t, const char* key, const std::string& def = std::string()) {
    lua_getfield(l, t, key);
    std::string v = lua_isstring(l, -1) ? lua_tostring(l, -1) : def;
    lua_pop(l, 1);
    return v;
}

double FieldNum(lua_State* l, int t, const char* key, double def) {
    lua_getfield(l, t, key);
    double v = lua_isnumber(l, -1) ? lua_tonumber(l, -1) : def;
    lua_pop(l, 1);
    return v;
}

bool FieldBool(lua_State* l, int t, const char* key, bool def) {
    lua_getfield(l, t, key);
    bool v = lua_isboolean(l, -1) ? lua_toboolean(l, -1) != 0 : def;
    lua_pop(l, 1);
    return v;
}

Callback FieldFunc(lua_State* l, int t, const char* key) {
    lua_getfield(l, t, key);
    Callback cb = MakeCallback(l, -1);
    lua_pop(l, 1);
    return cb;
}

// A string or a list of strings.
std::vector<std::string> FieldStrings(lua_State* l, int t, const char* key) {
    std::vector<std::string> out;
    lua_getfield(l, t, key);
    if (lua_isstring(l, -1)) {
        out.push_back(lua_tostring(l, -1));
    } else if (lua_istable(l, -1)) {
        const lua_Integer n = luaL_len(l, -1);
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(l, -1, i);
            if (lua_isstring(l, -1)) out.push_back(lua_tostring(l, -1));
            lua_pop(l, 1);
        }
    }
    lua_pop(l, 1);
    return out;
}

// A mod or game image; `cols` > 0 sets its cel layout (a strip of `cols` frames),
// otherwise the image keeps its own (game images such as "powerups" have one).
Texture* ModImage(const std::string& image, int cols) {
    return cols > 0 ? TextureManager::LoadGrid(image, cols, 1) : TextureManager::Get(image);
}

Board& NeedBoard(lua_State* l) {
    if (!sBoard) luaL_error(l, "only available during a mission");
    return *sBoard;
}

// ---- enemies ------------------------------------------------------------------------------

void PushEnemy(lua_State* l, const Craft& c) {
    lua_createtable(l, 0, 12);
    lua_pushnumber(l, c.x); lua_setfield(l, -2, "x");
    lua_pushnumber(l, c.y); lua_setfield(l, -2, "y");
    lua_pushnumber(l, c.vx); lua_setfield(l, -2, "vx");
    lua_pushnumber(l, c.vy); lua_setfield(l, -2, "vy");
    lua_pushnumber(l, c.hp); lua_setfield(l, -2, "hp");
    lua_pushnumber(l, c.maxHp); lua_setfield(l, -2, "max_hp");
    lua_pushinteger(l, c.type); lua_setfield(l, -2, "type");
    lua_pushinteger(l, c.W()); lua_setfield(l, -2, "w");
    lua_pushinteger(l, c.H()); lua_setfield(l, -2, "h");
    lua_pushboolean(l, c.boss); lua_setfield(l, -2, "boss");
    lua_pushboolean(l, c.friendly); lua_setfield(l, -2, "friendly");
    lua_pushboolean(l, c.hittable); lua_setfield(l, -2, "hittable");
    lua_pushlightuserdata(l, const_cast<Craft*>(&c)); lua_setfield(l, -2, "handle");
}

// The live craft behind an enemy table, or nullptr if it is gone.
Craft* EnemyFromArg(lua_State* l, int idx) {
    luaL_checktype(l, idx, LUA_TTABLE);
    lua_getfield(l, idx, "handle");
    void* p = lua_touserdata(l, -1);
    lua_pop(l, 1);
    if (!p || !sBoard) return nullptr;
    for (auto& c : sBoard->Crafts()) {
        if (c.get() == p) return c->dead ? nullptr : c.get();
    }
    return nullptr;
}

// Player damage on a craft, like the shell / weapon hit blocks: flash, sparks, or a kill.
void DamageCraft(Board& b, Craft& c, double amount) {
    if (c.dead || !c.hittable) return;
    c.flash = 0xff;
    c.hp -= amount;
    if (c.hp >= 0.0) {
        AudioSystem::PlaySoundId(SND_SMALLEXPLODE, b.Pan(c.x));
        b.SpawnParticles(TextureManager::Get("spark"), c.x, c.y, c.vx, c.vy,
                         (int)(10.0 * b.App().detail), 2.0, 0.05, 3.0, 30, true, -1);
    } else {
        c.Die();
        c.Remove();
    }
}

// ---- projectiles --------------------------------------------------------------------------

class ModProjectile : public Hazard {
public:
    ModProjectile(Board& board, const std::string& image, int cols, double x, double y, double vx, double vy)
        : Hazard(board, image.c_str(), x, y, vx, vy, false) {
        img = ModImage(image, cols);
        mCols = img ? std::max(1, img->cols) : 1;
        hittable = false;   // enemy fire does not collide with it
    }
    ~ModProjectile() override { Release(onHit); }

    void Update() override {
        if (life > 0 && --life == 0) {
            Remove();
            return;
        }
        if (homing > 0.0) Steer();
        vy += gravity;
        x += vx;
        y += vy;
        if (mCols > 1 && anim > 0.0) {
            frame += anim;
            if (frame >= mCols) frame -= mCols;
        }
        if (x < -400.0 || x > 400.0 || y < -150.0 || y > 500.0) {
            Remove();
            return;
        }
        // Collect first: a kill can add craft (debris, parts) to the list.
        std::vector<Craft*> hits;
        for (auto& c : b.Crafts()) {
            if (c->dead || !c->hittable || c->friendly) continue;
            if (pierce && std::find(mHit.begin(), mHit.end(), c.get()) != mHit.end()) continue;
            if (!Board::PixelHit(img, (int)x, (int)y, (int)frame, 0, false,
                                 c->img, (int)c->x, (int)c->y, (int)c->frame, 0, c->mirror)) continue;
            hits.push_back(c.get());
            if (!pierce) break;
        }
        for (Craft* c : hits) {
            if (onHit.ref != LUA_NOREF) {
                PushEnemy(L, *c);
                lua_pushnumber(L, x);
                lua_pushnumber(L, y);
                Call(onHit, 3, 0);
            }
            if (!c->dead) DamageCraft(b, *c, damage);
            mHit.push_back(c);
        }
        if (!hits.empty() && !pierce) Remove();
    }

    void Draw() override {
        if (glow) Gfx::SetDrawMode(1);
        Gfx::DrawSprite(img, (int)x, (int)y, true, (int)frame, 0, vx < 0.0 && flipLeft);
        if (glow) Gfx::SetDrawMode(0);
    }

    double gravity = 0.0, homing = 0.0, damage = 1.0, anim = 0.0;
    int life = 0;
    bool pierce = false, glow = false, flipLeft = false;
    Callback onHit;

private:
    void Steer() {
        Craft* best = nullptr;
        double bestD = 1e18;
        for (auto& c : b.Crafts()) {
            if (c->dead || !c->hittable || c->friendly) continue;
            double d = (c->x - x) * (c->x - x) + (c->y - y) * (c->y - y);
            if (d < bestD) { bestD = d; best = c.get(); }
        }
        if (!best) return;
        const double speed = std::sqrt(vx * vx + vy * vy);
        if (speed <= 0.0) return;
        const double cur = std::atan2(vy, vx);
        const double want = std::atan2(best->y - y, best->x - x);
        double diff = std::remainder(want - cur, 2.0 * 3.14159265358979);
        diff = std::clamp(diff, -homing, homing);
        vx = std::cos(cur + diff) * speed;
        vy = std::sin(cur + diff) * speed;
    }

    int mCols = 1;
    std::vector<Craft*> mHit;
};

// ---- hw.* : definitions -------------------------------------------------------------------

int L_pickup(lua_State* l) {
    luaL_checktype(l, 1, LUA_TTABLE);
    PickupDef d;
    d.mod = sCurrentMod;
    d.id = FieldStr(l, 1, "id");
    if (d.id.empty()) return luaL_error(l, "hw.pickup: `id` is required");
    for (const auto& p : sPickups) {
        if (p.id == d.id) return luaL_error(l, "hw.pickup: id '%s' is already used (by %s)", d.id.c_str(), p.mod.c_str());
    }
    d.image = FieldStr(l, 1, "image", "powerups");
    d.cols = (int)FieldNum(l, 1, "cols", 0);
    d.frame = (int)FieldNum(l, 1, "frame", 0);
    d.message = FieldStr(l, 1, "message");
    d.sound = FieldStr(l, 1, "sound");
    d.chance = std::max(0.0, FieldNum(l, 1, "chance", 1.0));
    d.tier = (int)FieldNum(l, 1, "tier", 0);
    d.max = (int)FieldNum(l, 1, "max", -1);
    d.campaign = FieldBool(l, 1, "campaign", true);
    d.survival = FieldBool(l, 1, "survival", true);
    d.onPickup = FieldFunc(l, 1, "on_pickup");
    sPickups.push_back(d);
    return 0;
}

int L_ability(lua_State* l) {
    luaL_checktype(l, 1, LUA_TTABLE);
    AbilityDef a;
    a.mod = sCurrentMod;
    a.id = FieldStr(l, 1, "id");
    if (a.id.empty()) return luaL_error(l, "hw.ability: `id` is required");
    for (const auto& x : sAbilities) {
        if (x.id == a.id) return luaL_error(l, "hw.ability: id '%s' is already used (by %s)", a.id.c_str(), x.mod.c_str());
    }
    a.name = FieldStr(l, 1, "name", a.id);
    std::string label;
    for (const std::string& k : FieldStrings(l, 1, "key")) {
        SDL_Scancode sc = SDL_GetScancodeFromName(k.c_str());
        if (sc == SDL_SCANCODE_UNKNOWN) return luaL_error(l, "hw.ability '%s': unknown key '%s'", a.id.c_str(), k.c_str());
        a.keys.push_back(sc);
        label += (label.empty() ? "" : " / ") + k;
    }
    for (const std::string& btn : FieldStrings(l, 1, "button")) {
        SDL_GamepadButton gb = SDL_GetGamepadButtonFromString(btn.c_str());
        if (gb != SDL_GAMEPAD_BUTTON_INVALID) {
            a.buttons.push_back(gb);
        } else {
            SDL_GamepadAxis ax = SDL_GetGamepadAxisFromString(btn.c_str());
            if (ax != SDL_GAMEPAD_AXIS_LEFT_TRIGGER && ax != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                return luaL_error(l, "hw.ability '%s': unknown gamepad button '%s'", a.id.c_str(), btn.c_str());
            }
            a.axes.push_back(ax);
        }
        label += (label.empty() ? "" : " / ") + btn;
    }
    for (char& ch : label) ch = (char)std::toupper((unsigned char)ch);
    a.keyLabel = label;
    a.startCharges = a.charges = (int)FieldNum(l, 1, "charges", -1);
    a.maxCharges = (int)FieldNum(l, 1, "max_charges", -1);
    a.cooldown = std::max(0, (int)FieldNum(l, 1, "cooldown", 0));
    a.hud = FieldBool(l, 1, "hud", true);
    a.onUse = FieldFunc(l, 1, "on_use");
    if (a.onUse.ref == LUA_NOREF) return luaL_error(l, "hw.ability '%s': `on_use` function is required", a.id.c_str());
    sAbilities.push_back(a);
    return 0;
}

int L_tweak_powerup(lua_State* l) {
    luaL_checktype(l, 1, LUA_TTABLE);
    std::string which = FieldStr(l, 1, "which");
    int i = SlotIndex(which);
    if (i < 0 || i > UP_SPREAD) return luaL_error(l, "hw.tweak_powerup: `which` must be nukes, shield, speed, power, rapid or spread");
    PowerUpInfo& p = OriginalPowerUp(i);
    p.max = (int)FieldNum(l, 1, "max", p.max);
    p.weight = FieldNum(l, 1, "unlock", p.weight);
    p.chance = std::max(0.0, FieldNum(l, 1, "chance", p.chance));
    p.message = FieldStr(l, 1, "message", p.message);
    return 0;
}

int L_on(lua_State* l) {
    const char* ev = luaL_checkstring(l, 1);
    luaL_checktype(l, 2, LUA_TFUNCTION);
    static const char* const kEvents[] = { "level_start", "tick", "enemy_killed", "tank_destroyed", "pickup" };
    bool known = false;
    for (const char* e : kEvents) known |= std::string(e) == ev;
    if (!known) return luaL_error(l, "hw.on: unknown event '%s'", ev);
    sHooks.push_back({ ev, MakeCallback(l, 2) });
    return 0;
}

int L_after(lua_State* l) {
    int ticks = (int)luaL_checkinteger(l, 1);
    luaL_checktype(l, 2, LUA_TFUNCTION);
    sTimers.push_back({ std::max(1, ticks), MakeCallback(l, 2) });
    return 0;
}

int L_log(lua_State* l) {
    std::string out;
    const int n = lua_gettop(l);
    for (int i = 1; i <= n; ++i) {
        if (i > 1) out += "\t";
        out += luaL_tolstring(l, i, nullptr);
        lua_pop(l, 1);
    }
    Log("%s: %s", sCurrentMod.c_str(), out.c_str());
    return 0;
}

// ---- hw.* : game state ----------------------------------------------------------------------

int L_message(lua_State* l) {
    Board& b = NeedBoard(l);
    const char* text = luaL_checkstring(l, 1);
    int x = (int)luaL_optnumber(l, 2, b.TankX());
    int y = (int)luaL_optnumber(l, 3, b.TankY() - 13);
    b.AddMessage(text, x, y, true, lua_toboolean(l, 4) != 0);
    return 0;
}

int L_big_message(lua_State* l) {
    ModBridge::ShowMessage(NeedBoard(l), luaL_checkstring(l, 1));
    return 0;
}

int L_sound(lua_State* l) {
    int pan = sBoard ? sBoard->Pan(luaL_optnumber(l, 2, 0.0)) : 0;
    if (lua_isinteger(l, 1)) {
        AudioSystem::PlaySoundId((int)lua_tointeger(l, 1), pan, (float)luaL_optnumber(l, 3, -1.0));
    } else {
        AudioSystem::PlaySound(luaL_checkstring(l, 1), (float)luaL_optnumber(l, 3, 1.0));
    }
    return 0;
}

int L_tank(lua_State* l) {
    Board& b = NeedBoard(l);
    lua_pushnumber(l, b.TankX());
    lua_pushnumber(l, b.TankY());
    lua_pushboolean(l, ModBridge::Respawn(b) == 0);
    return 3;
}

int L_lives(lua_State* l) {
    AppState& app = ModBridge::App(NeedBoard(l));
    if (lua_gettop(l) >= 1) app.lives = std::max(0, (int)luaL_checkinteger(l, 1));
    lua_pushinteger(l, app.lives);
    return 1;
}

int L_score(lua_State* l) {
    Board& b = NeedBoard(l);
    if (lua_gettop(l) >= 1) {
        int pts = (int)luaL_checkinteger(l, 1);
        b.AddScore(pts, (int)luaL_optnumber(l, 2, b.TankX()), (int)luaL_optnumber(l, 3, b.TankY() - 30));
    }
    lua_pushinteger(l, ModBridge::App(b).score);
    return 1;
}

int L_upgrade(lua_State* l) {
    AppState& app = ModBridge::App(NeedBoard(l));
    std::string name = luaL_checkstring(l, 1);
    int i = SlotIndex(name);
    if (i < 0) return luaL_error(l, "hw.upgrade: unknown slot '%s'", name.c_str());
    if (lua_gettop(l) >= 2) app.up[i] = std::max(0, (int)luaL_checkinteger(l, 2));
    lua_pushinteger(l, app.up[i]);
    return 1;
}

int L_mission(lua_State* l) {
    Board& b = NeedBoard(l);
    lua_pushinteger(l, ModBridge::App(b).mission);
    lua_pushboolean(l, ModBridge::Survival(b));
    lua_pushnumber(l, ModBridge::Progress(b));
    lua_pushinteger(l, b.Tier());
    return 4;
}

int L_fire(lua_State* l) {
    Board& b = NeedBoard(l);
    std::string kind = luaL_checkstring(l, 1);
    if (kind == "shell") {
        ModBridge::FireShell(b, luaL_optnumber(l, 2, 1.0), (int)luaL_optinteger(l, 3, 0), (int)luaL_optinteger(l, 4, 0));
    } else if (kind == "homing") {
        FireHomingMissile(b);
    } else if (kind == "rocket") {
        FireRocket(b, ModBridge::GunCel(b), (int)luaL_optinteger(l, 2, 1));
    } else if (kind == "thunderstrike") {
        FireStatic(b);
    } else if (kind == "nuke") {
        b.FireNuke();
    } else {
        return luaL_error(l, "hw.fire: unknown weapon '%s' (shell, homing, rocket, thunderstrike, nuke)", kind.c_str());
    }
    return 0;
}

int L_megalaser(lua_State* l) {
    ModBridge::MegaTime(NeedBoard(l)) = std::max(0, (int)luaL_checkinteger(l, 1));
    return 0;
}

int L_invulnerable(lua_State* l) {
    int& t = ModBridge::Invulnerable(NeedBoard(l));
    t = std::max(t, (int)luaL_checkinteger(l, 1));
    return 0;
}

int L_enemies(lua_State* l) {
    Board& b = NeedBoard(l);
    lua_newtable(l);
    int n = 0;
    for (auto& c : b.Crafts()) {
        if (c->dead) continue;
        PushEnemy(l, *c);
        lua_rawseti(l, -2, ++n);
    }
    return 1;
}

int L_damage(lua_State* l) {
    Board& b = NeedBoard(l);
    Craft* c = EnemyFromArg(l, 1);
    if (c) DamageCraft(b, *c, luaL_checknumber(l, 2));
    lua_pushboolean(l, c != nullptr);
    return 1;
}

int L_damage_area(lua_State* l) {
    Board& b = NeedBoard(l);
    double x = luaL_checknumber(l, 1), y = luaL_checknumber(l, 2), r = luaL_checknumber(l, 3);
    double amount = luaL_checknumber(l, 4);
    bool friendlyToo = lua_toboolean(l, 5) != 0;
    std::vector<Craft*> targets;
    for (auto& c : b.Crafts()) {
        if (c->dead || !c->hittable || (c->friendly && !friendlyToo)) continue;
        if ((c->x - x) * (c->x - x) + (c->y - y) * (c->y - y) <= r * r) targets.push_back(c.get());
    }
    for (Craft* c : targets) DamageCraft(b, *c, amount);   // Die() may push new crafts
    lua_pushinteger(l, (lua_Integer)targets.size());
    return 1;
}

int L_explosion(lua_State* l) {
    Board& b = NeedBoard(l);
    double x = luaL_checknumber(l, 1), y = luaL_checknumber(l, 2);
    int size = (int)luaL_optinteger(l, 3, 80);
    b.SpawnExplosion(x, y, size, size, 0.0, 0.0);
    if (lua_toboolean(l, 4) || lua_isnone(l, 4)) {
        AudioSystem::PlaySoundId(size >= 120 ? SND_BIGEXPLODE : SND_SMALLEXPLODE, b.Pan(x));
    }
    return 0;
}

int L_drop(lua_State* l) {
    Board& b = NeedBoard(l);
    std::string name = luaL_checkstring(l, 1);
    int type = SlotIndex(name);
    if (type < 0) {
        for (size_t i = 0; i < sPickups.size(); ++i) {
            if (sPickups[i].id == name) type = Mods::kModPickupBase + (int)i;
        }
    }
    if (type < 0) return luaL_error(l, "hw.drop: unknown pickup '%s'", name.c_str());
    b.DropPowerUp(luaL_optnumber(l, 2, b.TankX()), luaL_optnumber(l, 3, 100.0),
                  luaL_optnumber(l, 4, 0.0), luaL_optnumber(l, 5, 0.0), type);
    return 0;
}

int L_projectile(lua_State* l) {
    Board& b = NeedBoard(l);
    luaL_checktype(l, 1, LUA_TTABLE);
    std::string image = FieldStr(l, 1, "image");
    if (image.empty()) return luaL_error(l, "hw.projectile: `image` is required");
    double tx = b.TankX(), ty = b.TankY() - 20.0;
    auto p = std::make_unique<ModProjectile>(b, image, (int)FieldNum(l, 1, "cols", 0),
                                             FieldNum(l, 1, "x", tx), FieldNum(l, 1, "y", ty),
                                             FieldNum(l, 1, "vx", 0.0), FieldNum(l, 1, "vy", -6.0));
    p->gravity = FieldNum(l, 1, "gravity", 0.0);
    p->homing = FieldNum(l, 1, "homing", 0.0);
    p->damage = FieldNum(l, 1, "damage", 1.0);
    p->anim = FieldNum(l, 1, "anim", 0.0);
    p->frame = FieldNum(l, 1, "frame", 0.0);
    p->life = (int)FieldNum(l, 1, "life", 0.0);
    p->pierce = FieldBool(l, 1, "pierce", false);
    p->glow = FieldBool(l, 1, "glow", false);
    p->flipLeft = FieldBool(l, 1, "flip", false);
    p->onHit = FieldFunc(l, 1, "on_hit");
    b.AddHazard(std::move(p));
    return 0;
}

int L_charges(lua_State* l) {
    std::string id = luaL_checkstring(l, 1);
    for (AbilityDef& a : sAbilities) {
        if (a.id != id) continue;
        if (lua_gettop(l) >= 2) {
            a.charges = (int)luaL_checkinteger(l, 2);
            if (a.maxCharges >= 0) a.charges = std::min(a.charges, a.maxCharges);
        }
        lua_pushinteger(l, a.charges);
        return 1;
    }
    return luaL_error(l, "hw.charges: unknown ability '%s'", id.c_str());
}

int L_give(lua_State* l) {
    std::string id = luaL_checkstring(l, 1);
    int n = (int)luaL_optinteger(l, 2, 1);
    for (AbilityDef& a : sAbilities) {
        if (a.id != id) continue;
        if (a.charges >= 0) {
            a.charges += n;
            if (a.maxCharges >= 0) a.charges = std::min(a.charges, a.maxCharges);
        }
        lua_pushinteger(l, a.charges);
        return 1;
    }
    return luaL_error(l, "hw.give: unknown ability '%s'", id.c_str());
}

const luaL_Reg kApi[] = {
    { "pickup", L_pickup }, { "ability", L_ability }, { "tweak_powerup", L_tweak_powerup },
    { "on", L_on }, { "after", L_after }, { "log", L_log },
    { "message", L_message }, { "big_message", L_big_message }, { "sound", L_sound },
    { "tank", L_tank }, { "lives", L_lives }, { "score", L_score }, { "upgrade", L_upgrade },
    { "mission", L_mission }, { "fire", L_fire }, { "megalaser", L_megalaser },
    { "invulnerable", L_invulnerable }, { "enemies", L_enemies }, { "damage", L_damage },
    { "damage_area", L_damage_area }, { "explosion", L_explosion }, { "drop", L_drop },
    { "projectile", L_projectile }, { "charges", L_charges }, { "give", L_give },
    { nullptr, nullptr },
};

void OpenSandbox() {
    L = luaL_newstate();
    // Only pure libraries: no io, os, package/require or debug.
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_pop(L, 6);
    for (const char* name : { "dofile", "loadfile", "load", "require" }) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_pushcfunction(L, L_log);
    lua_setglobal(L, "print");

    luaL_newlib(L, kApi);
    // Sound ids of the original table, e.g. hw.SND.powerup.
    lua_newtable(L);
    static const struct { const char* name; int id; } kSounds[] = {
        { "powerup", SND_POWERUP }, { "gunpowerup", SND_GUNPOWERUP }, { "shieldup", SND_SHIELDUP },
        { "speedup", SND_SPEEDUP }, { "upgrade", SND_UPGRADE }, { "denied", SND_DENIED },
        { "bigexplode", SND_BIGEXPLODE }, { "smallexplode", SND_SMALLEXPLODE }, { "laser", SND_LASER },
        { "missile", SND_MISSILE }, { "nukeblast", SND_NUKEBLAST }, { "thunder", SND_THUNDER },
        { "alert", SND_ALERT }, { "star", SND_STAR }, { "tankfire", SND_TANKFIRE },
    };
    for (const auto& s : kSounds) {
        lua_pushinteger(L, s.id);
        lua_setfield(L, -2, s.name);
    }
    lua_setfield(L, -2, "SND");
    lua_pushstring(L, "1");
    lua_setfield(L, -2, "API_VERSION");
    lua_setglobal(L, "hw");

    lua_sethook(L, CountHook, LUA_MASKCOUNT, 100000);
}

bool RunModScript(const std::string& name, const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string code = ss.str();
    sCurrentMod = name;
    const std::string chunk = "@mods/" + name + "/mod.lua";
    if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK) {
        ++sErrors;
        Log("%s: %s", name.c_str(), lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    // Each mod gets its own globals (reads fall back to the shared ones).
    lua_newtable(L);
    lua_newtable(L);
    lua_pushglobaltable(L);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_pushstring(L, name.c_str());
    lua_setfield(L, -2, "MOD_NAME");
    lua_setupvalue(L, -2, 1);
    Callback cb;
    cb.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    cb.mod = name;
    bool ok = Call(cb, 0, 0);
    Release(cb);
    sCurrentMod.clear();
    return ok;
}

void Emit(const char* event, const std::function<int()>& pushArgs) {
    for (size_t i = 0; i < sHooks.size(); ++i) {
        if (sHooks[i].event != event) continue;
        Callback cb = sHooks[i].cb;   // hooks may be added during the call
        int n = pushArgs ? pushArgs() : 0;
        Call(cb, n, 0);
    }
}

} // namespace

// PowerUpInfo table lives in Supplies.cpp; Board pickups call into Mods below.

namespace Mods {

void Init(bool enabled) {
    if (!enabled || L) return;
    if (!Vfs::Exists("mods")) return;
    std::vector<std::pair<std::string, std::string>> mods;   // name, folder
    for (const std::string& name : Vfs::ListDirectory("mods")) {
        if (name.empty() || name[0] == '_' || name[0] == '.') continue;
        if (!Vfs::Exists("mods/" + name + "/mod.lua")) continue;
        mods.push_back({ name, Vfs::Resolve("mods/" + name) });
    }
    if (mods.empty()) return;

    // Later mods override earlier ones: each is pushed in front of the previous.
    for (const auto& m : mods) Vfs::AddSearchPathFront(m.second + "/");

    OpenSandbox();
    for (const auto& m : mods) {
        if (RunModScript(m.first, m.second + "/mod.lua")) {
            sLoaded.push_back(m.first);
            Log("loaded %s", m.first.c_str());
        }
    }
}

void Shutdown() {
    sBoard = nullptr;
    for (auto& t : sTimers) Release(t.cb);
    for (auto& h : sHooks) Release(h.cb);
    for (auto& p : sPickups) Release(p.onPickup);
    for (auto& a : sAbilities) Release(a.onUse);
    sTimers.clear();
    sHooks.clear();
    sPickups.clear();
    sAbilities.clear();
    if (L) lua_close(L);
    L = nullptr;
    if (sLog) std::fclose(sLog);
    sLog = nullptr;
}

const std::vector<std::string>& LoadedMods() { return sLoaded; }
int ErrorCount() { return sErrors; }

void AttachBoard(Board* b) {
    sBoard = b;
}

void DetachBoard(Board* b) {
    if (sBoard != b) return;
    sBoard = nullptr;
    for (auto& t : sTimers) Release(t.cb);
    sTimers.clear();   // timers belong to the mission they were started in
}

void OnLevelStart() {
    if (!L || !sBoard) return;
    for (AbilityDef& a : sAbilities) {
        a.cooldownLeft = 0;
        a.wasDown = true;   // a key held while the mission starts does not fire
    }
    Emit("level_start", [] {
        AppState& app = ModBridge::App(*sBoard);
        lua_pushinteger(L, app.mission);
        lua_pushboolean(L, ModBridge::Survival(*sBoard));
        return 2;
    });
}

void OnTick() {
    if (!L || !sBoard) return;
    Board& b = *sBoard;

    // Abilities.
    const bool* keys = SDL_GetKeyboardState(nullptr);
    SDL_Gamepad* pad = InputManager::Gamepad();
    for (size_t i = 0; i < sAbilities.size(); ++i) {
        AbilityDef& a = sAbilities[i];
        if (a.cooldownLeft > 0) --a.cooldownLeft;
        bool down = false;
        for (SDL_Scancode k : a.keys) down |= keys && keys[k];
        if (pad) {
            for (SDL_GamepadButton gb : a.buttons) down |= SDL_GetGamepadButton(pad, gb);
            for (SDL_GamepadAxis ax : a.axes) down |= SDL_GetGamepadAxis(pad, ax) > 16000;
        }
        const bool pressed = down && !a.wasDown;
        a.wasDown = down;
        if (!pressed || ModBridge::Respawn(b) != 0) continue;
        if (a.charges == 0 || a.cooldownLeft > 0) {
            AudioSystem::PlaySoundId(SND_DENIED, 0);
            continue;
        }
        Callback cb = a.onUse;
        if (!Call(cb, 0, 1)) continue;
        const bool consumed = !(lua_isboolean(L, -1) && !lua_toboolean(L, -1));   // `return false` = not used
        lua_pop(L, 1);
        if (!consumed || i >= sAbilities.size()) continue;
        AbilityDef& used = sAbilities[i];
        if (used.charges > 0) --used.charges;
        used.cooldownLeft = used.cooldown;
    }

    // Timers.
    std::vector<Timer> due;
    for (auto it = sTimers.begin(); it != sTimers.end();) {
        if (--it->ticks <= 0) {
            due.push_back(*it);
            it = sTimers.erase(it);
        } else {
            ++it;
        }
    }
    for (Timer& t : due) {
        if (sBoard) Call(t.cb, 0, 0);
        Release(t.cb);
    }

    if (sBoard) Emit("tick", nullptr);
}

void OnEnemyKilled(const Craft& c) {
    if (!L || !sBoard) return;
    Emit("enemy_killed", [&c] {
        PushEnemy(L, c);
        return 1;
    });
}

void OnTankDestroyed() {
    if (!L || !sBoard) return;
    Emit("tank_destroyed", [] {
        lua_pushinteger(L, ModBridge::App(*sBoard).lives);
        return 1;
    });
}

void OnRunReset() {
    for (AbilityDef& a : sAbilities) {
        a.charges = a.startCharges;
        a.cooldownLeft = 0;
    }
    for (PickupDef& p : sPickups) p.collected = 0;
}

void AddPickupCandidates(std::vector<PickupCandidate>& out, double tier, bool survival) {
    for (size_t i = 0; i < sPickups.size(); ++i) {
        const PickupDef& p = sPickups[i];
        if (p.chance <= 0.0 || tier < p.tier) continue;
        if (survival ? !p.survival : !p.campaign) continue;
        if (p.max >= 0 && p.collected >= p.max) continue;
        out.push_back({ kModPickupBase + (int)i, p.chance });
    }
}

Texture* PickupImage(int type, int& cel) {
    const int i = type - kModPickupBase;
    if (i < 0 || i >= (int)sPickups.size()) return nullptr;
    PickupDef& p = sPickups[i];
    if (!p.tex) p.tex = ModImage(p.image, p.cols);
    cel = p.frame;
    return p.tex;
}

void OnPickup(int type, double x, double y) {
    const int i = type - kModPickupBase;
    if (i < 0 || i >= (int)sPickups.size() || !sBoard) return;
    PickupDef& p = sPickups[i];
    ++p.collected;
    if (!p.message.empty()) sBoard->AddMessage(p.message, (int)sBoard->TankX(), sBoard->TankY() - 13, true);
    if (!p.sound.empty()) AudioSystem::PlaySound(p.sound);
    else AudioSystem::PlaySoundId(SND_POWERUP, sBoard->Pan(sBoard->TankX()));
    const std::string id = p.id;
    Callback cb = p.onPickup;
    if (cb.ref != LUA_NOREF) {
        lua_pushnumber(L, x);
        lua_pushnumber(L, y);
        Call(cb, 2, 0);
    }
    Emit("pickup", [&id, x, y] {
        lua_pushstring(L, id.c_str());
        lua_pushnumber(L, x);
        lua_pushnumber(L, y);
        return 3;
    });
}

void DrawHud() {
    if (!L || !sBoard) return;
    float y = 40.0f;
    for (const AbilityDef& a : sAbilities) {
        if (!a.hud) continue;
        std::string text = a.name;
        if (a.charges >= 0) text += " x" + std::to_string(a.charges);
        if (!a.keyLabel.empty()) text += "  [" + a.keyLabel + "]";
        const bool ready = a.charges != 0 && a.cooldownLeft == 0;
        const Color4f col = ready ? Color4f{ 0.3f, 1.0f, 0.3f, 0.9f } : Color4f{ 0.6f, 0.6f, 0.6f, 0.7f };
        FontRenderer::DrawString("Normal", text, 632.0f, y, col, 1.0f, ALIGN_RIGHT);
        y += 16.0f;
    }
}

} // namespace Mods
} // namespace HeavyWeapon
