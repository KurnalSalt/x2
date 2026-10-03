#pragma once

#include <memory>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "offline/account_repository.hpp"
#include "offline/protocol_builders.hpp"
#include "offline/table_blob.hpp"
#include "server/http.hpp"
#include "server/marsnet.hpp"

namespace x2::server {

class Router {
public:
    void set_tables(std::shared_ptr<offline::TableBlob> tables);
    void configure_gm(std::string settings_path);
    [[nodiscard]] x2::server::HttpResponse http(const HttpRequest& request);
    [[nodiscard]] std::vector<std::uint8_t> marsnet(const marsnet::Frame& frame);
    // Unsolicited pushes ready to go out (college build finished while no
    // request was in flight). The tcp heartbeat polls this.
    [[nodiscard]] std::vector<std::uint8_t> poll_pushes();
    [[nodiscard]] offline::Account& account() noexcept { return account_; }
    [[nodiscard]] const offline::Account& account() const noexcept { return account_; }

private:
    std::vector<std::uint8_t> marsnet_locked(const marsnet::Frame& frame);
    HttpResponse gm_http(const HttpRequest& request);
    void save_gm_settings();
    void prepare_developer(offline::Account& account);
    std::recursive_mutex mutex_;
    std::string gm_settings_path_;
    bool developer_mode_{false}, developer_available_{false};
    // GM battle page; saved next to the GM settings as <settings>_battle.
    offline::BattleMods battle_{};
    bool logged_in_{false};
    std::vector<std::uint8_t> gm_pushes_;
    [[nodiscard]] offline::Account& account_for_login(const marsnet::Frame& frame);
    // PlayerDataProto push at the next data version (see C2L_Login in router.cpp).
    [[nodiscard]] std::vector<std::uint8_t> push_player_data(const marsnet::Frame& frame);
    // L2C_ItemUpdate with the changed bag entries (empty packet when none).
    [[nodiscard]] std::vector<std::uint8_t> push_item_updates(const marsnet::Frame& frame,
                                                              const std::vector<offline::AccountItem>& items);
    // College build finished: L2C_UpLevelBuildingId (event 206) + full 584 push.
    [[nodiscard]] std::vector<std::uint8_t> college_done_pushes(const marsnet::Frame& frame,
                                                                std::int32_t building_id);
    // Empty L2C_QueryGrowthBase. CollegeModule.StartPowerTimer null-refs until this exists.
    [[nodiscard]] std::vector<std::uint8_t> push_growth(const marsnet::Frame& frame);
    [[nodiscard]] std::vector<std::uint8_t> push_card_pool(const marsnet::Frame& frame);
    // L2C_UpdatePlayerLevel. Empty when the level did not rise.
    [[nodiscard]] std::vector<std::uint8_t> push_level_up(const marsnet::Frame& frame, std::int32_t before,
                                                          std::int32_t after);

    std::shared_ptr<offline::TableBlob> tables_;
    offline::AccountRepository accounts_;
    // ponytail: one logged-in account for the whole backend (single-player
    // offline); per-connection sessions would be needed for multiple clients.
    offline::Account account_{};
    std::string account_key_{"anonymous"};
    std::int32_t data_version_{};
    std::string last_session_{}; // session id for unsolicited pushes
    struct ComposeReceipt {
        std::string session;
        std::uint32_t request{}, proto{};
        std::vector<std::uint8_t> body, response;
    };
    std::deque<ComposeReceipt> compose_receipts_;
    std::deque<ComposeReceipt> gift_receipts_;
};

} // namespace x2::server
