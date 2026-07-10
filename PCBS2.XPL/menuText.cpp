#include "menuText.h"
#include "il2cpp.h"
#include "logger.h"

#include <MinHook.h>
#include <Windows.h>
#include <cstdint>
#include <string>

// CommonUI.SetVersionText(bool force_off)
// Keep a trailing MethodInfo* parameter in the native signature. On IL2CPP/x64 it
// is harmless if unused by the target and safer if the generated method expects it.
typedef void (*CommonUI_SetVersionText_t)(void* self, bool forceOff, void* method);
typedef Il2CppString* (*UIText_GetText_t)(void* self, void* method);
typedef void (*UIText_SetText_t)(void* self, Il2CppString* value, void* method);

static CommonUI_SetVersionText_t g_setVersionTextOrig = nullptr;
static UIText_GetText_t g_textGetText = nullptr;
static UIText_SetText_t g_textSetText = nullptr;

static const Il2CppMethodInfo* g_textGetTextMethod = nullptr;
static const Il2CppMethodInfo* g_textSetTextMethod = nullptr;

static int g_versionTextOffset = -1;
static constexpr const char* kVersionPrefix = "PCBS2.XPL v1.0.5 | PC Building Simulator 2 ";
static bool g_hookInstalled = false;

struct GcThreadGuard {
    void* attached = nullptr;

    GcThreadGuard() {
        if (il2cpp_thread_current && il2cpp_thread_attach && il2cpp_domain_get) {
            if (!il2cpp_thread_current()) {
                Il2CppDomain* domain = il2cpp_domain_get();
                if (domain)
                    attached = il2cpp_thread_attach(domain);
            }
        }
    }

    ~GcThreadGuard() {
        if (attached && il2cpp_thread_detach)
            il2cpp_thread_detach(attached);
    }
};

static std::string Utf16ToUtf8(Il2CppString* value) {
    if (!value || value->length <= 0)
        return {};

    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, value->chars, value->length, nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return {};

    std::string result((size_t)needed, '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value->chars, value->length, &result[0], needed, nullptr, nullptr);
    return result;
}

static bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
        value.compare(0, prefix.size(), prefix) == 0;
}

static int FieldOff(Il2CppClass* klass, const char* fieldName) {
    if (!klass)
        return -1;

    Il2CppFieldInfo* field = il2cpp_class_get_field_from_name(klass, fieldName);
    return field ? (int)il2cpp_field_get_offset(field) : -1;
}

static const Il2CppMethodInfo* ResolveMethodInfo(
    Il2CppClass* klass, const char* methodName, int argCount) {
    return klass ? il2cpp_class_get_method_from_name(klass, methodName, argCount) : nullptr;
}

static void* ResolveMethodPointer(
    Il2CppClass* klass, const char* methodName, int argCount,
    const Il2CppMethodInfo** outMethodInfo = nullptr) {
    const Il2CppMethodInfo* method = ResolveMethodInfo(klass, methodName, argCount);
    if (outMethodInfo)
        *outMethodInfo = method;
    return method ? IL2CPP_GetMethodPointer(method) : nullptr;
}

static void* GetRefField(void* object, int offset) {
    if (!object || offset < 0)
        return nullptr;

    return *(void**)((uint8_t*)object + offset);
}

static void Hook_CommonUI_SetVersionText(void* self, bool forceOff, void* method) {
    GcThreadGuard guard;

    if (g_setVersionTextOrig)
        g_setVersionTextOrig(self, forceOff, method);

    if (forceOff || !self || !g_textSetText)
        return;

    void* versionText = GetRefField(self, g_versionTextOffset);
    if (!versionText)
        return;

    std::string current;
    if (g_textGetText)
        current = Utf16ToUtf8(g_textGetText(versionText, (void*)g_textGetTextMethod));

    if (current.empty() || StartsWith(current, kVersionPrefix))
        return;

    const std::string patched = std::string(kVersionPrefix) + current;
    Il2CppString* managedText = il2cpp_string_new(patched.c_str());
    if (!managedText)
        return;

    g_textSetText(versionText, managedText, (void*)g_textSetTextMethod);

    static bool s_loggedOnce = false;   // synchronous log write per pause-menu open caused a visible stutter
    if (!s_loggedOnce) {
        s_loggedOnce = true;
        Logger::Log("[+] Main menu version text patched: " + patched);
    }
}

bool VersionTextHook_Install() {
    if (g_hookInstalled)
        return true;

    Il2CppClass* commonUIClass = IL2CPP_FindClass("", "CommonUI");
    Il2CppClass* textClass = IL2CPP_FindClass("UnityEngine.UI", "Text");

    if (!commonUIClass) {
        Logger::Log("[!] Main menu version text hook skipped: CommonUI class not found");
        return false;
    }
    if (!textClass) {
        Logger::Log("[!] Main menu version text hook skipped: UnityEngine.UI.Text class not found");
        return false;
    }

    g_versionTextOffset = FieldOff(commonUIClass, "VersionText");
    if (g_versionTextOffset < 0) {
        Logger::Log("[!] Main menu version text hook skipped: CommonUI.VersionText field not found");
        return false;
    }

    g_textGetText = (UIText_GetText_t)ResolveMethodPointer(
        textClass, "get_text", 0, &g_textGetTextMethod);
    g_textSetText = (UIText_SetText_t)ResolveMethodPointer(
        textClass, "set_text", 1, &g_textSetTextMethod);

    if (!g_textSetText) {
        Logger::Log("[!] Main menu version text hook skipped: Text.set_text not found");
        return false;
    }

    if (!g_textGetText) {
        Logger::Log("[!] Main menu version text hook skipped: Text.get_text not found");
        return false;
    }

    void* setVersionTextPtr = ResolveMethodPointer(commonUIClass, "SetVersionText", 1);
    if (!setVersionTextPtr) {
        Logger::Log("[!] Main menu version text hook skipped: CommonUI.SetVersionText not found");
        return false;
    }

    MH_STATUS createStatus = MH_CreateHook(
        setVersionTextPtr,
        (LPVOID)Hook_CommonUI_SetVersionText,
        (LPVOID*)&g_setVersionTextOrig);
    if (createStatus != MH_OK) {
        Logger::Log("[-] Hook on CommonUI.SetVersionText failed at create (MH_STATUS=" +
            std::to_string(createStatus) + ")");
        return false;
    }

    MH_STATUS enableStatus = MH_EnableHook(setVersionTextPtr);
    if (enableStatus != MH_OK) {
        Logger::Log("[-] Hook on CommonUI.SetVersionText failed at enable (MH_STATUS=" +
            std::to_string(enableStatus) + ")");
        return false;
    }

    g_hookInstalled = true;
    Logger::Log("[+] Hooked CommonUI.SetVersionText");
    return true;
}