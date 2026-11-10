#pragma once

#include "DataModels.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace HeavyWeapon {

class XmlLoader {
public:
    static bool LoadCrafts(const std::string& path, std::unordered_map<std::string, CraftDef>& outCrafts);
    static bool LoadLevels(const std::string& path, std::vector<LevelDef>& outLevels);
    static bool LoadWaves(const std::string& path, std::vector<LevelDef>& inOutLevels);
    // survivalN.xml: one LevelDef per <Level>, each a tier of the endless mode.
    static bool LoadSurvival(const std::string& path, std::vector<LevelDef>& outTiers);
    static bool LoadBosses(const std::string& path, std::unordered_map<std::string, BossDef>& outBosses);
    static bool LoadAnims(const std::string& path, std::vector<std::vector<AnimDef>>& outLevelAnims);
};

} // namespace HeavyWeapon
