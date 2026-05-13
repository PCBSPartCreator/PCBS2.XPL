#include <Windows.h>
#include <string>
#include <MinHook.h>
#include "logger.h"
#include "il2cpp.h"
#include "hooks.h"
#include "config.h"

// Acts as a version.dll proxy: every export forwards to the real system DLL
// so the Windows loader sees a valid version.dll while we get a DllMain.
#pragma comment(linker, "/export:GetFileVersionInfoA=C:\\Windows\\System32\\version.GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoW=C:\\Windows\\System32\\version.GetFileVersionInfoW")
#pragma comment(linker, "/export:VerQueryValueA=C:\\Windows\\System32\\version.VerQueryValueA")
#pragma comment(linker, "/export:VerQueryValueW=C:\\Windows\\System32\\version.VerQueryValueW")

// Optional chain-load target. PCBS2.XPL coexists with JellysSockets by
// pulling it in here when present, so users only need one proxy DLL.
static HMODULE g_jellyModule = nullptr;

static DWORD WINAPI MainThread(LPVOID) {
    // GameAssembly.dll is loaded after our proxy, so spin until it appears.
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

    // Wait until IL2CPP metadata is populated. PartDescGPU is one of the
    // first PartDesc classes the game registers, so its appearance is a
    // reliable signal that we can start resolving classes and installing
    // hooks. Polling avoids the race between Unity thread-pool creation
    // and MinHook's thread-suspension logic.
    for (int i = 0; i < 100; i++) {
        if (IL2CPP_FindClass("", "PartDescGPU")) break;
        Sleep(50);
    }

    if (MH_Initialize() != MH_OK) {
        Logger::Log("[-] MinHook init failed");
        return 1;
    }

    if (!Hooks_Install()) {
        Logger::Log("[!] Some hooks failed");
    }

    // Static lifetime so the vector outlives MainThread and is still
    // available when the hooks fire during the game's part loading phase.
    static std::vector<ModPart> mods = Config_LoadMods(gameDir);
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