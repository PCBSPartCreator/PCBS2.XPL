#include "hooks.h"
#include "logger.h"
#include "il2cpp.h"
#include <MinHook.h>
#include <fstream>
#include <sstream>

typedef void (*PartsDatabase_Load_t)(void* self);
typedef void (*PartsDatabase_ImportFromHTML_t)(void* self, void* asset);
typedef void (*TextAsset_Ctor_t)(void* self, int options, Il2CppString* text);

static std::vector<ModFile>* g_pendingMods = nullptr;
static PartsDatabase_Load_t g_originalLoad = nullptr;
static PartsDatabase_ImportFromHTML_t g_importFromHTML = nullptr;
static Il2CppClass* g_textAssetClass = nullptr;
static TextAsset_Ctor_t g_textAssetCtor = nullptr;
static bool g_modsLoaded = false;

static std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void* CreateTextAsset(const std::string& content) {
    if (!g_textAssetClass || !g_textAssetCtor) return nullptr;

    Il2CppObject* obj = il2cpp_object_new(g_textAssetClass);
    if (!obj) return nullptr;
    il2cpp_runtime_object_init(obj);

    Il2CppString* text = il2cpp_string_new(content.c_str());
    if (!text) return nullptr;

    g_textAssetCtor(obj, 1, text);
    return obj;
}

static bool SafeImportFromHTML(void* db, void* asset) {
    __try {
        g_importFromHTML(db, asset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void LoadAllMods(void* dbInstance) {
    if (g_modsLoaded) return;
    if (!g_pendingMods || g_pendingMods->empty()) return;
    if (!g_importFromHTML) return;

    int loaded = 0;
    int failed = 0;
    for (const auto& mod : *g_pendingMods) {

        std::string content = ReadFile(mod.fullPath);
        if (content.empty()) {
            Logger::Log("[-] Empty or unreadable: " + mod.fileName);
            failed++;
            continue;
        }

        void* asset = CreateTextAsset(content);
        if (!asset) {
            Logger::Log("[-] TextAsset creation failed: " + mod.fileName);
            failed++;
            continue;
        }

        if (SafeImportFromHTML(dbInstance, asset)) {
            loaded++;
            Logger::Log("[+] Imported: " + mod.fileName);
        }
        else {
            Logger::Log("[-] ImportFromHTML crashed for: " + mod.fileName);
            failed++;
        }
    }

    g_modsLoaded = true;

    Logger::Log("[=] Mod load complete: " + std::to_string(loaded) +
        " imported, " + std::to_string(failed) + " failed");
}

static void Hook_PDB_Load(void* self) {
    g_originalLoad(self);
    LoadAllMods(self);
}

void Hooks_SetPendingMods(std::vector<ModFile>* mods) {
    g_pendingMods = mods;
}

bool Hooks_Install() {

    Il2CppClass* dbClass = IL2CPP_FindClass("", "PartsDatabase");
    if (!dbClass) {
        Logger::Log("[-] PartsDatabase class not found");
        return false;
    }

    const Il2CppMethodInfo* loadM = il2cpp_class_get_method_from_name(dbClass, "Load", 0);
    if (!loadM) {
        Logger::Log("[-] PartsDatabase.Load not found");
        return false;
    }
    void* loadPtr = IL2CPP_GetMethodPointer(loadM);
    if (!loadPtr) {
        Logger::Log("[-] PartsDatabase.Load has no method pointer");
        return false;
    }

    const Il2CppMethodInfo* importM = il2cpp_class_get_method_from_name(dbClass, "ImportFromHTML", 1);
    if (!importM) {
        Logger::Log("[-] PartsDatabase.ImportFromHTML not found");
        return false;
    }
    g_importFromHTML = (PartsDatabase_ImportFromHTML_t)IL2CPP_GetMethodPointer(importM);
    if (!g_importFromHTML) {
        Logger::Log("[-] ImportFromHTML has no method pointer");
        return false;
    }

    g_textAssetClass = IL2CPP_FindClass("UnityEngine", "TextAsset");
    if (!g_textAssetClass) {
        Logger::Log("[-] UnityEngine.TextAsset class not found");
        return false;
    }

    const Il2CppMethodInfo* ctorM = il2cpp_class_get_method_from_name(g_textAssetClass, ".ctor", 2);
    if (!ctorM) {
        Logger::Log("[-] TextAsset .ctor(CreateOptions, string) not found");
        return false;
    }
    g_textAssetCtor = (TextAsset_Ctor_t)IL2CPP_GetMethodPointer(ctorM);
    if (!g_textAssetCtor) {
        Logger::Log("[-] TextAsset .ctor has no method pointer");
        return false;
    }
    Logger::Log("[+] TextAsset .ctor(CreateOptions, string) resolved");

    MH_STATUS s = MH_CreateHook(loadPtr, (LPVOID)Hook_PDB_Load, (LPVOID*)&g_originalLoad);
    if (s != MH_OK) {
        Logger::Log("[-] CreateHook on PartsDatabase.Load failed (MH_STATUS=" + std::to_string(s) + ")");
        return false;
    }
    s = MH_EnableHook(loadPtr);
    if (s != MH_OK) {
        Logger::Log("[-] EnableHook on PartsDatabase.Load failed");
        return false;
    }

    Logger::Log("[+] Hooked PartsDatabase.Load");
    return true;
}