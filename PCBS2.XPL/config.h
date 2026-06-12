#pragma once
#include <string>
#include <vector>

struct ModFile {
    std::string fullPath;
    std::string fileName;
};

std::vector<ModFile> Config_ScanMods(const std::string& gameDir);

// Reads config/PCBS2.XPL.cfg; writes a default (SaveFix=true) if missing.
bool Config_IsSaveFixEnabled(const std::string& gameDir);