#include "il2cpp.h"
#include "logger.h"

#define DEF_IL2CPP(name) t_##name name = nullptr;
DEF_IL2CPP(il2cpp_domain_get)
DEF_IL2CPP(il2cpp_domain_get_assemblies)
DEF_IL2CPP(il2cpp_assembly_get_image)
DEF_IL2CPP(il2cpp_class_from_name)
DEF_IL2CPP(il2cpp_class_get_method_from_name)
DEF_IL2CPP(il2cpp_class_get_field_from_name)
DEF_IL2CPP(il2cpp_field_get_offset)
DEF_IL2CPP(il2cpp_field_static_get_value)
DEF_IL2CPP(il2cpp_field_static_set_value)
DEF_IL2CPP(il2cpp_object_new)
DEF_IL2CPP(il2cpp_string_new)
DEF_IL2CPP(il2cpp_runtime_class_init)
DEF_IL2CPP(il2cpp_gchandle_new)
DEF_IL2CPP(il2cpp_thread_attach)
DEF_IL2CPP(il2cpp_runtime_object_init)
#undef DEF_IL2CPP

bool IL2CPP_Init(HMODULE hGA) {
#define LOAD(name) \
    name = (t_##name)GetProcAddress(hGA, #name); \
    if (!name) { Logger::Log("[-] Missing: " #name); return false; }

    LOAD(il2cpp_domain_get)
        LOAD(il2cpp_domain_get_assemblies)
        LOAD(il2cpp_assembly_get_image)
        LOAD(il2cpp_class_from_name)
        LOAD(il2cpp_class_get_method_from_name)
        LOAD(il2cpp_class_get_field_from_name)
        LOAD(il2cpp_field_get_offset)
        LOAD(il2cpp_field_static_get_value)
        LOAD(il2cpp_field_static_set_value)
        LOAD(il2cpp_object_new)
        LOAD(il2cpp_string_new)
        LOAD(il2cpp_runtime_class_init)
        LOAD(il2cpp_gchandle_new)
        LOAD(il2cpp_thread_attach)
        LOAD(il2cpp_runtime_object_init)
#undef LOAD

        Logger::Log("[+] IL2CPP API loaded");
    return true;
}

Il2CppClass* IL2CPP_FindClass(const char* nameSpace, const char* name) {
    Il2CppDomain* domain = il2cpp_domain_get();
    if (!domain) return nullptr;

    size_t count = 0;
    const Il2CppAssembly** assemblies = il2cpp_domain_get_assemblies(domain, &count);
    if (!assemblies) return nullptr;

    for (size_t i = 0; i < count; i++) {
        const Il2CppImage* image = il2cpp_assembly_get_image(assemblies[i]);
        if (!image) continue;
        Il2CppClass* klass = il2cpp_class_from_name(image, nameSpace, name);
        if (klass) return klass;
    }
    return nullptr;
}