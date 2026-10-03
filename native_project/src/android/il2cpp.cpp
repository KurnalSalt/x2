#include "android/il2cpp.hpp"

#include <android/dlext.h>
#include <android/log.h>
#include <atomic>
#include <cstring>
#include <dlfcn.h>
#include <vector>
#include <thread>
#include <chrono>

#define GUM_STATIC
#include "frida-gum.h"

#include "crypto/obf_string.hpp"

namespace x2::android::il2cpp {

namespace {

#define kLogTag (OBFLAZY("x2offline"))

using LoaderDlopenFn = void* (*)(const char*, int, const void*);
using LoaderDlopenExtFn = void* (*)(const char*, int, const android_dlextinfo*, const void*);
using InitFn = void (*)(const char*);

struct Api {
    void* (*domain_get)();
    void** (*domain_get_assemblies)(void*, std::size_t*);
    void* (*assembly_get_image)(void*);
    void* (*class_from_name)(void*, const char*, const char*);
    void* (*class_get_method_from_name)(void*, const char*, int);
    void* (*class_get_field_from_name)(void*, const char*);
    std::size_t (*field_get_offset)(void*);
    void* (*class_get_parent)(void*);
    void* (*class_get_type)(void*);
    void* (*type_get_object)(void*);
    void (*field_static_get_value)(void*, void*);
} api{};

LoaderDlopenFn real_dlopen = nullptr;
LoaderDlopenExtFn real_dlopen_ext = nullptr;
using PublicDlopen = void*(*)(const char*,int);
using PublicDlopenExt = void*(*)(const char*,int,const android_dlextinfo*);
PublicDlopen public_dlopen=nullptr;
PublicDlopenExt public_dlopen_ext=nullptr;
InitFn real_init = nullptr;
std::atomic<bool> armed{false};
std::atomic<bool> ready{false}, initializing{false};

std::vector<std::function<void()>>& callbacks() {
    static std::vector<std::function<void()>> list;
    return list;
}

void run_ready() {
    if(ready.exchange(true)) return;
    __android_log_print(ANDROID_LOG_INFO,kLogTag,"il2cpp ready, running %zu hook installers",callbacks().size());
    for(auto& callback:callbacks()) callback();
}
void init_hook(const char* domain) {
    initializing.store(true);
    real_init(domain);
    run_ready();
    initializing.store(false);
}

template <class Fn>
void bind(void* handle, Fn& fn, const char* name) {
    fn = reinterpret_cast<Fn>(dlsym(handle, name));
}

void arm(void* handle, const char* path) {
    if (handle == nullptr || path == nullptr || std::strstr(path, OBFCSTR("libil2cpp.so")) == nullptr) return;
    if (armed.exchange(true)) return;
    bind(handle, api.domain_get, OBFCSTR("il2cpp_domain_get"));
    bind(handle, api.domain_get_assemblies, OBFCSTR("il2cpp_domain_get_assemblies"));
    bind(handle, api.assembly_get_image, OBFCSTR("il2cpp_assembly_get_image"));
    bind(handle, api.class_from_name, OBFCSTR("il2cpp_class_from_name"));
    bind(handle, api.class_get_method_from_name, OBFCSTR("il2cpp_class_get_method_from_name"));
    bind(handle, api.class_get_field_from_name, OBFCSTR("il2cpp_class_get_field_from_name"));
    bind(handle, api.field_get_offset, OBFCSTR("il2cpp_field_get_offset"));
    bind(handle, api.class_get_parent, OBFCSTR("il2cpp_class_get_parent"));
    bind(handle, api.class_get_type, OBFCSTR("il2cpp_class_get_type"));
    bind(handle, api.type_get_object, OBFCSTR("il2cpp_type_get_object"));
    bind(handle, api.field_static_get_value, OBFCSTR("il2cpp_field_static_get_value"));
    void* init = dlsym(handle, OBFCSTR("il2cpp_init"));
    const bool ok = init != nullptr && replace(init, reinterpret_cast<void*>(init_hook),
                                               reinterpret_cast<void**>(&real_init));
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "libil2cpp loaded, il2cpp_init hook=%d", ok);
}

void* dlopen_hook(const char* path, int flags, const void* caller) {
    void* handle = real_dlopen(path, flags, caller);
    arm(handle, path);
    return handle;
}

void* dlopen_ext_hook(const char* path, int flags, const android_dlextinfo* info, const void* caller) {
    void* handle = real_dlopen_ext(path, flags, info, caller);
    arm(handle, path);
    return handle;
}

void* public_open_hook(const char* path,int flags) {
    void* handle=public_dlopen(path,flags); arm(handle,path); return handle;
}
void* public_ext_hook(const char* path,int flags,const android_dlextinfo* info) {
    void* handle=public_dlopen_ext(path,flags,info); arm(handle,path); return handle;
}
void* find_class(const char* name_space, const char* klass) {
    if (api.domain_get == nullptr) return nullptr;
    std::size_t count = 0;
    void** assemblies = api.domain_get_assemblies(api.domain_get(), &count);
    for (std::size_t i = 0; i < count; ++i) {
        if (void* found = api.class_from_name(api.assembly_get_image(assemblies[i]), name_space, klass)) {
            return found;
        }
    }
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "class not found: %s.%s", name_space, klass);
    return nullptr;
}

} // namespace

