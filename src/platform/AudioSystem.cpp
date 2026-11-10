#include "AudioSystem.h"
#include "Vfs.h"
#include <SDL3_mixer/SDL_mixer.h>
#include <libopenmpt/libopenmpt.h>
#include <cstdio>
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

MIX_Mixer* AudioSystem::sMixer = nullptr;
MIX_Track* AudioSystem::sTracks[AudioSystem::kNumTracks] = {};
std::unordered_map<std::string, MIX_Audio*> AudioSystem::sSounds;
std::unordered_set<std::string> AudioSystem::sMissingSounds;
int AudioSystem::sMusicVolume = 100;
bool AudioSystem::sMusicEnabled = true;
int AudioSystem::sCurrentSong = -1;
bool AudioSystem::sCurrentSongNoLoop = false;
int AudioSystem::sSfxVolume = 100;
int AudioSystem::sEngineChannel = -1;
SDL_Mutex* AudioSystem::sSoundMutex = nullptr;
SDL_Thread* AudioSystem::sPreloadThread = nullptr;
volatile int AudioSystem::sPreloadDone = 0;
volatile int AudioSystem::sPreloadRunning = 0;

namespace {
struct SoundLock {
    SDL_Mutex* m;
    explicit SoundLock(SDL_Mutex* mu) : m(mu) { if (m) SDL_LockMutex(m); }
    ~SoundLock() { if (m) SDL_UnlockMutex(m); }
};

// Volumes stay on SDL2_mixer's 0..128 scale. The SDL2 build applied the SFX volume twice
// (channel volume x chunk volume = sfx * base), so effects keep that same mix here.
float SfxGain(int sfxVolume, float base) {
    return (sfxVolume / 128.0f) * std::clamp((int)(sfxVolume * base), 0, 128) / 128.0f;
}

// ---- Music --------------------------------------------------------------------------
// The original's music table (VA 0x5274c0). Song ids index it; LoadMusic(i, "music\\" +
// file) loads each entry as its own BASS channel, so two sections of the MO3 can play
// (cross-fade) at once. The same here: one track and one libopenmpt instance per song.
struct SongDef { const char* file; int order; };
constexpr SongDef kSongs[SONG_COUNT] = {
    { "Music/LoveTheme.ogg", -1 },
    { "Music/AtomicTank.mo3", 0 },
    { "Music/AtomicTank.mo3", 42 },
    { "Music/AtomicTank.mo3", 59 },
    { "Music/AtomicTank.mo3", 73 },
    { "Music/AtomicTank.mo3", 79 },
};
// MusicInterface fades step the volume once per 100 Hz update: FadeIn 0.002 (5 s),
// FadeOut/FadeOutAll 0.004 (2.5 s).
constexpr Sint64 kFadeInMs = 5000;
constexpr Sint64 kFadeOutMs = 2500;
// libopenmpt renders this 22-channel module hot (peaks ~2.3 at 0 dB); -5 dB keeps it
// near full scale and level with LoveTheme.ogg.
constexpr int kModuleGainMillibel = -500;
constexpr int kModuleRate = 48000;

struct SongState {
    MIX_Track* track = nullptr;
    MIX_Audio* audio = nullptr;          // LoveTheme.ogg, streamed from disk
    openmpt_module* mod = nullptr;       // AtomicTank.mo3 sections
    SDL_AudioStream* stream = nullptr;   // fed by RenderModule on the mixer thread
    bool loadTried = false;
};
SongState sSongs[SONG_COUNT];

// SDL_AudioStream get-callback: renders the module on demand. Runs on the audio thread
// with the stream locked; StartSong locks the same stream to reposition the module.
void SDLCALL RenderModule(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto* mod = static_cast<openmpt_module*>(user);
    float buf[2 * 1024];
    while (additional > 0) {
        size_t frames = (size_t)std::min(additional / 8 + 1, 1024);   // 8 bytes per frame
        size_t got = openmpt_module_read_interleaved_float_stereo(mod, kModuleRate, frames, buf);
        if (got == 0) break;   // a one-shot section ended
        SDL_PutAudioStreamData(stream, buf, (int)(got * 8));
        additional -= (int)(got * 8);
    }
}

std::vector<char> ReadWholeFile(const std::string& path) {
    std::vector<char> data;
    if (FILE* f = fopen(path.c_str(), "rb")) {
        char chunk[65536];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) data.insert(data.end(), chunk, chunk + n);
        fclose(f);
    }
    return data;
}
}

