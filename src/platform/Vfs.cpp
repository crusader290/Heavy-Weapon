#include "Vfs.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#ifndef F_OK
#define F_OK 0
#endif
#define access _access
#else
#include <unistd.h>
#include <dirent.h>
#endif
#include <algorithm>
#include <unordered_map>
#include <mutex>
#include "core/Perf.h"
#include <SDL2/SDL.h>

namespace HeavyWeapon {

std::string Vfs::sBasePath = "";
std::vector<std::string> Vfs::sSearchPaths;

std::string Vfs::NormalizeSlashes(const std::string& path) {
    std::string res = path;
    for (char& c : res) {
        if (c == '\\') c = '/';
    }
    return res;
}

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// One directory's contents, read once: lowercase name -> real name. Assets are read-only,
// so listings are never invalidated.
using DirListing = std::unordered_map<std::string, std::string>;

static const DirListing& GetListing(const std::string& dirPath) {
    static std::unordered_map<std::string, DirListing> sListings;
    auto it = sListings.find(dirPath);
    if (it != sListings.end()) return it->second;

    DirListing& listing = sListings[dirPath];
    auto add = [&](const char* name) {
        std::string lower = ToLower(name);
#ifdef HW_PERF
        if (listing.count(lower)) {
            PERF_LOG("Vfs: case-collision in %s: %s vs %s", dirPath.c_str(), listing[lower].c_str(), name);
        }
#endif
        listing.emplace(lower, name);
    };
#ifdef _WIN32
    std::string pattern = (dirPath.empty() ? std::string(".") : dirPath) + "/*";
    struct _finddata_t data;
    intptr_t handle = _findfirst(pattern.c_str(), &data);
    if (handle != -1) {
        do {
            add(data.name);
        } while (_findnext(handle, &data) == 0);
        _findclose(handle);
    }
#else
    DIR* dir = opendir(dirPath.empty() ? "." : dirPath.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            add(entry->d_name);
        }
        closedir(dir);
    }
#endif
    return listing;
}

// Case-insensitive path search for POSIX systems. PopCap data references paths with
// arbitrary casing (e.g. "frigistan\yetti" for Images/Anims/Frigistan/yetti.png), so every
// component after the search root is matched case-insensitively. Returns "" on a miss.
static std::string FindCaseInsensitive(const std::string& root, const std::string& relative) {
    std::string current = root;
    size_t start = 0;
    while (start <= relative.size()) {
        size_t slash = relative.find('/', start);
        std::string part = relative.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!part.empty()) {
            std::string dirPath = current.empty() ? "." : current;
            if (dirPath.size() > 1 && dirPath.back() == '/') dirPath.pop_back();
            const DirListing& listing = GetListing(dirPath);
            auto it = listing.find(ToLower(part));
            if (it == listing.end()) return std::string();
            current += it->second;
            if (slash != std::string::npos) current += "/";
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return current;
}

// The sound preload worker resolves paths while the main thread loads textures.
static std::mutex sVfsMutex;
static std::unordered_map<std::string, std::string> sMemo;

void Vfs::Init(const std::string& customBasePath) {
    std::lock_guard<std::mutex> lock(sVfsMutex);
    sMemo.clear();
    sSearchPaths.clear();
    if (!customBasePath.empty()) {
        sSearchPaths.push_back(NormalizeSlashes(customBasePath));
    }
#ifdef __vita__
    sSearchPaths.push_back("ux0:data/heavyweapon/");
    sSearchPaths.push_back("app0:");
#endif
    sSearchPaths.push_back("Heavy Weapon Deluxe/");
    sSearchPaths.push_back("./");
#ifndef __vita__
    // Next to the executable, so a double-clicked build finds data copied beside it
    // whatever the working directory is.
    if (char* base = SDL_GetBasePath()) {
        std::string exeDir = NormalizeSlashes(base);
        SDL_free(base);
        sSearchPaths.push_back(exeDir + "Heavy Weapon Deluxe/");
        sSearchPaths.push_back(exeDir);
    }
#endif
#ifdef HW_DATA_DIR
    // Build-time default (CMake -DHW_DATA_DIR=...), searched last.
    sSearchPaths.push_back(NormalizeSlashes(HW_DATA_DIR));
#endif
}

// Memoized resolution. Returns the real path and sets found; on a miss returns the cleaned
// input. relative path (lowercased) -> resolved path, or "" for a miss. Assets are
// read-only so nothing is ever invalidated.
static const std::string& ResolveMemo(const std::string& relativePath, const std::vector<std::string>& searchPaths,
                                      bool& found, std::string& scratch) {
    scratch = relativePath;
    for (char& c : scratch) if (c == '\\') c = '/';
    if (!scratch.empty() && scratch[0] == '/') scratch.erase(0, 1);

    const std::string key = ToLower(scratch);
    std::lock_guard<std::mutex> lock(sVfsMutex);
    auto memo = sMemo.find(key);
    if (memo != sMemo.end()) {
        found = !memo->second.empty();
        return found ? memo->second : scratch;
    }

    PERF_SCOPE_MIN("Vfs::Resolve " + relativePath, 5.0);
    std::string hit;
    for (const auto& base : searchPaths) {
        std::string root = base;
        if (!root.empty() && root.back() != '/' && root.back() != ':') {
            root += "/";
        }
        std::string resolved = FindCaseInsensitive(root, scratch);
        if (!resolved.empty() && access(resolved.c_str(), F_OK) == 0) {
            hit = resolved;
            break;
        }
    }
    // Already a full path that exists as given (callers pass Resolve() results to Exists()).
    if (hit.empty() && access(scratch.c_str(), F_OK) == 0) hit = scratch;
    auto ins = sMemo.emplace(key, hit).first;
    found = !hit.empty();
    return found ? ins->second : scratch;
}

std::string Vfs::Resolve(const std::string& relativePath) {
    bool found;
    std::string scratch;
    return ResolveMemo(relativePath, sSearchPaths, found, scratch);
}

bool Vfs::Exists(const std::string& relativePath) {
    bool found;
    std::string scratch;
    ResolveMemo(relativePath, sSearchPaths, found, scratch);
    return found;
}

std::vector<std::string> Vfs::ListDirectory(const std::string& relativeDir) {
    std::vector<std::string> names;
    if (!Exists(relativeDir)) return names;
    std::string dir = Resolve(relativeDir);
    if (dir.size() > 1 && dir.back() == '/') dir.pop_back();
    std::lock_guard<std::mutex> lock(sVfsMutex);
    for (const auto& kv : GetListing(dir)) {
        if (kv.second != "." && kv.second != "..") names.push_back(kv.second);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string Vfs::ReadTextFile(const std::string& relativePath) {
    std::string path = Resolve(relativePath);
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace HeavyWeapon
