#pragma once
#include <atomic>
#include <cstdint>
namespace x2::runtime {
inline std::atomic<bool> lock_hp{false}, lock_hp_ready{false};
inline std::atomic<bool> unity_requested{false}, unity_active{false};
inline std::atomic<bool> recharge_hook_ready{false};
inline std::atomic<std::uint64_t> blocked_hits{0};
}
