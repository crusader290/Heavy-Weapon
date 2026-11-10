#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "GLPlatform.h"
#include "Constants.h"
#include "GameEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
        "  --state <title|map|play|survival|armory|options|help|intro|middle|final>\n"
        "                                   start on this screen or movie (default: the intro)\n"
        "  --level <n>                      mission index (0-based) for map/play\n"
        "  --frames <n>                     quit after n frames (fixed 60 Hz timestep)\n"
        "  --screenshot <file.png>          save the final 640x480 frame (needs --frames)\n"
        "  --stretch                        fill the window instead of keeping 4:3\n"
        "  --autofire                       hold the fire button (testing)\n"
        "  --god                            the tank cannot be destroyed (testing)\n"
        "  --armory <n>                     all armory weapons and spread at level n (testing)\n"
        "  --progress <n>                   start the level at progress n (n < 0: that many ticks before the end)\n"
        "  --scale <n>                      window size as a multiple of 640x480 (e.g. 1.5)\n"
        "  --size <W>x<H>                   window size in pixels\n"
        "                                   (default: the largest half-step multiple of 640x480 that\n"
        "                                   fits 90% of the desktop; F11 toggles fullscreen)\n"
        "  --data <dir>                     game data directory (Images/, Sounds/, data/, ...)\n"
        "  --buttons <xbox|playstation|auto>  gamepad button names in prompts (default: settings.ini)\n"
        "  --no-mods                        do not load anything from the mods folder\n";
}

struct WindowSize {
    double scale = 0.0;   // --scale; 0: not given
    int w = 0, h = 0;     // --size; 0: not given
};

static bool ParseArgs(int argc, char* argv[], HeavyWeapon::LaunchOptions& opts, WindowSize& win) {
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
        else if (a == "--no-mods") opts.noMods = true;
        else if (a == "--scale" && hasValue) win.scale = std::max(0.5, std::atof(argv[++i]));
        else if (a == "--size" && hasValue) {
            if (std::sscanf(argv[++i], "%dx%d", &win.w, &win.h) != 2 || win.w <= 0 || win.h <= 0) {
                std::cerr << "--size expects WxH, e.g. 1280x960\n";
                return false;
            }
        }
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
    WindowSize winSize;
    if (!ParseArgs(argc, argv, opts, winSize)) {
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

    // Window size as in the Tanks recomp: by default the largest half-step multiple of the
    // 640x480 frame that fits 90% of the primary display's usable area (at least 1x).
    const int baseW = HeavyWeapon::SCREEN_WIDTH, baseH = HeavyWeapon::SCREEN_HEIGHT;
    int winW = winSize.w, winH = winSize.h;
    if (winW <= 0 || winH <= 0) {
        double sc = winSize.scale;
        if (sc <= 0.0) {
            sc = 1.0;
            SDL_Rect ub;
            if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &ub) && ub.w > 0 && ub.h > 0) {
                const double fit = std::min(ub.w * 0.9 / baseW, ub.h * 0.9 / baseH);
                sc = std::max(1.0, std::floor(fit * 2.0) / 2.0);
            }
        }
        winW = (int)std::lround(baseW * sc);
        winH = (int)std::lround(baseH * sc);
    }
    SDL_WindowFlags winFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    SDL_Window* window = SDL_CreateWindow("Heavy Weapon Deluxe", winW, winH, winFlags);
    if (window) {
        // Resizing keeps 4:3 (unless --stretch asks to fill any shape), down to half size.
        if (!opts.stretch) {
            const float aspect = (float)baseW / (float)baseH;
            SDL_SetWindowAspectRatio(window, aspect, aspect);
        }
        SDL_SetWindowMinimumSize(window, baseW / 2, baseH / 2);
        std::cout << "Window " << winW << "x" << winH << std::endl;
    }

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
