#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_thread.h>

namespace HeavyWeapon {

// Sound IDs: indices into the original game's sound table.
enum SoundId {
    SND_GUNPOWERUP = 0,
    SND_TANKFIRE = 1,
    SND_TANKFIRE1 = 2,
    SND_TANKFIRE2 = 3,
    SND_TANKFIRE3 = 4,
    SND_TANKFIRE4 = 5,
    SND_BIGEXPLODE = 6,
    SND_SMALLEXPLODE = 7,
    SND_BOMBFALL = 8,
    SND_BULLETHIT = 9,
    SND_TANKEXPLODE = 10,
    SND_MISSILE = 11,
    SND_NUKEBLAST = 12,
    SND_POWERUP = 13,
    SND_SPARKING = 14,
    SND_LASER = 15,
    SND_BOSSBLAST = 16,
    SND_BUTTONDOWN = 17,
    SND_BUTTONUP = 18,
    SND_SMALLMISSILE = 19,
    SND_TANKLASER = 20,
    SND_DENIED = 21,
    SND_STATS = 22,
    SND_ALERT = 23,
    SND_PUPCOPTER = 24,
    SND_STAR = 25,
    SND_LASERPOWERUP = 26,
    SND_SATLASER = 27,
    SND_SAT1 = 28,
    SND_SAT2 = 29,
    SND_SAT3 = 30,
    SND_SAT4 = 31,
    SND_SAT5 = 32,
    SND_SAT6 = 33,
    SND_SAT7 = 34,
    SND_SHORTKEY = 35,
    SND_LONGKEY = 36,
    SND_FRIENDLY1 = 37,
    SND_FRIENDLY2 = 38,
    SND_FRIENDLY3 = 39,
    SND_FRIENDLYDIE = 40,
    SND_MEGALASER = 41,
    SND_ENEMYTANKGUN = 42,
    SND_SHORTING = 43,
    SND_STATICHIT = 44,
    SND_STATICSHOT = 45,
    SND_MEGALASER_OUT = 46,
    SND_MEGALASER_START = 47,
    SND_MEGALASER_STOP = 48,
    SND_MAPOVER = 49,
    SND_STARSMASH = 50,
    SND_ENERGYFIRE = 51,
    SND_STATBEEP = 52,
    SND_REINFORCEMENTS = 53,
    SND_ENEMYFIRE = 54,
    SND_V_ATOMICTANK = 55,
    SND_V_GAMEOVER = 56,
    SND_V_GETREADY = 57,
    SND_V_LEVELCOMPLETE = 58,
    SND_V_MEGALASER = 59,
    SND_V_PREPARE = 60,
    SND_V_DANGER = 61,
    SND_V_WAFFLE = 62,
    SND_MEGAUP1 = 63,
    SND_MEGAUP2 = 64,
    SND_MEGAUP3 = 65,
    SND_MEGAUP4 = 66,
    SND_ORBHIT = 67,
    SND_FLAK = 68,
    SND_AIRRAID = 69,
    SND_ROBOTSMASH = 70,
    SND_ROBOTLASER = 71,
    SND_EARTHQUAKE = 72,
    SND_AIRMINE = 73,
    SND_METEOR = 74,
    SND_TRACTORBEAM = 75,
    SND_DIESEL = 76,
    SND_AIRBRAKE = 77,
    SND_CHAIN = 78,
    SND_BIGTHUD = 79,
    SND_THUNDER = 80,
    SND_BOLTCHARGE = 81,
    SND_JETDIVE = 82,
    SND_DEFLECT = 83,
    SND_BOSSCOPTER = 84,
    SND_SWOOSH = 85,
    SND_BOSSLASER = 86,
    SND_BOSSLASERBLAST = 87,
    SND_SHIELDUP = 88,
    SND_SHIELDDOWN = 89,
    SND_UPGRADE = 90,
    SND_SPEEDUP = 91,
    SND_COUNT
};

class AudioSystem {
public:
    static void Init();
    static void Shutdown();

    // Sound effects
    static void PlaySound(const std::string& name, float volumeMultiplier = 1.0f, int loops = 0);
    static void PreloadSound(const std::string& name);
    static void PreloadAllSounds(const std::function<void(int, int)>& progress = {});
    // Same, on a worker thread so texture uploads can overlap the OGG decoding. Poll
    // PreloadedSoundCount()/PreloadRunning(); FinishPreload() joins (safe to call any time).
    static void StartPreloadThread();
    static int PreloadedSoundCount();
    static bool PreloadRunning();
    static void FinishPreload();   // every entry of the sound table, once at boot

    // Original game's PlaySound(id, pan): pan is -10000..10000, volume < 0 uses the
    // table volume. Call Tick() once per 100 Hz game tick for the repeat cooldowns.
    static void PlaySoundId(int id, int pan = 0, float volume = -1.0f);
    static void Tick();
    // Looping sound instance per ID (megalaser hum, helicopter rotors...): starts, re-pans
    // or stops it. One instance per ID, like the original's held SoundInstance pointers.
    static void SetLoop(int id, bool on, int pan = 0);
    static void StopAllLoops();

    // Engine diesel sound loop
    static void UpdateEngineSound(bool moving);

    // Music
    static void PlayMusic(const std::string& path, bool loop = true);
    static void StopMusic();
    // Background music on/off. When off, PlayMusic remembers the request but loads nothing;
    // turning it back on starts the most recently requested track. SFX are unaffected.
    static void SetMusicEnabled(bool on);
    static bool IsMusicEnabled() { return sMusicEnabled; }
    static void PauseMusic();
    static void ResumeMusic();

    static void SetMusicVolume(int volume); // 0 to 128
    static void SetSfxVolume(int volume);   // 0 to 128

private:
    static std::unordered_map<std::string, Mix_Chunk*> sSounds;
    static std::unordered_set<std::string> sMissingSounds;
    static Mix_Music* sCurrentMusic;
    static int sMusicVolume;
    static bool sMusicEnabled;
    static std::string sMusicRequest;
    static bool sMusicRequestLoop;
    static int sSfxVolume;
    static int sEngineChannel;
    static int sCooldown[SND_COUNT];
    static int sLoopChannel[SND_COUNT];

    // Looks up a loaded chunk (lazy-loading it), thread-safe against the preload worker.
    static Mix_Chunk* GetChunk(const std::string& name);
    static SDL_mutex* sSoundMutex;
    static SDL_Thread* sPreloadThread;
    static volatile int sPreloadDone;
    static volatile int sPreloadRunning;

    static float GetDefaultVolume(const std::string& name);
};

} // namespace HeavyWeapon
