#pragma once
#include <string>
#include <vector>

struct ModFile {
    std::string fullPath;
    std::string fileName;
};

std::vector<ModFile> Config_ScanMods(const std::string& gameDir);