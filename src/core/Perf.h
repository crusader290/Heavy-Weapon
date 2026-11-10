#pragma once
// Lightweight profiler for the mission-start lag investigation.
// Everything compiles away unless built with -DHW_PERF=ON, so release builds are unchanged.
// Output goes to stdout and to perf.log (Vita: ux0:data/heavyweapon/perf.log), flushed per line.

#ifdef HW_PERF
#include <string>

namespace HeavyWeapon {
namespace Perf {

double NowMs();
void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Logs "name: X ms" on destruction when X >= minMs.
struct Scope {
    Scope(const std::string& n, double minMs = 0.0) : name(n), min(minMs), t0(NowMs()) {}
    ~Scope();
    std::string name;
    double min;
    double t0;
};

void TextureLoaded(const std::string& name, int w, int h);  // running count / w*h*4 bytes
void BoardTick(double ms);                                   // one Board::Update finished
void EndFrame(double frameMs, int respawn, int appTick);     // once per rendered play frame

} // namespace Perf
} // namespace HeavyWeapon

#define PERF_CAT2(a, b) a##b
#define PERF_CAT(a, b) PERF_CAT2(a, b)
#define PERF_SCOPE(name) ::HeavyWeapon::Perf::Scope PERF_CAT(perfScope_, __LINE__)(name)
#define PERF_SCOPE_MIN(name, minMs) ::HeavyWeapon::Perf::Scope PERF_CAT(perfScope_, __LINE__)(name, minMs)
#define PERF_BEGIN(var) double var = ::HeavyWeapon::Perf::NowMs()
// Logs the time since PERF_BEGIN / the previous PERF_LAP and restarts the lap.
#define PERF_LAP(var, label) do { double perfNow_ = ::HeavyWeapon::Perf::NowMs(); \
    ::HeavyWeapon::Perf::Log("%s: %.2f ms", label, perfNow_ - var); var = perfNow_; } while (0)
#define PERF_LOG(...) ::HeavyWeapon::Perf::Log(__VA_ARGS__)
#define PERF_TEXTURE_LOADED(name, w, h) ::HeavyWeapon::Perf::TextureLoaded(name, w, h)
#define PERF_BOARD_TICK(ms) ::HeavyWeapon::Perf::BoardTick(ms)
#define PERF_NOW() ::HeavyWeapon::Perf::NowMs()
#define PERF_END_FRAME(ms, respawn, tick) ::HeavyWeapon::Perf::EndFrame(ms, respawn, tick)

#else

#define PERF_SCOPE(name) ((void)0)
#define PERF_SCOPE_MIN(name, minMs) ((void)0)
#define PERF_BEGIN(var) ((void)0)
#define PERF_LAP(var, label) ((void)0)
#define PERF_LOG(...) ((void)0)
#define PERF_TEXTURE_LOADED(name, w, h) ((void)0)
#define PERF_BOARD_TICK(ms) ((void)0)
#define PERF_NOW() 0.0
#define PERF_END_FRAME(ms, respawn, tick) ((void)0)

#endif
