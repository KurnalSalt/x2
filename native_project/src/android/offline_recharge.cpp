#include "android/il2cpp.hpp"
#include "runtime/battle_control.hpp"
#include <android/log.h>
#include <cstdint>
#include <dlfcn.h>
namespace x2::android {
namespace {
using Handler=void(*)(void*,void*,void*);
Handler original{};
using GetUI=void*(*)(void*,void*);
using CloseUI=void(*)(void*,void*,bool,bool,void*);
using Unlock=void(*)(void*,bool,int,void*);
using Refresh=void(*)(void*,void*);
using NewString=void*(*)(const char*);
GetUI get_ui{}; CloseUI close_ui{}; Unlock unlock{}; Refresh refresh{}; NewString new_string{};
std::ptrdiff_t buying_offset=-1, code_offset=-1;
void permit_hook(void* self,void* message,void* method) {
    if(self && message && *reinterpret_cast<const int*>(static_cast<const std::uint8_t*>(message)+code_offset)==10) {
        // The embedded server has already granted and pushed the reward.
        // Acknowledge completion without entering the external payment SDK.
        *reinterpret_cast<bool*>(static_cast<std::uint8_t*>(self)+buying_offset)=false;
        if(void* manager=get_ui(nullptr,nullptr))
            close_ui(manager,new_string("UI_LiBao"),true,false,nullptr);
        unlock(nullptr,false,0,nullptr);
        refresh(self,nullptr);
        return;
    }
    original(self,message,method);
}
}
void install_offline_recharge_hook() {
    il2cpp::on_ready([] {
        buying_offset=il2cpp::field_offset("","RechargeModule","isBuyingGiftBox");
        code_offset=il2cpp::field_offset("CommandX2","L2C_RechargeGoodsInfo","code");
        void* target=il2cpp::method("","RechargeModule","OnGetPermitPayToLobby",1);
        get_ui=reinterpret_cast<GetUI>(il2cpp::method("Foundation.UI","UIManager","get_Instance",0));
        close_ui=reinterpret_cast<CloseUI>(il2cpp::method("Foundation.UI","UIManager","CloseUI",3));
        unlock=reinterpret_cast<Unlock>(il2cpp::method("","GameAPI","SetGlobalClickForbid",2));
        refresh=reinterpret_cast<Refresh>(il2cpp::method("","RechargeModule","SendGiftBoxInfo",0));
        if(void* library=dlopen("libil2cpp.so",RTLD_NOW|RTLD_NOLOAD)) {
            new_string=reinterpret_cast<NewString>(dlsym(library,"il2cpp_string_new")); dlclose(library);
        }
        const bool ok=buying_offset==0x114 && code_offset==0x10 && target && get_ui && close_ui && unlock && refresh && new_string &&
            il2cpp::replace(target,reinterpret_cast<void*>(permit_hook),reinterpret_cast<void**>(&original));
        runtime::recharge_hook_ready.store(ok);
        __android_log_print(ANDROID_LOG_INFO,"x2offline","offline recharge completion hook=%d",ok);
    });
}
}
