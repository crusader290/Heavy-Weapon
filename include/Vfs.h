#pragma once

#include <string>
#include <vector>

namespace HeavyWeapon {

class Vfs {
public:
    static void Init(const std::string& customBasePath = "");
    static std::string Resolve(const std::string& relativePath);
    static bool Exists(const std::string& relativePath);
    // Real (on-disk case) names in a directory, from the cached listing; empty if missing.
    static std::vector<std::string> ListDirectory(const std::string& relativeDir);
    static std::string ReadTextFile(const std::string& relativePath);

private:
    static std::string sBasePath;
    static std::vector<std::string> sSearchPaths;
    static std::string NormalizeSlashes(const std::string& path);
};

} // namespace HeavyWeapon
