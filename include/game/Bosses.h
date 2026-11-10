#pragma once

#include <memory>

namespace HeavyWeapon {

class Board;
class Craft;

// The boss for a mission (Board::Update 0x41b690 switch on mission % 9; mission 18 gets
// the final boss instead of the helicopter).
std::unique_ptr<Craft> CreateBoss(Board& b, int mission);

// Sub-folder of Images/ holding the sprites of the mission's boss, for preloading.
const char* BossSpriteFolder(int mission);

} // namespace HeavyWeapon
