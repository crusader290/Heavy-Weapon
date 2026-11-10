#include "TextureManager.h"
#include "Vfs.h"
#include "core/Perf.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>

namespace HeavyWeapon {

// Image table from the original executable (inner game image, VA 0x527820): 157 records
// of { char name[40]; int cols; int rows; int flag; }, in the game's own load order.
// The flag is set only on enemies, projectiles and pickups, so it most likely marks
// sprites the game hit-tests per pixel.
struct ImageMeta {
    const char* name;
    int cols;
    int rows;
    bool hitTested;
};

static const ImageMeta sImageMetaTable[] = {
    { "title", 1, 1, false },
    { "atomictank", 2, 1, false },
    { "loadingframe", 1, 1, false },
    { "loadinginside", 1, 1, false },
    { "loadingslider", 1, 1, false },
    { "loadingbullet", 1, 1, false },
    { "loadingglow", 1, 1, false },
    { "cursor_dragging", 1, 1, false },
    { "cursor_hand", 1, 1, false },
    { "cursor_pointer", 1, 1, false },
    { "cursor_text", 1, 1, false },
    { "statusbar", 1, 1, false },
    { "backgrounds/antagonistan_sky", 1, 1, false },
    { "backgrounds/antagonistan_bg", 1, 1, false },
    { "backgrounds/antagonistan_bg2", 1, 1, false },
    { "backgrounds/antagonistan_map", 1, 1, false },
    { "backgrounds/antagonistan_ground", 1, 1, false },
    { "tank", 10, 1, true },
    { "tankshadow", 1, 1, false },
    { "gun", 21, 5, false },
    { "bullets", 21, 5, true },
    { "rotors", 7, 1, false },
    { "spark", 10, 1, false },
    { "dumbbomb", 10, 1, true },
    { "lgb", 21, 1, true },
    { "hellfire", 21, 1, true },
    { "crater", 5, 1, false },
    { "rock", 1, 1, true },
    { "tankflame", 20, 1, false },
    { "nukeicon", 1, 1, false },
    { "muzzleflash", 5, 1, false },
    { "powerups", 12, 1, true },
    { "shield", 10, 1, true },
    { "shieldzap", 10, 1, false },
    { "smoke", 1, 1, false },
    { "dialog", 1, 1, false },
    { "fragbomb", 10, 1, true },
    { "bombfrag", 1, 1, true },
    { "missile", 20, 1, true },
    { "flakflash", 5, 1, true },
    { "buttonbracket", 3, 1, false },
    { "button", 3, 1, false },
    { "buttonglow", 3, 1, false },
    { "slidertrack", 1, 1, false },
    { "sliderthumb", 1, 1, false },
    { "checked", 1, 1, false },
    { "unchecked", 1, 1, false },
    { "tracks", 1, 1, false },
    { "gasstation", 1, 1, false },
    { "gassign", 1, 1, false },
    { "gaspump", 1, 1, false },
    { "armory", 1, 1, false },
    { "upgrades", 6, 1, false },
    { "laserglare", 10, 1, false },
    { "rocket", 21, 1, true },
    { "upgradeglow", 1, 1, false },
    { "ironbomb", 10, 1, true },
    { "pupcopter", 1, 1, true },
    { "puprotor", 7, 1, false },
    { "crates", 4, 1, true },
    { "casing", 1, 1, false },
    { "satlaser", 10, 1, false },
    { "dontshoot", 1, 1, false },
    { "beam", 1, 1, false },
    { "beamfire", 5, 1, false },
    { "beamfringe", 5, 1, false },
    { "enemygun", 10, 2, false },
    { "enemytankshadow", 1, 1, false },
    { "upgradebubble", 1, 1, false },
    { "armoryglow", 1, 1, false },
    { "bolt", 5, 1, true },
    { "staticspark", 10, 1, false },
    { "staticglow", 5, 1, false },
    { "megameter", 1, 1, false },
    { "mainmenu", 1, 1, false },
    { "mainsmallbtn", 1, 1, false },
    { "mainbigbtn", 1, 1, false },
    { "map", 1, 1, false },
    { "mission1", 1, 1, false },
    { "mission2", 1, 1, false },
    { "mission3", 1, 1, false },
    { "mission4", 1, 1, false },
    { "mission5", 1, 1, false },
    { "mission6", 1, 1, false },
    { "mission7", 1, 1, false },
    { "mission8", 1, 1, false },
    { "mission9", 1, 1, false },
    { "mission10", 1, 1, false },
    { "upgradebtns", 2, 2, false },
    { "mainglow", 1, 1, false },
    { "tankicon", 1, 1, false },
    { "mappointer", 1, 1, false },
    { "megalaser", 4, 1, false },
    { "maprect", 4, 1, false },
    { "reinforcement", 1, 1, false },
    { "largeinsignia", 19, 1, false },
    { "smallinsignia", 19, 1, false },
    { "nukebg", 1, 1, false },
    { "fire", 1, 1, false },
    { "orb", 2, 1, true },
    { "rpg", 10, 1, true },
    { "explosion", 20, 1, false },
    { "blimpprop", 5, 1, false },
    { "help", 1, 1, false },
    { "mouse", 3, 1, false },
    { "milemarker", 1, 1, false },
    { "miletext", 5, 1, false },
    { "tankflash", 1, 1, false },
    { "headmissile", 20, 1, true },
    { "sanddust", 1, 1, false },
    { "boulder", 5, 1, true },
    { "airmine", 5, 1, true },
    { "meteorite", 10, 1, true },
    { "bigdebris", 6, 1, false },
    { "fatboy", 10, 1, true },
    { "fatboyflash", 1, 1, false },
    { "rocketpod", 21, 1, false },
    { "flakguns", 21, 3, false },
    { "lasers", 21, 3, false },
    { "staticstrike", 1, 3, false },
    { "homing", 3, 1, false },
    { "upgradelvl", 4, 1, false },
    { "laserburn", 10, 1, false },
    { "statuscovers", 1, 1, false },
    { "burstrocket", 8, 1, true },
    { "moon", 1, 1, false },
    { "mushfire", 3, 1, false },
    { "mushsmoke", 3, 1, false },
    { "medrotor", 6, 1, false },
    { "dozershadow", 1, 1, false },
    { "deflectshield", 4, 1, false },
    { "credits", 1, 1, false },
    { "atmenu", 1, 1, false },
    { "danger", 1, 1, false },
    { "advancebtn", 1, 1, false },
    { "bulletglow", 2, 1, false },
    { "propfighter", 4, 1, true },
    { "smalljet", 1, 1, true },
    { "bomber", 1, 1, true },
    { "jetfighter", 1, 1, true },
    { "truck", 10, 1, true },
    { "bigbomber", 1, 1, true },
    { "smallcopter", 5, 1, true },
    { "medcopter", 7, 1, true },
    { "bigcopter", 9, 1, true },
    { "deltabomber", 1, 1, true },
    { "deltajet", 1, 1, true },
    { "bigmissile", 10, 2, true },
    { "superbomber", 1, 1, true },
    { "fatbomber", 1, 1, true },
    { "blimp", 1, 1, true },
    { "satellite", 10, 1, true },
    { "strafer", 10, 1, true },
    { "enemytank", 10, 1, true },
    { "dozer", 10, 1, true },
    { "deflector", 1, 1, true },
    { "cruise", 8, 1, true },
};

static std::string NormalizeKey(const std::string& path) {
    std::string s = path;
    std::replace(s.begin(), s.end(), '\\', '/');
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    // Strip "images/" prefix if present
    if (s.rfind("images/", 0) == 0) {
        s = s.substr(7);
    }
    // Strip trailing extension
    size_t dot = s.find_last_of('.');
    if (dot != std::string::npos) {
        std::string ext = s.substr(dot);
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif") {
            s = s.substr(0, dot);
        }
    }
    return s;
}