void AudioSystem::Init() {
    if (!sSoundMutex) sSoundMutex = SDL_CreateMutex();
    if (!MIX_Init()) {
        std::cerr << "[AudioSystem] MIX_Init error: " << SDL_GetError() << std::endl;
        return;
    }
    sMixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!sMixer) {
        std::cerr << "[AudioSystem] Cannot open audio device: " << SDL_GetError() << std::endl;
        MIX_Quit();
        return;
    }
    for (MIX_Track*& t : sTracks) t = MIX_CreateTrack(sMixer);
    for (SongState& s : sSongs) s.track = MIX_CreateTrack(sMixer);
}

void AudioSystem::Shutdown() {
    FinishPreload();
    StopMusic();
    sEngineChannel = -1;
    if (sMixer) MIX_StopAllTracks(sMixer, 0);
    for (auto& pair : sSounds) {
        if (pair.second) MIX_DestroyAudio(pair.second);
    }
    sSounds.clear();
    sMissingSounds.clear();
    if (sMixer) {
        MIX_DestroyMixer(sMixer);   // also destroys its tracks; no more stream callbacks
        sMixer = nullptr;
        for (MIX_Track*& t : sTracks) t = nullptr;
        MIX_Quit();
    }
    for (SongState& s : sSongs) {
        if (s.stream) SDL_DestroyAudioStream(s.stream);
        if (s.mod) openmpt_module_destroy(s.mod);
        if (s.audio) MIX_DestroyAudio(s.audio);
        s = SongState();
    }
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
static MIX_StereoGains PanGains(int pan) {
    float t = std::clamp(pan / 10000.0f, -1.0f, 1.0f);
    float atten = std::pow(10.0f, -std::abs(t) * 100.0f / 20.0f); // dB attenuation of the far side
    return { t > 0 ? atten : 1.0f, t < 0 ? atten : 1.0f };
}

bool AudioSystem::TrackBusy(int track) {
    if (track < 0 || track >= kNumTracks || !sTracks[track]) return false;
    return MIX_TrackPlaying(sTracks[track]) || MIX_TrackPaused(sTracks[track]);
}

void AudioSystem::HaltTrack(int track) {
    if (track >= 0 && track < kNumTracks && sTracks[track]) MIX_StopTrack(sTracks[track], 0);
}

int AudioSystem::PlayOnFreeTrack(MIX_Audio* audio, float gain, int pan, int loops) {
    if (!sMixer || !audio) return -1;
    for (int i = 0; i < kNumTracks; ++i) {
        if (!sTracks[i] || TrackBusy(i)) continue;
        MIX_Track* t = sTracks[i];
        MIX_SetTrackAudio(t, audio);
        MIX_SetTrackGain(t, gain);
        MIX_StereoGains g = PanGains(pan);
        MIX_SetTrackStereo(t, &g);
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
        bool ok = MIX_PlayTrack(t, props);
        SDL_DestroyProperties(props);
        return ok ? i : -1;
    }
    return -1;
}

void AudioSystem::PlaySoundId(int id, int pan, float volume) {
    if (id < 0 || id >= SND_COUNT) return;
    // FUN_00401150: each sound is suppressed for 10 ticks after playing (20 for the
    // explosion sounds) so bursts of hits do not stack.
    if (sCooldown[id] > 0) return;
    sCooldown[id] = (id == SND_BIGEXPLODE || id == SND_SMALLEXPLODE) ? 20 : 10;

    const std::string name = sSoundMetaTable[id].name;
    MIX_Audio* chunk = GetChunk(name);
    if (!chunk) return;

    float base = volume >= 0.0f ? volume : (float)sSoundMetaTable[id].defaultVolume;
    PlayOnFreeTrack(chunk, SfxGain(sSfxVolume, base), pan, 0);
}

void AudioSystem::SetLoop(int id, bool on, int pan) {
    if (id < 0 || id >= SND_COUNT) return;
    int& ch = sLoopChannel[id];   // stored as track + 1 (0 = none)
    if (!on) {
        if (ch) HaltTrack(ch - 1);
        ch = 0;
        return;
    }
    if (ch && TrackBusy(ch - 1)) {
        MIX_StereoGains g = PanGains(pan);
        MIX_SetTrackStereo(sTracks[ch - 1], &g);
        return;
    }
    const std::string name = sSoundMetaTable[id].name;
    MIX_Audio* chunk = GetChunk(name);
    if (!chunk) return;
    int c = PlayOnFreeTrack(chunk, SfxGain(sSfxVolume, sSoundMetaTable[id].defaultVolume), pan, -1);
    if (c >= 0) ch = c + 1;
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
    MIX_Audio* chunk = (exists && sMixer) ? MIX_LoadAudio(sMixer, resolved.c_str(), true) : nullptr;

    SoundLock lock(sSoundMutex);
    if (chunk) {
        if (sSounds.find(name) != sSounds.end()) MIX_DestroyAudio(chunk);   // lost a race with the other thread
        else sSounds[name] = chunk;
    } else if (sSounds.find(name) == sSounds.end()) {
        sMissingSounds.insert(name);
    }
}

MIX_Audio* AudioSystem::GetChunk(const std::string& name) {
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
    // Music too, so the first song of each kind does not hitch (the MO3 takes ~45 ms per song).
    for (int i = 0; i < SONG_COUNT; ++i) LoadSong(i);
}

void AudioSystem::StartPreloadThread() {
    if (sPreloadThread) return;
    sPreloadDone = 0;
    sPreloadRunning = 1;
    sPreloadThread = SDL_CreateThread(
        [](void*) -> int {
            PreloadAllSounds();
            sPreloadRunning = 0;
            return 0;
        }, "SoundPreload", nullptr);
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
    MIX_Audio* chunk = GetChunk(name);
    if (chunk) {
        PlayOnFreeTrack(chunk, SfxGain(sSfxVolume, GetDefaultVolume(name) * volumeMultiplier), 0, loops);
    }
}

void AudioSystem::UpdateEngineSound(bool moving) {
    if (moving) {
        if (!TrackBusy(sEngineChannel)) {
            MIX_Audio* chunk = GetChunk("diesel");
            if (chunk) {
                sEngineChannel = PlayOnFreeTrack(chunk, SfxGain(sSfxVolume, 0.35f), 0, -1); // Loop indefinitely
            }
        }
    } else if (TrackBusy(sEngineChannel)) {
        HaltTrack(sEngineChannel);
        sEngineChannel = -1;
    }
}

// Loads a song's data once (thread-safe: the preload worker loads them all at boot).
bool AudioSystem::LoadSong(int song) {
    if (song < 0 || song >= SONG_COUNT || !sMixer) return false;
    SoundLock lock(sSoundMutex);
    SongState& s = sSongs[song];
    if (s.loadTried) return s.audio || s.mod;
    s.loadTried = true;

    const SongDef& def = kSongs[song];
    std::string path = Vfs::Resolve(def.file);
    if (!Vfs::Exists(path)) {
        std::cerr << "[AudioSystem] Music not found: " << def.file << std::endl;
        return false;
    }
    if (def.order < 0) {
        s.audio = MIX_LoadAudio(sMixer, path.c_str(), false);   // streamed
        if (!s.audio) {
            std::cerr << "[AudioSystem] Failed to load music " << def.file << ": " << SDL_GetError() << std::endl;
            return false;
        }
        MIX_SetTrackAudio(s.track, s.audio);
        return true;
    }

    std::vector<char> data = ReadWholeFile(path);
    int error = 0;
    s.mod = openmpt_module_create_from_memory2(data.data(), data.size(), openmpt_log_func_silent, nullptr,
                                               openmpt_error_func_ignore, nullptr, &error, nullptr, nullptr);
    if (!s.mod) {
        std::cerr << "[AudioSystem] libopenmpt cannot load " << def.file << " (error " << error << ")" << std::endl;
        return false;
    }
    openmpt_module_set_render_param(s.mod, OPENMPT_MODULE_RENDER_MASTERGAIN_MILLIBEL, kModuleGainMillibel);
    openmpt_module_set_repeat_count(s.mod, -1);
    const SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, kModuleRate };
    s.stream = SDL_CreateAudioStream(&spec, &spec);
    if (!s.stream) {
        openmpt_module_destroy(s.mod);
        s.mod = nullptr;
        return false;
    }
    SDL_SetAudioStreamGetCallback(s.stream, RenderModule, s.mod);
    MIX_SetTrackAudioStream(s.track, s.stream);
    return true;
}

