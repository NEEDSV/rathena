// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED marble board - G-1(b) TEST BUILD ONLY kill injection.
//
// This file is NOT part of the repository src/ tree. apply_test_hooks.py copies it into a scratch
// copy of the sources, and the scratch build passes /p:DefineConstants=NEED_MARBLE_TEST_HOOKS.
// Even inside a test build every entry point refuses unless
//   - the map-server was started with the environment variable NEED_MARBLE_TEST_HOOKS=1,
//   - the map-server database is ragnarok_mbqa (the isolated G2 schema),
//   - the caller has group level 99.

#ifdef NEED_MARBLE_TEST_HOOKS

#include <cinttypes>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <common/showmsg.hpp>
#include <common/sql.hpp>

#include "map.hpp"
#include "mob.hpp"
#include "need_event_hunt.hpp"
#include "need_marble.hpp"
#include "need_marble_logic.hpp"
#include "pc.hpp"
#include "script.hpp"
#include "status.hpp"

int32 need_marble_test_kill(map_session_data* sd, int32 radius, int32 max_count);
int32 need_marble_test_kill_as(map_session_data* sd, const char* name, int32 radius, int32 max_count);
int32 need_marble_test_view_of(map_session_data* sd, const char* name);
int32 need_marble_test_hunt_feed(map_session_data* sd, int32 count);

namespace {

bool g_checked = false;
bool g_allowed = false;

bool test_guard(map_session_data* sd) {
	if (!g_checked) {
		g_checked = true;
		const char* env = getenv("NEED_MARBLE_TEST_HOOKS");
		const bool env_ok = env != nullptr && strcmp(env, "1") == 0;
		bool db_ok = false;
		if (env_ok && mmysql_handle != nullptr && SQL_SUCCESS == Sql_QueryStr(mmysql_handle, "SELECT DATABASE()") &&
			SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* name = nullptr;
			db_ok = SQL_SUCCESS == Sql_GetData(mmysql_handle, 0, &name, nullptr) && name != nullptr && strcmp(name, "ragnarok_mbqa") == 0;
		}
		if (mmysql_handle != nullptr)
			Sql_FreeResult(mmysql_handle);
		g_allowed = env_ok && db_ok;
		ShowWarning("need_marble_test: G-1(b) test hooks %s (env=%d, isolated db=%d).\n", g_allowed ? "ENABLED" : "refused",
			env_ok ? 1 : 0, db_ok ? 1 : 0);
	}
	if (!g_allowed) {
		ShowError("need_marble_test: refused - environment / database guard.\n");
		return false;
	}
	if (sd == nullptr || pc_get_group_level(sd) < 99) {
		ShowError("need_marble_test: refused - caller is not group level 99.\n");
		return false;
	}
	return true;
}

int32 collect_mob_id_sub(block_list* bl, va_list ap) {
	auto* out = va_arg(ap, std::vector<int32>*);
	out->push_back(bl->id);
	return 1;
}

std::vector<int32> mobs_around(map_session_data* sd, int32 radius) {
	std::vector<int32> ids;
	if (radius <= 0)
		map_foreachinmap(collect_mob_id_sub, sd->m, BL_MOB, &ids);
	else
		map_foreachinallrange(collect_mob_id_sub, sd, static_cast<int16>(radius), BL_MOB, &ids);
	return ids;
}

bool eligible(map_session_data* sd, mob_data* md) {
	return md != nullptr && md->prev != nullptr && md->status.hp > 0 &&
		need_event_hunt_normal_field_target(sd, md, 0, need_marble_policy_value(need_marble::P_HUNT_LEVEL_DIFF));
}

int64 view_kills(map_session_data* sd) {
	need_marble_view(sd);
	return pc_readreg(sd, add_str("@MB_HUNT_KILLS"));
}

/// Kills with sd as the damage source; counted = growth of sd's own marble count.
int32 kill_impl(map_session_data* sd, int32 radius, int32 max_count, int64& counted);

}  // namespace

/// Kills up to max_count eligible field mobs (radius 0 = whole map) through the normal damage path:
/// status_damage(src = sd) -> mob_damage / mob_log_damage -> mob_dead -> need_marble_hunt_on_kill.
/// Returns the number killed, -1 when refused. @MBQA_COUNTED = growth of the caller's own marble count.
int32 need_marble_test_kill(map_session_data* sd, int32 radius, int32 max_count) {
	if (!test_guard(sd))
		return -1;
	const int64 counted_reg = add_str("@MBQA_COUNTED");
	pc_setreg(sd, counted_reg, -1);
	int64 counted = -1;
	const int32 killed = kill_impl(sd, radius, max_count, counted);
	pc_setreg(sd, counted_reg, counted);
	return killed;
}

