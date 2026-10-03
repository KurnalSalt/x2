#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <span>

#include "offline/account_repository.hpp"
#include "offline/game_table.hpp"
#include "offline/jewel_compose.hpp"
#include "offline/mission.hpp"
#include "offline/npc_social.hpp"
#include "offline/protocol_builders.hpp"
#include "offline/shop.hpp"
#include "offline/gift_packages.hpp"
#include "offline/playable_heroes.hpp"
#include "proto/protobuf.hpp"
#include "persist/account_db.hpp"
#include "server/router.hpp"
#include "runtime/battle_control.hpp"
#include "server/protocol_ids.hpp"

using namespace x2;

int quantity(const offline::Account& account, int id) {
    const auto i = std::find_if(account.items.begin(), account.items.end(), [&](const auto& i) { return i.id == id; });
    return i == account.items.end() ? 0 : i->num;
}

offline::Account fixture() {
    offline::Account account;
    account.player.gold = 1000000;
    account.heroes.push_back({.id = 1003, .state = 2, .level = 60, .star = 11});
    account.items.push_back({.id = 1251014, .num = 6});
    return account;
}

std::vector<server::marsnet::Frame> frames(std::span<const std::uint8_t> data) {
    std::vector<server::marsnet::Frame> out;
    while (!data.empty()) {
        const auto decoded = server::marsnet::decode(data);
        assert(decoded.ok && decoded.consumed > 0);
        out.push_back(decoded.frame);
        data = data.subspan(decoded.consumed);
    }
    return out;
}

// Repeated length-delimited `field` entries of a message.
std::vector<std::span<const std::uint8_t>> messages(std::span<const std::uint8_t> data, std::uint32_t field) {
    std::vector<std::span<const std::uint8_t>> out;
    proto::Reader reader{data};
    while (!reader.eof()) {
        const auto f = reader.field();
        assert(f);
        if (f->number == field && f->wire_type == 2) out.push_back(*reader.bytes());
        else assert(reader.skip(f->wire_type));
    }
    return out;
}

std::optional<std::uint64_t> varint_field(std::span<const std::uint8_t> data, std::uint32_t field) {
    proto::Reader reader{data};
    while (!reader.eof()) {
        const auto f = reader.field();
        assert(f);
        if (f->number == field && f->wire_type == 0) return reader.varint();
        assert(reader.skip(f->wire_type));
    }
    return std::nullopt;
}

// FightData -> first FightHero -> HeroAttrAdd {1 attrId, 2 value}
std::map<std::uint64_t, std::int64_t> hero_attrs(const std::vector<std::uint8_t>& fight) {
    std::map<std::uint64_t, std::int64_t> attrs;
    const auto heroes = messages(fight, 2);
    assert(!heroes.empty());
    for (const auto add : messages(heroes[0], 9))
        attrs[*varint_field(add, 1)] = static_cast<std::int64_t>(*varint_field(add, 2));
    return attrs;
}


