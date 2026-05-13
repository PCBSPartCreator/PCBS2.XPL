#pragma once
#include <string>
#include <vector>

struct ModPart {
    std::string partType;
    std::string id;
    std::string fileName;
    std::vector<std::pair<std::string, std::string>> properties;
};

std::vector<ModPart> Config_LoadMods(const std::string& gameDir);