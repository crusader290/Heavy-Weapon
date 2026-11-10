#include "AudioSystem.h"
#include "Vfs.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cctype>

namespace HeavyWeapon {

struct SoundMeta {
    const char* name;
    float defaultVolume;
};

// Sound table from the original executable (VA 0x529808): 92 records of
// { char name[16]; double volume; }. The index is the sound ID the game code uses.
static const SoundMeta sSoundMetaTable[] = {
    { "gunpowerup", 0.50f }, // 0
    { "tankfire", 0.70f }, // 1
    { "tankfire1", 0.60f }, // 2
    { "tankfire2", 0.60f }, // 3
    { "tankfire3", 0.80f }, // 4
    { "tankfire4", 0.70f }, // 5
    { "bigexplode", 1.00f }, // 6
    { "smallexplode", 0.60f }, // 7
    { "bombfall", 0.70f }, // 8
    { "bullethit", 0.50f }, // 9
    { "tankexplode", 0.90f }, // 10
    { "missile", 0.70f }, // 11
    { "nukeblast", 0.90f }, // 12
    { "powerup", 0.50f }, // 13
    { "sparking", 1.00f }, // 14
    { "laser", 0.40f }, // 15
    { "bossblast", 1.00f }, // 16
    { "buttondown", 0.50f }, // 17
    { "buttonup", 0.50f }, // 18
    { "smallmissile", 0.60f }, // 19
    { "tanklaser", 0.50f }, // 20
    { "denied", 0.50f }, // 21
    { "stats", 0.50f }, // 22
    { "alert", 0.40f }, // 23
    { "pupcopter", 0.70f }, // 24
    { "star", 0.70f }, // 25
    { "laserpowerup", 0.50f }, // 26
    { "satlaser", 0.50f }, // 27
    { "sat1", 0.40f }, // 28
    { "sat2", 0.40f }, // 29
    { "sat3", 0.40f }, // 30
    { "sat4", 0.40f }, // 31
    { "sat5", 0.40f }, // 32
    { "sat6", 0.40f }, // 33
    { "sat7", 0.40f }, // 34
    { "shortkey", 0.40f }, // 35
    { "longkey", 0.40f }, // 36
    { "friendly1", 0.60f }, // 37
    { "friendly2", 0.60f }, // 38
    { "friendly3", 0.60f }, // 39
    { "friendlydie", 0.60f }, // 40
    { "megalaser", 0.70f }, // 41
    { "enemytankgun", 0.80f }, // 42
    { "shorting", 0.20f }, // 43
    { "statichit", 0.50f }, // 44
    { "staticshot", 0.50f }, // 45
    { "megalaser_out", 0.50f }, // 46
    { "megalaser_start", 0.50f }, // 47
    { "megalaser_stop", 0.50f }, // 48
    { "mapover", 0.60f }, // 49
    { "starsmash", 0.50f }, // 50
    { "energyfire", 0.70f }, // 51
    { "statbeep", 0.50f }, // 52
    { "reinforcements", 0.50f }, // 53
    { "enemyfire", 1.00f }, // 54
    { "v_atomictank", 0.80f }, // 55
    { "v_gameover", 0.80f }, // 56
    { "v_getready", 0.80f }, // 57
    { "v_levelcomplete", 0.80f }, // 58
    { "v_megalaser", 0.80f }, // 59
    { "v_prepare", 0.80f }, // 60
    { "v_danger", 0.80f }, // 61
    { "v_waffle", 0.80f }, // 62
    { "megaup1", 0.50f }, // 63
    { "megaup2", 0.50f }, // 64
    { "megaup3", 0.50f }, // 65
    { "megaup4", 0.50f }, // 66
    { "orbhit", 0.50f }, // 67
    { "flak", 0.50f }, // 68
    { "airraid", 0.60f }, // 69
    { "robotsmash", 0.50f }, // 70
    { "robotlaser", 0.50f }, // 71
    { "earthquake", 0.80f }, // 72
    { "airmine", 0.50f }, // 73
    { "meteor", 0.50f }, // 74
    { "tractorbeam", 0.50f }, // 75
    { "diesel", 0.70f }, // 76
    { "airbrake", 0.90f }, // 77
    { "chain", 0.50f }, // 78
    { "bigthud", 1.00f }, // 79
    { "thunder", 1.00f }, // 80
    { "boltcharge", 0.90f }, // 81
    { "jetdive", 1.00f }, // 82
    { "deflect", 0.50f }, // 83
    { "bosscopter", 0.80f }, // 84
    { "swoosh", 0.50f }, // 85
    { "bosslaser", 0.50f }, // 86
    { "bosslaserblast", 0.30f }, // 87
    { "shieldup", 0.50f }, // 88
    { "shielddown", 0.50f }, // 89
    { "upgrade", 1.00f }, // 90
    { "speedup", 0.60f }, // 91
};

std::unordered_map<std::string, Mix_Chunk*> AudioSystem::sSounds;
std::unordered_set<std::string> AudioSystem::sMissingSounds;
Mix_Music* AudioSystem::sCurrentMusic = nullptr;
int AudioSystem::sMusicVolume = 100;
bool AudioSystem::sMusicEnabled = true;
std::string AudioSystem::sMusicRequest;
bool AudioSystem::sMusicRequestLoop = true;
int AudioSystem::sSfxVolume = 100;
int AudioSystem::sEngineChannel = -1;
SDL_mutex* AudioSystem::sSoundMutex = nullptr;
SDL_Thread* AudioSystem::sPreloadThread = nullptr;
volatile int AudioSystem::sPreloadDone = 0;
volatile int AudioSystem::sPreloadRunning = 0;

namespace {
struct SoundLock {
    SDL_mutex* m;
    explicit SoundLock(SDL_mutex* mu) : m(mu) { if (m) SDL_LockMutex(m); }
    ~SoundLock() { if (m) SDL_UnlockMutex(m); }
};
}

void AudioSystem::Init() {
    if (!sSoundMutex) sSoundMutex = SDL_CreateMutex();
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "[AudioSystem] Mix_OpenAudio error: " << Mix_GetError() << std::endl;
        return;
    }
    Mix_AllocateChannels(32);
    Mix_Volume(-1, sSfxVolume);
    Mix_VolumeMusic(sMusicVolume);
}

