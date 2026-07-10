#include <Windows.h>
#include <string>
#include <process.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <MinHook.h>
#include "logger.h"
#include "il2cpp.h"
#include "hooks.h"
#include "config.h"

// Hook service for addons. By the time any addon's XPL_Initialize runs we are
// inside Bootstrap(), which executes after InitThread already called
// MH_Initialize() — so MinHook is ready here.
extern "C" __declspec(dllexport)
bool XPL_CreateHook(void* target, void* detour, void** original)
{
    if (!target || !detour)
        return false;

    if (MH_CreateHook(target, detour, original) != MH_OK)
        return false;

    return MH_EnableHook(target) == MH_OK;
}

#pragma comment(linker, "/export:GetFileVersionInfoA=C:\\Windows\\System32\\version.GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoW=C:\\Windows\\System32\\version.GetFileVersionInfoW")
#pragma comment(linker, "/export:VerQueryValueA=C:\\Windows\\System32\\version.VerQueryValueA")
#pragma comment(linker, "/export:VerQueryValueW=C:\\Windows\\System32\\version.VerQueryValueW")

static std::string g_gameDir;

using XPL_InitializeFn = bool (*)(const char* gameDir);
using XPL_ShutdownFn = void (*)();
using XPL_StringFn = const char* (*)();
using XPL_TickFn = void (*)();

struct LoadedAddon
{
    HMODULE module = nullptr;
    std::string path;
    std::string displayName;
    std::string version;
    XPL_InitializeFn initialize = nullptr;
    XPL_ShutdownFn shutdown = nullptr;
    XPL_TickFn tick = nullptr;
    bool tickCrashed = false;
};

struct LoadedLegacyAddon
{
    HMODULE module = nullptr;
    std::string path;
    std::string fileName;
};

static std::vector<LoadedAddon> g_addons;
static std::vector<LoadedLegacyAddon> g_legacyAddons;

// ------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------

