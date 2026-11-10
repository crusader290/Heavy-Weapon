#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "GLPlatform.h"

namespace HeavyWeapon {

struct Texture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
    int cols = 1;
    int rows = 1;
    bool hasAlpha = false;
    // Per-pixel alpha for collision tests (original 0x42d580: non-zero alpha = solid).
    // Kept for sprites only, not for backgrounds and full-screen images.
    std::vector<uint8_t> alpha;

    int GetCelWidth() const { return cols > 0 ? (width / cols) : width; }
    int GetCelHeight() const { return rows > 0 ? (height / rows) : height; }
};

class TextureManager {
public:
    static void Init();
    static void Shutdown();

    // Loads or retrieves a texture by asset name or path, automatically handling alpha masks (.jpg + _.png)
    static Texture* Load(const std::string& relativePath);
    static Texture* Get(const std::string& relativePath);
    // Load (or fetch) an image and give it an explicit cel grid (images outside the
    // game's image table, such as boss parts).
    static Texture* LoadGrid(const std::string& relativePath, int cols, int rows);
    static void Unload(const std::string& relativePath);
    static void Clear();

    // Loads every gameplay image of the original image table (everything except the menu,
    // map, armory and loading screens and the per-theme backgrounds), so nothing loads
    // mid-fight. Idempotent; textures stay cached across missions. Returns the count loaded.
    // `progress(done, total)` is called after each texture (may be empty).
    static int PreloadAll(const std::function<void(int, int)>& progress = {});

    // Loads every image in Images/<subFolder> (boss sprite sets). Already-cached ones are free.
    static void PreloadFolder(const std::string& subFolder);

    // Query dimensions and grid layout
    static void GetCelInfo(const std::string& name, int& outCols, int& outRows);

private:
    static std::unordered_map<std::string, Texture> sTextures;
    static Texture* LoadImpl(const std::string& relativePath, bool quiet);
    static Texture LoadFromFiles(const std::string& colorPath, const std::string& maskPath, int cols, int rows, bool keepAlpha);
};

} // namespace HeavyWeapon
