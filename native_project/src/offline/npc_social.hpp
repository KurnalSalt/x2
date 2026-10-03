#pragma once

#include <cstdint>
#include <vector>

#include "offline/account_state.hpp"
#include "offline/mission.hpp"
#include "offline/table_blob.hpp"

namespace x2::offline {

[[nodiscard]] std::vector<std::uint8_t> query_npc_blog(const Account &account,
                                                       const TableBlob *tables,
                                                       std::int64_t now_unix);

[[nodiscard]] std::vector<std::uint8_t>
like_npc_blog(Account &account, const TableBlob *tables, std::int32_t hero_id,
              std::int32_t group_id, std::int64_t now_unix);

[[nodiscard]] std::vector<std::uint8_t>
reply_npc_blog(Account &account, const TableBlob *tables, std::int32_t hero_id,
               std::int32_t group_id, std::int32_t chat_group_id,
               std::int32_t reply_id, std::int64_t now_unix);

[[nodiscard]] std::vector<std::uint8_t>
query_npc_letter(const Account &account, const TableBlob *tables,
                 std::int64_t now_unix);
[[nodiscard]] std::vector<std::uint8_t>
update_private_letter(Account &account, const TableBlob *tables,
                      std::int32_t hero_id, std::int32_t letter_id, bool ended,
                      std::int64_t now_unix);

std::vector<std::uint8_t> reply_private_letter(Account &, const TableBlob *,
                                               int, int, int, int, bool,
                                               std::int64_t);
std::vector<std::uint8_t> toggle_npc_block(Account &, int);
std::vector<std::uint8_t> favor_task_reply(Account &, const TableBlob *, int,
                                           std::int64_t);
bool accept_favor_tasks(Account &, const TableBlob *, const std::vector<int> &,
                        int, std::int64_t);
void count_favor_battle(Account &, const TableBlob &, const CheckoutRequest &);
TaskClaim claim_favor_task(Account &, const TableBlob *, int, int,
                           std::int64_t);
} // namespace x2::offline
