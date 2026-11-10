#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "GLPlatform.h"
#include "Constants.h"
#include "GameEngine.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>

// The Windows build is a GUI-subsystem app (no console window when double-clicked).
// When started from a terminal, write stdout/stderr there instead.
static void AttachParentConsole() {
    // Keep stdout/stderr that were redirected to a file or pipe.
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) return;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        std::cout.clear();
        std::cerr.clear();
    }
}
#endif

static void PrintUsage() {
    std::cout <<
        "Usage: heavyweapon [options]\n"
        "  --state <title|map|play|survival|armory|options|help>  start on this screen\n"
        "  --level <n>                      mission index (0-based) for map/play\n"
        "  --frames <n>                     quit after n frames (fixed 60 Hz timestep)\n"
        "  --screenshot <file.png>          save the final 640x480 frame (needs --frames)\n"
        "  --stretch                        fill the window instead of keeping 4:3\n"
        "  --autofire                       hold the fire button (testing)\n"
        "  --god                            the tank cannot be destroyed (testing)\n"
        "  --armory <n>                     all armory weapons and spread at level n (testing)\n"
        "  --progress <n>                   start the level at progress n (n < 0: that many ticks before the end)\n"
        "  --scale <n>                      initial window size as a multiple of 640x480\n"
        "  --data <dir>                     game data directory (Images/, Sounds/, data/, ...)\n"
        "  --buttons <xbox|playstation|auto>  gamepad button names in prompts (default: settings.ini)\n";
}

static bool ParseArgs(int argc, char* argv[], HeavyWeapon::LaunchOptions& opts, int& windowScale) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        bool hasValue = i + 1 < argc;
        if (a == "--state" && hasValue) opts.startState = argv[++i];
        else if (a == "--level" && hasValue) opts.level = std::atoi(argv[++i]);
        else if (a == "--frames" && hasValue) opts.maxFrames = std::atoi(argv[++i]);
        else if (a == "--screenshot" && hasValue) opts.screenshotPath = argv[++i];
        else if (a == "--stretch") opts.stretch = true;
        else if (a == "--autofire") opts.autoFire = true;
        else if (a == "--god") opts.god = true;
        else if (a == "--armory" && hasValue) opts.armoryLevel = std::atoi(argv[++i]);
        else if (a == "--progress" && hasValue) opts.startProgress = std::atoi(argv[++i]);
        else if (a == "--data" && hasValue) opts.dataPath = argv[++i];
        else if (a == "--buttons" && hasValue) opts.buttons = argv[++i];
        else if (a == "--scale" && hasValue) windowScale = std::max(1, std::atoi(argv[++i]));
        else if (a == "--help" || a == "-h") { PrintUsage(); return false; }
        else { std::cerr << "Unknown argument: " << a << "\n"; PrintUsage(); return false; }
    }
    return true;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    AttachParentConsole();
#endif
    HeavyWeapon::LaunchOptions opts;
    int windowScale = 2;
    if (!ParseArgs(argc, argv, opts, windowScale)) {
        return 1;
    }


    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return 1;
    }
    // Audio is optional: the game still runs (silently) without it.
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::cerr << "Audio unavailable: " << SDL_GetError() << std::endl;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    int winW = HeavyWeapon::SCREEN_WIDTH * windowScale;
    int winH = HeavyWeapon::SCREEN_HEIGHT * windowScale;
    SDL_WindowFlags winFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    SDL_Window* window = SDL_CreateWindow("Heavy Weapon Deluxe", winW, winH, winFlags);

    if (!window) {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "Failed to create GL context: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(1); // VSync enabled

    int exitCode = 0;
    {
        HeavyWeapon::GameEngine engine;
        if (engine.Init(window, opts)) {
            engine.Run();
        } else {
            exitCode = 1;
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Heavy Weapon Deluxe",
                "Failed to start. Check that the game data (data/, Images/, Sounds/, Fonts/, Music/) "
                "is next to the executable, in \"Heavy Weapon Deluxe/\", or passed with --data <dir>.",
                window);
        }
        engine.Shutdown();
    }

    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return exitCode;
}
