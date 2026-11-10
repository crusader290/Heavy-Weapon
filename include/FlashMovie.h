#pragma once

// The game's Flash movies (Images/intro.swf, middle.swf, final.swf), played by the native
// Flash runtime in 3rdparty/flashrt with their ActionScript translated to C++ at build time.
// One movie plays at a time; it is drawn as a full-screen 640x480 image.

namespace HeavyWeapon {

enum MovieId {
    MOVIE_INTRO = 0,   // intro screen, before the main menu (with LoveTheme)
    MOVIE_MIDDLE,      // story cutscene after mission 9
    MOVIE_FINAL,       // story cutscene after mission 19
    MOVIE_COUNT
};

class FlashMovie {
public:
    static bool Exists(MovieId id);   // the SWF is in the game data
    // Loads and starts the movie; false if it is missing or cannot be loaded.
    static bool Start(MovieId id);
    // Advances by dt seconds at the movie's own frame rate. Returns false once it has
    // finished: its main timeline stopped on (or wrapped past) its last frame, plus a
    // one-second hold on that frame.
    static bool Update(float dt);
    static void Draw();               // between Renderer::BeginFrame and EndFrame
    static void Stop();
    static void Shutdown();
};

} // namespace HeavyWeapon
