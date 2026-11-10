#include "Renderer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace HeavyWeapon {

#ifndef __vita__
// Framebuffer objects are core since GL 3.0 but not part of the GL 1.x ABI that
// libGL exports, so resolve them at runtime on desktop.
static PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers_ = nullptr;
static PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer_ = nullptr;
static PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D_ = nullptr;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus_ = nullptr;
static PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers_ = nullptr;
#define glGenFramebuffers glGenFramebuffers_
#define glBindFramebuffer glBindFramebuffer_
#define glFramebufferTexture2D glFramebufferTexture2D_
#define glCheckFramebufferStatus glCheckFramebufferStatus_
#define glDeleteFramebuffers glDeleteFramebuffers_

static bool LoadFramebufferFunctions() {
    glGenFramebuffers_ = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
    glBindFramebuffer_ = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
    glFramebufferTexture2D_ = (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2D");
    glCheckFramebufferStatus_ = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    glDeleteFramebuffers_ = (PFNGLDELETEFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteFramebuffers");
    return glGenFramebuffers_ && glBindFramebuffer_ && glFramebufferTexture2D_ &&
           glCheckFramebufferStatus_ && glDeleteFramebuffers_;
}
#endif

SDL_Window* Renderer::sWindow = nullptr;
ScaleMode Renderer::sScaleMode = SCALE_ASPECT;
GLuint Renderer::sFbo = 0;
GLuint Renderer::sFboTex = 0;
Color4f Renderer::sCurrentColor = Color4f::White();
bool Renderer::sIsAdditive = false;
float Renderer::sShakeIntensity = 0.0f;
float Renderer::sShakeTimer = 0.0f;
float Renderer::sShakeOffsetX = 0.0f;
float Renderer::sShakeOffsetY = 0.0f;

static void GetDrawableSize(SDL_Window* window, int& w, int& h) {
#ifdef __vita__
    (void)window;
    w = DISPLAY_WIDTH;
    h = DISPLAY_HEIGHT;
#else
    SDL_GL_GetDrawableSize(window, &w, &h);
#endif
}

// Destination rectangle of the game frame on a display of the given size.
static Rect ComputePresentRect(ScaleMode mode, int dispW, int dispH) {
    if (mode == SCALE_STRETCH) {
        return { 0.0f, 0.0f, (float)dispW, (float)dispH };
    }
    float scale = std::min((float)dispW / SCREEN_WIDTH, (float)dispH / SCREEN_HEIGHT);
    float w = std::round(SCREEN_WIDTH * scale);
    float h = std::round(SCREEN_HEIGHT * scale);
    return { std::floor((dispW - w) * 0.5f), std::floor((dispH - h) * 0.5f), w, h };
}

static void SetOrtho(float w, float h) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof(0.0f, w, h, 0.0f, -1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

bool Renderer::Init(SDL_Window* window) {
    sWindow = window;

#ifndef __vita__
    if (!LoadFramebufferFunctions()) {
        std::cerr << "[Renderer] OpenGL framebuffer objects are unavailable" << std::endl;
        return false;
    }
#endif

    glGenTextures(1, &sFboTex);
    glBindTexture(GL_TEXTURE_2D, sFboTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCREEN_WIDTH, SCREEN_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &sFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, sFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sFboTex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[Renderer] Game render target incomplete: 0x" << std::hex << status << std::dec << std::endl;
        return false;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    return true;
}

void Renderer::Shutdown() {
    if (sFbo) glDeleteFramebuffers(1, &sFbo);
    if (sFboTex) glDeleteTextures(1, &sFboTex);
    sFbo = 0;
    sFboTex = 0;
}

void Renderer::BeginFrame() {
    glBindFramebuffer(GL_FRAMEBUFFER, sFbo);
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetOrtho((float)SCREEN_WIDTH, (float)SCREEN_HEIGHT);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (sShakeTimer > 0.0f) {
        glTranslatef(sShakeOffsetX, sShakeOffsetY, 0.0f);
    }

    SetAdditiveBlend(false);
    SetColor(Color4f::White());
}

void Renderer::EndFrame() {
    int dispW = 0, dispH = 0;
    GetDrawableSize(sWindow, dispW, dispH);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, dispW, dispH);
    SetOrtho((float)dispW, (float)dispH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // The game target is a GL texture, so its first row is the bottom of the frame:
    // sample it with V flipped.
    Rect dst = ComputePresentRect(sScaleMode, dispW, dispH);
    GLfloat vertices[] = {
        dst.x,         dst.y,
        dst.x + dst.w, dst.y,
        dst.x,         dst.y + dst.h,
        dst.x + dst.w, dst.y + dst.h
    };
    GLfloat texCoords[] = { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };

    SetAdditiveBlend(false);
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, sFboTex);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, texCoords);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_BLEND);

#ifdef __vita__
    vglSwapBuffers(GL_FALSE);
#else
    SDL_GL_SwapWindow(sWindow);
#endif
}

void Renderer::DisplayToLogical(float dx, float dy, float& outX, float& outY) {
    int dispW = 0, dispH = 0;
    GetDrawableSize(sWindow, dispW, dispH);
    Rect dst = ComputePresentRect(sScaleMode, dispW, dispH);
    outX = (dx - dst.x) * SCREEN_WIDTH / dst.w;
    outY = (dy - dst.y) * SCREEN_HEIGHT / dst.h;
}

void Renderer::WindowToLogical(float wx, float wy, float& outX, float& outY) {
#ifdef __vita__
    DisplayToLogical(wx, wy, outX, outY);
#else
    int winW = 1, winH = 1, dispW = 1, dispH = 1;
    SDL_GetWindowSize(sWindow, &winW, &winH);
    SDL_GL_GetDrawableSize(sWindow, &dispW, &dispH);
    DisplayToLogical(wx * dispW / winW, wy * dispH / winH, outX, outY);
#endif
}

bool Renderer::SaveScreenshot(const std::string& path) {
#ifdef __vita__
    (void)path;
    return false;
#else
    std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, sFbo);
    glReadPixels(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_WIDTH, SCREEN_HEIGHT, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return false;
    const int pitch = SCREEN_WIDTH * 4;
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        uint8_t* row = static_cast<uint8_t*>(surf->pixels) + y * surf->pitch;
        std::memcpy(row, pixels.data() + (SCREEN_HEIGHT - 1 - y) * pitch, pitch);
        for (int x = 0; x < SCREEN_WIDTH; ++x) row[x * 4 + 3] = 255;
    }
    bool ok = IMG_SavePNG(surf, path.c_str()) == 0;
    SDL_FreeSurface(surf);
    return ok;
#endif
}

