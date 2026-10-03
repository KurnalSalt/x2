#include "android/il2cpp.hpp"
#include "runtime/battle_control.hpp"
#include <android/log.h>
#include <cstdint>
#include <cstring>
#define GUM_STATIC
#include "frida-gum.h"
namespace x2::android {
namespace {
struct Patch { std::uintptr_t offset; std::uint32_t before,after; };
constexpr Patch patches[]={
{0x1a8944,0xb90c426b,0xf9462e74},
{0x1a8abc,0xf90fba75,0xf90fba68},
{0x1a8ac0,0xf90faa75,0xf90faa68},
{0x1a8ac4,0xf90f9275,0xf90f9268},
{0x1a8acc,0xf90fa275,0xf90fa268},
{0x1a8ad0,0xf90f9a75,0xf90f9a68},
{0x1a8ad4,0xf90f8a75,0xf90f8a68},
{0x1a8ad8,0xf910b274,0xf910b268},
{0x1a8adc,0xf910fa76,0xf910fa68},
{0x1a8ae0,0xf910f276,0xf910f268},
{0x1a8ae4,0xf910ea76,0xf910ea68},
};
}
void install_unity_test_patches() {
 il2cpp::on_ready([] {
  if(!runtime::unity_requested.load()) return;
  GumModule* module=gum_process_find_module_by_name("libunity.so");
  if(!module) return;
  const auto base=gum_module_get_range(module)->base_address;
  for(const auto& p:patches) {
   std::uint32_t actual; std::memcpy(&actual,reinterpret_cast<const void*>(base+p.offset),4);
   if(actual!=p.before) { __android_log_print(ANDROID_LOG_WARN,"x2offline","Unity P1-P11 refused: unexpected instruction");g_object_unref(module);return; }
  }
  bool ok=true;
  for(const auto& p:patches) {
   ok &= gum_memory_patch_code(reinterpret_cast<void*>(base+p.offset),4,[](void* address,void* value){std::memcpy(address,value,4);},const_cast<std::uint32_t*>(&p.after));
  }
  if(!ok) {
   for(const auto& p:patches)
    gum_memory_patch_code(reinterpret_cast<void*>(base+p.offset),4,[](void* address,void* value){std::memcpy(address,value,4);},const_cast<std::uint32_t*>(&p.before));
   __android_log_print(ANDROID_LOG_WARN,"x2offline","Unity P1-P11 patch failed; original instructions restored");
  }
  runtime::unity_active.store(ok);
  __android_log_print(ANDROID_LOG_INFO,"x2offline","experimental Unity P1-P11 applied=%d",ok);
  g_object_unref(module);
 });
}
}
