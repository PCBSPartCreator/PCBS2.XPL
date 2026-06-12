#include <Windows.h>
#include <string>
#include <vector>
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

static HMODULE     g_jellyModule = nullptr;
static std::string g_gameDir;

static void LoadJellysSockets() {
    const std::string jellyPath = g_gameDir + "JellysSockets.dll";
    if (GetFileAttributesA(jellyPath.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    g_jellyModule = LoadLibraryA(jellyPath.c_str());
    if (g_jellyModule)
        Logger::Log("[+] Loaded JellysSockets.dll");
    else
        Logger::Log("[-] JellysSockets.dll present but LoadLibrary failed: " +
            std::to_string(GetLastError()));
}

// --- Bootstrap from a game thread -------------------------------------------
// IL2CPP metadata work (class lookups, hook install) must run on a thread the
// runtime/GC knows. runtime_invoke callers are already registered; our native
// init thread is not. Even getter APIs allocate and abort the Boehm GC with
// "Collecting from unknown thread" during cold start, so bootstrap runs from
// inside the il2cpp_runtime_invoke detour instead.

static t_il2cpp_runtime_invoke g_runtimeInvoke_orig = nullptr;
static volatile LONG g_bootstrapState = 0;     // 0 = pending, 1 = running, 2 = done
static LONG          g_bootstrapAttempts = 0;

static bool Bootstrap() {
    // Resolves on the first invoke; the retry path below covers late metadata.
    Il2CppClass* pdb = IL2CPP_FindClass("", "PartsDatabase");
    if (!pdb) return false;

    LoadJellysSockets();

    Hooks_SetSaveFixEnabled(Config_IsSaveFixEnabled(g_gameDir));

    if (!Hooks_Install())
        Logger::Log("[!] Some hooks failed");

    static std::vector<ModFile> mods = Config_ScanMods(g_gameDir);
    if (!mods.empty()) {
        Hooks_SetPendingMods(&mods);
        Logger::Log("[+] " + std::to_string(mods.size()) + " mods queued");
    }

    Logger::Log("[+] Ready");
    return true;
}

static Il2CppObject* Hook_RuntimeInvoke(const Il2CppMethodInfo* method, void* obj,
    void** params, Il2CppException** exc) {
    // One thread wins the CAS and bootstraps; others fast-path past. The hook
    // stays installed - disabling it from its own detour would repatch live code.
    if (g_bootstrapState != 2 &&
        InterlockedCompareExchange(&g_bootstrapState, 1, 0) == 0) {
        if (Bootstrap()) {
            InterlockedExchange(&g_bootstrapState, 2);
        }
        else if (InterlockedIncrement(&g_bootstrapAttempts) >= 200) {
            Logger::Log("[-] PartsDatabase unresolved, giving up");
            InterlockedExchange(&g_bootstrapState, 2);
        }
        else {
            InterlockedExchange(&g_bootstrapState, 0);   // retry on a later invoke
        }
    }
    return g_runtimeInvoke_orig(method, obj, params, exc);
}

// Native-only: wait for GameAssembly.dll, resolve exports, hook runtime_invoke.
// No IL2CPP call happens on this thread (see Hook_RuntimeInvoke).
static DWORD WINAPI InitThread(LPVOID) {
    HMODULE hGA = nullptr;
    while (!(hGA = GetModuleHandleA("GameAssembly.dll"))) Sleep(10);

    char path[MAX_PATH] = {};
    GetModuleFileNameA(hGA, path, MAX_PATH);
    g_gameDir.assign(path);
    g_gameDir = g_gameDir.substr(0, g_gameDir.find_last_of("\\/") + 1);

    Logger::Init(g_gameDir);

    if (!IL2CPP_Init(hGA)) {
        Logger::Log("[-] IL2CPP init failed");
        return 1;
    }

    if (MH_Initialize() != MH_OK) {
        Logger::Log("[-] MinHook init failed");
        return 1;
    }

    MH_STATUS s = MH_CreateHook((LPVOID)il2cpp_runtime_invoke,
        (LPVOID)Hook_RuntimeInvoke, (LPVOID*)&g_runtimeInvoke_orig);
    if (s != MH_OK || MH_EnableHook((LPVOID)il2cpp_runtime_invoke) != MH_OK) {
        Logger::Log("[-] Hook on il2cpp_runtime_invoke failed (MH_STATUS=" +
            std::to_string(s) + ")");
        return 1;
    }

    Logger::Log("[+] runtime_invoke hooked, bootstrap deferred to game thread");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        if (g_jellyModule) { FreeLibrary(g_jellyModule); g_jellyModule = nullptr; }
        Logger::Close();
    }
    return TRUE;
}