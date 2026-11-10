#pragma once

#include "DataModels.h"
#include "TextureManager.h"
#include <string>
#include <vector>

namespace HeavyWeapon {

class WorldRenderer {
public:
    static void Init();
    static void SetTheme(const std::string& themeName, const std::vector<AnimDef>& levelAnims);
    // One 100 Hz tick; groundScroll is how far the ground plane moved (pixels).
    static void Tick(float groundScroll);
    // Ambient Anims.xml animations of one plane (1 ground .. 4 sky), screen space.
    static void RenderAnims(int plane);

    static Texture* Sky() { return sSkyTex; }
    static Texture* Bg() { return sBgTex; }
    static Texture* Bg2() { return sBg2Tex; }
    static Texture* Ground() { return sGroundTex; }
    static Texture* MiniMap() { return sMapTex; }   // 138x18 HUD progress map

    // Trigger nuke effect across world animations
    static void TriggerNuke();

    static float GetScrollOffset() { return sScrollX; }

private:
    static std::string sCurrentTheme;
    static float sScrollX;

    // Background plane textures
    static Texture* sSkyTex;
    static Texture* sBg2Tex; // Far BG
    static Texture* sBgTex;  // Mid BG
    static Texture* sGroundTex;
    static Texture* sMapTex;

    // Active ambient animations
    static std::vector<ActiveAnim> sAnims;

};

} // namespace HeavyWeapon