std::unordered_map<std::string, Texture> TextureManager::sTextures;

void TextureManager::Init() {
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
}

void TextureManager::Shutdown() {
    Clear();
    IMG_Quit();
}

void TextureManager::GetCelInfo(const std::string& name, int& outCols, int& outRows) {
    std::string key = NormalizeKey(name);
    for (const auto& meta : sImageMetaTable) {
        if (key == meta.name) {
            outCols = meta.cols;
            outRows = meta.rows;
            return;
        }
    }
    outCols = 1;
    outRows = 1;
}

Texture TextureManager::LoadFromFiles(const std::string& colorPath, const std::string& maskPath, int cols, int rows, bool keepAlpha) {
    Texture tex;
    tex.cols = cols;
    tex.rows = rows;

    SDL_Surface* srcSurface = IMG_Load(colorPath.c_str());
    if (!srcSurface) {
        std::cerr << "[TextureManager] Failed to load image: " << colorPath << " (" << IMG_GetError() << ")" << std::endl;
        return tex;
    }

    SDL_Surface* rgbaSurface = SDL_ConvertSurfaceFormat(srcSurface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(srcSurface);
    if (!rgbaSurface) {
        return tex;
    }

    tex.width = rgbaSurface->w;
    tex.height = rgbaSurface->h;

    // A file named "_name" with no colour file is a PopCap alpha-only image: its
    // brightness is the alpha and the colour is white (font sheets such as _Normal.png).
    size_t slash = colorPath.find_last_of('/');
    bool maskOnly = colorPath[slash == std::string::npos ? 0 : slash + 1] == '_';
    if (maskOnly) {
        uint32_t* px = static_cast<uint32_t*>(rgbaSurface->pixels);
        for (int i = 0; i < tex.width * tex.height; ++i) {
            uint32_t r = px[i] & 0xFF;
            px[i] = 0x00FFFFFFu | (r << 24);
        }
        tex.hasAlpha = true;
    }
    const std::string mask = maskOnly ? std::string() : maskPath;

    // Apply alpha mask if present
    if (!mask.empty() && Vfs::Exists(mask)) {
        std::string resolvedMask = Vfs::Resolve(mask);
        SDL_Surface* maskSurface = IMG_Load(resolvedMask.c_str());
        if (maskSurface) {
            SDL_Surface* maskRgba = SDL_ConvertSurfaceFormat(maskSurface, SDL_PIXELFORMAT_RGBA32, 0);
            SDL_FreeSurface(maskSurface);

            if (maskRgba) {
                uint32_t* colorPixels = static_cast<uint32_t*>(rgbaSurface->pixels);
                uint32_t* maskPixels = static_cast<uint32_t*>(maskRgba->pixels);
                int count = std::min(rgbaSurface->w * rgbaSurface->h, maskRgba->w * maskRgba->h);

                for (int i = 0; i < count; ++i) {
                    uint8_t alpha = maskPixels[i] & 0xFF; // red channel as alpha
                    colorPixels[i] = (colorPixels[i] & 0x00FFFFFF) | (static_cast<uint32_t>(alpha) << 24);
                }
                SDL_FreeSurface(maskRgba);
                tex.hasAlpha = true;
            }
        }
    } else {
        tex.hasAlpha = true; // standard PNG already has alpha
    }

    if (keepAlpha && !(tex.width >= 640 && tex.height >= 480)) {
        const uint8_t* px = static_cast<const uint8_t*>(rgbaSurface->pixels);
        tex.alpha.resize((size_t)tex.width * tex.height);
        for (size_t i = 0; i < tex.alpha.size(); ++i) tex.alpha[i] = px[i * 4 + 3];
    }

    // Upload to the GPU
    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaSurface->pixels);

    SDL_FreeSurface(rgbaSurface);
    return tex;
}