static std::string ToLowerAscii(std::string value)
{
    for (size_t i = 0; i < value.size(); ++i)
    {
        char c = value[i];
        if (c >= 'A' && c <= 'Z')
            value[i] = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

static bool EndsWithNoCase(const std::string& value, const std::string& suffix)
{
    if (suffix.size() > value.size())
        return false;

    const size_t offset = value.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i)
    {
        char a = value[offset + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

static std::string JoinPath(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    const char last = a[a.size() - 1];
    if (last == '\\' || last == '/')
        return a + b;
    return a + "\\" + b;
}

static std::string GetFileNameOnly(const std::string& path)
{
    const size_t pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

static bool IsLegacyAddonFileName(const std::string& pathOrName)
{
    const std::string fileName = ToLowerAscii(GetFileNameOnly(pathOrName));

    // Legacy exceptions:
    // These two DLLs are allowed to load without XPL_ exports.
    // Everything else under addons/ must expose XPL_Initialize, XPL_GetName and XPL_GetVersion.
    return fileName == "jellyssockets.dll" ||
        fileName == "glumitytoolsuite2.dll";
}

static bool IsLegacyFileNameAlreadyLoaded(const std::string& pathOrName)
{
    const std::string fileName = ToLowerAscii(GetFileNameOnly(pathOrName));

    for (size_t i = 0; i < g_legacyAddons.size(); ++i)
    {
        if (ToLowerAscii(g_legacyAddons[i].fileName) == fileName)
            return true;
    }

    return false;
}

// ------------------------------------------------------------
// Recursive addon scanning
// ------------------------------------------------------------

static void ScanAddonDllsRecursive(const std::string& folder, std::vector<std::string>& outFiles)
{
    WIN32_FIND_DATAA fd = {};
    HANDLE hFind = FindFirstFileA(JoinPath(folder, "*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        const char* name = fd.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        const std::string fullPath = JoinPath(folder, name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            ScanAddonDllsRecursive(fullPath, outFiles);
        }
        else if (EndsWithNoCase(name, ".dll"))
        {
            outFiles.push_back(fullPath);
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
}

// ------------------------------------------------------------
// PE export inspection
// This checks XPL_ exports without LoadLibraryA, so DllMain is not executed
// for random helper DLLs.
// ------------------------------------------------------------

static bool ReadFileBytes(const std::string& path, std::vector<unsigned char>& outData)
{
    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0 || size.QuadPart > 64 * 1024 * 1024)
    {
        CloseHandle(hFile);
        return false;
    }

    outData.resize(static_cast<size_t>(size.QuadPart));
    DWORD bytesRead = 0;
    const BOOL ok = ReadFile(hFile, outData.data(), static_cast<DWORD>(outData.size()), &bytesRead, nullptr);
    CloseHandle(hFile);

    return ok && bytesRead == outData.size();
}

static const IMAGE_SECTION_HEADER* FindSectionForRva(const IMAGE_SECTION_HEADER* sections,
    unsigned short numberOfSections, DWORD rva)
{
    for (unsigned short i = 0; i < numberOfSections; ++i)
    {
        const IMAGE_SECTION_HEADER& sec = sections[i];
        const DWORD secStart = sec.VirtualAddress;
        const DWORD secSize = sec.Misc.VirtualSize ? sec.Misc.VirtualSize : sec.SizeOfRawData;
        const DWORD secEnd = secStart + secSize;
        if (rva >= secStart && rva < secEnd)
            return &sec;
    }
    return nullptr;
}

static const unsigned char* RvaToPtr(const std::vector<unsigned char>& data,
    const IMAGE_NT_HEADERS64* nt, DWORD rva)
{
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    const IMAGE_SECTION_HEADER* sec = FindSectionForRva(sections, nt->FileHeader.NumberOfSections, rva);
    if (!sec)
        return nullptr;

    const DWORD offsetInSection = rva - sec->VirtualAddress;
    const DWORD fileOffset = sec->PointerToRawData + offsetInSection;
    if (fileOffset >= data.size())
        return nullptr;

    return data.data() + fileOffset;
}

static bool FileHasRequiredXplExports(const std::string& path)
{
    std::vector<unsigned char> data;
    if (!ReadFileBytes(path, data) || data.size() < sizeof(IMAGE_DOS_HEADER))
        return false;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    if (dos->e_lfanew <= 0 || static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > data.size())
        return false;

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(data.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return false;

    const IMAGE_DATA_DIRECTORY& exportDirEntry =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!exportDirEntry.VirtualAddress || !exportDirEntry.Size)
        return false;

    const auto* exportDir = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(
        RvaToPtr(data, nt, exportDirEntry.VirtualAddress));
    if (!exportDir)
        return false;

    const auto* names = reinterpret_cast<const DWORD*>(RvaToPtr(data, nt, exportDir->AddressOfNames));
    if (!names)
        return false;

    bool hasInitialize = false;
    bool hasGetName = false;
    bool hasGetVersion = false;

    for (DWORD i = 0; i < exportDir->NumberOfNames; ++i)
    {
        const char* exportName = reinterpret_cast<const char*>(RvaToPtr(data, nt, names[i]));
        if (!exportName)
            continue;

        if (strcmp(exportName, "XPL_Initialize") == 0) hasInitialize = true;
        else if (strcmp(exportName, "XPL_GetName") == 0) hasGetName = true;
        else if (strcmp(exportName, "XPL_GetVersion") == 0) hasGetVersion = true;

        if (hasInitialize && hasGetName && hasGetVersion)
            return true;
    }

    return false;
}

// ------------------------------------------------------------
// Legacy exceptions
// ------------------------------------------------------------

static void LoadLegacyAddonFromPath(const std::string& dllPath)
{
    if (!IsLegacyAddonFileName(dllPath))
        return;

    const std::string fileName = GetFileNameOnly(dllPath);

    // Avoid loading both root and addons copies of the same legacy DLL.
    // Priority is: game root first, then addons scan fallback.
    if (IsLegacyFileNameAlreadyLoaded(fileName))
    {
        Logger::Log("[ ] Skipping duplicate legacy addon: " + dllPath);
        return;
    }

    if (GetFileAttributesA(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;

    HMODULE mod = LoadLibraryA(dllPath.c_str());
    if (!mod)
    {
        Logger::Log("[-] Legacy addon present but LoadLibrary failed: " + dllPath +
            " (GLE=" + std::to_string(GetLastError()) + ")");
        return;
    }

    LoadedLegacyAddon addon;
    addon.module = mod;
    addon.path = dllPath;
    addon.fileName = fileName;
    g_legacyAddons.push_back(addon);

    Logger::Log("[+] Loaded legacy addon exception: " + fileName + " [" + dllPath + "]");
}

static void LoadLegacyAddonsFromGameRoot()
{
    // Backward-compatible locations from the old PCBS2.XPL behavior.
    // These are intentionally loaded before the addons/ scan, so existing installs keep working.
    LoadLegacyAddonFromPath(JoinPath(g_gameDir, "JellysSockets.dll"));
    LoadLegacyAddonFromPath(JoinPath(g_gameDir, "GlumityToolSuite2.dll"));
}

// ------------------------------------------------------------
// Safe addon calls
// ------------------------------------------------------------

static bool SafeCallAddonTick(XPL_TickFn tick)
{
    __try
    {
        tick();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool SafeCallAddonShutdown(XPL_ShutdownFn shutdown)
{
    __try
    {
        shutdown();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// ------------------------------------------------------------
// XPL addon loading
// ------------------------------------------------------------

static void LoadXplAddons()
{
    // Load exact legacy exceptions first.
    // These two DLLs do not need XPL_ exports.
    LoadLegacyAddonsFromGameRoot();

    const std::string addonsDir = JoinPath(g_gameDir, "addons");
    DWORD addonsAttrs = GetFileAttributesA(addonsDir.c_str());
    if (addonsAttrs == INVALID_FILE_ATTRIBUTES)
    {
        if (!CreateDirectoryA(addonsDir.c_str(), nullptr))
        {
            Logger::Log("[-] Failed to create addons folder. Error: " + std::to_string(GetLastError()));
            return;
        }
        Logger::Log("[+] Created addons folder: " + addonsDir);
        return;
    }
    if (!(addonsAttrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        Logger::Log("[-] addons exists but is not a folder: " + addonsDir);
        return;
    }

    std::vector<std::string> candidates;
    ScanAddonDllsRecursive(addonsDir, candidates);
    std::sort(candidates.begin(), candidates.end());

    Logger::Log("[+] Addon scan found " + std::to_string(candidates.size()) + " DLL candidate(s)");

    size_t loadedCount = 0;
    for (const std::string& dllPath : candidates)
    {
        // Legacy exception inside addons/ or subfolders.
        // This lets users put them under addons/_Legacy/ if desired.
        if (IsLegacyAddonFileName(dllPath))
        {
            LoadLegacyAddonFromPath(dllPath);
            continue;
        }

        if (!FileHasRequiredXplExports(dllPath))
        {
            Logger::Log("[ ] Skipping non-XPL DLL: " + dllPath);
            continue;
        }

        HMODULE mod = LoadLibraryA(dllPath.c_str());
        if (!mod)
        {
            Logger::Log("[-] LoadLibrary failed for addon: " + dllPath +
                " (GLE=" + std::to_string(GetLastError()) + ")");
            continue;
        }

        LoadedAddon addon;
        addon.module = mod;
        addon.path = dllPath;
        addon.initialize = reinterpret_cast<XPL_InitializeFn>(GetProcAddress(mod, "XPL_Initialize"));
        addon.shutdown = reinterpret_cast<XPL_ShutdownFn>(GetProcAddress(mod, "XPL_Shutdown"));
        addon.tick = reinterpret_cast<XPL_TickFn>(GetProcAddress(mod, "XPL_Tick"));
        XPL_StringFn getName = reinterpret_cast<XPL_StringFn>(GetProcAddress(mod, "XPL_GetName"));
        XPL_StringFn getVersion = reinterpret_cast<XPL_StringFn>(GetProcAddress(mod, "XPL_GetVersion"));

        if (!addon.initialize || !getName || !getVersion)
        {
            Logger::Log("[-] Required XPL exports missing after load: " + dllPath);
            FreeLibrary(mod);
            continue;
        }

        const char* rawName = getName();
        const char* rawVersion = getVersion();
        addon.displayName = rawName ? rawName : GetFileNameOnly(dllPath);
        addon.version = rawVersion ? rawVersion : "unknown";

        bool initOk = false;
        __try
        {
            initOk = addon.initialize(g_gameDir.c_str());
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            initOk = false;
            Logger::Log("[-] Addon initialize crashed: " + dllPath);
        }

        if (!initOk)
        {
            Logger::Log("[-] Addon initialize failed: " + dllPath);
            FreeLibrary(mod);
            continue;
        }

        Logger::Log("[+] Loaded XPL addon: " + addon.displayName + " v" + addon.version +
            " [" + dllPath + "]");
        g_addons.push_back(addon);
        ++loadedCount;
    }

    Logger::Log("[+] Active XPL addons: " + std::to_string(loadedCount));
    Logger::Log("[+] Active legacy addon exceptions: " + std::to_string(g_legacyAddons.size()));
}

static void TickXplAddons()
{
    for (size_t i = 0; i < g_addons.size(); ++i)
    {
        LoadedAddon& addon = g_addons[i];
        if (!addon.tick || addon.tickCrashed)
            continue;

        if (!SafeCallAddonTick(addon.tick))
        {
            addon.tickCrashed = true;
            addon.tick = nullptr;
            Logger::Log("[-] Addon tick crashed; disabling tick for: " + addon.displayName);
        }
    }
}

static void ShutdownAddons()
{
    // Shut down modern XPL addons in reverse load order.
    for (size_t i = g_addons.size(); i > 0; --i)
    {
        LoadedAddon& addon = g_addons[i - 1];

        if (addon.shutdown)
        {
            if (!SafeCallAddonShutdown(addon.shutdown))
                Logger::Log("[-] Addon shutdown crashed: " + addon.displayName);
        }

        if (addon.module)
        {
            FreeLibrary(addon.module);
            addon.module = nullptr;
        }
    }

    g_addons.clear();

    // Legacy DLLs usually do their own cleanup in DllMain on FreeLibrary.
    // They do not have XPL_Shutdown.
    for (size_t i = g_legacyAddons.size(); i > 0; --i)
    {
        LoadedLegacyAddon& addon = g_legacyAddons[i - 1];
        if (addon.module)
        {
            FreeLibrary(addon.module);
            addon.module = nullptr;
        }
    }

    g_legacyAddons.clear();
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

static bool Bootstrap()
{
    // Resolves on the first invoke; the retry path below covers late metadata.
    Il2CppClass* pdb = IL2CPP_FindClass("", "PartsDatabase");
    if (!pdb) return false;

    LoadXplAddons();

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
    void** params, Il2CppException** exc)
{
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
    if (g_bootstrapState == 2)
        TickXplAddons();

    return g_runtimeInvoke_orig(method, obj, params, exc);
}

// Native-only: wait for GameAssembly.dll, resolve exports, hook runtime_invoke.
// No IL2CPP call happens on this thread (see Hook_RuntimeInvoke).
static DWORD WINAPI InitThread(LPVOID)
{
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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        uintptr_t hThread = _beginthreadex(nullptr, 0,
            [](void*) -> unsigned { InitThread(nullptr); return 0; },
            nullptr, 0, nullptr);
        if (hThread) CloseHandle((HANDLE)hThread);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        ShutdownAddons();
        Logger::Close();
    }
    return TRUE;
}