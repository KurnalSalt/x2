#include "offline/gift_packages.hpp"
#include <algorithm>
#include "proto/protobuf.hpp"
#include "offline/protocol_builders.hpp"
namespace x2::offline {
namespace {
std::int64_t day(std::int64_t now) { return (now + 8 * 3600) / 86400; }
bool selected(std::int32_t id) { return (id >= 2700000 && id <= 2700033) || id == 2700092; }
int days_left(const AccountPlayer::GiftClaim& s, std::int64_t now) {
    return s.count ? static_cast<int>(std::clamp<std::int64_t>(30 - (day(now) - day(s.purchased)), 0, 30)) : 0;
}
}
std::vector<GiftPackageView> gift_packages(const Account& a, const TableBlob* tables, std::int64_t now) {
    GameTable packages;
    std::vector<GiftPackageView> out;
    if (!tables || !packages.load(*tables, "GiftPackage")) return out;
    std::map<int,int> next;
    for (int id = 2700000; id <= 2700033; ++id) {
        const auto row = packages.row(id);
        if (!row || packages.int_field(*row,9) != 3) continue;
        const auto track = packages.ints_field(*row,11);
        const auto it = a.player.gift_claims.find(id);
        if (!track.empty() && !next.contains(track[0]) && (it == a.player.gift_claims.end() || !it->second.count)) next[track[0]] = id;
    }
    for (const auto& row : packages.rows()) {
        const int id = static_cast<int>(row.key), type = packages.int_field(row,9).value_or(0);
        if (!selected(id)) continue;
        const auto it = a.player.gift_claims.find(id);
        const auto s = it == a.player.gift_claims.end() ? AccountPlayer::GiftClaim{} : it->second;
        GiftPackageView v{.id=id,.purchases=s.count};
        if (type == 3) {
            const auto track = packages.ints_field(row,11), gate = packages.ints_field(row,10);
            if (track.empty() || next[track[0]] != id) continue;
            if (!gate.empty() && a.player.level < gate[0]) v.state = 3;
        }
        if (id == 2700001) {
            // GiftBoxNode handles active month cards in Unpurchasable, displaying
            // leftTime. Purchased skips that branch and retains the prefab's no-buy label.
            v.days = days_left(s,now); v.state = v.days ? 3 : 0; v.purchases = v.days ? 1 : 0;
            v.deadline = v.days ? s.purchased + 30 * 86400 : 0;
        } else if (type == 2) {
            v.state = s.count && day(s.purchased) == day(now) ? 1 : 0;
            const auto cycle=packages.ints_field(row,10);
            if(!cycle.empty() && cycle[0]>0)
                v.purchases = v.state && s.count ? (s.count-1)%cycle[0]+1 : s.count%cycle[0];
        }
        else if (const int limit=packages.int_field(row,14).value_or(0); limit && s.count >= limit) v.state=2;
        out.push_back(v);
    }
    std::ranges::sort(out,{},&GiftPackageView::id);
    return out;
}
std::vector<std::uint8_t> gift_package_data(const GiftPackageView& v) {
    proto::Writer w; w.int32(1,v.id); w.int32(2,v.state); w.int32(3,0);
    w.int64(4,v.deadline ? client_time(v.deadline) : 0); w.int32(5,v.days); w.int32(6,v.purchases); w.int64(7,0); return w.data();
}
GiftPurchase buy_gift_package(Account& a,const TableBlob* tables,int id,int count,bool recharge,std::int64_t now) {
    GiftPurchase result;
    if (count != 1 || !selected(id) || !tables) return result;
    GameTable packages,gifts,items;
    if (!packages.load(*tables,"GiftPackage") || !gifts.load(*tables,"Gift") || !items.load(*tables,"Item")) return result;
    const auto row=packages.row(id);
    const auto views=gift_packages(a,tables,now);
    const auto v=std::ranges::find(views,id,&GiftPackageView::id);
    if (!row || v==views.end() || v->state || (packages.int_field(*row,15)==919) != recharge) return result;
    const int currency=packages.int_field(*row,15).value_or(0);
    if (currency!=902 && currency!=919) return result;
    const int price=currency==902 ? packages.int_field(*row,16).value_or(0) : 0;
    if (price<0 || a.player.crystal<price) return result;
    auto groups=packages.ints_field(*row,17);
    if(packages.int_field(*row,9)==2) {
        const auto cycle=packages.ints_field(*row,10), bonus=packages.ints_field(*row,11);
        const auto previous=a.player.gift_claims.find(id);
        const auto claimed=previous==a.player.gift_claims.end()?0:previous->second.count;
        if(!cycle.empty() && cycle[0]>0 && (claimed+1)%cycle[0]==0)
            groups.insert(groups.end(),bonus.begin(),bonus.end());
    }
    std::vector<AccountItem> rewards;
    for(const auto group:groups) {
        const auto g=gifts.row(group);
        if(!g) return result;
        const auto ids=gifts.ints_field(*g,4), nums=gifts.ints_field(*g,5);
        if(ids.empty() || ids.size()!=nums.size()) return result;
        for(std::size_t i=0;i<ids.size();++i) if(nums[i]<=0 || !items.row(ids[i])) return result;
        expand_gift(gifts,group,rewards);
    }
    if(rewards.empty()) return result;
    if(id==2700001) {
        GameTable recharge_table;
        if(!recharge_table.load(*tables,"Recharge")) return result;
        const auto charge=recharge_table.row(packages.int_field(*row,16).value_or(0));
        if(!charge || recharge_table.int_field(*charge,12)!=id) return result;
        const auto initial=recharge_table.int_field(*charge,16).value_or(0); // MonthCardAddAward.
        if(initial<0) return result;
        if(initial) rewards.push_back({.id=1237902,.num=initial});
    }
    auto candidate=a;
    candidate.player.crystal-=price;
    auto& claim=candidate.player.gift_claims[id];
    claim.count=clamp_add(claim.count,1); claim.purchased=now;
    result.paid=pay_rewards(candidate,tables,rewards);
    if(id==2700001) claim.daily=day(now); // GiftID contains today's full allowance.
    result.paid.player=true; a=std::move(candidate); result.ok=true; return result;
}
bool settle_month_card(Account& a,const TableBlob* tables,std::int64_t now) {
    const auto it=a.player.gift_claims.find(2700001);
    if(it==a.player.gift_claims.end() || !days_left(it->second,now) || it->second.daily==day(now)) return false;
    GameTable p,gifts,items;
    if(!tables || !p.load(*tables,"GiftPackage") || !gifts.load(*tables,"Gift") || !items.load(*tables,"Item")) return false;
    const auto row=p.row(2700001); if(!row) return false;
    std::vector<AccountItem> rewards;
    for(const auto group:p.ints_field(*row,17)) {
        const auto gift=gifts.row(group); if(!gift) return false;
        const auto ids=gifts.ints_field(*gift,4), nums=gifts.ints_field(*gift,5);
        if(ids.empty() || ids.size()!=nums.size()) return false;
        for(std::size_t i=0;i<ids.size();++i) if(nums[i]<=0 || !items.row(ids[i])) return false;
        expand_gift(gifts,group,rewards);
    }
    if(rewards.empty()) return false;
    auto candidate=a;
    pay_rewards(candidate,tables,rewards);
    candidate.player.gift_claims.at(2700001).daily=day(now);
    a=std::move(candidate); return true;
}
}
