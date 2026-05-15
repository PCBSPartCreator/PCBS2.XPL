#include <Windows.h>
#include <string>
#include <MinHook.h>
#include "logger.h"
#include "il2cpp.h"
#include "hooks.h"
#include "config.h"

#pragma comment(linker, "/export:GetFileVersionInfoA=C:\\Windows\\System32\\version.GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoW=C:\\Windows\\System32\\version.GetFileVersionInfoW")
#pragma comment(linker, "/export:VerQueryValueA=C:\\Windows\\System32\\version.VerQueryValueA")
#pragma comment(linker, "/export:VerQueryValueW=C:\\Windows\\System32\\version.VerQueryValueW")

static HMODULE g_jellyModule = nullptr;

static DWORD WINAPI MainThread(LPVOID) {

    HMODULE hGA = nullptr;
    while (!(hGA = GetModuleHandleA("GameAssembly.dll"))) Sleep(100);

    char path[MAX_PATH] = {};
    GetModuleFileNameA(hGA, path, MAX_PATH);
    std::string gameDir(path);
    gameDir = gameDir.substr(0, gameDir.find_last_of("\\/") + 1);

    Logger::Init(gameDir);

    std::string jellyPath = gameDir + "JellysSockets.dll";
    g_jellyModule = LoadLibraryA(jellyPath.c_str());
    if (g_jellyModule) {
        Logger::Log("[+] Loaded JellysSockets.dll");
    }

    if (!IL2CPP_Init(hGA)) {
        Logger::Log("[-] IL2CPP init failed");
        return 1;
    }

    for (int i = 0; i < 100; i++) {
        if (IL2CPP_FindClass("", "PartsDatabase")) break;
        Sleep(50);
    }

    if (MH_Initialize() != MH_OK) {
        Logger::Log("[-] MinHook init failed");
        return 1;
    }

    if (!Hooks_Install()) {
        Logger::Log("[!] Some hooks failed");
    }

    static std::vector<ModFile> mods = Config_ScanMods(gameDir);
    if (!mods.empty()) {
        Hooks_SetPendingMods(&mods);
        Logger::Log("[+] " + std::to_string(mods.size()) + " mods queued");
    }

    Logger::Log("[+] Ready");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        if (g_jellyModule) { FreeLibrary(g_jellyModule); g_jellyModule = nullptr; }
        Logger::Close();
    }
    return TRUE;
}