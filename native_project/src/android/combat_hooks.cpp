#include "android/il2cpp.hpp"
#include "runtime/battle_control.hpp"
#include <android/log.h>
#include <cstdint>
namespace x2::android {
namespace {
using Hurt = bool(*)(void*,void*,std::int64_t,int,int,int,int,bool,int,int,int,bool,void*);
Hurt player_hurt{}, unit_hurt{};
std::ptrdiff_t type_offset=-1,camp_offset=-1;
bool friendly(void* self) {
    if(!self || type_offset<0 || camp_offset<0) return false;
    const auto p=reinterpret_cast<const std::uint8_t*>(self);
    const int type=*reinterpret_cast<const int*>(p+type_offset), camp=*reinterpret_cast<const int*>(p+camp_offset);
    return (type==1 || type==2) && camp==1;
}
bool hurt(Hurt original,void* self,void* attacker,std::int64_t damage,int result,int damage_type,int damage_id,int spell,bool first,int bane,int player_class,int target_class,bool thorns,void* method) {
    if(damage>0 && runtime::lock_hp.load(std::memory_order_relaxed) && friendly(self)) {
        runtime::blocked_hits.fetch_add(1,std::memory_order_relaxed); return false;
    }
    return original(self,attacker,damage,result,damage_type,damage_id,spell,first,bane,player_class,target_class,thorns,method);
}
#define PARAMETERS void* self,void* attacker,std::int64_t damage,int result,int damage_type,int damage_id,int spell,bool first,int bane,int pc,int tc,bool thorns,void* method
#define ARGUMENTS self,attacker,damage,result,damage_type,damage_id,spell,first,bane,pc,tc,thorns,method
bool player_hook(PARAMETERS) { return hurt(player_hurt,ARGUMENTS); }
bool unit_hook(PARAMETERS) { return hurt(unit_hurt,ARGUMENTS); }
}
void install_combat_hooks() {
    il2cpp::on_ready([] {
        type_offset=il2cpp::field_offset("LogicX2","Unit","UnitType");
        camp_offset=il2cpp::field_offset("LogicX2","Unit","Camp");
        void* player=il2cpp::method("LogicX2","Player","Hurt",11);
        void* unit=il2cpp::method("LogicX2","Unit","Hurt",11);
        // Verify the known metadata layout before reading managed fields.
        const bool layout=type_offset==0x500 && camp_offset==0x504;
        const bool a=layout && player && il2cpp::replace(player,reinterpret_cast<void*>(player_hook),reinterpret_cast<void**>(&player_hurt));
        const bool b=layout && unit && unit!=player && il2cpp::replace(unit,reinterpret_cast<void*>(unit_hook),reinterpret_cast<void**>(&unit_hurt));
        runtime::lock_hp_ready.store(a && b);
        __android_log_print(ANDROID_LOG_INFO,"x2offline","combat HP hooks player=%d unit=%d layout=%d",a,b,layout);
    });
}
}
