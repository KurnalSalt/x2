#include "server/router.hpp"
#include "runtime/battle_control.hpp"
#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <unordered_map>
#include "offline/game_table.hpp"
#include "offline/mission.hpp"
#include "offline/shop.hpp"
#include "offline/protocol_builders.hpp"
#include "offline/playable_heroes.hpp"
#include "proto/protobuf.hpp"

namespace x2::server {
namespace {
std::string quote(std::string_view value) {
    std::string out{"\""};
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 32) out += ' ';
        else out += c;
    }
    return out + '"';
}
HttpResponse reply(bool ok, std::string_view message) {
    return {.body = std::format("{{\"ok\":{},\"message\":{}}}", ok, quote(message))};
}
std::string decode(std::string_view raw) {
    std::string result;
    auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;};
    for(std::size_t i=0;i<raw.size();++i) {
        if(raw[i]=='%' && i+2<raw.size() && hex(raw[i+1])>=0 && hex(raw[i+2])>=0) {result+=static_cast<char>(hex(raw[i+1])*16+hex(raw[i+2]));i+=2;}
        else result+=raw[i]=='+'?' ':raw[i];
    }
    return result;
}
std::map<std::string, std::string> form(std::string_view text) {
    std::map<std::string, std::string> result;
    while (!text.empty()) {
        const auto end = text.find('&');
        const auto pair = text.substr(0, end);
        const auto eq = pair.find('=');
        if (eq != pair.npos) result.emplace(decode(pair.substr(0, eq)), decode(pair.substr(eq + 1)));
        if (end == text.npos) break;
        text.remove_prefix(end + 1);
    }
    return result;
}
bool number(std::string_view text, std::int32_t& value) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value >= 0;
}
}

void Router::configure_gm(std::string path) {
    std::lock_guard lock{mutex_};
    gm_settings_path_ = std::move(path);
    std::ifstream file{gm_settings_path_};
    int available = 0, active = 0;
    if (file >> available >> active) { developer_available_ = available == 1; developer_mode_ = active == 1 && developer_available_; }
    std::ifstream battle{gm_settings_path_ + "_battle"};
    int attack = 100, speed = 100, god = 0;
    if (battle >> attack >> speed >> god && attack >= 100 && attack <= 100000 && speed >= 50 && speed <= 500)
        battle_ = {.attack_percent = attack, .speed_percent = speed, .god = god == 1};
    runtime::lock_hp.store(battle_.god);
    std::ifstream unity{gm_settings_path_+"_unity"}; int use_unity=0;
    if(unity>>use_unity) runtime::unity_requested.store(use_unity==1);
}
void Router::save_gm_settings() {
    if (!gm_settings_path_.empty()) {
        std::ofstream file{gm_settings_path_, std::ios::trunc};
        file << developer_available_ << ' ' << developer_mode_;
        std::ofstream battle{gm_settings_path_ + "_battle", std::ios::trunc};
        battle << battle_.attack_percent << ' ' << battle_.speed_percent << ' ' << (battle_.god ? 1 : 0);
        std::ofstream unity{gm_settings_path_+"_unity",std::ios::trunc}; unity<<runtime::unity_requested.load();
    }
}
void Router::prepare_developer(offline::Account& account) {
    account.is_create_role = false;
    account.player.nickname = "X2开发者";
    account.player.level = 60;
    account.player.rewarded_level = 60;
    account.player.gold = 10000000;
    account.player.crystal = 10000;
    account.player.power = 999;
    account.player.hero_exp = 1000000;
    account.player.jewel_chip = 100000;
    account.player.main_section = 2110106;
    account.player.main_chapter = 2010200;
    account.player.pending_section = account.player.pending_chapter = 0;
    if (!tables_) return;
    const auto allowed = offline::playable_heroes(tables_.get());
    std::erase_if(account.heroes,[&](const auto& hero){return !allowed.contains(hero.id);});
    offline::GameTable units, guides, sections;
    if (units.load(*tables_, "UnitBase")) {
        for (const auto& row : units.rows()) {
            if (!allowed.contains(static_cast<std::int32_t>(row.key))) continue;
            if (!std::ranges::contains(account.heroes, row.key, &offline::AccountHero::id))
                account.heroes.push_back({.id = static_cast<std::int32_t>(row.key), .state = 2, .level = 60,
                    .star = offline::default_hero_star(tables_.get(), row.key)});
        }
    }
    if (guides.load(*tables_, "TitoGuideTable"))
        for (const auto& row : guides.rows()) {
            const auto group = guides.int_field(row, 2).value_or(0);
            if (group > 0) account.player.guides[group] = 1;
        }
    if (sections.load(*tables_, "SectionTable"))
        for (const auto& row : sections.rows()) {
            const auto id = static_cast<std::int32_t>(row.key);
            if (id >= 2110001 && id <= 2110106 && sections.int_field(row,8).value_or(0)==0 &&
                !std::ranges::contains(account.player.cleared_main, id)) account.player.cleared_main.push_back(id);
        }
    if (!std::ranges::contains(account.items, 1251014, &offline::AccountItem::id)) account.items.push_back({.id = 1251014, .num = 30});
}