void AudioSystem::StartSong(int song, bool fade, bool noLoop) {
    if (song < 0 || song >= SONG_COUNT) return;
    sCurrentSong = song;
    sCurrentSongNoLoop = noLoop;
    if (!sMusicEnabled || !LoadSong(song)) return;   // remembered for SetMusicEnabled(true)
    std::cout << "[AudioSystem] Song " << song << (fade ? " fades in" : " starts")
              << (kSongs[song].order >= 0 ? " at order " + std::to_string(kSongs[song].order) : std::string())
              << std::endl;

    SongState& s = sSongs[song];
    if (s.mod) {
        SDL_LockAudioStream(s.stream);
        openmpt_module_set_repeat_count(s.mod, noLoop ? 0 : -1);
        openmpt_module_set_position_order_row(s.mod, kSongs[song].order, 0);
        SDL_ClearAudioStream(s.stream);
        SDL_UnlockAudioStream(s.stream);
    }
    MIX_SetTrackGain(s.track, sMusicVolume / 128.0f);
    SDL_PropertiesID props = SDL_CreateProperties();
    // An OGG loops by seeking; a module stream loops inside libopenmpt (repeat count).
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, (s.audio && !noLoop) ? -1 : 0);
    if (fade) SDL_SetNumberProperty(props, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, kFadeInMs);
    MIX_PlayTrack(s.track, props);
    SDL_DestroyProperties(props);
}

