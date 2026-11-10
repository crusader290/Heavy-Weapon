#pragma once

// Thin equivalent of the PopCap SexyAppFramework Graphics calls the original game code
// makes, so translated game code can stay close to the decompiled source:
//   - integer pixel coordinates in 640x480 space, with a translation offset
//   - colour only tints images while "colorize images" is on (SetColorizeImages)
//   - draw mode 0 = normal alpha blend, 1 = additive (SetDrawMode)

#include "Renderer.h"
#include "TextureManager.h"

namespace HeavyWeapon {
namespace Gfx {

void Reset();                          // called at the start of every frame
void Translate(int dx, int dy);
int  TransX();
int  TransY();

void SetColor(int r, int g, int b, int a = 255);
void SetColorizeImages(bool on);
void SetDrawMode(int mode);            // 0 normal, 1 additive

// Original DrawSprite (0x42d6b0): draws cel (col,row) of a sprite strip at (x,y),
// centred on (x,y) when `centered`, horizontally mirrored when `mirror`.
void DrawSprite(const Texture* img, int x, int y, bool centered = false, int col = 0, int row = 0, bool mirror = false);

// DrawImage(img, destRect, srcRect)
void DrawImageRect(const Texture* img, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, bool mirror = false);

void FillRect(int x, int y, int w, int h);
void FillPolygon(const int xs[4], const int ys[4]);   // four-point polygon (0x4716c0)

} // namespace Gfx
} // namespace HeavyWeapon