/// G2-A V18-C: the same kills with another online character as the damage source. A character with a
/// captcha on screen cannot type commands, so the GM drives its kills. @MBQA_COUNTED (on the caller) =
/// growth of that character's marble count. -3 when the character is not online.
int32 need_marble_test_kill_as(map_session_data* sd, const char* name, int32 radius, int32 max_count) {
	if (!test_guard(sd))
		return -1;
	const int64 counted_reg = add_str("@MBQA_COUNTED");
	pc_setreg(sd, counted_reg, -1);
	map_session_data* tsd = map_nick2sd(name, false);
	if (tsd == nullptr)
		return -3;
	int64 counted = -1;
	const int32 killed = kill_impl(tsd, radius, max_count, counted);
	pc_setreg(sd, counted_reg, counted);
	return killed;
}

/// G2-A V18-C: today's marble kill count (DB + in-memory batch) of another online character, -3 if offline.
int32 need_marble_test_view_of(map_session_data* sd, const char* name) {
	if (!test_guard(sd))
		return -1;
	map_session_data* tsd = map_nick2sd(name, false);
	return tsd == nullptr ? -3 : static_cast<int32>(view_kills(tsd));
}

namespace {

int32 kill_impl(map_session_data* sd, int32 radius, int32 max_count, int64& counted) {
	if (pc_isdead(sd)) {
		// A dead top damager credits nobody: refuse instead of reporting kills that cannot count.
		ShowWarning("need_marble_test: kill refused - damage source is dead.\n");
		return -2;
	}
	int32 killed = 0;
	counted = 0;
	const int64 reg = add_str("@MB_HUNT_KILLS");
	for (int32 id : mobs_around(sd, radius)) {
		if (killed >= max_count)
			break;
		mob_data* md = map_id2md(id);   // re-resolved: an earlier kill may have changed the map
		if (!eligible(sd, md))
			continue;
		// Diagnostic: the marble count (DB + in-memory batch) must grow by exactly one per kill.
		need_marble_view(sd);
		const int64 before = pc_readreg(sd, reg);
		const int32 mob_id = md->mob_id, lv = md->level, x = md->x, y = md->y, hp = md->status.hp;
		status_damage(sd, md, md->status.hp, 0, 0, 0, 0, 0);
		need_marble_view(sd);
		const int64 after = pc_readreg(sd, reg);
		if (after - before != 1)
			ShowWarning("need_marble_test: kill #%d not counted once (delta %" PRId64 ") mob_id=%d lv=%d at %d,%d hp=%d char_lv=%d dead=%d\n",
				killed + 1, after - before, mob_id, lv, x, y, hp, sd->status.base_level, pc_isdead(sd) ? 1 : 0);
		counted += after - before;
		killed++;
	}
	return killed;
}

}  // namespace

/// Ledger-level feed: calls need_marble_hunt_on_kill count times with one live eligible mob as the
/// template. The mob does not die; mob_dead, the damage log and the party rule are skipped.
/// Returns the number fed, 0 when no eligible mob is on the map, -1 when refused, -2 when the caller is dead.
/// @MBQA_COUNTED = growth of the marble count (DB + in-memory batch) over the feed, read by the harness.
int32 need_marble_test_hunt_feed(map_session_data* sd, int32 count) {
	if (!test_guard(sd))
		return -1;
	const int64 counted_reg = add_str("@MBQA_COUNTED");
	pc_setreg(sd, counted_reg, -1);
	if (pc_isdead(sd)) {
		ShowWarning("need_marble_test: feed refused - caller is dead.\n");
		return -2;
	}
	const int64 reg = add_str("@MB_HUNT_KILLS");
	for (int32 id : mobs_around(sd, 0)) {
		mob_data* md = map_id2md(id);
		if (!eligible(sd, md))
			continue;
		need_marble_view(sd);
		const int64 before = pc_readreg(sd, reg);
		for (int32 i = 0; i < count; i++)
			need_marble_hunt_on_kill(sd, md, 0);
		need_marble_view(sd);
		pc_setreg(sd, counted_reg, pc_readreg(sd, reg) - before);
		return count;
	}
	return 0;
}

#endif  // NEED_MARBLE_TEST_HOOKS