// Finds the colour image for an asset base path (no extension) and its optional alpha
// mask. PopCap stores colour and alpha separately as "name.ext" + "name_.ext" (or the
// older "_name.ext"), with any mix of .png/.jpg/.gif.
static bool FindImageFiles(const std::string& base, std::string& outColor, std::string& outMask) {
    static const char* kExts[] = { ".png", ".jpg", ".gif" };
    outColor.clear();
    outMask.clear();
    for (const char* ext : kExts) {
        if (Vfs::Exists(base + ext)) {
            outColor = base + ext;
            break;
        }
    }
    if (outColor.empty()) return false;

    size_t slash = base.find_last_of('/');
    std::string dir = (slash == std::string::npos) ? "" : base.substr(0, slash + 1);
    std::string file = (slash == std::string::npos) ? base : base.substr(slash + 1);
    for (const char* ext : kExts) {
        for (const std::string& candidate : { base + "_" + ext, dir + "_" + file + ext }) {
            if (Vfs::Exists(candidate)) {
                outMask = candidate;
                return true;
            }
        }
    }
    return true;
}

static std::string StripImageExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return path;
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif") return path.substr(0, dot);
    return path;
}

Texture* TextureManager::Load(const std::string& relativePath) {
    return LoadImpl(relativePath, false);
}

