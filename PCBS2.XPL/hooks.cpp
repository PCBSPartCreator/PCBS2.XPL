#include "hooks.h"
#include "logger.h"
#include "il2cpp.h"
#include <MinHook.h>
#include <array>
#include <string>

typedef bool (*ImportProp_t)(void*, void*, void*, void*, void*);
typedef void (*AddNewPart_t)(void*, void*, void*, void*);
typedef void (*Ctor_t)(void*, void*);

static std::vector<ModPart>* g_pendingMods = nullptr;
static AddNewPart_t          g_addNewPart = nullptr;
static void* g_dbInstance = nullptr;

struct PartHook {
    const char* typeName;
    const char* className;
    ImportProp_t original;
    bool         injected;
};

// Each entry pairs the part type as it appears in mod XMLs with the IL2CPP
// class that owns the matching ImportProp method. Several PartDesc classes
// share a single ImportProp body; that case is handled in Hooks_Install.
static PartHook g_hooks[] = {
    { "GPU",              "PartDescGPU",              nullptr, false },
    { "CPU",              "PartDescCPU",              nullptr, false },
    { "RAM",              "PartDescRAM",              nullptr, false },
    { "Motherboard",      "PartDescMotherboard",      nullptr, false },
    { "PSU",              "PartDescPSU",              nullptr, false },
    { "Case",             "PartDescCase",             nullptr, false },
    { "Storage",          "PartDescStorage",          nullptr, false },

    { "Cooler",           "PartDescCooler",           nullptr, false },
    { "CPUBlock",         "PartDescCPUBlock",         nullptr, false },
    { "GPUBlock",         "PartDescGPUBlock",         nullptr, false },
    { "MotherboardBlock", "PartDescMotherboardBlock", nullptr, false },
    { "MemoryBlock",      "PartDescMemoryBlock",      nullptr, false },
    { "PumpReservoir",    "PartDescPumpReservoir",    nullptr, false },
    { "Pump",             "PartDescPump",             nullptr, false },
    { "Reservoir",        "PartDescReservoir",        nullptr, false },

    { "MonitorPerif",     "PartDescMonitorPerif",     nullptr, false },
    { "KeyboardPerif",    "PartDescKeyboardPerif",    nullptr, false },
    { "MousePerif",       "PartDescMousePerif",       nullptr, false },
    { "MousePadPerif",    "PartDescMousePadPerif",    nullptr, false },
    { "HeadsetPerif",     "PartDescHeadsetPerif",     nullptr, false },
    { "MicrophonePerif",  "PartDescMicrophonePerif",  nullptr, false },

    { "Cable",            "PartDescCable",            nullptr, false },
    { "CableConnector",   "PartDescCableConnector",   nullptr, false },
    { "Pipe",             "PartDescPipe",             nullptr, false },
    { "PipeConnector",    "PartDescPipeConnector",    nullptr, false },

    { "Tool",             "PartDescTool",             nullptr, false },
    { "Coolant",          "PartDescCoolant",          nullptr, false },
    { "Decoration",       "PartDescDecoration",       nullptr, false },
    { "LEDStrip",         "PartDescLEDStrip",         nullptr, false },
    { "RamHeatsink",      "PartDescRamHeatsink",      nullptr, false },

    { "PowerSplitter",    "PartDescPowerSplitter",    nullptr, false },
    { "PowerAdapter",     "PartDescPowerAdapter",     nullptr, false },
};
static constexpr int HOOK_COUNT = sizeof(g_hooks) / sizeof(g_hooks[0]);