void AudioSystem::Shutdown() {
    FinishPreload();
    StopMusic();
    if (sEngineChannel >= 0) {
        Mix_HaltChannel(sEngineChannel);
        sEngineChannel = -1;
    }
    for (auto& pair : sSounds) {
        if (pair.second) {
            Mix_FreeChunk(pair.second);
        }
    }
    sSounds.clear();
    sMissingSounds.clear();
    Mix_CloseAudio();
}

int AudioSystem::sCooldown[SND_COUNT] = {};
int AudioSystem::sLoopChannel[SND_COUNT] = {};

void AudioSystem::Tick() {
    for (int& c : sCooldown) {
        if (c > 0) --c;
    }
}

// PopCap pan is DirectSound-style (hundredths of a dB, -10000..10000); the game passes
// x * 3 for world x in -320..320, i.e. a gentle stereo spread.
static void ApplyPan(int channel, int pan) {
    float t = std::clamp(pan / 10000.0f, -1.0f, 1.0f);
    float atten = std::pow(10.0f, -std::abs(t) * 100.0f / 20.0f); // dB attenuation of the far side
    Uint8 left = (Uint8)(255 * (t > 0 ? atten : 1.0f));
    Uint8 right = (Uint8)(255 * (t < 0 ? atten : 1.0f));
    Mix_SetPanning(channel, left, right);
}

