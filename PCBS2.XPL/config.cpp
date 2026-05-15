#include "config.h"
#include "logger.h"
#include <windows.h>
#include <fstream>

static void ScanDirectory(const std::string& dir, std::vector<ModFile>& out) {
    std::string searchPattern = dir + "\\*";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPattern.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        const std::string name = findData.cFileName;
        if (name == "." || name == "..") continue;

        const std::string fullPath = dir + "\\" + name;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ScanDirectory(fullPath, out);
            continue;
        }

        if (name.size() < 4) continue;
        std::string ext = name.substr(name.size() - 4);
        for (auto& c : ext) c = (char)tolower(c);
        if (ext != ".xml") continue;

        out.push_back({ fullPath, name });
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
}

std::vector<ModFile> Config_ScanMods(const std::string& gameDir) {
    std::vector<ModFile> mods;
    const std::string modsDir = gameDir + "mods";

    DWORD attrs = GetFileAttributesA(modsDir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryA(modsDir.c_str(), nullptr)) {
            Logger::Log("[-] Failed to create mods folder. Error: " + std::to_string(GetLastError()));
            return mods;
        }
        Logger::Log("[+] Created mods folder: " + modsDir);
        return mods;
    }
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        Logger::Log("[-] mods exists but is not a folder: " + modsDir);
        return mods;
    }

    ScanDirectory(modsDir, mods);

    if (mods.empty()) {
        Logger::Log("[!] No .xml mods found in mods/ folder");
    }
    else {
        for (const auto& m : mods) {
            Logger::Log("[+] Found mod file: " + m.fileName);
        }
        Logger::Log("[+] Total mod files: " + std::to_string(mods.size()));
    }
    return mods;
}