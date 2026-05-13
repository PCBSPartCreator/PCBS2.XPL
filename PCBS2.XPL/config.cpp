#include "config.h"
#include "logger.h"
#include <fstream>
#include <windows.h>

static std::string DecodeEntities(const std::string& s) {
    std::string r = s;
    size_t pos = 0;
    auto replace = [&](const std::string& from, const std::string& to) {
        pos = 0;
        while ((pos = r.find(from, pos)) != std::string::npos) {
            r.replace(pos, from.length(), to);
            pos += to.length();
        }
        };
    replace("&amp;", "&");
    replace("&quot;", "\"");
    replace("&lt;", "<");
    replace("&gt;", ">");
    replace("&apos;", "'");
    return r;
}

static std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Map user-facing part type aliases to the internal IL2CPP class suffix.
// Storage variants and cooler subtypes share one PartDesc class in PCBS2.
static std::string NormalizePartType(const std::string& raw) {
    if (raw == "HDD" || raw == "SSD" || raw == "M2" || raw == "M.2")
        return "Storage";

    if (raw == "AirCooler" || raw == "LiquidCooler")
        return "Cooler";

    if (raw == "CableConnectors") return "CableConnector";
    if (raw == "PipeConnectors")  return "PipeConnector";
    if (raw == "RAMHeatsink")     return "RamHeatsink";

    if (raw == "Monitor")    return "MonitorPerif";
    if (raw == "Keyboard")   return "KeyboardPerif";
    if (raw == "Mouse")      return "MousePerif";
    if (raw == "MousePad")   return "MousePadPerif";
    if (raw == "Headset")    return "HeadsetPerif";
    if (raw == "Microphone") return "MicrophonePerif";

    return raw;
}

// Mod files follow the same <td div="key">value</td> layout the game uses
// for its own part XMLs, so the parser stays minimal on purpose.
static ModPart ParseModFile(const std::string& path) {
    ModPart part;
    part.fileName = path;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return part;

    std::string content((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    file.close();

    size_t pos = 0;
    while (pos < content.size()) {
        size_t tdStart = content.find("<td div=\"", pos);
        if (tdStart == std::string::npos) break;

        size_t nameStart = tdStart + 9;
        size_t nameEnd = content.find("\"", nameStart);
        if (nameEnd == std::string::npos) break;

        std::string name = content.substr(nameStart, nameEnd - nameStart);

        size_t valueStart = content.find(">", nameEnd);
        if (valueStart == std::string::npos) break;
        valueStart++;

        size_t valueEnd = content.find("</td>", valueStart);
        if (valueEnd == std::string::npos) break;

        name = Trim(name);
        std::string value = Trim(DecodeEntities(content.substr(valueStart, valueEnd - valueStart)));

        if (name.empty()) {
            pos = valueEnd + 5;
            continue;
        }

        part.properties.emplace_back(name, value);
        if (name == "Part Type") part.partType = NormalizePartType(value);
        if (name == "ID")        part.id = value;
        pos = valueEnd + 5;
    }

    return part;
}

std::vector<ModPart> Config_LoadMods(const std::string& gameDir) {
    std::vector<ModPart> mods;

    std::string modsDir = gameDir + "mods";
    std::string searchPattern = modsDir + "\\*.xml";

    DWORD attrs = GetFileAttributesA(modsDir.c_str());

    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (CreateDirectoryA(modsDir.c_str(), nullptr)) {
            Logger::Log("[+] Created mods folder: " + modsDir);
        }
        else {
            Logger::Log("[-] Failed to create mods folder. Error: " + std::to_string(GetLastError()));
            return mods;
        }
    }
    else if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        Logger::Log("[-] mods exists but is not a folder: " + modsDir);
        return mods;
    }

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPattern.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        Logger::Log("[!] No mods found in mods/ folder");
        return mods;
    }

    do {
        std::string fullPath = modsDir + "\\" + findData.cFileName;
        ModPart part = ParseModFile(fullPath);

        if (part.partType.empty() || part.id.empty()) {
            Logger::Log("[!] Skipped (missing Part Type or ID): " + std::string(findData.cFileName));
            continue;
        }

        bool duplicate = false;
        for (const auto& existing : mods) {
            if (existing.id == part.id) {
                Logger::Log("[-] Duplicate mod ID: " + part.id + " (" + findData.cFileName + ") - skipped");
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            Logger::Log("[+] Loaded: " + part.id + " (" + part.partType + ")");
            mods.push_back(part);
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    Logger::Log("[+] Total mods: " + std::to_string(mods.size()));
    return mods;
}