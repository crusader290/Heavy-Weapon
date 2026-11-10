#include "Perf.h"

#ifdef HW_PERF
#include <SDL2/SDL.h>
#include <cstdarg>
#include <cstdio>

namespace HeavyWeapon {
namespace Perf {

#ifdef __vita__
static const char* kLogPath = "ux0:data/heavyweapon/perf.log";
#else
static const char* kLogPath = "./perf.log";
#endif

static FILE* sFile = nullptr;
static bool sOpened = false;

double NowMs() {
    return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

void Log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (!sOpened) {
        sOpened = true;
        sFile = fopen(kLogPath, "w");
    }
    printf("[perf %9.1f] %s\n", NowMs(), buf);
    fflush(stdout);
    if (sFile) {
        fprintf(sFile, "[%9.1f] %s\n", NowMs(), buf);
        fflush(sFile);
    }
}

Scope::~Scope() {
    double ms = NowMs() - t0;
    if (ms >= min) Log("%s: %.2f ms", name.c_str(), ms);
}

static int sTexCount = 0;
static double sTexBytes = 0.0;

void TextureLoaded(const std::string& name, int w, int h) {
    ++sTexCount;
    sTexBytes += (double)w * (double)h * 4.0;
    Log("tex #%d %s %dx%d (total %.1f MB)", sTexCount, name.c_str(), w, h, sTexBytes / (1024.0 * 1024.0));
}

// Per-second play statistics.
static double sWorstTickMs = 0.0;
static double sWorstFrameMs = 0.0;
static int sTicks = 0;
static double sWindowStart = -1.0;

void BoardTick(double ms) {
    ++sTicks;
    if (ms > sWorstTickMs) sWorstTickMs = ms;
}

void EndFrame(double frameMs, int respawn, int appTick) {
    double now = NowMs();
    if (sWindowStart < 0.0) sWindowStart = now;
    if (frameMs > sWorstFrameMs) sWorstFrameMs = frameMs;
    double elapsed = now - sWindowStart;
    if (elapsed < 1000.0) return;
    Log("play: ticks=%d ticks/sec=%.1f worstFrame=%.2f ms worstBoardUpdate=%.2f ms mRespawn=%d app.tick=%d textures=%d (%.1f MB)",
        sTicks, sTicks * 1000.0 / elapsed, sWorstFrameMs, sWorstTickMs, respawn, appTick,
        sTexCount, sTexBytes / (1024.0 * 1024.0));
    sTicks = 0;
    sWorstTickMs = 0.0;
    sWorstFrameMs = 0.0;
    sWindowStart = now;
}

} // namespace Perf
} // namespace HeavyWeapon
#endif
