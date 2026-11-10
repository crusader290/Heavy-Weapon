#pragma once

#include "Renderer.h"
#include <string>
#include <unordered_map>

namespace HeavyWeapon {

enum TextAlign {
    ALIGN_LEFT = 0,
    ALIGN_CENTER,
    ALIGN_RIGHT
};

struct FontGlyph {
    Rect srcRect;
    int advance = 0;
    int ox = 0;
    int oy = 0;
};

struct Font {
    std::string name;
    Texture* texture = nullptr;
    int height = 20;
    int ascent = 0;     // LayerSetAscent: PopCap draws text with y at the baseline
    std::unordered_map<char, FontGlyph> glyphs;
};

class FontRenderer {
public:
    static void Init();
    static bool LoadFont(const std::string& fontName);
    static void DrawString(const std::string& fontName, const std::string& text, float x, float y, const Color4f& color = Color4f::White(), float scale = 1.0f, TextAlign align = ALIGN_LEFT);
    static float GetStringWidth(const std::string& fontName, const std::string& text, float scale = 1.0f);
    static float GetStringHeight(const std::string& fontName, float scale = 1.0f);
    static int GetAscent(const std::string& fontName);

    // PopCap Graphics::DrawString semantics: (x, y) is the left end of the baseline.
    static void DrawStringBaseline(const std::string& fontName, const std::string& text, int x, int y,
                                   const Color4f& color = Color4f::White(), TextAlign align = ALIGN_LEFT);

private:
    static std::unordered_map<std::string, Font> sFonts;
};

} // namespace HeavyWeapon
