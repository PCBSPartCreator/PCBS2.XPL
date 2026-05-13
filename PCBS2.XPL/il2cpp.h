#pragma once
#include <Windows.h>
#include <cstdint>

struct Il2CppDomain;
struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppClass;
struct Il2CppMethodInfo;
struct Il2CppFieldInfo;
struct Il2CppObject;

struct Il2CppString {
    void* klass;
    void* monitor;
    int32_t length;
    wchar_t chars[1];
};

#define DECL_IL2CPP(ret, name, ...) \
    typedef ret (*t_##name)(__VA_ARGS__); \
    extern t_##name name;

DECL_IL2CPP(Il2CppDomain*, il2cpp_domain_get);
DECL_IL2CPP(const Il2CppAssembly**, il2cpp_domain_get_assemblies, Il2CppDomain*, size_t*);
DECL_IL2CPP(const Il2CppImage*, il2cpp_assembly_get_image, const Il2CppAssembly*);
DECL_IL2CPP(Il2CppClass*, il2cpp_class_from_name, const Il2CppImage*, const char*, const char*);
DECL_IL2CPP(const Il2CppMethodInfo*, il2cpp_class_get_method_from_name, Il2CppClass*, const char*, int);
DECL_IL2CPP(Il2CppFieldInfo*, il2cpp_class_get_field_from_name, Il2CppClass*, const char*);
DECL_IL2CPP(size_t, il2cpp_field_get_offset, Il2CppFieldInfo*);
DECL_IL2CPP(void, il2cpp_field_static_get_value, Il2CppFieldInfo*, void*);
DECL_IL2CPP(void, il2cpp_field_static_set_value, Il2CppFieldInfo*, void*);
DECL_IL2CPP(Il2CppObject*, il2cpp_object_new, Il2CppClass*);
DECL_IL2CPP(Il2CppString*, il2cpp_string_new, const char*);
DECL_IL2CPP(void, il2cpp_runtime_class_init, Il2CppClass*);
DECL_IL2CPP(uint32_t, il2cpp_gchandle_new, void*, bool);
DECL_IL2CPP(void*, il2cpp_thread_attach, Il2CppDomain*);

#undef DECL_IL2CPP

// Il2CppMethodInfo->methodPointer is the first field on Unity 2019.4 through
// 2022.x. Revisit if PCBS2 ever moves to a newer Unity that changes the layout.
inline void* IL2CPP_GetMethodPointer(const Il2CppMethodInfo* method) {
    return method ? *(void**)method : nullptr;
}

bool         IL2CPP_Init(HMODULE hGameAssembly);
Il2CppClass* IL2CPP_FindClass(const char* nameSpace, const char* name);