void AudioSystem::PlaySoundId(int id, int pan, float volume) {
    if (id < 0 || id >= SND_COUNT) return;
    // FUN_00401150: each sound is suppressed for 10 ticks after playing (20 for the
    // explosion sounds) so bursts of hits do not stack.
    if (sCooldown[id] > 0) return;
    sCooldown[id] = (id == SND_BIGEXPLODE || id == SND_SMALLEXPLODE) ? 20 : 10;

    const std::string name = sSoundMetaTable[id].name;
    Mix_Chunk* chunk = GetChunk(name);
    if (!chunk) return;

    float base = volume >= 0.0f ? volume : (float)sSoundMetaTable[id].defaultVolume;
    Mix_VolumeChunk(chunk, std::clamp((int)(sSfxVolume * base), 0, 128));
    int channel = Mix_PlayChannel(-1, chunk, 0);
    if (channel >= 0) ApplyPan(channel, pan);
}

void AudioSystem::SetLoop(int id, bool on, int pan) {
    if (id < 0 || id >= SND_COUNT) return;
    int& ch = sLoopChannel[id];   // stored as channel + 1 (0 = none)
    if (!on) {
        if (ch) Mix_HaltChannel(ch - 1);
        ch = 0;
        return;
    }
    if (ch && Mix_Playing(ch - 1)) {
        ApplyPan(ch - 1, pan);
        return;
    }
    const std::string name = sSoundMetaTable[id].name;
    Mix_Chunk* chunk = GetChunk(name);
    if (!chunk) return;
    Mix_VolumeChunk(chunk, std::clamp((int)(sSfxVolume * sSoundMetaTable[id].defaultVolume), 0, 128));
    int c = Mix_PlayChannel(-1, chunk, -1);
    if (c >= 0) {
        ApplyPan(c, pan);
        ch = c + 1;
    }
}

void AudioSystem::StopAllLoops() {
    for (int id = 0; id < SND_COUNT; ++id) SetLoop(id, false);
}

float AudioSystem::GetDefaultVolume(const std::string& name) {
    for (const auto& meta : sSoundMetaTable) {
        if (name == meta.name) {
            return meta.defaultVolume;
        }
    }
    return 0.7f;
}

void AudioSystem::PreloadSound(const std::string& name) {
    {
        SoundLock lock(sSoundMutex);
        if (sSounds.find(name) != sSounds.end()) return;
        if (sMissingSounds.count(name)) return;   // known-missing or undecodable: do not retry
    }

    std::string path = "Sounds/" + name;
    if (path.find('.') == std::string::npos) {
        path += ".ogg";
    }

    std::string resolved = Vfs::Resolve(path);
    bool exists = Vfs::Exists(resolved);
    // Decode outside the lock so the main thread never waits on the worker's decoding.
    Mix_Chunk* chunk = exists ? Mix_LoadWAV(resolved.c_str()) : nullptr;

    SoundLock lock(sSoundMutex);
    if (chunk) {
        if (sSounds.find(name) != sSounds.end()) Mix_FreeChunk(chunk);   // lost a race with the other thread
        else sSounds[name] = chunk;
    } else if (sSounds.find(name) == sSounds.end()) {
        sMissingSounds.insert(name);
    }
}

Mix_Chunk* AudioSystem::GetChunk(const std::string& name) {
    {
        SoundLock lock(sSoundMutex);
        auto it = sSounds.find(name);
        if (it != sSounds.end()) return it->second;
    }
    PreloadSound(name);
    SoundLock lock(sSoundMutex);
    auto it = sSounds.find(name);
    return it != sSounds.end() ? it->second : nullptr;
}

void AudioSystem::PreloadAllSounds(const std::function<void(int, int)>& progress) {
    const int total = (int)SND_COUNT;
    int done = 0;
    for (const auto& meta : sSoundMetaTable) {
        PreloadSound(meta.name);
        sPreloadDone = ++done;
        if (progress) progress(done, total);
    }
}

void AudioSystem::StartPreloadThread() {
    if (sPreloadThread) return;
    sPreloadDone = 0;
    sPreloadRunning = 1;
    sPreloadThread = SDL_CreateThreadWithStackSize(
        [](void*) -> int {
            PreloadAllSounds();
            sPreloadRunning = 0;
            return 0;
        }, "SoundPreload", 512 * 1024, nullptr);
    if (!sPreloadThread) {   // no thread available: fall back to loading inline
        sPreloadRunning = 0;
        PreloadAllSounds();
    }
}