void AudioSystem::PlaySong(int song, bool noLoop) { StartSong(song, false, noLoop); }
void AudioSystem::FadeInSong(int song, bool noLoop) { StartSong(song, true, noLoop); }

void AudioSystem::FadeOutSong(int song) {
    if (song < 0 || song >= SONG_COUNT) return;
    if (sCurrentSong == song) sCurrentSong = -1;
    MIX_Track* t = sSongs[song].track;
    if (t && MIX_TrackPlaying(t)) MIX_StopTrack(t, MIX_TrackMSToFrames(t, kFadeOutMs));
}

void AudioSystem::FadeOutAllSongs() {
    for (int i = 0; i < SONG_COUNT; ++i) FadeOutSong(i);
    sCurrentSong = -1;
}

bool AudioSystem::IsSongPlaying(int song) {
    if (song < 0 || song >= SONG_COUNT || sCurrentSong != song) return false;
    if (!sMusicEnabled || !sMixer) return true;
    MIX_Track* t = sSongs[song].track;
    return t && (MIX_TrackPlaying(t) || MIX_TrackPaused(t));
}

void AudioSystem::StopMusic() {
    for (SongState& s : sSongs) {
        if (s.track) MIX_StopTrack(s.track, 0);
    }
}

void AudioSystem::SetMusicEnabled(bool on) {
    if (on == sMusicEnabled) return;
    sMusicEnabled = on;
    if (!on) {
        StopMusic();
    } else if (sCurrentSong >= 0) {
        StartSong(sCurrentSong, false, sCurrentSongNoLoop);
    }
}

void AudioSystem::PauseMusic() {
    for (SongState& s : sSongs) {
        if (s.track && MIX_TrackPlaying(s.track)) MIX_PauseTrack(s.track);
    }
}

void AudioSystem::ResumeMusic() {
    for (SongState& s : sSongs) {
        if (s.track && MIX_TrackPaused(s.track)) MIX_ResumeTrack(s.track);
    }
}

void AudioSystem::SetMusicVolume(int volume) {
    sMusicVolume = std::clamp(volume, 0, 128);
    for (SongState& s : sSongs) {
        if (s.track) MIX_SetTrackGain(s.track, sMusicVolume / 128.0f);
    }
}

void AudioSystem::SetSfxVolume(int volume) {
    // Applies to sounds started from now on.
    sSfxVolume = std::clamp(volume, 0, 128);
}

} // namespace HeavyWeapon
