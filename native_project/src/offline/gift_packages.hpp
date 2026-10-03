#pragma once
#include "offline/mission.hpp"
namespace x2::offline {
struct GiftPackageView { std::int32_t id{}, state{}, purchases{}, days{}; std::int64_t deadline{}; };
std::vector<GiftPackageView> gift_packages(const Account&, const TableBlob*, std::int64_t now);
std::vector<std::uint8_t> gift_package_data(const GiftPackageView&);
struct GiftPurchase { bool ok{}; Paid paid; };
GiftPurchase buy_gift_package(Account&, const TableBlob*, std::int32_t id, std::int32_t count, bool recharge, std::int64_t now);
bool settle_month_card(Account&, const TableBlob*, std::int64_t now);
}