Texture* TextureManager::LoadImpl(const std::string& relativePath, bool quiet) {
    std::string key = NormalizeKey(relativePath);
    auto it = sTextures.find(key);
    if (it != sTextures.end()) {
        return it->second.id ? &it->second : nullptr;
    }
    PERF_SCOPE("TextureManager::Load miss " + key);

    int cols = 1, rows = 1;
    GetCelInfo(key, cols, rows);

    std::string requested = StripImageExtension(relativePath);
    std::string colorPath, maskPath;
    bool found = FindImageFiles(requested, colorPath, maskPath);
    if (!found) {
        for (const std::string& base : { "Images/" + key, key, "Images/Backgrounds/" + key, "Fonts/" + key }) {
            if (FindImageFiles(base, colorPath, maskPath)) {
                found = true;
                break;
            }
        }
    }

    if (!found) {
        if (!quiet) std::cerr << "[TextureManager] File does not exist for asset: " << relativePath << " (key: " << key << ")" << std::endl;
        sTextures[key] = Texture{}; // remember the miss so it is not searched for every frame
        return nullptr;
    }

    bool keepAlpha = key.rfind("backgrounds/", 0) != 0;
    Texture tex = LoadFromFiles(Vfs::Resolve(colorPath), maskPath, cols, rows, keepAlpha);
    sTextures[key] = tex;
    if (tex.id) PERF_TEXTURE_LOADED(key, tex.width, tex.height);
    return tex.id ? &sTextures[key] : nullptr;
}

Texture* TextureManager::LoadGrid(const std::string& relativePath, int cols, int rows) {
    Texture* t = Load(relativePath);
    if (t) {
        t->cols = cols;
        t->rows = rows;
    }
    return t;
}

Texture* TextureManager::Get(const std::string& relativePath) {
    return Load(relativePath);
}

void TextureManager::Unload(const std::string& relativePath) {
    std::string key = NormalizeKey(relativePath);
    auto it = sTextures.find(key);
    if (it != sTextures.end()) {
        if (it->second.id != 0) {
            glDeleteTextures(1, &it->second.id);
        }
        sTextures.erase(it);
    }
}

void TextureManager::Clear() {
    for (auto& pair : sTextures) {
        if (pair.second.id != 0) {
            glDeleteTextures(1, &pair.second.id);
        }
    }
    sTextures.clear();
}

} // namespace HeavyWeapon

namespace HeavyWeapon {

static bool IsPreloadSkipped(const std::string& n) {
    static const char* kPrefixes[] = {
        "title", "loading", "cursor_", "backgrounds/", "mainmenu", "mainsmallbtn", "mainbigbtn",
        "mainglow", "mission", "upgradebtns", "armory", "credits", "atmenu", "help", "mouse",
        "advancebtn", "maprect"
    };
    for (const char* p : kPrefixes) if (n.rfind(p, 0) == 0) return true;
    return n == "map";
}

void TextureManager::PreloadFolder(const std::string& sub) {
    PERF_BEGIN(t0);
    int n = 0;
    for (const std::string& file : Vfs::ListDirectory("Images/" + sub)) {
        std::string base = StripImageExtension(file);
        if (base == file || base.empty() || base[0] == '_' || base.back() == '_') continue;
        if (LoadImpl("Images/" + sub + "/" + base, true)) ++n;
    }
    PERF_LOG("preload folder %s: %d textures, %.1f ms", sub.c_str(), n, PERF_NOW() - t0);
}

int TextureManager::PreloadAll(const std::function<void(int, int)>& progress) {
    static bool sDone = false;
    if (sDone) return 0;
    sDone = true;
    PERF_BEGIN(t0);
    // Collect the names first so progress can report a total.
    std::vector<std::string> names;
    for (const auto& meta : sImageMetaTable) {
        if (IsPreloadSkipped(meta.name)) continue;
        names.push_back(meta.name);
    }
    int loaded = 0, done = 0;
    const int total = (int)names.size();
    for (const std::string& n : names) {
        if (LoadImpl(n, true)) ++loaded;
        if (progress) progress(++done, total);
    }
    PERF_LOG("preload: %d textures loaded, total %.1f ms", loaded, PERF_NOW() - t0);
    return loaded;
}

} // namespace HeavyWeapon
