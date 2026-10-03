#pragma once

#include "offline/account_state.hpp"
#include "offline/table_blob.hpp"

namespace x2::offline {

struct JewelComposeRequest {
    std::int32_t recipe{};
    std::int32_t count{};
    std::int32_t hero{};
    std::int32_t hole{-1};
    bool equipped{};
};

struct JewelComposeResult {
    std::int32_t code{13};
    AccountItem reward{};
    std::vector<AccountItem> changed_items;
    std::int32_t changed_hero{};
};

// Validate all costs and outputs before changing the account.
JewelComposeResult compose_jewel(Account& account, const TableBlob* tables,
                                 const JewelComposeRequest& request);

} // namespace x2::offline
