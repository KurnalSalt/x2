#include "offline/jewel_compose.hpp"

#include <algorithm>
#include <limits>
#include <map>

#include "offline/game_table.hpp"
#include "offline/mission.hpp"

namespace x2::offline {

JewelComposeResult compose_jewel(Account& account, const TableBlob* tables,
                                 const JewelComposeRequest& request) {
    JewelComposeResult result;
    if (!tables || request.count < 1 || request.count > 999) return result;
    GameTable recipes, items;
    if (!recipes.load(*tables, "Recipe") || !items.load(*tables, "Item")) return result;
    const auto row = recipes.row(request.recipe);
    if (!row) return result;
    const auto value = [&](unsigned field) { return recipes.int_field(*row, field).value_or(0); };
    const auto type = value(2);
    const auto product = value(5);
    const auto output = std::int64_t{value(6)} * request.count;
    const auto gold = std::int64_t{value(7)} * request.count;
    const auto power = std::int64_t{value(8)} * request.count;
    constexpr auto max = std::numeric_limits<std::int32_t>::max();
    if (type < 1 || type > 3 || product <= 0 || output <= 0 || output > max ||
        gold < 0 || power < 0 || gold > max || power > max || value(12) || value(13)) return result;
    const auto product_row = items.row(product);
    if (!product_row) return result;
    if (value(10) && (value(10) != 1 ||
        std::find(account.player.cleared_main.begin(), account.player.cleared_main.end(), value(11)) ==
        account.player.cleared_main.end())) return result;

    const auto ids = recipes.ints_field(*row, 3);
    const auto nums = recipes.ints_field(*row, 4);
    if (ids.empty() || ids.size() != nums.size()) return result;
    std::map<std::int32_t, std::int64_t> costs;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] <= 0 || nums[i] <= 0 || !items.row(ids[i])) return result;
        costs[ids[i]] += std::int64_t{nums[i]} * request.count;
        if (costs[ids[i]] > max) return result;
    }
    costs[1237901] += gold;
    costs[1237900] += power;

    Account candidate = account;
    AccountHero* hero = nullptr;
    if (request.equipped) {
        if (request.count != 1 || type != 1 || items.int_field(*product_row, 6).value_or(0) != 11 ||
            request.hole < 0 || request.hole > 5 || output != 1) return result;
        const auto it = std::find_if(candidate.heroes.begin(), candidate.heroes.end(), [&](const auto& h) {
            return h.id == request.hero && h.state == 2;
        });
        if (it == candidate.heroes.end()) return result;
        hero = &*it;
        const auto slot = hero->artifact.jewels.find(request.hole);
        if (slot == hero->artifact.jewels.end() || slot->second <= 0) return result;
        const auto cost = costs.find(slot->second);
        if (cost == costs.end() || cost->second < 1) return result;
        // The worn jewel supplies one ingredient; consume the rest from the bag.
        --cost->second;
    }

    for (const auto& [id, num] : costs) {
        if (num == 0) continue;
        if (num < 0 || num > max) return result;
        if (const auto currency = item_currency_enu(tables, id)) {
            auto* amount = wallet(candidate.player, *currency);
            if (!amount || *amount < num) return result;
            *amount -= static_cast<std::int32_t>(num);
        } else {
            const auto item = std::find_if(candidate.items.begin(), candidate.items.end(),
                [&](const auto& i) { return i.id == id; });
            if (item == candidate.items.end() || item->locked || item->num < num) return result;
            item->num -= static_cast<std::int32_t>(num);
            result.changed_items.push_back(*item); // Include zero counts to remove consumed entries.
        }
    }
    result.reward = {.id = product, .num = static_cast<std::int32_t>(output)};
    if (hero) {
        hero->artifact.jewels[request.hole] = product;
        result.changed_hero = hero->id;
    } else {
        if (const auto currency = item_currency_enu(tables, product)) {
            auto* amount = wallet(candidate.player, *currency);
            if (!amount || std::int64_t{*amount} + output > max) return JewelComposeResult{};
            *amount += static_cast<std::int32_t>(output);
        } else {
            auto item = std::find_if(candidate.items.begin(), candidate.items.end(),
                [&](const auto& i) { return i.id == product; });
            if (item == candidate.items.end()) item = candidate.items.insert(item, AccountItem{.id = product});
            if (std::int64_t{item->num} + output > max) return JewelComposeResult{};
            item->num += static_cast<std::int32_t>(output);
            result.changed_items.push_back(*item);
        }
    }
    account = std::move(candidate);
    result.code = 10;
    return result;
}

} // namespace x2::offline