void Renderer::SetAdditiveBlend(bool additive) {
    if (sIsAdditive != additive) {
        sIsAdditive = additive;
        if (additive) {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        } else {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }
}

void Renderer::SetColor(const Color4f& color) {
    sCurrentColor = color;
    glColor4f(color.r, color.g, color.b, color.a);
}

void Renderer::DrawTexture(const Texture* tex, float dx, float dy) {
    if (!tex || tex->id == 0) return;
    DrawTexture(tex, dx, dy, (float)tex->width, (float)tex->height);
}

void Renderer::DrawTexture(const Texture* tex, float dx, float dy, float dw, float dh) {
    if (!tex || tex->id == 0) return;
    Rect dst = { dx, dy, dw, dh };
    Rect src = { 0.0f, 0.0f, (float)tex->width, (float)tex->height };
    DrawTexture(tex, dst, src);
}

void Renderer::DrawTexture(const Texture* tex, const Rect& dst, const Rect& src) {
    if (!tex || tex->id == 0) return;

    glBindTexture(GL_TEXTURE_2D, tex->id);

    float u0 = src.x / (float)tex->width;
    float v0 = src.y / (float)tex->height;
    float u1 = (src.x + src.w) / (float)tex->width;
    float v1 = (src.y + src.h) / (float)tex->height;

    GLfloat vertices[] = {
        dst.x,         dst.y,
        dst.x + dst.w, dst.y,
        dst.x,         dst.y + dst.h,
        dst.x + dst.w, dst.y + dst.h
    };

    GLfloat texCoords[] = {
        u0, v0,
        u1, v0,
        u0, v1,
        u1, v1
    };

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, texCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

void Renderer::DrawTextureRotated(const Texture* tex, const Rect& dst, const Rect& src, float angleDegrees, float pivotX, float pivotY) {
    if (!tex || tex->id == 0) return;

    float px = (pivotX < 0.0f) ? dst.x + dst.w * 0.5f : dst.x + pivotX;
    float py = (pivotY < 0.0f) ? dst.y + dst.h * 0.5f : dst.y + pivotY;

    glPushMatrix();
    glTranslatef(px, py, 0.0f);
    glRotatef(angleDegrees, 0.0f, 0.0f, 1.0f);
    glTranslatef(-px, -py, 0.0f);

    DrawTexture(tex, dst, src);

    glPopMatrix();
}

void Renderer::DrawCel(const Texture* tex, int col, int row, float x, float y, bool centered, float scaleX, float scaleY, float angleDegrees) {
    if (!tex || tex->id == 0) return;

    float celW = (float)tex->GetCelWidth();
    float celH = (float)tex->GetCelHeight();
    Rect src = { (float)col * celW, (float)row * celH, celW, celH };

    float dw = celW * scaleX;
    float dh = celH * scaleY;
    Rect dst = centered ? Rect{ x - dw * 0.5f, y - dh * 0.5f, dw, dh } : Rect{ x, y, dw, dh };

    if (angleDegrees != 0.0f) {
        DrawTextureRotated(tex, dst, src, angleDegrees);
    } else {
        DrawTexture(tex, dst, src);
    }
}

void Renderer::DrawRect(float x, float y, float w, float h, const Color4f& color) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(color.r, color.g, color.b, color.a);

    GLfloat vertices[] = {
        x,     y,
        x + w, y,
        x + w, y + h,
        x,     y + h
    };

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glDrawArrays(GL_LINE_LOOP, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);

    glEnable(GL_TEXTURE_2D);
    SetColor(sCurrentColor);
}

void Renderer::DrawFillRect(float x, float y, float w, float h, const Color4f& color) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(color.r, color.g, color.b, color.a);

    GLfloat vertices[] = {
        x,     y,
        x + w, y,
        x,     y + h,
        x + w, y + h
    };

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);

    glEnable(GL_TEXTURE_2D);
    SetColor(sCurrentColor);
}

