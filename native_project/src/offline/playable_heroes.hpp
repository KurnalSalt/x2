#pragma once
#include "offline/game_table.hpp"
#include <set>
namespace x2::offline {
// UnitType=1 also contains NPCs, old prototypes, and combat-only stand-ins.
// A lobby hero needs a released PlayerAttrib prototype and its progression data.
inline std::set<std::int32_t> playable_heroes(const TableBlob* tables) {
    std::set<std::int32_t> ids;
    if (!tables) return ids;
    GameTable attrib, units, stages, skills;
    if (!attrib.load(*tables,"PlayerAttrib") || !units.load(*tables,"UnitBase") ||
        !stages.load(*tables,"PlayerStage") || !skills.load(*tables,"SkillBase")) return ids;
    for (const auto& row : attrib.rows()) {
        if (row.key < 1000 || row.key >= 2000 || attrib.int_field(row,10).value_or(0)!=1 ||
            attrib.int_field(row,8).value_or(0)<=0 || attrib.int_field(row,11).value_or(0)<=0 ||
            attrib.int_field(row,13).value_or(0)<=0) continue;
        const auto unit = units.row(row.key);
        if (!unit || units.int_field(*unit,20).value_or(0)!=1 || !stages.row(attrib.int_field(row,7).value_or(0))) continue;
        const auto basic = units.ints_field(*unit,22);
        if (basic.empty() || !skills.row(basic.front())) continue;
        ids.insert(static_cast<std::int32_t>(row.key));
    }
    return ids;
}
}
