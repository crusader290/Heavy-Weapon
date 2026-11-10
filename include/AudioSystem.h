#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>

// SDL3_mixer handles; the mixer header stays out of every includer.
struct MIX_Mixer;
struct MIX_Audio;
struct MIX_Track;

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

// Song ids: the original's music table (VA 0x5274c0, { char file[40]; int order; }).
// Songs 1-5 are sections of Music/AtomicTank.mo3 starting at the given order; each
// section loops on its own.
enum SongId {
    SONG_LOVETHEME = 0,  // LoveTheme.ogg, played once at the intro screen
    SONG_MISSION,        // order 0: missions (faded in at mission start)
    SONG_BOSS,           // order 42: after the boss warning
    SONG_MENU,           // order 59: main menu, map, armory
    SONG_DEBRIEF,        // order 73: level complete
    SONG_GAMEOVER,       // order 79: last tank destroyed
    SONG_COUNT
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

    // Music, as the original's PopCap MusicInterface calls (song ids are SongId).
    // PlaySong = PlayMusic(id, order): starts at once. FadeInSong = FadeIn(id, order, 0.002):
    // 5 s ramp. FadeOutSong / FadeOutAllSongs = FadeOut / FadeOutAll(true, 0.004): 2.5 s.
    static void PlaySong(int song, bool noLoop = false);
    static void FadeInSong(int song, bool noLoop = false);
    static void FadeOutSong(int song);
    static void FadeOutAllSongs();
    // The song was started and has not been faded out or stopped (a finished one-shot
    // song is not playing). With music off: whether it is the song that would play.
    static bool IsSongPlaying(int song);
    static void StopMusic();   // every song, immediately
    // Background music on/off. When off, songs are remembered but not played; turning it
    // back on restarts the most recently requested song. SFX are unaffected.
    static void SetMusicEnabled(bool on);
    static bool IsMusicEnabled() { return sMusicEnabled; }
    static void PauseMusic();
    static void ResumeMusic();

    static void SetMusicVolume(int volume); // 0 to 128
    static void SetSfxVolume(int volume);   // 0 to 128

private:
    // SDL2_mixer-style channel pool: one-shot and looping sounds each take a free track.
    static constexpr int kNumTracks = 32;
    static MIX_Mixer* sMixer;
    static MIX_Track* sTracks[kNumTracks];
    static std::unordered_map<std::string, MIX_Audio*> sSounds;
    static std::unordered_set<std::string> sMissingSounds;
    static int sMusicVolume;
    static bool sMusicEnabled;
    static int sCurrentSong;          // last song started (-1: none / faded out)
    static bool sCurrentSongNoLoop;
    static void StartSong(int song, bool fade, bool noLoop);
    static bool LoadSong(int song);
    static int sSfxVolume;
    static int sEngineChannel;
    static int sCooldown[SND_COUNT];
    static int sLoopChannel[SND_COUNT];

    // Looks up a loaded sound (lazy-loading it), thread-safe against the preload worker.
    static MIX_Audio* GetChunk(const std::string& name);
    // Starts audio on a free track with a linear gain, pan and loop count (-1 = forever).
    // Returns the track index, or -1 when none is free.
    static int PlayOnFreeTrack(MIX_Audio* audio, float gain, int pan, int loops);
    static bool TrackBusy(int track);
    static void HaltTrack(int track);
    static SDL_Mutex* sSoundMutex;
    static SDL_Thread* sPreloadThread;
    static volatile int sPreloadDone;
    static volatile int sPreloadRunning;

    static float GetDefaultVolume(const std::string& name);
};

} // namespace HeavyWeapon
