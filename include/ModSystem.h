#pragma once

// Mod support: Lua scripts and asset overrides (see docs/MODDING.md).
//
// A mod is a folder `mods/<name>/` next to the game data (or the executable) with a
// `mod.lua`. Its other files mirror the game's paths and override the originals
// (`Images/...`, `Sounds/...`, `Music/...`, `data/...`); mods later in alphabetical order
// win. Folders starting with `_` or `.` are skipped. Scripts run sandboxed (no io/os, no
// file loading, an instruction budget per call) and register content through `hw.*`.

#include <string>
#include <vector>

namespace HeavyWeapon {

struct Texture;
class Board;
class Craft;

namespace Mods {

// Finds the mods, puts their folders in front of the asset search path and runs each
// mod.lua. Call right after Vfs::Init. With enabled = false nothing is loaded.
void Init(bool enabled);
void Shutdown();
const std::vector<std::string>& LoadedMods();
int ErrorCount();   // script errors so far (details in mods.log)

// ---- Board lifetime and game events -----------------------------------------------
void AttachBoard(Board* b);    // Board constructor
void DetachBoard(Board* b);    // Board destructor
void OnLevelStart();           // after the board is set up
void OnTick();                 // once per 100 Hz board tick: abilities, timers, `tick`
void OnEnemyKilled(const Craft& c);
void OnTankDestroyed();
void OnRunReset();             // new campaign or survival run: ability charges restart

// ---- Pickups ------------------------------------------------------------------------
// Mod pickups share the power-up type number space from kModPickupBase upwards.
constexpr int kModPickupBase = 1000;
struct PickupCandidate { int type; double weight; };
// Mod pickups that may drop now (tier = Board::Tier(), survival run or not).
void AddPickupCandidates(std::vector<PickupCandidate>& out, double tier, bool survival);
// Sprite and cel of a mod pickup type (nullptr if the type is not a mod pickup).
Texture* PickupImage(int type, int& cel);
void OnPickup(int type, double x, double y);

// ---- HUD --------------------------------------------------------------------------------
void DrawHud();   // ability charges / cooldowns, screen space

} // namespace Mods
} // namespace HeavyWeapon