HttpResponse Router::gm_http(const HttpRequest& request) {
    if(request.path=="/gm/save/export" && request.method=="GET") {
        if(!logged_in_) return reply(false,"请先登录游戏");
        const auto bytes=offline::encode_save(account_);
        if(bytes.size()>400000) return reply(false,"存档超出当前导出大小限制");
        std::string hex; constexpr char digits[]="0123456789abcdef";
        for(auto b:bytes) {hex+=digits[b>>4];hex+=digits[b&15];}
        return {.body=std::format("{{\"ok\":true,\"format\":\"X2SAVE-V1\",\"version\":\"V0.1\",\"developer\":{},\"name\":{},\"data\":{}}}",account_key_=="__x2_developer__",quote(account_.player.nickname),quote(hex))};
    }
    if (request.path == "/gm/status") {
        const auto& p = account_.player;
        return {.body = std::format(R"({{"ok":true,"connected":{},"developer":{},"developerAvailable":{},"name":{},"level":{},"gold":{},"crystal":{},"power":{},"heroes":{},"attackPercent":{},"speedPercent":{},"god":{},"hpHookReady":{},"blockedHits":{},"unityRequested":{},"unityActive":{},"rechargeHookReady":{}}})",
            logged_in_, account_key_ == "__x2_developer__", developer_available_, quote(p.nickname), p.level, p.gold, p.crystal, p.power, account_.heroes.size(),
            battle_.attack_percent, battle_.speed_percent, battle_.god, runtime::lock_hp_ready.load(), runtime::blocked_hits.load(),
            runtime::unity_requested.load(),runtime::unity_active.load(),runtime::recharge_hook_ready.load())};
    }
    const auto parameters = form(request.body);
    const auto get = [&](const char* key) -> std::string_view { const auto it = parameters.find(key); return it == parameters.end() ? std::string_view{} : it->second; };
    if(request.path=="/gm/save/import" && request.method=="POST") {
        if(!logged_in_) return reply(false,"请先登录游戏");
        if(get("confirm")!="1") return reply(false,"导入需要确认覆盖当前账号");
        if(get("developer")!=(account_key_=="__x2_developer__"?"1":"0")) return reply(false,"存档账号类型不匹配，请先切换到对应账号并重启");
        const auto hex=get("data");
        if(hex.empty() || hex.size()>800000 || hex.size()%2) return reply(false,"存档格式或大小无效");
        const auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
        std::vector<std::uint8_t> bytes;
        for(std::size_t i=0;i<hex.size();i+=2) {int a=digit(hex[i]),b=digit(hex[i+1]);if(a<0||b<0)return reply(false,"存档编码无效");bytes.push_back(static_cast<std::uint8_t>(a*16+b));}
        auto imported=offline::decode_save(bytes);
        if(!imported) return reply(false,"存档损坏或版本不兼容，未修改当前存档");
        imported->id=account_.id;
        accounts_.save("__x2_gm_backup__"+account_key_,account_);
        account_=std::move(*imported); accounts_.save(account_key_,account_);
        gm_pushes_.clear(); logged_in_=false;
        return reply(true,"导入成功，已备份原存档；请退出并重启游戏后继续");
    }
    if(request.path=="/gm/unity" && request.method=="POST") {
        int enabled=0;
        if(!number(get("enabled"),enabled) || enabled<0 || enabled>1) return reply(false,"开关参数错误");
        runtime::unity_requested.store(enabled==1); save_gm_settings();
        return reply(true,"实验 Unity 补丁设置已保存，重启游戏后生效");
    }
    if (request.path == "/gm/battle" && request.method == "POST") {
        std::int32_t attack = 0, speed = 0, god = 0;
        if (!number(get("attack"), attack) || !number(get("speed"), speed) || !number(get("god"), god) ||
            attack < 100 || attack > 100000 || speed < 50 || speed > 500 || god < 0 || god > 1)
            return reply(false, "攻击倍率 100%~100000%，移速倍率 50%~500%");
#ifdef __ANDROID__
        if(god && !runtime::lock_hp_ready.load()) return reply(false,"锁血功能尚未就绪，请等待游戏完成加载");
#endif
        battle_ = {.attack_percent = attack, .speed_percent = speed, .god = god == 1};
        runtime::lock_hp.store(battle_.god);
        save_gm_settings();
        return reply(true, std::format("战斗设置已保存：攻击 {}% · 移速 {}% · 锁血{}；攻击和移速下一场生效",
                                       attack, speed, god == 1 ? "开" : "关"));
    }
    if (request.path == "/gm/catalog") {
        const auto kind = get("kind");
        if (kind != "skin" && kind != "hero" && kind != "jewel" && kind != "item" && kind != "equip") return reply(false, "分类不存在");
        const auto name = kind == "skin" ? "Appearance" : kind == "hero" ? "UnitBase" : kind == "equip" ? "EquibBase" : "Item";
        offline::GameTable rows, lang;
        if (!tables_ || !rows.load(*tables_, name)) return reply(false, "资料表不可用");
        lang.load(*tables_, "Language");
        std::unordered_map<std::uint64_t,std::string> labels;
        for(const auto& row:lang.rows()) labels.emplace(row.key,lang.string_field(row,2).value_or(""));
        std::int32_t page=0; if(!get("page").empty() && (!number(get("page"),page) || page>10000)) return reply(false,"页码不正确");
        const auto query=get("q"); int total=0;

        const auto skins = offline::collect_hero_skins(account_, tables_.get());
        const auto allowed = offline::playable_heroes(tables_.get());
        std::string entries;
        for (const auto& row : rows.rows()) {
            const auto id = static_cast<std::int32_t>(row.key);
            if (kind == "hero" && !allowed.contains(id)) continue;
            if (kind == "jewel" && (id < 1250000 || id >= 1260000)) continue;
            const auto hero = kind == "skin" ? rows.int_field(row, 18).value_or(0) : 0;
            if (kind == "skin" && !allowed.contains(hero)) continue;
            const auto condition = kind == "skin" ? rows.int_field(row, 2).value_or(0) : 0;
            const auto key = rows.int_field(row, kind == "skin" ? 5 : 2).value_or(0);
            std::string label = std::to_string(id);
            if(const auto text=labels.find(key);text!=labels.end() && !text->second.empty()) label=text->second;
            if(!query.empty() && label.find(query)==label.npos && std::to_string(id).find(query)==std::string::npos) continue;
            const auto index=total++;
            if(index<page*40 || index>=page*40+40) continue;
            bool owned = false;
            if (kind == "skin") { const auto it = skins.find(hero); owned = it != skins.end() && it->second.contains(id); }
            else if (kind == "hero") owned = std::ranges::contains(account_.heroes, id, &offline::AccountHero::id);
            else if(kind=="equip") owned=std::ranges::contains(account_.player.equips,id,&offline::AccountEquip::type_id);
            else owned = std::ranges::any_of(account_.items, [id](const auto& item) { return item.id == id && item.num > 0; });
            if (!entries.empty()) entries += ',';
            entries += std::format(R"({{"id":{},"name":{},"hero":{},"condition":{},"owned":{}}})", id, quote(label), hero, condition, owned);
        }
        return {.body = std::format("{{\"ok\":true,\"total\":{},\"page\":{},\"pageSize\":40,\"entries\":[{}]}}",total,page,entries)};
    }
    if (request.path == "/gm/command" && request.method == "POST") {
        if (!logged_in_) return reply(false, "请先登录游戏");
        std::int32_t opt=0, first=0, second=0;
        if (!number(get("opt"),opt) || !number(get("v1"),second) || second>100000000) return reply(false,"参数格式或范围不正确");
        const std::set<int> supported{0,1,2,3,4,6,7,8,13,15,16,17,18,19,22,23,24,29,50};
        if (!supported.contains(opt)) return reply(false,"离线模块尚未实现此官方指令");
        std::vector<std::int32_t> values;
        auto raw = get("v0");
        do {
            const auto end=raw.find(',');
            if (!number(raw.substr(0,end),first)) return reply(false,"编号或数值必须为非负整数");
            values.push_back(first);
            if (values.size()>64) return reply(false,"一次最多处理64个编号");
            if (end==raw.npos) break;
            raw.remove_prefix(end+1);
        } while(!raw.empty());
        if (opt!=50 && values.size()!=1) return reply(false,"此指令只接受一个编号或数值");
        first=values[0];
        if (first>100000000) return reply(false,"数值超出范围");
        if (opt==4 || opt==6 || opt==7 || opt==22 || opt==23 || opt==24) {
            if (!offline::playable_heroes(tables_.get()).contains(first)) return reply(false,"该角色不是完整可用的神格");
            if (opt!=4 && !std::ranges::contains(account_.heroes,first,&offline::AccountHero::id)) return reply(false,"请先解锁神格");
        }
        if (opt==8 || opt==19 || opt==50) {
            offline::GameTable items;
            if (!tables_ || !items.load(*tables_,"Item") || second<1 || second>999999) return reply(false,"道具数量应在1到999999之间");
            for (auto id:values) if (!items.row(id)) return reply(false,"列表包含不存在的道具编号");
        }
        if (opt==15 || opt==29) {
            offline::GameTable table;
            if (!tables_ || !table.load(*tables_,opt==15?"EquibBase":"SectionTable") || !table.row(first)) return reply(false,"编号不存在于对应资料表");
        }
        if (opt==13) {
            if (get("confirm")!="1") return reply(false,"重置操作需要确认");
            accounts_.save("__x2_gm_backup__"+account_key_,account_);
        }
        proto::Writer body; body.int32(1,opt); body.boolean(2,get("set")=="1");
        for(auto value:values) body.int32(3,value);
        if(opt==50) body.string(4,std::to_string(second));
        else body.int32(3,second);
        auto bytes=marsnet({.head={.session_id=last_session_},.proto_id=124,.body=body.data()});
        gm_pushes_.insert(gm_pushes_.end(),bytes.begin(),bytes.end());
        return reply(true,opt==13?"已自动备份并重置进度，请重新登录":"官方GM指令已保存并推送");
    }
    if (request.path != "/gm/action" || request.method != "POST") return reply(false, "无效操作");
    const auto action = get("action");
    // Local developer tooling can reopen the isolated test account. The release
    // window never offers this operation while the developer entry is hidden.
    if(action=="enable_developer") {
        developer_available_=true; developer_mode_=true; save_gm_settings();
        return reply(true,"独立开发者测试账号已启用，重启后生效");
    }
    if (action == "developer" || action == "normal" || action == "hide_developer") {
        if (action == "developer" && !developer_available_) return reply(false, "开发者账号入口已隐藏");
        developer_mode_ = action == "developer";
        if (action == "hide_developer") developer_available_ = false;
        save_gm_settings();
        return reply(true, "账号切换已保存，重启游戏后生效");
    }
    if (action=="restore_backup") {
        const auto backup=accounts_.find("__x2_gm_backup__"+account_key_);
        if (!backup) return reply(false,"当前账号没有重置备份");
        account_=*backup; accounts_.save(account_key_,account_);
        return reply(true,"已恢复备份，请重新登录加载完整数据");
    }
    if (!logged_in_) return reply(false, "请先登录游戏");
    std::int32_t id = 0, count = 0;
    if (!number(get("id"), id) || !number(get("count"), count) || count > 100000000) return reply(false, "请输入有效编号和数量");
    int opt = -1;
    std::vector<std::int32_t> values;
    if (action == "gold" || action == "crystal" || action == "power" || action == "dust") {
        opt = action == "gold" ? 1 : action == "crystal" ? 2 : action == "power" ? 0 : 18;
        values = {count};
    } else if (action == "hero" || action == "star" || action == "hero_level") {
        offline::GameTable units;
        if (!tables_ || !units.load(*tables_, "UnitBase")) return reply(false, "神格资料不可用");
        const auto row = units.row(id);
        if (!row || !offline::playable_heroes(tables_.get()).contains(id)) return reply(false, "该角色缺少完整的可用神格资料");
        opt = action == "hero" ? 4 : action == "star" ? 7 : 6;
        values = {id, count};
    } else if (action == "skin") {
        offline::GameTable skins, items;
        if (!tables_ || !skins.load(*tables_, "Appearance") || !items.load(*tables_, "Item")) return reply(false, "皮肤资料不可用");
        const auto row = skins.row(id);
        if (!row) return reply(false, "皮肤编号不存在");
        const auto hero = skins.int_field(*row, 18).value_or(0);
        if (!std::ranges::contains(account_.heroes, hero, &offline::AccountHero::id)) return reply(false, "请先解锁对应神格");
        if (skins.int_field(*row, 2).value_or(0) == 2) {
            opt = 7; values = {hero, 20};
        } else {
            const auto item = items.row(id);
            if (!item || items.int_field(*item, 21).value_or(0) != 18) return reply(false, "该外观不支持道具解锁");
            opt = 8; values = {id, 1};
        }
    } else if (action == "item") {
        offline::GameTable items;
        if (!tables_ || !items.load(*tables_, "Item") || !items.row(id) || count < 1) return reply(false, "道具编号或数量无效");
        opt = 8; values = {id, count};
    } else return reply(false, "操作不存在");
    proto::Writer body;
    body.int32(1, opt);
    for (const auto value : values) body.int32(3, value);
    const auto bytes = marsnet({.head = {.session_id = last_session_}, .proto_id = 124, .body = body.data()});
    gm_pushes_.insert(gm_pushes_.end(), bytes.begin(), bytes.end());
    return reply(true, "已保存，游戏数据即将刷新");
}
} // namespace x2::server
