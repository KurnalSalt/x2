#include "offline/npc_social.hpp"
#include "offline/game_table.hpp"
#include "offline/mission.hpp"
#include "offline/protocol_builders.hpp"
#include "proto/protobuf.hpp"
#include <algorithm>
#include <ctime>
#include <map>
namespace x2::offline {
namespace {
using proto::Writer;
constexpr std::int64_t epoch = 1514736000;
int stamp(std::int64_t now) { return static_cast<int>(now - epoch); }
const AccountHero *owned(const Account &a, int id) {
  auto h = std::ranges::find(a.heroes, id, &AccountHero::id);
  return h != a.heroes.end() && h->state == 2 ? &*h : nullptr;
}
int favor(const AccountHero &h, const TableBlob *t) {
  if (h.favor_level > 0)
    return h.favor_level;
  GameTable g;
  if (t && g.load(*t, "FavorabilityHero"))
    if (auto r = g.row(h.id))
      return g.int_field(*r, 2).value_or(1);
  return 1;
}
bool available(const Account &a, const TableBlob *t, int hero, int trigger,
               int value) {
  auto h = owned(a, hero);
  if (!h)
    return false;
  switch (trigger) {
  case 1:
    return favor(*h, t) >= value;
  case 5:
    return std::ranges::contains(a.player.cleared_main, value);
  case 13: {
    auto now = std::time(nullptr) + 8 * 3600;
    auto tm = *std::gmtime(&now);
    int date = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
    return value == date;
  }
  default:
    return false;
  }
}
void pair(Writer &w, int field, int k, int v) {
  Writer e;
  e.int32(1, k);
  e.int32(2, v);
  w.message(field, e.data());
}
void blocked(Writer &w, int field, const Account &a) {
  for (const auto &h : a.heroes)
    if (h.state == 2) {
      auto it = a.player.npc_blocked.find(h.id);
      pair(w, field, h.id, it == a.player.npc_blocked.end() ? 0 : it->second);
    }
}
std::vector<const TableRow *> blogs(const Account &a, const TableBlob *t,
                                    const GameTable &g, int hero = 0) {
  std::vector<const TableRow *> out;
  for (const auto &r : g.rows()) {
    int id = g.int_field(r, 2).value_or(0);
    if ((!hero || hero == id) &&
        available(a, t, id, g.int_field(r, 3).value_or(0),
                  g.int_field(r, 4).value_or(0)))
      out.push_back(&r);
  }
  return out;
}
std::vector<std::uint8_t> blog_box(const Account &a, const GameTable &g,
                                   const std::vector<const TableRow *> &rows,
                                   int hero, std::int64_t now) {
  Writer box;
  box.int32(1, hero);
  for (auto r : rows) {
    int id = static_cast<int>(r->key);
    Writer e;
    e.int32(1, id);
    e.int32(2, stamp(now) - 86400);
    auto like = a.player.npc_blog_likes.find(id);
    if (like != a.player.npc_blog_likes.end())
      e.int32(3, like->second);
    auto replies = a.player.npc_blog_replies.find(id);
    if (replies != a.player.npc_blog_replies.end()) {
      std::map<int, Writer> chats;
      for (auto v : replies->second)
        pair(chats[v.chat_group_id], 2, v.reply_id, v.time_offset);
      for (auto &[k, w] : chats) {
        w.int32(1, k);
        e.message(4, w.data());
      }
    }
    box.message(2, e.data());
  }
  return box.data();
}
const TableRow *root(const GameTable &g, int group) {
  for (const auto &r : g.rows())
    if (g.int_field(r, 3) == group && g.int_field(r, 6) == 1)
      return &r;
  return nullptr;
}
void extend(AccountPlayer::NpcLetterProgress &p, const GameTable &g,
            int group) {
  for (int limit = 0; limit < 128 && !p.chain.empty(); ++limit) {
    auto r = g.row(p.chain.back());
    if (!r || !g.ints_field(*r, 11).empty())
      break;
    auto next = g.ints_field(*r, 13);
    if (next.size() != 1 || std::ranges::contains(p.chain, next[0]))
      break;
    auto n = g.row(next[0]);
    if (!n || g.int_field(*n, 3) != group)
      break;
    p.chain.push_back(next[0]);
  }
}
std::vector<std::uint8_t> letter_box(const Account &a, const TableBlob *t,
                                     int hero, std::int64_t now) {
  Writer box;
  box.int32(1, hero);
  GameTable g;
  if (!t || !g.load(*t, "PrivateMail"))
    return box.data();
  int last = 0;
  for (const auto &r : g.rows()) {
    if (g.int_field(r, 6) != 1 || g.int_field(r, 2) != hero ||
        !available(a, t, hero, g.int_field(r, 4).value_or(0),
                   g.int_field(r, 5).value_or(0)))
      continue;
    int id = g.int_field(r, 3).value_or(0);
    auto it = a.player.npc_letters.find(id);
    auto p = it == a.player.npc_letters.end()
                 ? AccountPlayer::NpcLetterProgress{}
                 : it->second;
    if (p.chain.empty())
      p.chain.push_back(static_cast<int>(r.key));
    extend(p, g, id);
    Writer e;
    e.int32(1, p.start ? p.start : stamp(now) - 86400);
    int index = 0;
    for (auto lid : p.chain) {
      auto line = g.row(lid);
      if (!line || g.int_field(*line, 3) != id)
        continue;
      Writer l;
      l.int32(1, lid);
      auto rp = p.replies.find(lid);
      l.int32(2, rp == p.replies.end() ? 0 : rp->second);
      l.int32(3, index++);
      e.message(2, l.data());
    }
    e.int32(3, p.ended ? 1 : 0);
    box.message(2, e.data());
    last = std::max(last, p.last);
  }
  box.int32(3, last);
  return box.data();
}
bool has_groups(const std::vector<std::uint8_t> &b) {
  proto::Reader r{b};
  while (!r.eof()) {
    auto f = r.field();
    if (!f)
      break;
    if (f->number == 2)
      return true;
    r.skip(f->wire_type);
  }
  return false;
}
void roll_favor(Account &a, const TableBlob *t, std::int64_t now) {
  int day = static_cast<int>((now + 8 * 3600) / 86400);
  if (a.player.favor_task_day == day) {
    // The selection page requires WAITING (0). UNLOCK (1) enters the
    // task page but neither offers a selection nor enables the Go button.
    for (auto &[id, state] : a.player.favor_tasks)
      if (state == 1)
        state = 0;
    return;
  }
  a.player.favor_task_day = day;
  a.player.favor_tasks.clear();
  for (auto it = a.player.favor_progress.begin();
       it != a.player.favor_progress.end();)
    if (it->first >= 635000 && it->first < 640000)
      it = a.player.favor_progress.erase(it);
    else
      ++it;
  GameTable g, c;
  if (!t || !g.load(*t, "FavorabilityDailyTask") ||
      !c.load(*t, "TaskCondition"))
    return;
  std::map<int, std::vector<int>> byhero;
  for (const auto &r : g.rows()) {
    int h = g.int_field(r, 3).value_or(0);
    auto cr = c.row(g.int_field(r, 2).value_or(0));
    if (owned(a, h) && cr && c.int_field(*cr, 2) == 11)
      byhero[h].push_back(static_cast<int>(r.key));
  }
  for (auto &[h, ids] : byhero)
    if (!ids.empty() && a.player.favor_tasks.size() < 6)
      a.player.favor_tasks[ids[day % ids.size()]] = 0;
}
int progress(const Account &a, int id) {
  auto it = a.player.favor_progress.find(id);
  return it == a.player.favor_progress.end() ? 0 : it->second;
}
void add_favor(AccountHero &h, const TableBlob *t, int exp) {
  GameTable levels, heroes;
  if (!t || !levels.load(*t, "FavorabilityLevel") ||
      !heroes.load(*t, "FavorabilityHero"))
    return;
  h.favor_level = favor(h, t);
  int limit = 20;
  if (auto r = heroes.row(h.id))
    limit = heroes.int_field(*r, 3).value_or(20);
  h.favor_exp = clamp_add(h.favor_exp, exp);
  while (h.favor_level < limit) {
    auto row = levels.row(h.favor_level), next = levels.row(h.favor_level + 1);
    if (!next)
      break;
    int cost = levels.int_field(*next, 2).value_or(0);
    if (cost <= 0 || h.favor_exp < cost)
      break;
    if (row && levels.int_field(*row, 3).value_or(0)) {
      h.favor_exp = cost;
      break;
    }
    h.favor_exp -= cost;
    ++h.favor_level;
  }
  if (h.favor_level >= limit)
    h.favor_exp = 0;
}
} // namespace
std::vector<std::uint8_t> query_npc_blog(const Account &a, const TableBlob *t,
                                         std::int64_t now) {
  Writer b;
  b.int32(1, 10);
  GameTable g;
  if (t && g.load(*t, "FavorabilityBlog")) {
    std::map<int, std::vector<const TableRow *>> groups;
    for (auto r : blogs(a, t, g))
      groups[g.int_field(*r, 2).value_or(0)].push_back(r);
    for (auto &[h, rows] : groups)
      b.message(2, blog_box(a, g, rows, h, now));
  }
  blocked(b, 3, a);
  return b.data();
}
std::vector<std::uint8_t> query_npc_letter(const Account &a, const TableBlob *t,
                                           std::int64_t now) {
  Writer b;
  b.int32(1, 10);
  for (const auto &h : a.heroes)
    if (h.state == 2) {
      auto box = letter_box(a, t, h.id, now);
      if (has_groups(box))
        b.message(2, box);
    }
  blocked(b, 4, a);
  return b.data();
}
std::vector<std::uint8_t> update_private_letter(Account &a, const TableBlob *t,
                                                int hero, int letter,
                                                bool ended, std::int64_t now) {
  return reply_private_letter(a, t, hero, 0, letter, 0, ended, now);
}
std::vector<std::uint8_t> reply_private_letter(Account &a, const TableBlob *t,
                                               int hero, int group, int letter,
                                               int reply, bool ended,
                                               std::int64_t now) {
  Writer b;
  GameTable g;
  bool ok = false;
  if (t && g.load(*t, "PrivateMail")) {
    auto r = g.row(letter);
    if (r && g.int_field(*r, 2) == hero) {
      int id = g.int_field(*r, 3).value_or(0);
      auto head = root(g, id);
      if (head && (!group || group == id) &&
          available(a, t, hero, g.int_field(*head, 4).value_or(0),
                    g.int_field(*head, 5).value_or(0))) {
        auto p = a.player.npc_letters.contains(id)
                     ? a.player.npc_letters.at(id)
                     : AccountPlayer::NpcLetterProgress{};
        if (p.chain.empty()) {
          p.chain.push_back(static_cast<int>(head->key));
          p.start = stamp(now) - 86400;
        }
        extend(p, g, id);
        ok = std::ranges::contains(p.chain, letter);
        if (ok && reply) {
          auto choices = g.ints_field(*r, 11), next = g.ints_field(*r, 13);
          auto choice = std::ranges::find(choices, reply);
          auto old = p.replies.find(letter);
          ok = choice != choices.end() &&
               (old == p.replies.end() || old->second == reply);
          if (ok) {
            p.replies[letter] = reply;
            auto index = static_cast<std::size_t>(choice - choices.begin());
            if (index < next.size()) {
              auto n = g.row(next[index]);
              if (n && g.int_field(*n, 3) == id &&
                  !std::ranges::contains(p.chain, next[index]))
                p.chain.push_back(next[index]);
            }
            extend(p, g, id);
          }
        }
        if (ok) {
          p.last = stamp(now);
          p.ended = ended;
          a.player.npc_letters[id] = std::move(p);
        }
      }
    }
  }
  b.int32(1, ok ? 10 : 13);
  b.message(2, letter_box(a, t, hero, now));
  blocked(b, 3, a);
  if (group) {
    b.int32(4, hero);
    b.int32(5, group);
    b.int32(6, letter);
    b.int32(7, reply);
  }
  return b.data();
}
std::vector<std::uint8_t> toggle_npc_block(Account &a, int hero) {
  Writer b;
  bool ok = owned(a, hero);
  if (ok)
    a.player.npc_blocked[hero] = a.player.npc_blocked[hero] ? 0 : 1;
  b.int32(1, ok ? 10 : 13);
  blocked(b, 2, a);
  return b.data();
}
std::vector<std::uint8_t> like_npc_blog(Account &a, const TableBlob *t,
                                        int hero, int id, std::int64_t now) {
  Writer b;
  GameTable g;
  auto rows = t && g.load(*t, "FavorabilityBlog")
                  ? blogs(a, t, g, hero)
                  : std::vector<const TableRow *>{};
  bool ok = std::ranges::any_of(
      rows, [&](auto r) { return r->key == static_cast<std::uint64_t>(id); });
  if (ok) {
    if (a.player.npc_blog_likes.contains(id))
      a.player.npc_blog_likes.erase(id);
    else
      a.player.npc_blog_likes[id] = stamp(now);
  }
  b.int32(1, ok ? 10 : 13);
  b.message(2, blog_box(a, g, rows, hero, now));
  blocked(b, 3, a);
  return b.data();
}
std::vector<std::uint8_t> reply_npc_blog(Account &a, const TableBlob *t,
                                         int hero, int id, int chat, int reply,
                                         std::int64_t now) {
  Writer b;
  GameTable g;
  auto rows = t && g.load(*t, "FavorabilityBlog")
                  ? blogs(a, t, g, hero)
                  : std::vector<const TableRow *>{};
  auto row = std::ranges::find_if(
      rows, [&](auto r) { return r->key == static_cast<std::uint64_t>(id); });
  bool ok = row != rows.end() &&
            std::ranges::contains(g.ints_field(**row, 13), reply);
  if (ok) {
    auto &rs = a.player.npc_blog_replies[id];
    auto old = std::ranges::find(rs, chat,
                                 &AccountPlayer::NpcBlogReply::chat_group_id);
    if (old == rs.end())
      rs.push_back({reply, stamp(now), chat});
    else
      ok = old->reply_id == reply;
  }
  b.int32(1, ok ? 10 : 13);
  b.message(2, blog_box(a, g, rows, hero, now));
  blocked(b, 3, a);
  return b.data();
}
std::vector<std::uint8_t> favor_task_reply(Account &a, const TableBlob *t,
                                           int type, std::int64_t now) {
  roll_favor(a, t, now);
  Writer b;
  b.int32(1, 10);
  b.int32(2, type);
  GameTable g, c;
  if (t &&
      g.load(*t,
             type == 6 ? "FavorabilityDailyTask" : "FavorabilitySpecialTask") &&
      c.load(*t, "TaskCondition"))
    for (const auto &r : g.rows()) {
      int id = static_cast<int>(r.key), h = g.int_field(r, 3).value_or(0);
      if (!owned(a, h) || (type == 6 && !a.player.favor_tasks.contains(id)))
        continue;
      auto cr = c.row(g.int_field(r, 2).value_or(0));
      int target = cr ? std::max(1, c.int_field(*cr, 5).value_or(1)) : 1;
      int current = progress(a, id);
      int state = type == 6 ? a.player.favor_tasks[id] : 2;
      if (type == 5 &&
          std::ranges::contains(a.player.favor_special_claimed, id))
        state = 4;
      if (state == 2 && current >= target)
        state = 3;
      Writer w;
      w.int32(1, id);
      w.int32(2, state);
      w.int32(3, std::min(current, target));
      w.int32(4, stamp(now));
      w.int32(5, state == 4 ? 1 : 0);
      b.message(3, w.data());
    }
  return b.data();
}
bool accept_favor_tasks(Account &a, const TableBlob *t,
                        const std::vector<int> &ids, int type,
                        std::int64_t now) {
  roll_favor(a, t, now);
  if (type != 6 || ids.empty() || ids.size() > 3)
    return false;
  auto candidate = a.player.favor_tasks;
  for (int id : ids) {
    auto it = candidate.find(id);
    if (it == candidate.end() || it->second == 4)
      return false;
    it->second = 2;
  }
  if (std::ranges::count_if(candidate, [](auto p) { return p.second >= 2; }) >
      3)
    return false;
  a.player.favor_tasks = std::move(candidate);
  return true;
}
void count_favor_battle(Account &a, const TableBlob &t,
                        const CheckoutRequest &req) {
  if (!req.success)
    return;
  roll_favor(a, &t, std::time(nullptr));
  GameTable c;
  if (!c.load(t, "TaskCondition"))
    return;
  for (auto name : {"FavorabilityDailyTask", "FavorabilitySpecialTask"}) {
    GameTable g;
    if (!g.load(t, name))
      continue;
    for (const auto &r : g.rows()) {
      int id = static_cast<int>(r.key), hero = g.int_field(r, 3).value_or(0);
      if (!std::ranges::contains(req.heroes, hero))
        continue;
      if (id < 640000 &&
          (!a.player.favor_tasks.contains(id) || a.player.favor_tasks[id] != 2))
        continue;
      auto cr = c.row(g.int_field(r, 2).value_or(0));
      if (!cr)
        continue;
      int type = c.int_field(*cr, 2).value_or(0);
      auto sections = c.ints_field(*cr, 4);
      bool match = type == 11 && std::ranges::contains(sections, req.section);
      if (type == 25 && req.expert_mode)
        match = true;
      if (match)
        a.player.favor_progress[id] = clamp_add(progress(a, id), 1);
    }
  }
}
TaskClaim claim_favor_task(Account &a, const TableBlob *t, int id, int type,
                           std::int64_t now) {
  if (type != 5 && type != 6)
    return {};
  roll_favor(a, t, now);
  GameTable g, c, gifts;
  if (!t ||
      !g.load(*t, type == 6 ? "FavorabilityDailyTask"
                            : "FavorabilitySpecialTask") ||
      !c.load(*t, "TaskCondition") || !gifts.load(*t, "Gift"))
    return {};
  auto r = g.row(id);
  if (!r || !owned(a, g.int_field(*r, 3).value_or(0)))
    return {};
  if (type == 6 &&
      (!a.player.favor_tasks.contains(id) || a.player.favor_tasks[id] < 2))
    return {};
  if ((type == 6 && a.player.favor_tasks[id] == 4) ||
      (type == 5 && std::ranges::contains(a.player.favor_special_claimed, id)))
    return {.ok = true, .already = true};
  auto cr = c.row(g.int_field(*r, 2).value_or(0));
  if (!cr || progress(a, id) < std::max(1, c.int_field(*cr, 5).value_or(1)))
    return {};
  auto candidate = a;
  std::vector<AccountItem> rewards;
  int group = g.int_field(*r, type == 6 ? 5 : 8).value_or(0);
  if (group) {
    if (!gifts.row(group))
      return {};
    expand_gift(gifts, group, rewards);
  }
  auto paid = pay_rewards(candidate, t, rewards);
  int hero = g.int_field(*r, 3).value_or(0);
  auto h = std::ranges::find(candidate.heroes, hero, &AccountHero::id);
  add_favor(*h, t, g.int_field(*r, type == 6 ? 4 : 7).value_or(0));
  if (type == 6)
    candidate.player.favor_tasks[id] = 4;
  else
    candidate.player.favor_special_claimed.push_back(id);
  int ups = paid.role_exp ? settle_account_level(candidate, t) : 0;
  a = std::move(candidate);
  return {.ok = true,
          .player = true,
          .level_ups = ups,
          .shown = std::move(paid.shown),
          .bag = std::move(paid.bag),
          .equips = std::move(paid.equips)};
}
} // namespace x2::offline
