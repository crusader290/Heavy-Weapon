#include "WorldRenderer.h"
#include "Renderer.h"
#include <cmath>
#include <iostream>

namespace HeavyWeapon {

std::string WorldRenderer::sCurrentTheme = "";
float WorldRenderer::sScrollX = 0.0f;
Texture* WorldRenderer::sSkyTex = nullptr;
Texture* WorldRenderer::sBg2Tex = nullptr;
Texture* WorldRenderer::sBgTex = nullptr;
Texture* WorldRenderer::sGroundTex = nullptr;
Texture* WorldRenderer::sMapTex = nullptr;
std::vector<ActiveAnim> WorldRenderer::sAnims;

void WorldRenderer::Init() {
    sScrollX = 0.0f;
}

void WorldRenderer::SetTheme(const std::string& themeName, const std::vector<AnimDef>& levelAnims) {
    sCurrentTheme = themeName;
    sScrollX = 0.0f;

    std::string prefix = "Images/Backgrounds/" + themeName;
    sSkyTex = TextureManager::Load(prefix + "_sky");
    sBg2Tex = TextureManager::Load(prefix + "_bg2");
    sBgTex = TextureManager::Load(prefix + "_bg");
    sGroundTex = TextureManager::Load(prefix + "_ground");
    sMapTex = TextureManager::Load(prefix + "_map");

    sAnims.clear();
    for (const auto& def : levelAnims) {
        if (!kDecorativeAnims) break;
        ActiveAnim anim;
        anim.def = def;
        anim.currentFrame = 0.0f;
        anim.worldX = (float)def.offset;
        anim.y = (float)def.y;
        anim.nuked = false;
        anim.visible = true;

        // Preload animation texture strip
        std::string animTexPath = "Images/Anims/" + def.name;
        TextureManager::Load(animTexPath);

        sAnims.push_back(anim);
    }
}

void WorldRenderer::Tick(float groundScroll) {
    sScrollX += groundScroll;

    for (auto& anim : sAnims) {
        // Anims.xml: speed is frames per program cycle (100 Hz); <Delay> overrides per frame.
        float animSpeed = anim.def.speed;
        int curIntFrame = (int)anim.currentFrame;
        for (const auto& delay : anim.def.delays) {
            if (delay.frame == curIntFrame) {
                animSpeed = delay.speed;
                break;
            }
        }

        if (anim.def.type == "looping") {
            anim.currentFrame += animSpeed;
            int maxFrames = anim.def.nuke ? anim.def.frames - 1 : anim.def.frames;
            if (maxFrames <= 0) maxFrames = 1;
            if (anim.currentFrame >= (float)maxFrames) {
                anim.currentFrame = std::fmod(anim.currentFrame, (float)maxFrames);
            }
        }

        anim.worldX += anim.def.mx;
    }
}

void WorldRenderer::TriggerNuke() {
    for (auto& anim : sAnims) {
        if (anim.def.nuke) {
            anim.nuked = true;
            anim.currentFrame = (float)(anim.def.frames - 1); // Last frame is nuked frame
        }
    }
}

static void RenderPlaneAnims(int plane, std::vector<ActiveAnim>& anims, float scrollX) {
    // Plane scroll rates relative to the ground (Board::UpdateF 0x4112f0).
    float scrollFactor = (plane == 4) ? 0.1f : (plane == 3) ? 0.2f : (plane == 2) ? 0.3f : 1.0f;

    for (auto& anim : anims) {
        if (anim.def.plane != plane || !anim.visible) continue;

        std::string animTexPath = "Images/Anims/" + anim.def.name;
        Texture* tex = TextureManager::Get(animTexPath);
        if (!tex) continue;

        float frameW = (float)tex->width / (float)anim.def.frames;
        float frameH = (float)tex->height;
        int curF = (int)anim.currentFrame;
        if (curF >= anim.def.frames) curF = anim.def.frames - 1;

        Rect src = { (float)curF * frameW, 0.0f, frameW, frameH };

        // Position relative to scrolling plane
        float screenX = anim.worldX - (scrollX * scrollFactor);
        // Wrap smoothly around screen
        float period = 960.0f;
        screenX = std::fmod(screenX, period);
        if (screenX < -frameW) screenX += period;

        Rect dst = { screenX - frameW * 0.5f, anim.y - frameH * 0.5f, frameW, frameH };
        Renderer::DrawTexture(tex, dst, src);
    }
}

void WorldRenderer::RenderAnims(int plane) {
    RenderPlaneAnims(plane, sAnims, sScrollX);
}

} // namespace HeavyWeapon
