#pragma once

#include "ButtonLabels.h"
#include "Constants.h"
#include "DataModels.h"
#include "game/AppState.h"
#include "game/Board.h"
#include <memory>
#include "XmlLoader.h"
#include "WorldRenderer.h"
#include "FontRenderer.h"
#include "AudioSystem.h"
#include <vector>
#include <unordered_map>

struct SDL_Window;

namespace HeavyWeapon {

// Command-line options, used mainly by the desktop build for testing.
struct LaunchOptions {
    std::string startState;      // "", "title", "map", "play", "survival", "armory"
    int level = 0;               // mission index for map/play
    int maxFrames = -1;          // > 0: run exactly this many fixed-timestep frames, then quit
    std::string screenshotPath;  // saved after the final frame
    bool stretch = false;
    bool autoFire = false;       // hold the fire button (testing)
    bool god = false;            // the tank cannot be destroyed (testing)
    int startProgress = -1;      // skip ahead: level progress in ticks, or ticks before the end if negative < -1 (testing)
    int armoryLevel = 0;         // give every armory weapon (and spread) this level (testing)
    std::string dataPath;        // extra game-data directory, searched first
    std::string buttons;         // xbox|playstation|auto for this run; "" = settings.ini
    bool noMods = false;         // --no-mods: skip the mods folder
};

class GameEngine {
public:
    GameEngine();
    ~GameEngine();

    bool Init(SDL_Window* window, const LaunchOptions& opts);
    void Run();
    void Shutdown();

    void Update(float dt);

    void UpdateMenuMusic();
    void Render();

private:
    bool mRunning = true;
    bool mInitialized = false;
    LaunchOptions mOpts;
    GameState mState = STATE_TITLE;

    // Game data loaded from XML
    std::unordered_map<std::string, CraftDef> mCraftDefs;
    std::vector<LevelDef> mLevels;
    std::vector<std::vector<LevelDef>> mSurvivalSets;   // survival0..9.xml, each a list of tiers
    std::unordered_map<std::string, BossDef> mBossDefs;
    std::vector<std::vector<AnimDef>> mLevelAnims;

    // Runtime state
    AppState mApp;
    std::unique_ptr<Board> mBoard;
    int mCurrentLevelIndex = 0;
    float mTickAccum = 0.0f;
    int mLevelEndTimer = 0;
    // Debriefing (0x426e60) values, computed when the level ends.
    struct Debrief {
        int score = 0, kills = 0, percent = 0, killBonus = 0, friendlyBonus = 0, survivalBonus = 0, lost = 0;
        int rank = 0;
        int timer = 0;
    } mDebrief;
    int mUpgradePoints = 0;
    bool mSurvivalMode = false;
    int mSurvivalTime = 0, mBestSurvival = 0;   // seconds; best is persisted in settings.ini
    ButtonStyle mButtonStyle = ButtonStyle::Xbox;     // settings.ini buttons=, or --buttons
    const ButtonLabels& Labels() const;   // button names for prompts (resolves auto)
    bool mSurvivalNewBest = false;

    // Menu & UI State
    GameState mPrevState = STATE_BOOT;
    int mMenuSelection = 0;
    int mOptionsSelection = 0;
    int mArmorySelection = 0;
    float mMenuGlowAnim = 0.0f;

    // Internal state updates
    void UpdateTitle(float dt);
    void UpdateMissionSelect(float dt);
    void UpdatePlaying(float dt);
    void UpdateArmory(float dt);
    void ToggleMusic();
    void UpdatePaused(float dt);
    void UpdateGameOver(float dt);
    void UpdateOptions();
    void UpdateHelp();

    // Internal state renders
    void RenderTitle();
    void RenderMissionSelect();
    void RenderPlaying();
    void UpdateDebrief();
    void RenderDebrief();
    void RenderArmory();
    void RenderPaused();
    void RenderGameOver();
    void RenderOptions();
    void RenderHelp();

    void StartLevel(int levelIndex, bool survival = false);
    void ApplyBoardInput();
};

} // namespace HeavyWeapon
