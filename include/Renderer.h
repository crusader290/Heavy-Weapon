#pragma once

#include "Constants.h"
#include "TextureManager.h"
#include "GLPlatform.h"
#include <string>

struct SDL_Window;

namespace HeavyWeapon {

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct Color4f {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    static Color4f White() { return { 1.0f, 1.0f, 1.0f, 1.0f }; }
    static Color4f Red()   { return { 1.0f, 0.2f, 0.2f, 1.0f }; }
    static Color4f Green() { return { 0.2f, 1.0f, 0.2f, 1.0f }; }
    static Color4f Blue()  { return { 0.2f, 0.5f, 1.0f, 1.0f }; }
    static Color4f Yellow(){ return { 1.0f, 1.0f, 0.2f, 1.0f }; }
    static Color4f Black() { return { 0.0f, 0.0f, 0.0f, 1.0f }; }
};

// How the 640x480 game frame is fitted onto the display.
enum ScaleMode {
    SCALE_ASPECT = 0, // 4:3, pillarboxed (725x544 on Vita)
    SCALE_STRETCH     // fill the whole display
};

class Renderer {
public:
    static bool Init(SDL_Window* window);
    static void Shutdown();
    static void BeginFrame();   // binds the 640x480 game target
    static void EndFrame();     // scales the game target to the display and presents

    static void SetScaleMode(ScaleMode mode) { sScaleMode = mode; }
    static ScaleMode GetScaleMode() { return sScaleMode; }

    // Converts a point in physical display pixels to logical 640x480 game space.
    static void DisplayToLogical(float dx, float dy, float& outX, float& outY);
    // Same, for SDL window coordinates (mouse events), which differ from display
    // pixels on HiDPI desktops.
    static void WindowToLogical(float wx, float wy, float& outX, float& outY);

    // Writes the most recent game frame (640x480) to a PNG. Desktop only.
    static bool SaveScreenshot(const std::string& path);

    static void SetAdditiveBlend(bool additive);
    static void SetColor(const Color4f& color);

    // Draw texture rectangle
    static void DrawTexture(const Texture* tex, float dx, float dy);
    static void DrawTexture(const Texture* tex, float dx, float dy, float dw, float dh);
    static void DrawTexture(const Texture* tex, const Rect& dst, const Rect& src);
    
    // Draw rotated texture around center or custom pivot
    static void DrawTextureRotated(const Texture* tex, const Rect& dst, const Rect& src, float angleDegrees, float pivotX = -1.0f, float pivotY = -1.0f);

    // Draw sprite cel using texture columns and rows grid
    static void DrawCel(const Texture* tex, int col, int row, float x, float y, bool centered = true, float scaleX = 1.0f, float scaleY = 1.0f, float angleDegrees = 0.0f);

    // Draw primitives
    static void DrawRect(float x, float y, float w, float h, const Color4f& color);
    static void DrawFillRect(float x, float y, float w, float h, const Color4f& color);
    // Filled convex quad, corners in drawing order.
    static void DrawFillQuad(const float xs[4], const float ys[4], const Color4f& color);
    static void DrawLine(float x1, float y1, float x2, float y2, const Color4f& color, float width = 1.0f);

    // Screen Shake effect
    static void AddScreenShake(float intensity, float durationSeconds);
    static void UpdateScreenShake(float dt);

private:
    static SDL_Window* sWindow;
    static ScaleMode sScaleMode;
    static GLuint sFbo;
    static GLuint sFboTex;
    static Color4f sCurrentColor;
    static bool sIsAdditive;
    static float sShakeIntensity;
    static float sShakeTimer;
    static float sShakeOffsetX;
    static float sShakeOffsetY;
};

} // namespace HeavyWeapon
