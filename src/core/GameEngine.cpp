#include "GameEngine.h"
#include "Vfs.h"
#include "InputManager.h"
#include "ModSystem.h"
#include "Perf.h"
#include "game/Bosses.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <SDL3/SDL.h>

namespace HeavyWeapon {

GameEngine::GameEngine() {}

// ---- settings.ini (music=0|1, survival_best=N, buttons=xbox|playstation|auto) -----
static const char* kSettingsPath = "./settings.ini";

// Missing or corrupt file means defaults (music on).
static bool LoadMusicSetting() {
    bool on = true;
    if (FILE* f = fopen(kSettingsPath, "r")) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            int v;
            if (sscanf(line, " music = %d", &v) == 1) on = (v != 0);
        }
        fclose(f);
    }
    return on;
}

static int LoadBestSurvival() {
    int best = 0;
    if (FILE* f = fopen(kSettingsPath, "r")) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            int v;
            if (sscanf(line, " survival_best = %d", &v) == 1) best = std::max(0, v);
        }
        fclose(f);
    }
    return best;
}

static ButtonStyle LoadButtonSetting() {
    ButtonStyle style = ButtonStyle::Xbox;
    if (FILE* f = fopen(kSettingsPath, "r")) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            char name[32];
            ButtonStyle s;
            if (sscanf(line, " buttons = %31s", name) == 1 && ParseButtonStyle(name, s)) style = s;
        }
        fclose(f);
    }
    return style;
}

// The button style written back is whatever settings.ini had (a --buttons override is
// for one run only).
static ButtonStyle sSavedButtons = ButtonStyle::Xbox;

static void SaveSettings(bool music, int bestSurvival) {
    if (FILE* f = fopen(kSettingsPath, "w")) {
        fprintf(f, "music=%d\nsurvival_best=%d\nbuttons=%s\n", music ? 1 : 0, bestSurvival,
                ButtonStyleName(sSavedButtons));
        fclose(f);
    }
}

const ButtonLabels& GameEngine::Labels() const {
    ButtonStyle style = mButtonStyle;
    if (style == ButtonStyle::Auto) {
        style = InputManager::PlayStationPadConnected() ? ButtonStyle::PlayStation : ButtonStyle::Xbox;
    }
    return GetButtonLabels(style);
}

void GameEngine::ToggleMusic() {
    bool on = !AudioSystem::IsMusicEnabled();
    AudioSystem::SetMusicEnabled(on);
    SaveSettings(on, mBestSurvival);
    AudioSystem::PlaySound("buttondown", 0.5f);
}