void on_ready(std::function<void()> callback) {
    callbacks().push_back(std::move(callback));
}

void install() {
    gum_init_embedded();
    // Never replace Bionic/driver loader entry points. Their caller namespace
    // and tiny trampoline implementations vary across Android releases.
    std::thread([] {
        for(std::uint64_t attempt=0;!ready.load();++attempt) {
            if(attempt==6000)
                __android_log_print(ANDROID_LOG_INFO,kLogTag,"IL2CPP still loading; observer remains active with reduced polling");
            void* handle=dlopen("libil2cpp.so",RTLD_NOW|RTLD_NOLOAD);
            if(handle) {
                arm(handle,"libil2cpp.so");
                if(ready.load()) return;
                if(api.domain_get && api.domain_get_assemblies && !initializing.load()) {
                    void* domain=api.domain_get(); std::size_t count=0;
                    if(domain && api.domain_get_assemblies(domain,&count) && count) {
                        // The init call may have finished before the observer saw
                        // the library. Wait for metadata initialization to settle.
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        if(!initializing.load()) {
                            using Attach=void*(*)(void*); using Detach=void(*)(void*);
                            const auto attach=reinterpret_cast<Attach>(dlsym(handle,"il2cpp_thread_attach"));
                            const auto detach=reinterpret_cast<Detach>(dlsym(handle,"il2cpp_thread_detach"));
                            if(attach && detach) {
                                void* thread=attach(domain);
                                if(thread) { run_ready(); detach(thread); return; }
                            }
                        }
                    }
                }
                dlclose(handle);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(attempt<6000?10:100));
        }
    }).detach();
}

void* method(const char* name_space, const char* klass, const char* name, int argc) {
    void* cls = find_class(name_space, klass);
    void* info = cls ? api.class_get_method_from_name(cls, name, argc) : nullptr;
    if (info == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "method not found: %s::%s/%d", klass, name, argc);
        return nullptr;
    }
    return *static_cast<void**>(info); // MethodInfo::methodPointer is the first member.
}

std::ptrdiff_t field_offset(const char* name_space, const char* klass, const char* field) {
    void* cls = find_class(name_space, klass);
    void* info = cls ? api.class_get_field_from_name(cls, field) : nullptr;
    return info ? static_cast<std::ptrdiff_t>(api.field_get_offset(info)) : -1;
}

void* klass(const char* name_space, const char* name) {
    return find_class(name_space, name);
}

void* parent(void* cls) {
    return cls ? api.class_get_parent(cls) : nullptr;
}

void* type_object(void* cls) {
    return cls ? api.type_get_object(api.class_get_type(cls)) : nullptr;
}

void* static_object(void* cls, const char* field) {
    void* info = cls ? api.class_get_field_from_name(cls, field) : nullptr;
    void* value = nullptr;
    if (info) api.field_static_get_value(info, &value);
    return value;
}

bool replace(void* target, void* replacement, void** original) {
    auto* interceptor = gum_interceptor_obtain();
    gum_interceptor_begin_transaction(interceptor);
    const auto status = gum_interceptor_replace_fast(interceptor, target, replacement, original, nullptr);
    gum_interceptor_end_transaction(interceptor);
    return status == GUM_REPLACE_OK;
}

} // namespace x2::android::il2cpp
