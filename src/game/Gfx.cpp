#include "game/Gfx.h"

namespace HeavyWeapon {
namespace Gfx {

static int sTransX = 0;
static int sTransY = 0;
static Color4f sColor = Color4f::White();
static bool sColorize = false;

void Reset() {
    sTransX = 0;
    sTransY = 0;
    sColor = Color4f::White();
    sColorize = false;
    Renderer::SetAdditiveBlend(false);
    Renderer::SetColor(Color4f::White());
}

void Translate(int dx, int dy) {
    sTransX += dx;
    sTransY += dy;
}

int TransX() { return sTransX; }
int TransY() { return sTransY; }

void SetColor(int r, int g, int b, int a) {
    sColor = { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
}

void SetColorizeImages(bool on) {
    sColorize = on;
}

void SetDrawMode(int mode) {
    Renderer::SetAdditiveBlend(mode == 1);
}

static void ApplyImageColor() {
    Renderer::SetColor(sColorize ? sColor : Color4f::White());
}

void DrawImageRect(const Texture* img, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, bool mirror) {
    if (!img || img->id == 0) return;
    ApplyImageColor();
    Rect dst = { (float)(dx + sTransX), (float)(dy + sTransY), (float)dw, (float)dh };
    Rect src = { (float)sx, (float)sy, (float)sw, (float)sh };
    if (mirror) {
        src.x += src.w;
        src.w = -src.w;
    }
    Renderer::DrawTexture(img, dst, src);
}

void DrawSprite(const Texture* img, int x, int y, bool centered, int col, int row, bool mirror) {
    if (!img || img->id == 0) return;
    int cw = img->GetCelWidth();
    int ch = img->GetCelHeight();
    if (centered) {
        x -= cw / 2;
        y -= ch / 2;
    }
    DrawImageRect(img, x, y, cw, ch, cw * col, ch * row, cw, ch, mirror);
}

void FillPolygon(const int xs[4], const int ys[4]) {
    float fx[4], fy[4];
    for (int i = 0; i < 4; ++i) {
        fx[i] = (float)(xs[i] + sTransX);
        fy[i] = (float)(ys[i] + sTransY);
    }
    Renderer::DrawFillQuad(fx, fy, sColor);
}

void FillRect(int x, int y, int w, int h) {
    Renderer::DrawFillRect((float)(x + sTransX), (float)(y + sTransY), (float)w, (float)h, sColor);
}

} // namespace Gfx
} // namespace HeavyWeapon
