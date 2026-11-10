#include "WorldRenderer.h"
#include "Constants.h"

namespace HeavyWeapon {

std::string WorldRenderer::sCurrentTheme = "";
Texture* WorldRenderer::sSkyTex = nullptr;
Texture* WorldRenderer::sBg2Tex = nullptr;
Texture* WorldRenderer::sBgTex = nullptr;
Texture* WorldRenderer::sGroundTex = nullptr;
Texture* WorldRenderer::sMapTex = nullptr;
std::vector<AnimDef> WorldRenderer::sLevelAnims;

void WorldRenderer::Init() {}

void WorldRenderer::SetTheme(const std::string& themeName, const std::vector<AnimDef>& levelAnims) {
    sCurrentTheme = themeName;

    std::string prefix = "Images/Backgrounds/" + themeName;
    sSkyTex = TextureManager::Load(prefix + "_sky");
    sBg2Tex = TextureManager::Load(prefix + "_bg2");
    sBgTex = TextureManager::Load(prefix + "_bg");
    sGroundTex = TextureManager::Load(prefix + "_ground");
    sMapTex = TextureManager::Load(prefix + "_map");

    sLevelAnims.clear();
    if (!kDecorativeAnims) return;
    sLevelAnims = levelAnims;
    // Load the strips now (the loader sets each image's cel count to `frames`), so a
    // spawning animation never loads mid-mission.
    for (const AnimDef& def : sLevelAnims) AnimImage(def);
}

Texture* WorldRenderer::AnimImage(const AnimDef& def) {
    return TextureManager::LoadGrid("Images/Anims/" + def.name, std::max(1, def.frames), 1);
}

} // namespace HeavyWeapon
