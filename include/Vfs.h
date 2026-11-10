#pragma once

#include <string>
#include <vector>

namespace HeavyWeapon {

class Vfs {
public:
    static void Init(const std::string& customBasePath = "");
    // Searched before every other path (mods override game files this way).
    static void AddSearchPathFront(const std::string& path);
    static std::string Resolve(const std::string& relativePath);
    static bool Exists(const std::string& relativePath);
    // Index of the search path the file resolves in (0 = searched first), -1 if missing.
    // Lets callers keep files that belong together (an image and its mask) from one place.
    static int Rank(const std::string& relativePath);
    // Real (on-disk case) names in a directory, from the cached listing; empty if missing.
    static std::vector<std::string> ListDirectory(const std::string& relativeDir);
    static std::string ReadTextFile(const std::string& relativePath);

private:
    static std::string sBasePath;
    static std::vector<std::string> sSearchPaths;
    static std::string NormalizeSlashes(const std::string& path);
};

} // namespace HeavyWeapon