void Renderer::DrawFillQuad(const float xs[4], const float ys[4], const Color4f& color) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(color.r, color.g, color.b, color.a);

    // Strip order 0,1,3,2 covers the polygon 0-1-2-3.
    GLfloat vertices[] = { xs[0], ys[0], xs[1], ys[1], xs[3], ys[3], xs[2], ys[2] };

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);

    glEnable(GL_TEXTURE_2D);
    SetColor(sCurrentColor);
}

void Renderer::DrawLine(float x1, float y1, float x2, float y2, const Color4f& color, float width) {
    glDisable(GL_TEXTURE_2D);
    glLineWidth(width);
    glColor4f(color.r, color.g, color.b, color.a);

    GLfloat vertices[] = { x1, y1, x2, y2 };

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glDrawArrays(GL_LINES, 0, 2);
    glDisableClientState(GL_VERTEX_ARRAY);

    glLineWidth(1.0f);
    glEnable(GL_TEXTURE_2D);
    SetColor(sCurrentColor);
}

void Renderer::AddScreenShake(float intensity, float durationSeconds) {
    sShakeIntensity = std::max(sShakeIntensity, intensity);
    sShakeTimer = std::max(sShakeTimer, durationSeconds);
}

void Renderer::UpdateScreenShake(float dt) {
    if (sShakeTimer > 0.0f) {
        sShakeTimer -= dt;
        if (sShakeTimer <= 0.0f) {
            sShakeTimer = 0.0f;
            sShakeIntensity = 0.0f;
            sShakeOffsetX = 0.0f;
            sShakeOffsetY = 0.0f;
        } else {
            float progress = sShakeTimer;
            float maxOffset = sShakeIntensity * progress;
            sShakeOffsetX = ((float)rand() / (float)RAND_MAX * 2.0f - 1.0f) * maxOffset;
            sShakeOffsetY = ((float)rand() / (float)RAND_MAX * 2.0f - 1.0f) * maxOffset;
        }
    }
}

} // namespace HeavyWeapon
