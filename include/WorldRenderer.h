#pragma once

#include "DataModels.h"
#include "TextureManager.h"
#include <algorithm>
#include <string>
#include <vector>

namespace HeavyWeapon {

// Background plane images of the current mission, and its ambient animations from
// Images/Anims/Anims.xml (spawned, moved and drawn by Board, as in the original).
class WorldRenderer {
public:
    static void Init();
    static void SetTheme(const std::string& themeName, const std::vector<AnimDef>& levelAnims);

    static Texture* Sky() { return sSkyTex; }
    static Texture* Bg() { return sBgTex; }
    static Texture* Bg2() { return sBg2Tex; }
    static Texture* Ground() { return sGroundTex; }
    static Texture* MiniMap() { return sMapTex; }   // 138x18 HUD progress map

    // The current mission's Anims.xml entries (empty with kDecorativeAnims off).
    static const std::vector<AnimDef>& LevelAnims() { return sLevelAnims; }
    // The animation's strip, one cel per frame.
    static Texture* AnimImage(const AnimDef& def);

private:
    static std::string sCurrentTheme;
    static Texture* sSkyTex;
    static Texture* sBg2Tex; // Far BG
    static Texture* sBgTex;  // Mid BG
    static Texture* sGroundTex;
    static Texture* sMapTex;
    static std::vector<AnimDef> sLevelAnims;
};

} // namespace HeavyWeapon
