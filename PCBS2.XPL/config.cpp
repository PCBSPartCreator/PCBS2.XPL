#include "config.h"
#include "logger.h"
#include <windows.h>

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

// --- SaveFix config ---------------------------------------------------------
static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool ParseBool(const std::string& v, bool fallback) {
    std::string s = v;
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    if (s == "true" || s == "1" || s == "yes" || s == "on")  return true;
    if (s == "false" || s == "0" || s == "no" || s == "off") return false;
    return fallback;
}

bool Config_IsSaveFixEnabled(const std::string& gameDir) {
    const std::string cfgDir = gameDir + "config";
    const std::string cfgPath = cfgDir + "\\PCBS2.XPL.cfg";

    if (GetFileAttributesA(cfgDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (CreateDirectoryA(cfgDir.c_str(), nullptr))
            Logger::Log("[+] Created config folder: " + cfgDir);
        else
            Logger::Log("[-] Failed to create config folder. Error: " + std::to_string(GetLastError()));
    }

    // First run: write the default config.
    if (GetFileAttributesA(cfgPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        static const char kDefault[] =
            "# PCBS2.XPL configuration\r\n"
            "# SaveFix=true  -> remove parts from saves whose mod is no longer installed\r\n"
            "# SaveFix=false -> leave saves untouched (part injection still runs)\r\n"
            "SaveFix=true\r\n";
        HANDLE h = CreateFileA(cfgPath.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            WriteFile(h, kDefault, (DWORD)(sizeof(kDefault) - 1), &w, nullptr);
            CloseHandle(h);
            Logger::Log("[+] Created default config: " + cfgPath);
        }
        else {
            Logger::Log("[-] Could not create config file (defaulting SaveFix=true): " + cfgPath);
        }
        return true;
    }

    HANDLE h = CreateFileA(cfgPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        Logger::Log("[-] Could not open config file (defaulting SaveFix=true): " + cfgPath);
        return true;
    }
    LARGE_INTEGER sz{};
    std::string data;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0) {
        data.resize((size_t)sz.QuadPart);
        DWORD got = 0;
        ::ReadFile(h, &data[0], (DWORD)sz.QuadPart, &got, nullptr);
        data.resize(got);
    }
    CloseHandle(h);

    bool saveFix = true;   // default if key absent
    size_t start = 0;
    while (start < data.size()) {
        size_t nl = data.find('\n', start);
        size_t end = (nl == std::string::npos) ? data.size() : nl;
        std::string t = Trim(data.substr(start, end - start));
        start = (nl == std::string::npos) ? data.size() : nl + 1;

        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(t.substr(0, eq));
        std::string val = Trim(t.substr(eq + 1));
        for (auto& c : key) c = (char)tolower((unsigned char)c);
        if (key == "savefix") saveFix = ParseBool(val, true);
    }
    return saveFix;
}