int AudioSystem::PreloadedSoundCount() { return sPreloadDone; }
bool AudioSystem::PreloadRunning() { return sPreloadRunning != 0; }

void AudioSystem::FinishPreload() {
    if (sPreloadThread) {
        SDL_WaitThread(sPreloadThread, nullptr);
        sPreloadThread = nullptr;
    }
}

void AudioSystem::PlaySound(const std::string& name, float volumeMultiplier, int loops) {
    Mix_Chunk* chunk = GetChunk(name);
    if (chunk) {
        float baseVol = GetDefaultVolume(name);
        int vol = (int)(sSfxVolume * baseVol * volumeMultiplier);
        if (vol < 0) vol = 0;
        if (vol > 128) vol = 128;
        Mix_VolumeChunk(chunk, vol);
        Mix_PlayChannel(-1, chunk, loops);
    }
}

void AudioSystem::UpdateEngineSound(bool moving) {
    if (moving) {
        if (sEngineChannel < 0 || !Mix_Playing(sEngineChannel)) {
            Mix_Chunk* chunk = GetChunk("diesel");
            if (chunk) {
                Mix_VolumeChunk(chunk, (int)(sSfxVolume * 0.35f));
                sEngineChannel = Mix_PlayChannel(-1, chunk, -1); // Loop indefinitely
            }
        }
    } else {
        if (sEngineChannel >= 0 && Mix_Playing(sEngineChannel)) {
            Mix_HaltChannel(sEngineChannel);
            sEngineChannel = -1;
        }
    }
}

void AudioSystem::SetMusicEnabled(bool on) {
    if (on == sMusicEnabled) return;
    sMusicEnabled = on;
    if (!on) {
        StopMusic();
    } else if (!sMusicRequest.empty()) {
        PlayMusic(sMusicRequest, sMusicRequestLoop);
    }
}

void AudioSystem::PlayMusic(const std::string& path, bool loop) {
    StopMusic();
    sMusicRequest = path;
    sMusicRequestLoop = loop;
    if (!sMusicEnabled) return;   // nothing is loaded while music is off

    // MO3 (tracker module with compressed samples) cannot be decoded by this build; trying
    // only wasted load time. Skip it.
    if (path.size() >= 4) {
        std::string ext = path.substr(path.size() - 4);
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".mo3") {
            static bool sLogged = false;
            if (!sLogged) {
                sLogged = true;
                std::cerr << "[AudioSystem] Skipping unsupported .mo3 music: " << path << std::endl;
            }
            return;
        }
    }

    std::string resolved = Vfs::Resolve(path);
    if (!Vfs::Exists(resolved)) {
        std::cerr << "[AudioSystem] Music not found: " << path << std::endl;
        return;
    }

    sCurrentMusic = Mix_LoadMUS(resolved.c_str());
    if (sCurrentMusic) {
        Mix_VolumeMusic(sMusicVolume);
        Mix_PlayMusic(sCurrentMusic, loop ? -1 : 1);
    } else {
        std::cerr << "[AudioSystem] Failed to load music: " << Mix_GetError() << std::endl;
    }
}

void AudioSystem::StopMusic() {
    if (sCurrentMusic) {
        Mix_HaltMusic();
        Mix_FreeMusic(sCurrentMusic);
        sCurrentMusic = nullptr;
    }
}

void AudioSystem::PauseMusic() {
    Mix_PauseMusic();
}

void AudioSystem::ResumeMusic() {
    Mix_ResumeMusic();
}

void AudioSystem::SetMusicVolume(int volume) {
    sMusicVolume = std::clamp(volume, 0, 128);
    Mix_VolumeMusic(sMusicVolume);
}

void AudioSystem::SetSfxVolume(int volume) {
    sSfxVolume = std::clamp(volume, 0, 128);
    Mix_Volume(-1, sSfxVolume);
}

} // namespace HeavyWeapon
