// Flash movie playback through the Tanks recomp's runtime (3rdparty/flashrt): the timeline
// runs at the movie's frame rate, the display list is rasterized by PlutoVG into a
// 640x480 premultiplied ARGB buffer and shown as a GL texture inside the game's frame.

#include "FlashMovie.h"
#include "GLPlatform.h"
#include "Renderer.h"
#include "TextureManager.h"
#include "Vfs.h"
#include "display.h"    // flashrt
#include "platform.h"
#include "raster.h"
#include <algorithm>
#include <iostream>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

namespace fl {
void initGlobals();
void runIntervals();
void movieReset();
// Translated ActionScript of each movie (or an empty table when the SWF was not there at
// build time): see the Flash section of CMakeLists.txt.
extern const GameCode g_code_mv_intro, g_code_mv_middle, g_code_mv_final;
}

namespace HeavyWeapon {

namespace {

struct MovieInfo { const char* path; const fl::GameCode* code; };
const MovieInfo kMovies[MOVIE_COUNT] = {
    { "Images/intro.swf", &fl::g_code_mv_intro },
    { "Images/middle.swf", &fl::g_code_mv_middle },
    { "Images/final.swf", &fl::g_code_mv_final },
};

bool sRuntimeReady = false;
bool sActive = false;
bool sDirty = false;            // a frame ran since the stage was last rasterized
bool sEnded = false;
double sAccumMs = 0.0, sTickMs = 1000.0 / 12.0, sHoldMs = 0.0;
int sLastFrame = 0;
fl::BitmapData* sStage = nullptr;
Texture sTex;                   // the stage as a texture (GL id, 640x480)

fl::MovieClip* Root() { return (fl::MovieClip*)fl::vm.root; }

// One timeline tick; notes when the main timeline has reached its end.
void Tick() {
    fl::g_player.timeMs += sTickMs;
    fl::runFrame();
    fl::runIntervals();
    fl::gcCollect();
    sDirty = true;
    fl::MovieClip* root = Root();
    if (!root || !root->def) return;
    const int last = root->def->nframes;
    // Stopped on the last frame (the movies' own stop()), or wrapped back to the start
    // (a movie without its translated scripts loops).
    if ((root->cur >= last && !root->playing) || root->cur < sLastFrame) sEnded = true;
    sLastFrame = root->cur;
}

} // namespace

bool FlashMovie::Exists(MovieId id) {
    return id >= 0 && id < MOVIE_COUNT && Vfs::Exists(kMovies[id].path);
}

bool FlashMovie::Start(MovieId id) {
    Stop();
    if (!Exists(id)) return false;
    if (!sRuntimeReady) {
        fl::platformInitLimits();
        fl::initGlobals();
        sRuntimeReady = true;
    }
    fl::movieReset();
    fl::g_code = kMovies[id].code;
    std::string err;
    const std::string path = Vfs::Resolve(kMovies[id].path);
    if (!fl::g_movie.load(path, err)) {
        std::cerr << "[FlashMovie] Cannot load " << kMovies[id].path << ": " << err << std::endl;
        return false;
    }
    fl::g_player = fl::Player();
    fl::playerInit();
    if (!sStage) {
        sStage = fl::gcNew<fl::BitmapData>();
        fl::gcPin(sStage);
        sStage->transparent = false;
    }
    sTickMs = 1000.0 / (fl::g_movie.frameRate > 0.0f ? fl::g_movie.frameRate : 12.0f);
    sAccumMs = 0.0;
    sHoldMs = 0.0;
    sLastFrame = 0;
    sEnded = false;
    sActive = true;
    Tick();   // show frame 1 right away
    std::cout << "[FlashMovie] Playing " << kMovies[id].path << " (" << fl::g_movie.rootDef.nframes << " frames at "
              << fl::g_movie.frameRate << " fps" << (kMovies[id].code->nFrames ? "" : ", without scripts") << ")" << std::endl;
    return true;
}

bool FlashMovie::Update(float dt) {
    if (!sActive) return false;
    if (sEnded) {
        sHoldMs += dt * 1000.0;
        return sHoldMs < 1000.0;
    }
    sAccumMs += dt * 1000.0;
    for (int steps = 0; sAccumMs >= sTickMs && steps < 4 && !sEnded; ++steps) {
        Tick();
        sAccumMs -= sTickMs;
    }
    if (sAccumMs > sTickMs * 4) sAccumMs = 0.0;   // far behind: drop ticks instead of spiralling
    return true;
}

void FlashMovie::Draw() {
    if (!sActive || !sStage) return;
    const int w = fl::g_movie.width > 0 ? fl::g_movie.width : 640;
    const int h = fl::g_movie.height > 0 ? fl::g_movie.height : 480;
    if (sDirty || sTex.id == 0) {
        if (sStage->w != w || sStage->h != h) {
            sStage->w = w;
            sStage->h = h;
            sStage->px.assign((size_t)w * h, 0);
        }
        std::fill(sStage->px.begin(), sStage->px.end(), fl::g_movie.bgColor | 0xff000000u);
        sStage->lazy = false;
        sStage->version++;
        fl::Mat m;   // stage units are pixels already
        fl::bitmapDraw(sStage, (fl::Object*)fl::vm.root, m, nullptr);

        // Premultiplied ARGB words are B,G,R,A bytes in memory; the stage is opaque.
        if (sTex.id == 0 || sTex.width != w || sTex.height != h) {
            if (sTex.id == 0) glGenTextures(1, &sTex.id);
            glBindTexture(GL_TEXTURE_2D, sTex.id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, sStage->px.data());
            sTex.width = w;
            sTex.height = h;
        } else {
            glBindTexture(GL_TEXTURE_2D, sTex.id);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, sStage->px.data());
        }
        sDirty = false;
    }
    // The movies are 640x480, the game's own frame size.
    Renderer::DrawTexture(&sTex, Rect{ 0.0f, 0.0f, 640.0f, 480.0f },
                          Rect{ 0.0f, 0.0f, (float)sTex.width, (float)sTex.height });
}

void FlashMovie::Stop() {
    sActive = false;
    sEnded = false;
}

void FlashMovie::Shutdown() {
    Stop();
    if (sTex.id) glDeleteTextures(1, &sTex.id);
    sTex = Texture();
}

} // namespace HeavyWeapon