int terminal_checks(const offline::TableBlob* tables) {
    constexpr std::int64_t now=1790000000;
    auto a=fixture(); a.heroes.push_back({.id=1025,.state=2});
    auto list=offline::query_npc_blog(a,tables,now);
    assert(!messages(list,2).empty()); // Initial level defaults to the shipped table, not zero.
    auto like=offline::like_npc_blog(a,tables,1025,550127,now);
    assert(varint_field(like,1)==10 && a.player.npc_blog_likes.contains(550127));
    assert(varint_field(offline::like_npc_blog(a,tables,1003,550127,now),1)==13);
    assert(varint_field(offline::reply_npc_blog(a,tables,1025,550127,1,999,now),1)==13);
    assert(varint_field(offline::reply_npc_blog(a,tables,1025,550127,1,55012711,now),1)==10);
    assert(varint_field(offline::toggle_npc_block(a,1025),1)==10 && a.player.npc_blocked[1025]==1);
    assert(varint_field(offline::toggle_npc_block(a,9999),1)==13);
    a.heroes[0].favor_level=3;
    auto mail=offline::query_npc_letter(a,tables,now);assert(!messages(mail,2).empty());
    assert(varint_field(offline::reply_private_letter(a,tables,1003,200303,20030301,999,false,now),1)==13);
    assert(a.player.npc_letters.empty());
    assert(varint_field(offline::reply_private_letter(a,tables,1003,200303,20030301,200303011,false,now),1)==10);
    assert(a.player.npc_letters[200303].chain.size()==2 && a.player.npc_letters[200303].chain.back()==20030302);
    assert(varint_field(offline::reply_private_letter(a,tables,1003,200303,20030302,200303022,false,now),1)==10);
    assert(a.player.npc_letters[200303].chain.back()==20030305);
    const auto chain=a.player.npc_letters[200303].chain;
    assert(varint_field(offline::reply_private_letter(a,tables,1003,200303,20030302,200303021,false,now),1)==13 && a.player.npc_letters[200303].chain==chain);
    auto wishes=offline::favor_task_reply(a,tables,6,now);auto tasks=messages(wishes,3);assert(tasks.size()==2);
    assert(varint_field(tasks[0],2)==0); // Client opens the selection page only for WAITING.
    int id=static_cast<int>(*varint_field(tasks[0],1));
    assert(!offline::accept_favor_tasks(a,tables,{999999},6,now));
    assert(offline::accept_favor_tasks(a,tables,{id},6,now));
    assert(!offline::claim_favor_task(a,tables,id,6,now).ok);
    offline::GameTable g,c;g.load(*tables,"FavorabilityDailyTask");c.load(*tables,"TaskCondition");auto r=g.row(id);auto cond=c.row(g.int_field(*r,2).value());
    offline::CheckoutRequest battle{.section=c.ints_field(*cond,4).front(),.success=true,.heroes={g.int_field(*r,3).value()}};
    // The event records current server time, so pin the task day to today's snapshot.
    auto live=offline::favor_task_reply(a,tables,6,std::time(nullptr));auto live_tasks=messages(live,3);id=static_cast<int>(*varint_field(live_tasks[0],1));r=g.row(id);cond=c.row(g.int_field(*r,2).value());battle.section=c.ints_field(*cond,4).front();battle.heroes={g.int_field(*r,3).value()};
    assert(offline::accept_favor_tasks(a,tables,{id},6,std::time(nullptr)));
    offline::count_favor_battle(a,*tables,battle);assert(a.player.favor_progress[id]==1);
    assert(offline::claim_favor_task(a,tables,id,6,std::time(nullptr)).ok);
    auto gold=a.player.gold;auto exp=a.player.exp;
    assert(offline::claim_favor_task(a,tables,id,6,std::time(nullptr)).already && a.player.gold==gold && a.player.exp==exp);
    offline::AccountRepository db;db.set_tables({});db.save("terminal-regression",a);offline::AccountRepository reopened;reopened.set_tables({});const auto restored=*reopened.find("terminal-regression");
    assert(restored.player.npc_letters.at(200303).chain==chain && restored.player.npc_letters.at(200303).replies.at(20030302)==200303022);
    assert(restored.player.npc_blocked.at(1025)==1 && restored.player.npc_blog_likes.contains(550127));
    assert(restored.player.favor_tasks.at(id)==4 && restored.player.favor_progress.at(id)==1);
    offline::favor_task_reply(a,tables,6,std::time(nullptr)+86400);assert(!a.player.favor_tasks.contains(id) || a.player.favor_tasks.at(id)!=4);
    return 28;
}