static const char* MusicLabel() { return AudioSystem::IsMusicEnabled() ? "MUSIC: ON" : "MUSIC: OFF"; }
// Touch hit-boxes (logical 640x480): the black oval on the title art, and a line on the pause screen.
static bool InRect(float x, float y, float rx, float ry, float rw, float rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

// Boot loading screen: backdrop + "LOADING" + a progress bar, presented at most every ~100 ms
// so the screen never looks frozen but presenting stays cheap.
struct LoadScreen {
    std::string backdrop;
    int texDone = 0, texTotal = 0, sndDone = 0, sndTotal = 0;
    Uint64 lastPresent = 0;

    void Draw() {
        const int total = texTotal + sndTotal;
        float frac = total > 0 ? (float)(texDone + sndDone) / (float)total : 0.0f;
        Renderer::BeginFrame();
        Renderer::DrawTexture(TextureManager::Get(backdrop), 0.0f, 0.0f);
        const float bx = 170.0f, by = 430.0f, bw = 300.0f, bh = 12.0f;
        const std::string label = "LOADING";
        float tw = FontRenderer::GetStringWidth("RubberStampLET20", label);
        FontRenderer::DrawStringBaseline("RubberStampLET20", label, (int)(320 - tw / 2), 420);
        Renderer::DrawFillRect(bx - 2, by - 2, bw + 4, bh + 4, { 0.0f, 0.0f, 0.0f, 0.8f });
        Renderer::DrawFillRect(bx, by, bw * std::clamp(frac, 0.0f, 1.0f), bh, { 0.95f, 0.75f, 0.1f, 1.0f });
        Renderer::EndFrame();
        lastPresent = SDL_GetTicks();
    }
    void Step() {
        SDL_PumpEvents();
        if (SDL_GetTicks() - lastPresent >= 100) Draw();
    }
};

// Draws one frame of `backdrop` so the screen is not black, then loads every gameplay
// texture and sound (once per app run; later calls are no-ops) while showing progress.
// Replaces the first-use loads that used to stall the first seconds of each mission.
static void PreloadAssetsBehindFrame(const std::string& backdrop) {
    static bool sDone = false;
    if (sDone) return;
    sDone = true;
    PERF_BEGIN(perfPre);
    LoadScreen ls;
    ls.backdrop = backdrop;
    ls.sndTotal = (int)SND_COUNT;
    ls.Draw();
    PERF_LAP(perfPre, "preload backdrop frame");
    AudioSystem::StartPreloadThread();
    TextureManager::PreloadAll([&](int done, int total) {
        ls.texDone = done; ls.texTotal = total;
        ls.sndDone = AudioSystem::PreloadedSoundCount();
        ls.Step();
    });
    PERF_LAP(perfPre, "preload textures (main thread)");
    // Sounds decode on a worker while textures upload here; wait for the rest behind the screen.
    while (AudioSystem::PreloadRunning()) {
        ls.sndDone = AudioSystem::PreloadedSoundCount();
        ls.Step();
        SDL_Delay(5);
    }
    AudioSystem::FinishPreload();
    ls.sndDone = ls.sndTotal;
    PERF_LAP(perfPre, "preload sounds (wait after textures)");
}

GameEngine::~GameEngine() {
    Shutdown();
}

bool GameEngine::Init(SDL_Window* window, const LaunchOptions& opts) {
    mOpts = opts;
    Vfs::Init(opts.dataPath);
    Mods::Init(!opts.noMods);   // before anything loads: mods may override game files
    TextureManager::Init();
    if (!Renderer::Init(window)) {
        return false;
    }
    Renderer::SetScaleMode(opts.stretch ? SCALE_STRETCH : SCALE_ASPECT);
    mInitialized = true;
    FontRenderer::Init();
    AudioSystem::Init();
    AudioSystem::SetMusicEnabled(LoadMusicSetting());
    mBestSurvival = LoadBestSurvival();
    sSavedButtons = LoadButtonSetting();
    mButtonStyle = sSavedButtons;
    if (!opts.buttons.empty() && !ParseButtonStyle(opts.buttons, mButtonStyle)) {
        std::cerr << "[GameEngine] Unknown --buttons value: " << opts.buttons << " (xbox, playstation, auto)" << std::endl;
    }
    InputManager::Init();

    // Load game definitions
    XmlLoader::LoadCrafts("data/craft.xml", mCraftDefs);
    XmlLoader::LoadLevels("data/levels.xml", mLevels);
    XmlLoader::LoadWaves("data/waves.xml", mLevels);
    XmlLoader::LoadBosses("data/bosses.xml", mBossDefs);
    for (int i = 0; i < 10; ++i) {
        std::vector<LevelDef> tiers;
        if (XmlLoader::LoadSurvival("data/survival" + std::to_string(i) + ".xml", tiers)) {
            mSurvivalSets.push_back(std::move(tiers));
        }
    }
    XmlLoader::LoadAnims("Images/Anims/Anims.xml", mLevelAnims);

    PreloadAssetsBehindFrame("mainmenu");

    mState = STATE_TITLE;
    mCurrentLevelIndex = std::clamp(opts.level, 0, std::max(0, (int)mLevels.size() - 1));
    if (opts.startState == "map") {
        mState = STATE_MISSION_SELECT;
    } else if (opts.startState == "play") {
        if (opts.armoryLevel > 0) {
            for (int i = UP_ORBS; i <= UP_STATIC; ++i) mApp.up[i] = std::min(opts.armoryLevel, 3);
            mApp.up[UP_SPREAD] = std::min(opts.armoryLevel, 4);
        }
        StartLevel(mCurrentLevelIndex);
        if (opts.startProgress != -1) {
            mBoard->SetProgress(opts.startProgress >= 0 ? opts.startProgress : mBoard->Length() + opts.startProgress);
        }
    } else if (opts.startState == "survival") {
        StartLevel(mCurrentLevelIndex, true);
        if (opts.startProgress > 0) mBoard->SetProgress(opts.startProgress);
    } else if (opts.startState == "options") {
        mState = STATE_OPTIONS;
    } else if (opts.startState == "help") {
        mState = STATE_HELP;
    } else if (opts.startState == "armory") {
        mUpgradePoints = 3;
        mState = STATE_ARMORY;
    }

    // The original's intro screen plays LoveTheme once; the menus then fade in their own
    // section of AtomicTank.mo3 (UpdateMenuMusic).
    if (mState == STATE_TITLE) AudioSystem::PlaySong(SONG_LOVETHEME, true);
    return true;
}

void GameEngine::Shutdown() {
    if (!mInitialized) return;
    mInitialized = false;
    mBoard.reset();       // its projectiles hold mod callbacks
    Mods::Shutdown();
    AudioSystem::Shutdown();
    InputManager::Shutdown();
    TextureManager::Shutdown();
    Renderer::Shutdown();
}

void GameEngine::Run() {
    // With --frames the timestep is fixed so captured frames are reproducible.
    const bool fixedRun = mOpts.maxFrames > 0;
    int frame = 0;
    Uint64 lastTicks = SDL_GetTicks();
    while (mRunning) {
        Uint64 currentTicks = SDL_GetTicks();
        float dt = (float)(currentTicks - lastTicks) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f; // Cap at 20fps minimum
        lastTicks = currentTicks;
        if (fixedRun) dt = FIXED_DT;

        PERF_BEGIN(perfFrame);
        Update(dt);
        Render();
        if (mState == STATE_PLAYING && mBoard) PERF_END_FRAME(PERF_NOW() - perfFrame, mBoard->RespawnTimer(), mApp.tick);

        if (InputManager::GetState().quitRequested) mRunning = false;
        if (fixedRun && ++frame >= mOpts.maxFrames) {
            if (!mOpts.screenshotPath.empty()) {
                bool ok = Renderer::SaveScreenshot(mOpts.screenshotPath);
                std::cout << (ok ? "[GameEngine] Saved screenshot " : "[GameEngine] Failed to save screenshot ")
                          << mOpts.screenshotPath << std::endl;
            }
            mRunning = false;
        }
    }
}

void GameEngine::Update(float dt) {
    InputManager::Update();

    mMenuGlowAnim += 3.0f * dt;

    if (mState != mPrevState) {   // pause, game over, new level...: drop any stale finger tracking
        InputManager::ResetTouch();
        mPrevState = mState;
    }

    switch (mState) {
        case STATE_TITLE:          UpdateTitle(dt); break;
        case STATE_MISSION_SELECT: UpdateMissionSelect(dt); break;
        case STATE_PLAYING:        UpdatePlaying(dt); break;
        case STATE_ARMORY:         UpdateArmory(dt); break;
        case STATE_PAUSED:         UpdatePaused(dt); break;
        case STATE_GAMEOVER:       UpdateGameOver(dt); break;
        case STATE_DEBRIEF:        UpdateDebrief(); break;
        case STATE_OPTIONS:        UpdateOptions(); break;
        case STATE_HELP:           UpdateHelp(); break;
        default: break;
    }
    UpdateMenuMusic();
}

// The original's main menu, map and armory each do `if (!IsPlaying(3)) { FadeOutAll();
// FadeIn(3, 59); }` (0x425e20, 0x410690, 0x40f190), so the menu section keeps playing
// across them. The title (and its options/help pages) first lets LoveTheme play out.
void GameEngine::UpdateMenuMusic() {
    switch (mState) {
        case STATE_TITLE:
        case STATE_OPTIONS:
        case STATE_HELP:
            if (AudioSystem::IsSongPlaying(SONG_LOVETHEME)) return;
            [[fallthrough]];
        case STATE_MISSION_SELECT:
        case STATE_ARMORY:
        case STATE_VICTORY:
            if (!AudioSystem::IsSongPlaying(SONG_MENU)) {
                AudioSystem::FadeOutAllSongs();
                AudioSystem::FadeInSong(SONG_MENU);
            }
            break;
        default:
            break;
    }
}

// Main menu buttons are baked into mainmenu.jpg; these are their centres (measured from
// the art). The highlight sprites mainbigbtn (110x110) / mainsmallbtn (120x40) are
// additive rim glows centred on the selected button.
struct MenuButton {
    float cx, cy;
    bool big;
};
static const MenuButton kMenuButtons[] = {
    {  99.5f, 250.0f, true  }, // MISSION
    { 229.5f, 380.0f, true  }, // SURVIVAL
    { 534.5f, 215.0f, false }, // HEROES
    { 505.0f, 285.0f, false }, // OPTIONS
    { 504.5f, 354.5f, false }, // HELP
    { 534.0f, 425.0f, false }, // QUIT
};
enum { MENU_MISSION = 0, MENU_SURVIVAL, MENU_HEROES, MENU_OPTIONS, MENU_HELP, MENU_QUIT, MENU_COUNT };

static int HitTestMenu(float x, float y) {
    for (int i = 0; i < MENU_COUNT; ++i) {
        const MenuButton& b = kMenuButtons[i];
        float hw = b.big ? 50.0f : 55.0f;
        float hh = b.big ? 50.0f : 16.0f;
        if (std::abs(x - b.cx) <= hw && std::abs(y - b.cy) <= hh) return i;
    }
    return -1;
}

void GameEngine::UpdateTitle(float dt) {
    (void)dt;
    const InputState& input = InputManager::GetState();

    // Big buttons on the left, small ones in a column on the right.
    int prevSel = mMenuSelection;
    if (input.upPressed) {
        mMenuSelection = (mMenuSelection - 1 + MENU_COUNT) % MENU_COUNT;
    } else if (input.downPressed) {
        mMenuSelection = (mMenuSelection + 1) % MENU_COUNT;
    } else if (input.rightPressed && kMenuButtons[mMenuSelection].big) {
        mMenuSelection = MENU_HEROES;
    } else if (input.leftPressed && !kMenuButtons[mMenuSelection].big) {
        mMenuSelection = MENU_MISSION;
    }

    if (input.musicTogglePressed ||
        (input.touchPressed && InRect(input.touchX, input.touchY, 385.0f, 35.0f, 220.0f, 70.0f))) {
        ToggleMusic();
        return;
    }

    bool activate = input.confirmPressed;
    if (input.pointerAim || input.touchPressed) {
        int hit = HitTestMenu(input.touchX, input.touchY);
        if (hit >= 0) {
            mMenuSelection = hit;
            activate |= input.touchPressed;
        }
    }
    if (mMenuSelection != prevSel) {
        AudioSystem::PlaySound("mapover", 0.5f);
    }

    if (!activate) return;

    AudioSystem::PlaySound("buttondown");
    switch (mMenuSelection) {
        case MENU_MISSION:
            mCurrentLevelIndex = 0;
            mApp = AppState();
            Mods::OnRunReset();
            mState = STATE_MISSION_SELECT;
            break;
        case MENU_SURVIVAL:
            if (mSurvivalSets.empty()) {
                AudioSystem::PlaySound("denied", 0.5f);   // survival*.xml missing
                break;
            }
            mApp = AppState();
            Mods::OnRunReset();
            StartLevel(std::rand() % std::max(1, (int)mLevels.size()), true);
            break;
        case MENU_OPTIONS:
            mOptionsSelection = 0;
            mState = STATE_OPTIONS;
            break;
        case MENU_HELP:
            mState = STATE_HELP;
            break;
        case MENU_QUIT:
            mRunning = false;
            break;
        default:
            AudioSystem::PlaySound("denied", 0.5f); // Heroes not implemented yet
            break;
    }
}

// Options and Help screens: a dark panel over the title art.
static const float kOptRowX = 170.0f, kOptRowW = 300.0f, kOptRowH = 40.0f;
static const float kOptRowY[2] = { 160.0f, 220.0f };   // MUSIC, BACK

void GameEngine::UpdateOptions() {
    const InputState& input = InputManager::GetState();
    const int kRows = 2;
    int prevSel = mOptionsSelection;
    if (input.upPressed) mOptionsSelection = (mOptionsSelection + kRows - 1) % kRows;
    if (input.downPressed) mOptionsSelection = (mOptionsSelection + 1) % kRows;

    bool activate = input.confirmPressed;
    if (input.pointerAim || input.touchPressed) {
        for (int i = 0; i < kRows; ++i) {
            if (InRect(input.touchX, input.touchY, kOptRowX, kOptRowY[i], kOptRowW, kOptRowH)) {
                mOptionsSelection = i;
                activate |= input.touchPressed;
            }
        }
    }
    if (mOptionsSelection != prevSel) AudioSystem::PlaySound("mapover", 0.5f);

    if (input.cancelPressed) {
        AudioSystem::PlaySound("buttonup");
        mState = STATE_TITLE;
    } else if (input.musicTogglePressed) {
        ToggleMusic();
    } else if (activate) {
        if (mOptionsSelection == 0) {
            ToggleMusic();
        } else {
            AudioSystem::PlaySound("buttonup");
            mState = STATE_TITLE;
        }
    }
}

void GameEngine::UpdateHelp() {
    const InputState& input = InputManager::GetState();
    if (input.cancelPressed || input.confirmPressed || input.touchPressed) {
        AudioSystem::PlaySound("buttonup");
        mState = STATE_TITLE;
    }
}

void GameEngine::UpdateMissionSelect(float dt) {
    (void)dt;
    const InputState& input = InputManager::GetState();

    if (input.confirmPressed || input.touchPressed) {
        AudioSystem::PlaySound("buttondown");
        AudioSystem::PlaySound("v_atomictank");
        StartLevel(mCurrentLevelIndex);
    } else if (input.cancelPressed) {
        AudioSystem::PlaySound("buttonup");
        mState = STATE_TITLE;
    }
}

void GameEngine::StartLevel(int levelIndex, bool survival) {
    PERF_BEGIN(perfTotal);
    PERF_BEGIN(perfLap);
    mCurrentLevelIndex = levelIndex;
    mApp.mission = survival ? 0 : levelIndex;   // survival runs as mission 0; its backdrop is its own (below)
    mSurvivalMode = survival;
    mApp.lives = survival ? 1 : 3;   // three tanks per mission (0x403e20); survival gives a single one
    mTickAccum = 0.0f;
    mLevelEndTimer = 0;

    const LevelDef* level = (levelIndex < (int)mLevels.size()) ? &mLevels[levelIndex] : nullptr;
    // Region of the mission (0x4015f0): the nine regions repeat, mission 19 is Red Star HQ
    // and survival has its own backdrop; Anims.xml has one entry per region.
    const int region = survival ? 10 : (levelIndex == 18 ? 9 : levelIndex % 9);
    std::string theme = survival ? "survival" : (level ? level->bgTheme : "antagonistan");
    PreloadAssetsBehindFrame("Images/Backgrounds/" + theme + "_sky");   // no-op after boot
    std::vector<AnimDef> anims;
    if (region < (int)mLevelAnims.size()) anims = mLevelAnims[region];
    WorldRenderer::SetTheme(theme, anims);
    PERF_LAP(perfLap, "StartLevel SetTheme");
    if (!survival) TextureManager::PreloadFolder(BossSpriteFolder(levelIndex));   // this mission's boss, not all of them
    PERF_LAP(perfLap, "StartLevel boss sprites");

    std::vector<CraftDef> byId(mCraftDefs.size() + 1);
    for (const auto& kv : mCraftDefs) {
        if (kv.second.id > 0 && kv.second.id < (int)byId.size()) byId[kv.second.id] = kv.second;
    }
    mBoard = std::make_unique<Board>(mApp, survival ? nullptr : level, byId, survival);
    if (survival) mBoard->SetSurvivalLevels(&mSurvivalSets[std::rand() % mSurvivalSets.size()]);   // one of ten sets per run
    mBoard->SetBossDefs(&mBossDefs);
    PERF_LAP(perfLap, "StartLevel Board ctor");

    // 0x4184b0: FadeOutAll, then FadeIn(1, 0): the mission section.
    AudioSystem::FadeOutAllSongs();
    AudioSystem::FadeInSong(SONG_MISSION);
    PERF_LAP(perfLap, "StartLevel music");
    AudioSystem::PlaySoundId(SND_V_GETREADY);
    mState = STATE_PLAYING;
    InputManager::ResetTouch();
    Mods::OnLevelStart();
    PERF_LAP(perfTotal, "StartLevel TOTAL");
}

// Feeds input to the board. The tank drives with A/D (or the arrows, left stick, D-pad) and
// aims at the mouse cursor or along the right stick. On a touch screen the finger is the
// point the tank drives toward and aims at, like the original's mouse handlers.
void GameEngine::ApplyBoardInput() {
    const InputState& input = InputManager::GetState();
    Board& board = *mBoard;

    bool stickAim = std::abs(input.aimAxisX) > 0.0f || std::abs(input.aimAxisY) > 0.0f;
    // A gamepad stick/D-pad or the keyboard always wins over a finger on the screen (an
    // accidental touch must not hijack the tank); touch can still fire.
    bool fingerSteers = input.fingerDown && !stickAim && std::abs(input.moveAxisX) == 0.0f;
    bool mouseAims = !input.fingerDown && (input.pointerAim || input.touchDown) && !stickAim;
    if (fingerSteers) {
        board.SetTarget((int)input.touchX, (int)input.touchY);
        board.SetDriveOverride(false, 0.0f);
    } else if (mouseAims) {
        // The mouse only aims; driving stays on the keys.
        board.SetTarget((int)input.touchX, (int)input.touchY);
        board.SetDriveOverride(true, input.moveAxisX);
    } else {
        // Point the virtual cursor 200px out along the right stick (straight up when idle),
        // so the original aiming code applies unchanged.
        float ax = input.aimAxisX, ay = input.aimAxisY;
        if (!stickAim) { ax = 0.0f; ay = -1.0f; }
        if (ay > 0.0f) ay = 0.0f;
        float len = std::sqrt(ax * ax + ay * ay);
        if (len < 0.001f) { ax = 0.0f; ay = -1.0f; len = 1.0f; }
        int tx = (int)(board.TankX() + 320.0 + ax / len * 200.0);
        int ty = (int)(board.TankY() - 12 + ay / len * 200.0);
        board.SetTarget(tx, ty);
        board.SetDriveOverride(true, input.moveAxisX);
    }
    board.SetFiring(input.fireCannon || input.touchDown || mOpts.autoFire);
    board.SetGodMode(mOpts.god);
    if (input.fireNukePressed) board.FireNuke();
}

void GameEngine::UpdatePlaying(float dt) {
    const InputState& input = InputManager::GetState();
    if (input.pausePressed) {
        mState = STATE_PAUSED;
        return;
    }

    // The original logic runs at a fixed 100 Hz; render frames run at the display rate.
    mTickAccum += dt * POPCAP_TICKS_PER_SEC;
    if (mTickAccum > 10.0f) mTickAccum = 10.0f;
    while (mTickAccum >= 1.0f) {
        mTickAccum -= 1.0f;
        ApplyBoardInput();
        PERF_BEGIN(perfTick);
        mBoard->Update();
        PERF_BOARD_TICK(PERF_NOW() - perfTick);
        AudioSystem::Tick();
    }

    if (mBoard->IsGameOver()) {
        if (mSurvivalMode) {
            mSurvivalTime = mBoard->SurvivalSeconds();
            mSurvivalNewBest = mSurvivalTime > mBestSurvival;
            if (mSurvivalNewBest) {
                mBestSurvival = mSurvivalTime;
                SaveSettings(AudioSystem::IsMusicEnabled(), mBestSurvival);
            }
        }
        AudioSystem::PlaySoundId(SND_V_GAMEOVER);
        mState = STATE_GAMEOVER;
        return;
    }

    if (mBoard->IsLevelComplete()) {
        // Debriefing bonuses (0x426e60), scaled by the mission number.
        Debrief d;
        int m = mApp.mission + 1;
        d.kills = mBoard->Kills();
        d.percent = mBoard->Spawned() > 0 ? (int)((double)mBoard->Kills() / mBoard->Spawned() * 100.0) : 0;
        d.percent = std::clamp(d.percent, 0, 100);
        d.killBonus = d.percent == 100 ? m * 10000 : d.percent >= 95 ? m * 5000 : d.percent >= 90 ? m * 1000 : 0;
        d.friendlyBonus = mBoard->FriendlyKills() == 0 ? m * 5000 : 0;
        d.survivalBonus = mApp.lives == 2 ? m * 5000 : 0;
        d.lost = 2 - mApp.lives;
        d.score = mApp.score;
        mApp.score += d.killBonus + d.friendlyBonus + d.survivalBonus;
        d.rank = std::min(mApp.mission + 1, 19);
        mDebrief = d;
        AudioSystem::StopAllLoops();
        AudioSystem::PlaySoundId(SND_V_LEVELCOMPLETE);
        // 0x41bbb5: the debriefing starts PlayMusic(4, 73), then FadeOut(2) the boss music
        // (and the mission music, in case the level ended without a boss warning).
        AudioSystem::FadeOutSong(SONG_BOSS);
        AudioSystem::FadeOutSong(SONG_MISSION);
        AudioSystem::PlaySong(SONG_DEBRIEF);
        mState = STATE_DEBRIEF;
    }
}

void GameEngine::UpdateDebrief() {
    const InputState& input = InputManager::GetState();
    ++mDebrief.timer;
    // A stat line appears every 40 frames, each with a beep.
    if (mDebrief.timer > 60 && mDebrief.timer <= 60 + 7 * 40 && (mDebrief.timer - 60) % 40 == 0) {
        AudioSystem::PlaySoundId(SND_STATBEEP);
    }
    if (mDebrief.timer > 60 && (input.confirmPressed || input.touchPressed)) {
        AudioSystem::PlaySoundId(SND_BUTTONDOWN);
        mUpgradePoints++;
        mState = STATE_ARMORY;
    }
}

void GameEngine::RenderDebrief() {
    if (mBoard) mBoard->Draw();
    static const char* kRanks[20] = {
        "PRIVATE", "PRIVATE FIRST CLASS", "CORPORAL", "SERGEANT", "SERGEANT FIRST CLASS", "MASTER SERGEANT",
        "FIRST SERGEANT", "WARRANT OFFICER", "CHIEF WARRANT OFFICER", "SECOND LIEUTENANT", "FIRST LIEUTENANT",
        "CAPTAIN", "MAJOR", "LIEUTENANT COLONEL", "COLONEL", "MAJOR GENERAL", "LIEUTENANT GENERAL", "GENERAL",
        "GENERAL OF THE ARMY", "ULTIMATE TANKER"
    };
    const Debrief& d = mDebrief;
    // Panels fade in (0x427460): dark report area, grey rank panel, divider line.
    float a = std::min(d.timer * 4, 200) / 255.0f;
    Renderer::DrawFillRect(0, 0, 480, 480, { 0, 0, 0, a });
    Renderer::DrawFillRect(480, 0, 160, 480, { 0.5f, 0.5f, 0.5f, std::min(a * 1.75f, 1.0f) });
    Renderer::DrawFillRect(479, 0, 3, 480, { 0, 0, 0, 1 });
    if (d.timer < 30) return;

    const Color4f white = Color4f::White();
    float tw = FontRenderer::GetStringWidth("RubberStampLET42", "DEBRIEFING");
    FontRenderer::DrawStringBaseline("RubberStampLET42", "DEBRIEFING", (int)(240 - tw / 2), 100, white);
    Renderer::DrawCel(TextureManager::Get("largeinsignia"), d.rank, 0, 560, 200, true);
    float pw = FontRenderer::GetStringWidth("Outlined", "PROMOTION!");
    FontRenderer::DrawStringBaseline("Outlined", "PROMOTION!", (int)(560 - pw / 2), 100, white);
    // Rank name, split over two lines when it does not fit the 160px panel.
    std::string rank = kRanks[d.rank], second;
    if (FontRenderer::GetStringWidth("Outlined", rank) > 150.0f) {
        size_t cut = rank.rfind(' ');
        if (cut != std::string::npos) {
            second = rank.substr(cut + 1);
            rank = rank.substr(0, cut);
        }
    }
    float rw = FontRenderer::GetStringWidth("Outlined", rank);
    FontRenderer::DrawStringBaseline("Outlined", rank, (int)(560 - rw / 2), 300, white);
    if (!second.empty()) {
        float sw = FontRenderer::GetStringWidth("Outlined", second);
        FontRenderer::DrawStringBaseline("Outlined", second, (int)(560 - sw / 2), 322, white);
    }

    struct Line { const char* label; std::string value; };
    std::vector<Line> lines = {
        { "TOTAL SCORE:", std::to_string(d.score) },
        { "ENEMIES DESTROYED:", std::to_string(d.kills) },
        { "KILL PERCENTAGE:", std::to_string(d.percent) + "%" },
        { "FRIENDLY FIRE BONUS:", std::to_string(d.friendlyBonus) },
        { "SURVIVAL BONUS:", std::to_string(d.survivalBonus) },
        { "REINFORCEMENTS:", "" },
        { d.percent == 100 ? "TOTAL DESTRUCTION:" : d.percent >= 95 ? "MASSIVE DESTRUCTION:" : "MAJOR DESTRUCTION:",
          std::to_string(d.killBonus) },
    };
    static const int kY[7] = { 150, 195, 225, 255, 285, 315, 345 };
    for (size_t i = 0; i < lines.size(); ++i) {
        if (d.timer < 60 + (int)i * 40) break;
        FontRenderer::DrawStringBaseline("RubberStampLET20", lines[i].label, 20, kY[i], white);
        float vw = FontRenderer::GetStringWidth("RubberStampLET20", lines[i].value);
        FontRenderer::DrawStringBaseline("RubberStampLET20", lines[i].value, (int)(460 - vw), kY[i], white);
        if (i == 5) {
            Texture* r = TextureManager::Get("reinforcement");
            for (int k = 0; k < mApp.lives && r; ++k) Renderer::DrawCel(r, 0, 0, 300 + k * 30, kY[i] - 8, true);
        }
    }
    if (Texture* adv = TextureManager::Get("advancebtn")) {
        Rect src = { 0, 0, 135, 45 };
        Rect dst = { 495, 420, 135, 45 };
        Renderer::DrawTexture(adv, dst, src);
    }
}

void GameEngine::UpdateArmory(float dt) {
    (void)dt;
    const InputState& input = InputManager::GetState();
    constexpr int kWeapons = UP_STATIC - UP_ORBS + 1;

    if (input.upPressed) {
        mArmorySelection = (mArmorySelection - 1 + kWeapons) % kWeapons;
        AudioSystem::PlaySoundId(SND_MAPOVER);
    } else if (input.downPressed) {
        mArmorySelection = (mArmorySelection + 1) % kWeapons;
        AudioSystem::PlaySoundId(SND_MAPOVER);
    }

    int& level = mApp.up[UP_ORBS + mArmorySelection];
    if (input.rightPressed || input.altFirePressed) {
        if (mUpgradePoints > 0 && level < 3) {
            ++level;
            --mUpgradePoints;
            AudioSystem::PlaySoundId(SND_UPGRADE);
        } else {
            AudioSystem::PlaySoundId(SND_DENIED);
        }
    } else if (input.leftPressed) {
        if (level > 0) {
            --level;
            ++mUpgradePoints;
            AudioSystem::PlaySoundId(SND_BUTTONUP);
        }
    }

    if (input.confirmPressed) {
        AudioSystem::PlaySoundId(SND_BUTTONDOWN);
        mCurrentLevelIndex++;
        mState = (mCurrentLevelIndex >= NUM_CAMPAIGN_MISSIONS) ? STATE_TITLE : STATE_MISSION_SELECT;
    }
}

void GameEngine::UpdatePaused(float dt) {
    (void)dt;
    const InputState& input = InputManager::GetState();
    if (input.musicTogglePressed ||
        (input.touchPressed && InRect(input.touchX, input.touchY, 170.0f, 325.0f, 300.0f, 40.0f))) {
        ToggleMusic();
    } else if (input.pausePressed || input.confirmPressed) {
        mState = STATE_PLAYING;
    } else if (input.cancelPressed) {
        mState = STATE_TITLE;
    }
}

void GameEngine::UpdateGameOver(float dt) {
    (void)dt;
    const InputState& input = InputManager::GetState();
    if (input.confirmPressed || input.pausePressed || input.touchPressed) {
        mApp = AppState();
        Mods::OnRunReset();
        mState = STATE_TITLE;
    }
}

// -------------------------------------------------------------
// Rendering implementations
// -------------------------------------------------------------

void GameEngine::Render() {
    Renderer::BeginFrame();

    switch (mState) {
        case STATE_TITLE:          RenderTitle(); break;
        case STATE_MISSION_SELECT: RenderMissionSelect(); break;
        case STATE_PLAYING:        RenderPlaying(); break;
        case STATE_ARMORY:         RenderArmory(); break;
        case STATE_PAUSED:         RenderPlaying(); RenderPaused(); break;
        case STATE_GAMEOVER:       RenderPlaying(); RenderGameOver(); break;
        case STATE_DEBRIEF:        RenderDebrief(); break;
        case STATE_OPTIONS:        RenderOptions(); break;
        case STATE_HELP:           RenderHelp(); break;
        default: break;
    }

    Renderer::EndFrame();
}

void GameEngine::RenderTitle() {
    Renderer::DrawTexture(TextureManager::Get("mainmenu"), 0.0f, 0.0f);

    // Hover state: mainglow is a full-screen "over" image of which only the area around
    // the selected button is drawn (lighting its bezel), plus the button's own rim glow.
    const MenuButton& sel = kMenuButtons[mMenuSelection];
    Texture* glowTex = TextureManager::Get("mainglow");
    if (glowTex) {
        Rect area = sel.big ? Rect{ sel.cx - 85.0f, sel.cy - 85.0f, 170.0f, 170.0f }
                            : Rect{ sel.cx - 95.0f, sel.cy - 35.0f, SCREEN_WIDTH - (sel.cx - 95.0f), 70.0f };
        area.x = std::max(area.x, 0.0f);
        area.y = std::max(area.y, 0.0f);
        area.w = std::min(area.w, SCREEN_WIDTH - area.x);
        area.h = std::min(area.h, SCREEN_HEIGHT - area.y);
        Renderer::SetAdditiveBlend(true);
        Renderer::SetColor({ 1.0f, 1.0f, 1.0f, 0.75f + 0.25f * std::sin(mMenuGlowAnim) });
        Renderer::DrawTexture(glowTex, area, area);
        Renderer::SetColor(Color4f::White());
        Renderer::SetAdditiveBlend(false);
    }

    Texture* hlTex = TextureManager::Get(sel.big ? "mainbigbtn" : "mainsmallbtn");
    if (hlTex) {
        Renderer::SetAdditiveBlend(true);
        Renderer::DrawCel(hlTex, 0, 0, sel.cx, sel.cy, true);
        Renderer::SetAdditiveBlend(false);
    }

    // Music toggle readout in the empty black slot of the title art (Tab / Back, or click it).
    FontRenderer::DrawString("Normal", MusicLabel(), 495.0f, 55.0f, { 0.3f, 1.0f, 0.3f, 1.0f }, 1.0f, ALIGN_CENTER);

    // Loaded mods, bottom left (problems are written to mods.log).
    if (!Mods::LoadedMods().empty() || Mods::ErrorCount() > 0) {
        std::string line = "MODS:";
        for (const std::string& m : Mods::LoadedMods()) {
            std::string up = m;
            for (char& ch : up) ch = (char)std::toupper((unsigned char)ch);
            line += (line == "MODS:" ? " " : ", ") + up;
        }
        if (Mods::LoadedMods().empty()) line += " NONE LOADED";
        FontRenderer::DrawString("Normal", line, 8.0f, 452.0f, { 0.3f, 1.0f, 0.3f, 0.9f });
        if (Mods::ErrorCount() > 0) {
            FontRenderer::DrawString("Normal", "MOD ERRORS: SEE MODS.LOG", 8.0f, 434.0f, { 1.0f, 0.3f, 0.3f, 1.0f });
        }
    }
}

void GameEngine::RenderMissionSelect() {
    Renderer::DrawTexture(TextureManager::Get("map"), 0.0f, 0.0f);

    // Mission briefing panel. Provisional layout until the original war-room screen
    // (missionN.png territory overlays, mappointer, maprect) is decompiled.
    const float panelX = 40.0f, panelY = 330.0f, panelW = 560.0f, panelH = 120.0f;
    Renderer::DrawFillRect(panelX, panelY, panelW, panelH, { 0.0f, 0.0f, 0.0f, 0.75f });
    Renderer::DrawRect(panelX, panelY, panelW, panelH, { 0.85f, 0.85f, 0.85f, 0.9f });

    if (mCurrentLevelIndex < (int)mLevels.size()) {
        const LevelDef& level = mLevels[mCurrentLevelIndex];
        std::string title = "MISSION " + std::to_string(mCurrentLevelIndex + 1) + ": " + level.name;
        FontRenderer::DrawString("RubberStampLET20", title, panelX + 14.0f, panelY + 10.0f, { 1.0f, 0.85f, 0.2f, 1.0f });
        if (!level.intelList.empty()) {
            // Greedy word wrap to the panel width.
            std::istringstream words(level.intelList[0].text);
            std::string word, line;
            float lineY = panelY + 40.0f;
            const float maxW = panelW - 28.0f;
            const float lineH = FontRenderer::GetStringHeight("Normal");
            while (words >> word && lineY < panelY + panelH - 44.0f) {
                std::string candidate = line.empty() ? word : line + " " + word;
                if (!line.empty() && FontRenderer::GetStringWidth("Normal", candidate) > maxW) {
                    FontRenderer::DrawString("Normal", line, panelX + 14.0f, lineY, Color4f::White());
                    lineY += lineH;
                    line = word;
                } else {
                    line = candidate;
                }
            }
            if (!line.empty() && lineY < panelY + panelH - 44.0f) {
                FontRenderer::DrawString("Normal", line, panelX + 14.0f, lineY, Color4f::White());
            }
        }
    }
    FontRenderer::DrawString("Normal", Labels().beginPrompt, SCREEN_WIDTH * 0.5f, panelY + panelH - 24.0f,
                             { 0.6f, 1.0f, 0.6f, 1.0f }, 1.0f, ALIGN_CENTER);
}

void GameEngine::RenderPlaying() {
    if (mBoard) mBoard->Draw();
}

void GameEngine::RenderArmory() {
    Renderer::DrawTexture(TextureManager::Get("armory"), 0.0f, 0.0f);

    // Six weapon sockets baked into armory.jpg, three per side (measured from the art).
    // Index order matches WeaponType.
    static const float kRowY[3] = { 60.0f, 160.0f, 260.0f };
    auto socketX = [](int i) { return (i < 3) ? 108.0f : 532.0f; };
    auto levelX = [](int i) { return (i < 3) ? 40.0f : 602.0f; };

    Texture* upgradesTex = TextureManager::Get("upgrades");     // 6 x 72x72
    Texture* lvlTex = TextureManager::Get("upgradelvl");        // 4 x 20x31
    Texture* btnTex = TextureManager::Get("upgradebtns");       // 2x2 +/- glows

    for (int i = 0; i < WEAPON_COUNT; ++i) {
        float cy = kRowY[i % 3];
        if (upgradesTex) {
            Renderer::DrawCel(upgradesTex, i, 0, socketX(i), cy, true);
        }
        if (lvlTex) {
            int lvl = std::clamp(mApp.up[UP_ORBS + i], 0, lvlTex->cols - 1);
            Renderer::DrawCel(lvlTex, lvl, 0, levelX(i), cy, true);
        }
        if (mArmorySelection == i && btnTex) {
            Renderer::SetAdditiveBlend(true);
            float bx = (i < 3) ? 161.0f : 478.0f;
            // Column 0 matches the left-hand sockets' buttons, column 1 the right-hand ones.
            Renderer::DrawCel(btnTex, (i < 3) ? 0 : 1, 0, bx, cy - 14.5f, true);
            Renderer::DrawCel(btnTex, (i < 3) ? 0 : 1, 1, bx, cy + 14.0f, true);
            Renderer::SetAdditiveBlend(false);
        }
    }

    // Central screen: selected weapon and remaining points.
    // Names from the executable's weapon table, in armory slot order.
    const char* weaponNames[WEAPON_COUNT] = {
        "DEFENSE ORBS", "HOMING MISSILE", "LASER", "ROCKETS", "FLAK CANNON", "THUNDERSTRIKE"
    };
    FontRenderer::DrawString("Normal", weaponNames[mArmorySelection], 316.0f, 110.0f,
                             { 0.6f, 1.0f, 0.6f, 1.0f }, 1.0f, ALIGN_CENTER);
    FontRenderer::DrawString("Normal", "POINTS: " + std::to_string(mUpgradePoints), 316.0f, 140.0f,
                             { 0.6f, 1.0f, 0.6f, 1.0f }, 1.0f, ALIGN_CENTER);

    Texture* advanceTex = TextureManager::Get("advancebtn");
    if (advanceTex) {
        // Strip of three 135px button states (normal / over / down); draw "normal".
        float w = advanceTex->width / 3.0f;
        Rect src = { 0.0f, 0.0f, w, (float)advanceTex->height };
        Rect dst = { (SCREEN_WIDTH - w) * 0.5f, 420.0f, w, src.h };
        Renderer::DrawTexture(advanceTex, dst, src);
    }
}

void GameEngine::RenderPaused() {
    Renderer::DrawFillRect(0.0f, 0.0f, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT, { 0.0f, 0.0f, 0.0f, 0.65f });
    FontRenderer::DrawString("RubberStampLET42", "PAUSED", SCREEN_WIDTH * 0.5f, 210.0f, { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f, ALIGN_CENTER);
    FontRenderer::DrawString("Normal", Labels().pauseHint, SCREEN_WIDTH * 0.5f, 290.0f, { 0.85f, 0.85f, 0.85f, 1.0f }, 1.0f, ALIGN_CENTER);
    std::string music = std::string(MusicLabel()) + Labels().pauseMusicHint;
    FontRenderer::DrawString("Normal", music, SCREEN_WIDTH * 0.5f, 335.0f, { 0.85f, 0.85f, 0.85f, 1.0f }, 1.0f, ALIGN_CENTER);
}

static void DrawMenuPanelBackdrop(const char* title) {
    Renderer::DrawTexture(TextureManager::Get("mainmenu"), 0.0f, 0.0f);
    Renderer::DrawFillRect(0.0f, 0.0f, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT, { 0.0f, 0.0f, 0.0f, 0.93f });
    FontRenderer::DrawString("RubberStampLET42", title, SCREEN_WIDTH * 0.5f, 22.0f, { 1.0f, 0.85f, 0.2f, 1.0f }, 1.0f, ALIGN_CENTER);
}

void GameEngine::RenderOptions() {
    DrawMenuPanelBackdrop("OPTIONS");
    const std::string rows[2] = { MusicLabel(), "BACK" };
    for (int i = 0; i < 2; ++i) {
        bool sel = (mOptionsSelection == i);
        if (sel) {
            float a = 0.25f + 0.1f * std::sin(mMenuGlowAnim);
            Renderer::DrawFillRect(kOptRowX, kOptRowY[i], kOptRowW, kOptRowH, { 0.3f, 1.0f, 0.3f, a });
            Renderer::DrawRect(kOptRowX, kOptRowY[i], kOptRowW, kOptRowH, { 0.3f, 1.0f, 0.3f, 0.9f });
        }
        FontRenderer::DrawString("Normal", rows[i], SCREEN_WIDTH * 0.5f, kOptRowY[i] + 10.0f,
                                 sel ? Color4f{ 1.0f, 1.0f, 1.0f, 1.0f } : Color4f{ 0.7f, 0.7f, 0.7f, 1.0f }, 1.0f, ALIGN_CENTER);
    }
    const char* hint = Labels().optionsHint;
    FontRenderer::DrawString("Normal", hint, SCREEN_WIDTH * 0.5f, 400.0f, { 0.6f, 0.6f, 0.6f, 1.0f }, 1.0f, ALIGN_CENTER);
}

// Read-only keymap. Mirrors the bindings in InputManager.cpp; keep the two in sync.
void GameEngine::RenderHelp() {
    DrawMenuPanelBackdrop("CONTROLS");
    struct Row { const char* keys; const char* what; };
    const ButtonLabels& labels = Labels();
    const Row kPad[] = {
        { "LEFT STICK / D-PAD", "MOVE" },
        { "RIGHT STICK",        "AIM (PUSH FAR = AUTO-FIRE)" },
        { labels.helpFire,      "FIRE" },
        { labels.helpNuke,      "NUKE" },
        { labels.helpPause,     "PAUSE" },
        { labels.helpMusic,     "MUSIC ON/OFF" },
        { labels.helpMenu,      "MENU: CONFIRM / BACK" },
    };
    static const Row kKeys[] = {
        { "A D / ARROWS",       "MOVE" },
        { "MOUSE",              "AIM;  LEFT CLICK / CTRL / J: FIRE" },
        { "X / N / RIGHT CLICK","NUKE" },
        { "P / ESC",            "PAUSE" },
        { "TAB",                "MUSIC ON/OFF" },
        { "ENTER / ESC",        "MENU: CONFIRM / BACK" },
    };
    const float lineH = 22.0f, colKeys = 40.0f, colWhat = 270.0f;
    float y = 88.0f;
    auto section = [&](const char* name) {
        FontRenderer::DrawString("Normal", name, colKeys, y, { 1.0f, 0.85f, 0.2f, 1.0f });
        y += lineH;
    };
    auto row = [&](const Row& r) {
        FontRenderer::DrawString("Normal", r.keys, colKeys + 10.0f, y, { 0.3f, 1.0f, 0.3f, 1.0f });
        FontRenderer::DrawString("Normal", r.what, colWhat, y, Color4f::White());
        y += lineH;
    };
    section("KEYBOARD / MOUSE");
    for (const Row& r : kKeys) row(r);
    y += 8.0f;
    section("GAMEPAD");
    for (const Row& r : kPad) row(r);
    FontRenderer::DrawString("Normal", Labels().helpBackHint, SCREEN_WIDTH * 0.5f, 448.0f, { 0.6f, 0.6f, 0.6f, 1.0f }, 1.0f, ALIGN_CENTER);
}

void GameEngine::RenderGameOver() {
    Renderer::DrawFillRect(0.0f, 0.0f, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT, { 0.3f, 0.04f, 0.04f, 0.82f });
    if (mSurvivalMode) {
        char buf[64];
        FontRenderer::DrawString("RubberStampLET42", "SURVIVAL OVER", SCREEN_WIDTH * 0.5f, 190.0f, { 1.0f, 0.2f, 0.2f, 1.0f }, 1.0f, ALIGN_CENTER);
        snprintf(buf, sizeof(buf), "TIME SURVIVED: %d:%02d", mSurvivalTime / 60, mSurvivalTime % 60);
        FontRenderer::DrawString("Normal", buf, SCREEN_WIDTH * 0.5f, 265.0f, { 1.0f, 1.0f, 1.0f, 1.0f }, 1.2f, ALIGN_CENTER);
        if (mSurvivalNewBest) {
            FontRenderer::DrawString("Computer", "NEW BEST TIME!", SCREEN_WIDTH * 0.5f, 310.0f, { 0.3f, 1.0f, 0.3f, 1.0f }, 1.0f, ALIGN_CENTER);
        } else {
            snprintf(buf, sizeof(buf), "BEST: %d:%02d", mBestSurvival / 60, mBestSurvival % 60);
            FontRenderer::DrawString("Computer", buf, SCREEN_WIDTH * 0.5f, 310.0f, { 0.8f, 0.8f, 0.8f, 1.0f }, 1.0f, ALIGN_CENTER);
        }
    } else {
        FontRenderer::DrawString("RubberStampLET42", "MISSION FAILED", SCREEN_WIDTH * 0.5f, 190.0f, { 1.0f, 0.2f, 0.2f, 1.0f }, 1.0f, ALIGN_CENTER);
        std::string scoreStr = "FINAL SCORE: " + std::to_string(mApp.score);
        FontRenderer::DrawString("Normal", scoreStr, SCREEN_WIDTH * 0.5f, 275.0f, { 1.0f, 1.0f, 1.0f, 1.0f }, 1.2f, ALIGN_CENTER);
    }

    FontRenderer::DrawString("Computer", Labels().gameOverPrompt, SCREEN_WIDTH * 0.5f, 370.0f, { 1.0f, 0.9f, 0.2f, 1.0f }, 1.0f, ALIGN_CENTER);
}

} // namespace HeavyWeapon