// Resolve PartsDatabase.s_instance and its AddNewPart method on first use.
// Done lazily so we don't depend on the database existing at DLL load time.
static bool InitDatabase() {
    if (g_dbInstance) return true;

    Il2CppClass* dbClass = IL2CPP_FindClass("", "PartsDatabase");
    if (!dbClass) return false;

    Il2CppFieldInfo* field = il2cpp_class_get_field_from_name(dbClass, "s_instance");
    if (!field) return false;

    il2cpp_field_static_get_value(field, &g_dbInstance);
    if (!g_dbInstance) return false;

    const Il2CppMethodInfo* m = il2cpp_class_get_method_from_name(dbClass, "AddNewPart", 2);
    if (!m) return false;
    g_addNewPart = (AddNewPart_t)IL2CPP_GetMethodPointer(m);

    return g_addNewPart != nullptr;
}

// SEH wrapper around AddNewPart. Keep this free of C++ objects with
// destructors - SEH and RAII don't mix inside the same function.
static bool SafeAddNewPart(void* db, Il2CppString* id, Il2CppObject* part) {
    __try {
        g_addNewPart(db, id, part, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Construct each pending mod of the given type and register it with the
// database. Reusing the game's own ImportProp populates the part the same
// way a vanilla XML load would.
static void InjectMods(int hookIndex, void* errorStack) {
    PartHook& h = g_hooks[hookIndex];
    if (h.injected || !g_pendingMods || !InitDatabase()) return;

    // Set the flag before touching h.original. If ImportProp recursively
    // hits this hook again, the early-out above prevents double injection.
    h.injected = true;

    Il2CppClass* klass = IL2CPP_FindClass("", h.className);
    if (!klass) return;

    const Il2CppMethodInfo* ctorM = il2cpp_class_get_method_from_name(klass, ".ctor", 0);
    Ctor_t ctor = ctorM ? (Ctor_t)IL2CPP_GetMethodPointer(ctorM) : nullptr;

    for (const auto& mod : *g_pendingMods) {
        if (mod.partType != h.typeName) continue;

        Il2CppObject* part = il2cpp_object_new(klass);
        if (!part) {
            Logger::Log("[-] Failed to create object for: " + mod.id);
            continue;
        }
        if (ctor) ctor(part, nullptr);

        int set = 0;
        for (const auto& prop : mod.properties) {
            Il2CppString* k = il2cpp_string_new(prop.first.c_str());
            Il2CppString* v = il2cpp_string_new(prop.second.c_str());
            if (k && v && h.original(part, k, v, errorStack, nullptr)) set++;
        }

        Il2CppString* id = il2cpp_string_new(mod.id.c_str());

        if (!g_dbInstance || !g_addNewPart || !id || !part) {
            Logger::Log("[-] AddNewPart skipped (invalid argument): " + mod.id);
            continue;
        }

        if (SafeAddNewPart(g_dbInstance, id, part)) {
            Logger::Log("[+] Injected: " + mod.id + " (" + std::to_string(set) + " props)");
        }
        else {
            Logger::Log("[-] AddNewPart crashed for: " + mod.id);
        }
    }
}

// MinHook needs a distinct compile-time function for every detour target,
// so one trampoline is generated per index instead of a single dispatcher.
#define MAKE_HOOK(index) \
    static bool Hook_##index(void* self, void* name, void* val, void* err, void* mi) { \
        if (!g_hooks[index].injected && err && g_pendingMods) \
            InjectMods(index, err); \
        return g_hooks[index].original(self, name, val, err, mi); \
    }

MAKE_HOOK(0)
MAKE_HOOK(1)
MAKE_HOOK(2)
MAKE_HOOK(3)
MAKE_HOOK(4)
MAKE_HOOK(5)
MAKE_HOOK(6)
MAKE_HOOK(7)
MAKE_HOOK(8)
MAKE_HOOK(9)
MAKE_HOOK(10)
MAKE_HOOK(11)
MAKE_HOOK(12)
MAKE_HOOK(13)
MAKE_HOOK(14)
MAKE_HOOK(15)
MAKE_HOOK(16)
MAKE_HOOK(17)
MAKE_HOOK(18)
MAKE_HOOK(19)
MAKE_HOOK(20)
MAKE_HOOK(21)
MAKE_HOOK(22)
MAKE_HOOK(23)
MAKE_HOOK(24)
MAKE_HOOK(25)
MAKE_HOOK(26)
MAKE_HOOK(27)
MAKE_HOOK(28)
MAKE_HOOK(29)
MAKE_HOOK(30)
MAKE_HOOK(31)

static void* g_hookFuncs[] = {
    (void*)Hook_0,  (void*)Hook_1,  (void*)Hook_2,  (void*)Hook_3,
    (void*)Hook_4,  (void*)Hook_5,  (void*)Hook_6,  (void*)Hook_7,
    (void*)Hook_8,  (void*)Hook_9,  (void*)Hook_10, (void*)Hook_11,
    (void*)Hook_12, (void*)Hook_13, (void*)Hook_14, (void*)Hook_15,
    (void*)Hook_16, (void*)Hook_17, (void*)Hook_18, (void*)Hook_19,
    (void*)Hook_20, (void*)Hook_21, (void*)Hook_22, (void*)Hook_23,
    (void*)Hook_24, (void*)Hook_25, (void*)Hook_26, (void*)Hook_27,
    (void*)Hook_28, (void*)Hook_29, (void*)Hook_30, (void*)Hook_31,
};

// If g_hooks gains or loses an entry without updating MAKE_HOOK/g_hookFuncs,
// fail the build here instead of crashing at runtime.
static_assert(sizeof(g_hookFuncs) / sizeof(g_hookFuncs[0]) == HOOK_COUNT,
    "g_hookFuncs must stay in sync with g_hooks - add a MAKE_HOOK and a "
    "g_hookFuncs entry when extending g_hooks.");

void Hooks_SetPendingMods(std::vector<ModPart>* mods) {
    g_pendingMods = mods;
}

bool Hooks_Install() {
    bool ok = true;
    std::array<void*, HOOK_COUNT> hookPtrs{};

    for (int i = 0; i < HOOK_COUNT; i++) {
        Il2CppClass* klass = IL2CPP_FindClass("", g_hooks[i].className);
        if (!klass) {
            Logger::Log("[-] Class not found: " + std::string(g_hooks[i].className));
            ok = false; continue;
        }

        const Il2CppMethodInfo* m = il2cpp_class_get_method_from_name(klass, "ImportProp", 3);
        if (!m) {
            Logger::Log("[-] ImportProp not found on: " + std::string(g_hooks[i].className));
            ok = false; continue;
        }

        void* ptr = IL2CPP_GetMethodPointer(m);
        if (!ptr) {
            Logger::Log("[-] No method pointer for: " + std::string(g_hooks[i].className));
            ok = false; continue;
        }

        hookPtrs[i] = ptr;

        MH_STATUS s = MH_CreateHook(ptr, g_hookFuncs[i], (LPVOID*)&g_hooks[i].original);
        if (s != MH_OK) {
            // PCBS2 generates the same ImportProp body for several PartDesc
            // classes. MinHook rejects a second hook on the same address, so
            // reuse the trampoline from the first class that owned it.
            bool shared = false;
            for (int j = 0; j < i; j++) {
                if (g_hooks[j].original && hookPtrs[j] == ptr) {
                    g_hooks[i].original = g_hooks[j].original;
                    Logger::Log("[~] " + std::string(g_hooks[i].className) +
                        " shares ImportProp with " + std::string(g_hooks[j].className));
                    shared = true;
                    break;
                }
            }
            if (!shared) {
                Logger::Log("[-] CreateHook failed for: " + std::string(g_hooks[i].className) +
                    " (MH_STATUS=" + std::to_string(s) + ")");
                ok = false;
            }
            continue;
        }

        s = MH_EnableHook(ptr);
        if (s != MH_OK) {
            Logger::Log("[-] EnableHook failed for: " + std::string(g_hooks[i].className));
            ok = false; continue;
        }

        Logger::Log("[+] Hooked: " + std::string(g_hooks[i].className));
    }
    return ok;
}