// Battle page, god wish pools and activities added for the final build.
int final_build_checks(const offline::TableBlob* tables) {
    int checks = 0;
    auto gifts=fixture(); gifts.player.level=80; gifts.player.crystal=10000;
    constexpr std::int64_t now=1790000000;
    const auto listed_gifts=offline::gift_packages(gifts,tables,now);
    assert(listed_gifts.size()==5); // Daily, monthly, two level tracks, limited voucher pack.
    auto paid=offline::buy_gift_package(gifts,tables,2700002,1,false,now);
    assert(paid.ok && gifts.player.gift_claims[2700002].count==1);
    assert(!offline::buy_gift_package(gifts,tables,2700002,1,false,now).ok);
    assert(!offline::buy_gift_package(gifts,tables,2700004,1,false,now).ok);
    assert(offline::buy_gift_package(gifts,tables,2700003,1,false,now).ok);
    auto poor=fixture(); poor.player.level=80; poor.player.crystal=0;
    assert(!offline::buy_gift_package(poor,tables,2700002,1,false,now).ok);
    assert(poor.player.gift_claims.empty());
    assert(!offline::buy_gift_package(gifts,tables,2700001,1,false,now).ok);
    const auto before_month=gifts.player.crystal;
    const auto before_month_ai=gifts.player.ai_coin;
    const auto before_month_cards=quantity(gifts,1202013);
    assert(offline::buy_gift_package(gifts,tables,2700001,1,true,now).ok);
    assert(gifts.player.crystal==before_month+380 && gifts.player.ai_coin==before_month_ai+20);
    const auto month_views=offline::gift_packages(gifts,tables,now);
    const auto month_view=std::ranges::find(month_views,2700001,&offline::GiftPackageView::id);
    assert(month_view!=month_views.end() && month_view->state==3 && month_view->days==30);
    assert(month_view!=month_views.end() && varint_field(offline::gift_package_data(*month_view),4)==offline::client_time(now+30*86400));
    checks+=2;
    assert(!offline::settle_month_card(gifts,tables,now));
    const auto initial=gifts.player.crystal;
    assert(offline::settle_month_card(gifts,tables,now+86400));
    assert(gifts.player.crystal==initial+80 && !offline::settle_month_card(gifts,tables,now+86400));
    assert(gifts.player.ai_coin==before_month_ai+40 && quantity(gifts,1202013)==before_month_cards+2);
    checks+=2;
    assert(!offline::settle_month_card(gifts,tables,now+31*86400));
    assert(offline::buy_gift_package(gifts,tables,2700092,1,false,now).ok);
    assert(offline::buy_gift_package(gifts,tables,2700092,1,false,now).ok);
    assert(offline::buy_gift_package(gifts,tables,2700092,1,false,now).ok);
    const auto limited_balance=gifts.player.crystal;
    assert(!offline::buy_gift_package(gifts,tables,2700092,1,false,now).ok && gifts.player.crystal==limited_balance);
    assert(!offline::buy_gift_package(gifts,tables,2700092,2,false,now).ok);
    auto daily=fixture();
    for(int d=0;d<7;++d) {
        assert(offline::buy_gift_package(daily,tables,2700000,1,false,now+d*86400).ok);
        assert(!offline::buy_gift_package(daily,tables,2700000,1,false,now+d*86400).ok);
    }
    assert(daily.player.jewel_coin==1);
    const auto seventh=offline::gift_packages(daily,tables,now+6*86400);
    assert(seventh.front().purchases==7 && seventh.front().state==1);
    assert(offline::gift_packages(daily,tables,now+7*86400).front().purchases==0);
    assert(offline::buy_gift_package(daily,tables,2700000,1,false,now+7*86400).ok);
    assert(daily.player.jewel_coin==1);
    checks+=21;
    offline::AccountRepository storage; storage.set_tables({});
    storage.save("daily-gift-regression",daily);
    storage.save("gift-regression",gifts);
    offline::AccountRepository reopened;
    reopened.set_tables({});
    const auto restored=*reopened.find("gift-regression");
    assert(reopened.find("daily-gift-regression")->player.jewel_coin==1);
    assert(reopened.find("daily-gift-regression")->player.gift_claims.at(2700000).count==8);
    checks+=2;
    assert(restored.player.gift_claims.at(2700002).count==1);
    assert(restored.player.gift_claims.at(2700001).daily==gifts.player.gift_claims.at(2700001).daily);
    assert(restored.player.ai_coin==before_month_ai+40);
    checks+=1;
    checks+=18;
    server::Router gift_router;
    gift_router.set_tables(std::shared_ptr<offline::TableBlob>{const_cast<offline::TableBlob*>(tables),[](auto*){}});
    gift_router.account()=fixture(); gift_router.account().player.level=80; gift_router.account().player.crystal=10000;
    proto::Writer request; request.int32(1,2700092); request.int32(2,1);
    server::marsnet::Frame buy{.head={.request_id=912,.session_id="gift-test"},.proto_id=542,.body=request.data()};
    const auto receipt=gift_router.marsnet(buy);
    assert(frames(receipt).back().proto_id==543);
    assert(gift_router.account().player.gift_claims.at(2700092).count==1);
    const auto balance=gift_router.account().player.crystal;
    assert(gift_router.marsnet(buy)==receipt && gift_router.account().player.crystal==balance);
    assert(gift_router.account().player.gift_claims.at(2700092).count==1);
    request.int32(2,2); buy.body=request.data();
    assert(varint_field(frames(gift_router.marsnet(buy)).back().body,1)==13);
    const auto query_gift=frames(gift_router.marsnet({.head={.request_id=913,.session_id="gift-test"},.proto_id=531}));
    assert(query_gift.back().proto_id==532 && messages(query_gift.back().body,2).size()==5);
    proto::Writer charge;charge.int32(1,22099);
    server::marsnet::Frame charge_frame{.head={.request_id=914,.session_id="gift-test"},.proto_id=659,.body=charge.data()};
    assert(varint_field(frames(gift_router.marsnet(charge_frame)).back().body,1)==13);
    runtime::recharge_hook_ready.store(true); // Simulate the verified Android client completion hook.
    checks+=1;
    const auto charge_receipt=gift_router.marsnet(charge_frame);
    assert(frames(charge_receipt).back().proto_id==660 && varint_field(frames(charge_receipt).back().body,1)==10);
    assert(std::ranges::any_of(frames(charge_receipt),[](const auto& f){return f.proto_id==543;}));
    assert(gift_router.account().player.gift_claims.at(2700001).count==1);
    const auto charged_balance=gift_router.account().player.crystal;
    assert(charged_balance==balance+380);
    assert(gift_router.marsnet(charge_frame)==charge_receipt && gift_router.account().player.crystal==charged_balance);
    charge_frame.head.request_id=915;
    assert(varint_field(frames(gift_router.marsnet(charge_frame)).back().body,1)==13);
    assert(gift_router.account().player.crystal==charged_balance);
    checks+=7;
    const auto banner=gift_router.http({.method="GET",.path="/gifticon/2700001.png"});
    assert(banner.status==200 && banner.content_type=="image/png" && banner.body.size()>100000);
    assert(gift_router.http({.method="GET",.path="/gifticon/../accounts.db"}).status==404);
    const auto recommendations=offline::build_recommend_shop(gift_router.account(),tables);
    assert(!messages(recommendations,2).empty());
    checks+=9;
    // GM battle modifiers reach FightHero.attrAdd.
    const auto account = fixture();
    const auto base = hero_attrs(offline::ProtocolBuilders::fight_data(tables, account, {1003}, 2110001));
    const auto modded = hero_attrs(offline::ProtocolBuilders::fight_data(
        tables, account, {1003}, 2110001, {.attack_percent = 500, .speed_percent = 150, .god = true}));
    assert(base.at(100) > 0 && modded.at(100) == base.at(100) * 5);
    assert(modded.at(136) == base.at(136) * 150 / 100);
    assert(modded.at(104) == base.at(104) && modded.at(102) == base.at(102));
    assert(modded.at(106) == base.at(106));
    checks += 4;

    // Every god pool is open (novice, standard, 30 rate-up, 3 limited); jewel pools are not.
    const auto pools = offline::open_pool_ids(tables);
    assert(pools.size() >= 35);
    for (const auto pool : pools) {
        auto one = fixture();
        one.player.crystal = 100000;
        const auto result = offline::luck_draw(one, tables, pool, 0);
        if (!result.ok || result.cards.empty()) fprintf(stderr, "pool %d cannot draw\n", pool);
        assert(result.ok && !result.cards.empty());
    }
    assert(std::ranges::contains(pools, 22201) && std::ranges::contains(pools, 22235) && std::ranges::contains(pools, 22401));
    assert(!std::ranges::contains(pools, 22700));
    const auto pool_packet=offline::ProtocolBuilders::card_pool(*tables, account, 1790000000);
    const auto listed = messages(pool_packet, 2);
    assert(listed.size() == pools.size());
    checks += 4;

    // A ten-draw in a newly opened rate-up pool pays and grants ten results.
    auto drawer = fixture();
    drawer.player.crystal = 100000;
    const auto before = drawer.player.crystal;
    const auto drawn = offline::luck_draw(drawer, tables, 22205, 1);
    assert(drawn.ok && drawn.cards.size() >= 10 && drawer.player.crystal < before);
    assert(!offline::luck_draw(drawer, tables, 22700, 1).ok);
    checks += 2;

    // Unimplemented seasonal activities stay closed; opening them requests
    // unsupported reward pools and causes a client ActivityNotice null reference.
    const auto activity_packet=offline::ProtocolBuilders::query_activity(*tables, 1790000000);
    const auto acts = messages(activity_packet, 2);
    assert(!acts.empty());
    for (const auto act : acts) {
        assert(varint_field(act, 2) == 0u);
        assert(*varint_field(act, 6) > *varint_field(act, 5));
    }
    checks += 1;
    return checks;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{input}, {}};
    auto tables = std::make_shared<offline::TableBlob>();
    assert(tables->own_blob_and_load(std::move(bytes)));
    int checks = 0;
    for (const int count : {1, 2}) {
        auto account = fixture();
        auto r = offline::compose_jewel(account, tables.get(), {.recipe = 29026, .count = count});
        assert(r.code == 10 && r.reward.id == 1251015 && r.reward.num == count);
        assert(quantity(account, 1251014) == 6 - 3 * count && quantity(account, 1251015) == count);
        assert(account.player.gold == 1000000 - 100000 * count);
        ++checks;
    }
    for (const int count : {-1, 0, 1000, 2147483647}) {
        auto account = fixture();
        assert(offline::compose_jewel(account, tables.get(), {.recipe = 29026, .count = count}).code == 13);
        assert(quantity(account, 1251014) == 6 && account.player.gold == 1000000);
        ++checks;
    }
    for (int reason = 0; reason < 4; ++reason) {
        auto account = fixture();
        if (reason == 0) account.player.gold = 99999;
        if (reason == 1) account.items[0].num = 2;
        if (reason == 2) account.items[0].locked = true;
        const auto before = account;
        auto r = offline::compose_jewel(account, tables.get(), {.recipe = reason == 3 ? 999999 : 29026, .count = 1});
        assert(r.code == 13 && account.player.gold == before.player.gold && account.items[0].num == before.items[0].num);
        ++checks;
    }
    {
        auto account = fixture();
        account.items[0].num = 2;
        account.heroes[0].artifact.jewels[0] = 1251014;
        auto r = offline::compose_jewel(account, tables.get(), {.recipe = 29026, .count = 1, .hero = 1003, .hole = 0, .equipped = true});
        assert(r.code == 10 && account.heroes[0].artifact.jewels[0] == 1251015);
        assert(quantity(account, 1251014) == 0 && quantity(account, 1251015) == 0 && account.player.gold == 900000);
        ++checks;
        const auto gold = account.player.gold;
        r = offline::compose_jewel(account, tables.get(), {.recipe = 29026, .count = 1, .hero = 1003, .hole = 0, .equipped = true});
        assert(r.code == 13 && account.player.gold == gold && account.heroes[0].artifact.jewels[0] == 1251015);
        ++checks;
    }
    {
        auto account = fixture();
        auto skins = offline::collect_hero_skins(account, tables.get());
        assert(skins[1003].contains(1220302) && !skins[1003].contains(1220303));
        account.heroes[0].star = 10;
        assert(!offline::collect_hero_skins(account, tables.get())[1003].contains(1220302));
        account.items.push_back({.id = 1220303, .num = 1});
        assert(offline::collect_hero_skins(account, tables.get())[1003].contains(1220303));
        account.items.back().num = 0;
        assert(!offline::collect_hero_skins(account, tables.get())[1003].contains(1220303));
        assert(offline::contains_skin_items({{.id = 1220303, .num = 1}}, tables.get()));
        checks += 5;
    }
    {
        server::Router router;
        router.set_tables(tables);
        router.account() = fixture();
        proto::Writer body;
        body.int32(1, 29026); body.int32(2, 1);
        const server::marsnet::Frame packet{.head = {.request_id = 90001, .session_id = "regression"}, .proto_id = 144, .body = body.data()};
        const auto response = router.marsnet(packet);
        const auto decoded = frames(response);
        assert(decoded.back().proto_id == 147);
        assert(router.marsnet(packet) == response && quantity(router.account(), 1251015) == 1);
        const auto gold = router.account().player.gold;
        auto conflict = packet; conflict.body = {8, 128}; // truncated varint; same request ID
        assert(frames(router.marsnet(conflict)).back().proto_id == 147);
        assert(router.account().player.gold == gold);
        checks += 3;
    }
    {
        server::Router router;
        router.set_tables(tables);
        router.account() = fixture();
        proto::Writer body;
        body.int32(1, 50); body.int32(3, 1220303); body.string(4, "1");
        const auto response = frames(router.marsnet({.head = {.request_id = 90002, .session_id = "regression"}, .proto_id = 124, .body = body.data()}));
        assert(std::any_of(response.begin(), response.end(), [](const auto& f) { return f.proto_id == 570; }));
        assert(offline::collect_hero_skins(router.account(), tables.get())[1003].contains(1220303));
        checks += 2;
    }
    {
        offline::GameTable appearance;
        assert(appearance.load(*tables, "Appearance"));
        int purchased = 0, stage = 0;
        for (const auto& row : appearance.rows()) {
            const auto condition = appearance.int_field(row, 2).value_or(0);
            const auto hero_id = appearance.int_field(row, 18).value_or(0);
            const auto skin_id = static_cast<std::int32_t>(row.key);
            if (hero_id <= 0 || (condition != 2 && condition != 4)) continue;
            offline::Account account;
            account.heroes.push_back({.id = hero_id, .state = 2, .star = 10});
            if (condition == 4) {
                assert(!offline::collect_hero_skins(account, tables.get())[hero_id].contains(skin_id));
                account.items.push_back({.id = skin_id, .num = 1});
                assert(offline::collect_hero_skins(account, tables.get())[hero_id].contains(skin_id));
                account.items.back().num = 0;
                assert(!offline::collect_hero_skins(account, tables.get())[hero_id].contains(skin_id));
                ++purchased;
                checks += 3;
            } else {
                assert(!offline::collect_hero_skins(account, tables.get())[hero_id].contains(skin_id));
                account.heroes[0].star = 11;
                assert(offline::collect_hero_skins(account, tables.get())[hero_id].contains(skin_id));
                ++stage;
                checks += 2;
            }
        }
        printf("Skin coverage: %d purchased skins, %d stage skins\n", purchased, stage);
        assert(purchased == 36 && stage == 39);
    }
    {
        assert(persist::init_database());
        auto account = fixture();
        assert(offline::compose_jewel(account, tables.get(), {.recipe = 29026, .count = 1}).code == 10);
        account.items.push_back({.id = 1220303, .num = 1});
        account.heroes[0].outer_skin = account.heroes[0].battle_skin = 1220303;
        offline::AccountRepository repository;
        repository.set_tables(tables);
        repository.save("regression-persistence", account);
        offline::AccountRepository reopened;
        reopened.set_tables(tables);
        const auto restored = reopened.create_or_get("regression-persistence", "unused");
        assert(quantity(restored, 1251014) == 3 && quantity(restored, 1251015) == 1);
        assert(restored.player.gold == 900000);
        assert(offline::collect_hero_skins(restored, tables.get())[1003].contains(1220303));
        const auto hero = std::find_if(restored.heroes.begin(), restored.heroes.end(), [](const auto& h) { return h.id == 1003; });
        assert(hero != restored.heroes.end() && hero->outer_skin == 1220303 && hero->battle_skin == 1220303);
        checks += 4;
    }
    {
        offline::AccountRepository clean_developer; clean_developer.set_tables(tables);
        clean_developer.save("__x2_developer__",fixture());
        server::Router router;
        router.set_tables(tables);
        const auto call = [&](const char* path, const char* body) {
            return router.http({.method = "POST", .path = path, .body = body});
        };
        { std::ofstream settings{"gm-regression.settings"}; settings << "1 0"; }
        router.configure_gm("gm-regression.settings");
        assert(call("/gm/action", "action=gold&id=0&count=10").body.find("\"ok\":false") != std::string::npos);
        router.marsnet({.head = {.session_id = ""}, .proto_id = 54});
        assert(router.http({.method = "GET", .path = "/gm/status"}).body.find("\"connected\":true") != std::string::npos);
        router.account() = fixture();
        router.marsnet({.head = {.session_id = "gm-regression"}, .proto_id = 9999});
        const auto gold = router.account().player.gold;
        assert(call("/gm/action", "action=gold&id=0&count=100").body.find("\"ok\":true") != std::string::npos);
        assert(router.account().player.gold == gold + 100);
        assert(!router.poll_pushes().empty() && router.poll_pushes().empty());
        call("/gm/action", "action=gold&id=0&count=1");
        call("/gm/action", "action=gold&id=0&count=2");
        const auto interleaved = frames(router.marsnet({.head = {.session_id = "gm-regression"}, .proto_id = 9999}));
        assert(std::count_if(interleaved.begin(), interleaved.end(), [](const auto& frame) { return frame.proto_id == 1000; }) == 2);
        assert(router.poll_pushes().empty());
        assert(router.account().player.gold == gold + 103);
        assert(call("/gm/action", "action=item&id=1251014&count=-1").body.find("\"ok\":false") != std::string::npos);
        assert(call("/gm/action", "action=item&id=9999999&count=1").body.find("\"ok\":false") != std::string::npos);
        assert(call("/gm/action", "action=skin&id=1220303&count=1").body.find("\"ok\":true") != std::string::npos);
        assert(offline::collect_hero_skins(router.account(), tables.get())[1003].contains(1220303));
        assert(call("/gm/catalog", "kind=skin").body.find("1220303") != std::string::npos);
        assert(call("/gm/action", "action=developer").body.find("\"ok\":true") != std::string::npos);
        proto::Writer login;
        login.string(3, "gm-normal-save");
        router.marsnet({.head = {.session_id = "gm-login"}, .proto_id = 54, .body = login.data()});
        assert(router.account().player.nickname == "X2开发者" && !router.account().is_create_role);
        assert(router.account().player.main_section > 0 && !router.account().player.guides.empty());
        offline::GameTable valid_sections; assert(valid_sections.load(*tables,"SectionTable"));
        assert(valid_sections.row(router.account().player.main_section));
        assert(std::ranges::contains(router.account().player.cleared_main,2110106));
        checks+=2;
        for (const auto& entry : router.account().player.guides) assert(entry.second == 1);
        assert(call("/gm/action", "action=hide_developer").body.find("\"ok\":true") != std::string::npos);
        assert(call("/gm/action", "action=developer").body.find("\"ok\":false") != std::string::npos);
        server::Router restored_settings;
        restored_settings.configure_gm("gm-regression.settings");
        assert(restored_settings.http({.method = "GET", .path = "/gm/status"}).body.find("\"developerAvailable\":false") != std::string::npos);
        assert(restored_settings.http({.method = "POST", .path = "/gm/action", .body = "action=developer"}).body.find("\"ok\":false") != std::string::npos);
        router.marsnet({.head = {.session_id = "gm-normal"}, .proto_id = 54, .body = login.data()});
        assert(router.account().player.nickname != "X2开发者");
        offline::AccountRepository repository;
        repository.set_tables(tables);
        auto dev = fixture(); dev.player.nickname = "isolation-developer";
        dev.player.cleared_main.resize(10000, 101);
        repository.save("__x2_developer__", dev);
        const auto normal = repository.create_or_get("fresh-isolation-check", "unused");
        assert(normal.player.nickname != "isolation-developer");
        assert(repository.find("__x2_developer__").has_value());
        checks += 23;
    }

    {
        const auto allowed = offline::playable_heroes(tables.get());
        assert(allowed.size() == 39 && allowed.contains(1003) && allowed.contains(1039));
        assert(!allowed.contains(1041) && !allowed.contains(5936));
        checks += 2;
        server::Router router; router.set_tables(tables);
        const auto call = [&](const char* path, const std::string& body) {
            return router.http({.method="POST", .path=path, .body=body}).body;
        };
        proto::Writer login; login.string(3,"expanded-gm-check");
        router.marsnet({.head={.session_id=""}, .proto_id=54, .body=login.data()});
        router.account()=fixture();
        assert(call("/gm/action","action=hero&id=1041&count=1").find("\"ok\":false")!=std::string::npos);
        assert(router.account().heroes.size()==1);
        assert(call("/gm/command","opt=1&v0=12345&v1=0&set=1").find("\"ok\":true")!=std::string::npos);
        assert(router.account().player.gold==12345);
        assert(call("/gm/command","opt=1&v0=5&v1=0").find("\"ok\":true")!=std::string::npos);
        assert(router.account().player.gold==12350);
        assert(call("/gm/command","opt=5&v0=1&v1=0").find("\"ok\":false")!=std::string::npos);
        assert(call("/gm/command","opt=4&v0=1041&v1=0").find("\"ok\":false")!=std::string::npos);
        assert(call("/gm/command","opt=8&v0=9999999&v1=1").find("\"ok\":false")!=std::string::npos);
        assert(call("/gm/command","opt=50&v0=1251014,1251015&v1=2").find("\"ok\":true")!=std::string::npos);
        assert(quantity(router.account(),1251014)==8 && quantity(router.account(),1251015)==2);
        const auto mail_count=router.account().player.mails.size();
        assert(call("/gm/command","opt=19&v0=1251014&v1=2").find("\"ok\":true")!=std::string::npos);
        assert(router.account().player.mails.size()==mail_count+1);
        assert(call("/gm/command","opt=13&v0=0&v1=0").find("\"ok\":false")!=std::string::npos);
        assert(router.account().player.gold==12350);
        assert(call("/gm/command","opt=13&v0=0&v1=0&confirm=1").find("\"ok\":true")!=std::string::npos);
        assert(router.account().player.gold==0);
        assert(call("/gm/action","action=restore_backup").find("\"ok\":true")!=std::string::npos);
        assert(router.account().player.gold==12350 && quantity(router.account(),1251014)==8);
        const auto catalog=call("/gm/catalog","kind=hero&page=0");
        assert(catalog.find("\"total\":39")!=std::string::npos && catalog.find("\"id\":1041,")==std::string::npos);
        assert(call("/gm/catalog","kind=hero&page=1").find("\"entries\":[]")!=std::string::npos);
        assert(call("/gm/catalog","kind=item&q=1251014").find("\"id\":1251014,")!=std::string::npos);
        assert(call("/gm/catalog","kind=item&page=-1").find("\"ok\":false")!=std::string::npos);
        checks += 23;
        // Existing developer saves must lose invalid combat-only units on login.
        offline::AccountRepository repository; repository.set_tables(tables);
        auto dev=fixture(); dev.player.nickname="X2开发者";
        dev.heroes.push_back({.id=1041,.state=2});
        repository.save("__x2_developer__",dev);
        {std::ofstream file{"expanded-gm.settings"}; file<<"1 1";}
        server::Router repaired; repaired.set_tables(tables); repaired.configure_gm("expanded-gm.settings");
        repaired.marsnet({.head={.session_id=""},.proto_id=54,.body=login.data()});
        assert(repaired.account().heroes.size()==1 && repaired.account().heroes[0].id==1003);
        offline::AccountRepository reloaded; reloaded.set_tables(tables);
        assert(reloaded.find("__x2_developer__")->heroes.size()==1);
        checks+=2;
    }
    checks += final_build_checks(tables.get());
    checks += terminal_checks(tables.get());
    {
        auto a=fixture(); a.player.skin_coupon=1000; a.player.crystal=1000;
        const auto before=a.player.skin_coupon;
        assert(!offline::buy_commercial_goods(a,tables.get(),1980001,1,919).ok);
        assert(a.player.skin_coupon==before && a.player.crystal==1000);
        const auto purchase=offline::buy_commercial_goods(a,tables.get(),1980001,1,923);
        assert(purchase.ok && a.player.skin_coupon==680 && a.player.crystal==1000);
        assert(!offline::buy_commercial_goods(a,tables.get(),1980001,1,923).ok && a.player.skin_coupon==680);
        assert(!offline::can_wear_skin(a,tables.get(),1003,1221103,3));
        assert(!offline::can_wear_skin(a,tables.get(),1003,1220303,3));
        a.items.push_back({.id=1220303,.num=1});
        assert(offline::can_wear_skin(a,tables.get(),1003,1220303,3));
        assert(!offline::can_wear_skin(a,tables.get(),9999,0,3));
        assert(!offline::can_wear_skin(a,tables.get(),1003,0,4));
        assert(offline::can_wear_skin(a,tables.get(),1003,0,3));
        const auto listing=offline::query_commercial_goods(a,tables.get(),1);
        proto::Reader r{listing.body}; int goods=0;
        while(auto f=r.field()) {
            if(f->number!=2) {r.skip(f->wire_type);continue;}
            auto b=r.bytes(); assert(b); proto::Reader g{*b}; bool currency=false,permanent=false,payment=false,expiry=false;
            while(auto gf=g.field()) {
                if(gf->wire_type!=0) {g.skip(gf->wire_type);continue;}
                auto value=g.varint();assert(value);
                if(gf->number==7) {assert(*value==902 || *value==923);currency=true;}
                if(gf->number==9) {assert(*value==2);permanent=true;}
                if(gf->number==11) {assert(*value==0);payment=true;}
                if(gf->number==3) {assert(*value==2147483647);expiry=true;}
            }
            assert(currency && permanent && payment && expiry);++goods;
        }
        assert(goods>20);
        checks+=11+goods*5;
    }
    {
        auto a=fixture(); a.player.npc_blog_likes[550127]=true;
        a.player.favor_tasks[635068]=2;
        const auto save=offline::encode_save(a);
        const auto restored=offline::decode_save(save);
        assert(restored && offline::encode_save(*restored)==save);
        assert(restored->player.npc_blog_likes.at(550127));
        assert(restored->player.favor_tasks.at(635068)==2);
        auto damaged=save; damaged[20]^=1; assert(!offline::decode_save(damaged));
        assert(!offline::decode_save(std::span(save).first(save.size()-1)));
        server::Router router; router.set_tables(tables);
        proto::Writer login; login.string(3,"save-import-check");
        router.marsnet({.head={.session_id=""},.proto_id=54,.body=login.data()});
        router.account()=a;
        std::string hex; const char* digits="0123456789abcdef";
        for(auto b:save) {hex+=digits[b>>4];hex+=digits[b&15];}
        auto call=[&](const std::string& body){return router.http({.method="POST",.path="/gm/save/import",.body=body}).body;};
        assert(call("data="+hex+"&developer=0").find("\"ok\":false")!=std::string::npos);
        assert(call("data="+hex+"&developer=1&confirm=1").find("\"ok\":false")!=std::string::npos);
        assert(offline::encode_save(router.account())==save);
        router.account().player.gold=99;
        assert(call("data="+hex+"&developer=0&confirm=1").find("\"ok\":true")!=std::string::npos);
        assert(router.account().player.gold==a.player.gold);
        offline::AccountRepository repository; repository.set_tables(tables);
        assert(repository.find("__x2_gm_backup__save-import-check")->player.gold==99);
        checks+=11;
    }
    printf("PASS: %d regression checks against embedded APK tables (including SQLite reload and GM isolation)\n", checks);
}
