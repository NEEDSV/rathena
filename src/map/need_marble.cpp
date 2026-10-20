// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED marble board - account shared ledger (Phase B/C). See need_marble.hpp.
//
// Transaction rules (ledger design v0.5 sec.4):
// - Only the public entry points below open a transaction, through ledger_tx.
//   ledger_tx refuses to open while another one is active (no nested START TRANSACTION).
// - *_in_tx helpers require an open transaction and never START / COMMIT / ROLLBACK.
// - Any failure inside a transaction rolls the whole transaction back (no partial success).
// - A failed COMMIT means the outcome is unknown: the module fails closed until the
//   map-server restarts and the operator reconciles (design v0.5 sec.8.4 / 13).

#include "need_marble.hpp"

#include <cinttypes>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>
#include <common/timer.hpp>

#include "battle.hpp"
#include "map.hpp"
#include "mob.hpp"
#include "need_event_hunt.hpp"
#include "need_marble_logic.hpp"
#include "npc.hpp"
#include "party.hpp"
#include "path.hpp"
#include "pc.hpp"
#include "script.hpp"

using namespace need_marble;

namespace {

constexpr int32 ACT_HUNT = 1;
constexpr int32 ACT_INSTANCE = 2;
constexpr int32 ACT_BG = 3;

constexpr int32 TX_EARN = 1;
constexpr int32 TX_EXCHANGE = 2;
constexpr int32 TX_SPEND = 3;

constexpr int32 REF_ACTIVITY = 1;
constexpr int32 REF_EXCHANGE = 2;
constexpr int32 REF_ROLL = 3;

constexpr int32 PAY_TILE = 1;
constexpr int32 PAY_LAP = 2;

constexpr int32 ST_IDLE = 0;
constexpr int32 ST_MOVING = 1;
constexpr int32 ST_PAUSED = 2;
constexpr int32 ST_RECOVERY = 3;
constexpr int32 ST_CHOICE = 4;

constexpr int32 REQ_ISSUED = 1;
constexpr int32 REQ_COMMITTED = 2;
constexpr int32 REQ_REJECTED = 3;
constexpr int32 REQ_EXPIRED = 4;

constexpr int32 RESULT_WARP = 100;
constexpr int32 TEST_SEASON_MIN = 900000;
constexpr int32 GM_FORCE_LEVEL = 99;
constexpr int32 TIMER_INTERVAL_MS = 5000;

constexpr const char* NOTIFY_NPC = "NEED_MarbleBoard";
constexpr const char* NOTIFY_EVENT = "NEED_MarbleBoard::OnNotify";

const char* const LEDGER_TABLES[] = {
	"need_marble_season", "need_marble_policy", "need_marble_boot", "need_marble_account_seq",
	"need_marble_wallet", "need_marble_wallet_tx", "need_marble_period", "need_marble_activity",
	"need_marble_dungeon_daily", "need_marble_hunt_progress", "need_marble_hunt_batch",
	"need_marble_exchange_req", "need_marble_state", "need_marble_roll", "need_marble_jump",
	"need_marble_event", "need_marble_cp_payout",
};

// Grade A instances only (INITIAL_BALANCE_V1_0, OPEN_DECISIONS v0.3 N8). Keys as passed to F_NEED_MD_ClearBonus.
struct dungeon_entry {
	const char* key;
	int32 code;
};
const dungeon_entry GRADE_A_DUNGEONS[] = {
	{ "GEFFEN", 1 }, { "CULVERT", 2 }, { "JITTERBUG", 3 }, { "AIRSHIP", 4 }, { "CHARLESTON", 5 },
	{ "DEVILTOWER", 6 }, { "CENTRALLAB", 7 }, { "HORROR", 8 }, { "SARAH", 9 }, { "LASTROOM", 10 },
};

bool g_schema_ok = false;
bool g_fault = false;
bool g_in_tx = false;
uint32 g_boot_id = 0;
policy_values g_pol = policy_defaults();
uint32 g_pol_day = 0;
bool g_pol_ok = false;
int32 g_timer = INVALID_TIMER;
t_tick g_last_bg_sample = 0;

struct hunt_buf {
	uint32 char_id = 0;
	uint64 collect_session = 0;
	bool session_failed = false;
	uint32 batch_no = 0;
	std::map<uint32, uint32> pending;   // logical day -> kills not sent yet
	uint32 frozen_no = 0;
	std::map<uint32, uint32> frozen;    // batch sent at least once, resent unchanged until acknowledged
	t_tick last_flush = 0;
	uint32 done_day = 0;                // every tier of this day is already reached
};
std::unordered_map<uint32, hunt_buf> g_hunt;

struct bg_sample {
	uint32 total = 0;
	uint32 active = 0;
};
std::map<std::pair<uint32, int32>, bg_sample> g_bg;   // (account_id, bg_id)

void fault(const char* why) {
	if (!g_fault)
		ShowError("need_marble: %s - ledger fails closed until restart and operator reconciliation.\n", why);
	g_fault = true;
}

bool sq(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int32 r = Sql_QueryV(mmysql_handle, fmt, ap);
	va_end(ap);
	if (r == SQL_ERROR) {
		Sql_ShowDebug(mmysql_handle);
		return false;
	}
	return true;
}

uint64 affected() {
	return Sql_NumRowsAffected(mmysql_handle);
}

bool next_row() {
	return Sql_NextRow(mmysql_handle) == SQL_SUCCESS;
}

int64 col_i64(size_t c) {
	char* data = nullptr;
	if (Sql_GetData(mmysql_handle, c, &data, nullptr) != SQL_SUCCESS || data == nullptr)
		return 0;
	return strtoll(data, nullptr, 10);
}

uint64 col_u64(size_t c) {
	char* data = nullptr;
	if (Sql_GetData(mmysql_handle, c, &data, nullptr) != SQL_SUCCESS || data == nullptr)
		return 0;
	return strtoull(data, nullptr, 10);
}

void free_result() {
	Sql_FreeResult(mmysql_handle);
}

// The only owner of START TRANSACTION / COMMIT / ROLLBACK in this module.
class ledger_tx {
public:
	ledger_tx() = default;
	ledger_tx(const ledger_tx&) = delete;
	ledger_tx& operator=(const ledger_tx&) = delete;
	~ledger_tx() { rollback(); }

	bool begin() {
		if (g_in_tx) {
			ShowError("need_marble: nested transaction refused.\n");
			return false;
		}
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
			Sql_ShowDebug(mmysql_handle);
			return false;
		}
		g_in_tx = active_ = true;
		return true;
	}

	bool commit() {
		if (!active_)
			return false;
		active_ = g_in_tx = false;
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
			Sql_ShowDebug(mmysql_handle);
			fault("COMMIT failed, outcome unknown");
			return false;
		}
		return true;
	}

	void rollback() {
		if (!active_)
			return;
		active_ = g_in_tx = false;
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "ROLLBACK")) {
			Sql_ShowDebug(mmysql_handle);
			fault("ROLLBACK failed");
		}
	}

private:
	bool active_ = false;
};

#define MARBLE_REQUIRE_TX() \
	do { \
		if (!g_in_tx) { \
			ShowError("need_marble: %s called outside a transaction.\n", __func__); \
			return NEED_MARBLE_INTERNAL; \
		} \
	} while (0)

uint32 today() {
	return logical_day(static_cast<int64_t>(time(nullptr)));
}

int32 P(int32 key) {
	return key >= 0 && key < POLICY_MAX ? g_pol[key] : 0;
}

uint32 season() {
	return static_cast<uint32>(P(P_SEASON_ID));
}

// Re-reads need_marble_policy once per logical day. Invalid values disable the module.
void policy_refresh(bool force) {
	uint32 day = today();
	if (!force && day == g_pol_day)
		return;
	policy_values v = policy_defaults();
	bool loaded = true;
	if (g_schema_ok && !g_in_tx) {
		if (!sq("SELECT `p`.`policy_key`,`p`.`value` FROM `need_marble_policy` `p` "
			"JOIN (SELECT `policy_key`,MAX(`effective_day`) `d` FROM `need_marble_policy` WHERE `effective_day`<='%u' GROUP BY `policy_key`) `m` "
			"ON `m`.`policy_key`=`p`.`policy_key` AND `m`.`d`=`p`.`effective_day`", day)) {
			loaded = false;
		} else {
			while (next_row()) {
				int64 key = col_i64(0);
				if (key >= 0 && key < POLICY_MAX)
					v[static_cast<size_t>(key)] = static_cast<int32>(col_i64(1));
			}
			free_result();
		}
	}
	int32 bad_key = 0;
	int32 err = validate_policy(v, &bad_key);
	g_pol = v;
	g_pol_day = day;
	g_pol_ok = loaded && err == PE_OK;
	if (!loaded)
		ShowError("need_marble: policy table unreadable, marble disabled.\n");
	else if (err != PE_OK)
		ShowError("need_marble: invalid policy (error %d at key %d), marble disabled.\n", err, bad_key);
}

bool ready() {
	if (!g_schema_ok || g_fault || mmysql_handle == nullptr)
		return false;
	policy_refresh(false);
	return g_pol_ok && P(P_ENABLED) == 1;
}

bool schema_check() {
	if (mmysql_handle == nullptr)
		return false;
	std::string list;
	for (const char* t : LEDGER_TABLES) {
		if (!list.empty())
			list += ",";
		list += "'";
		list += t;
		list += "'";
	}
	if (!sq("SELECT COUNT(*) FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA`=DATABASE() AND `ENGINE`='InnoDB' AND `TABLE_NAME` IN (%s)", list.c_str()))
		return false;
	uint64 n = next_row() ? col_u64(0) : 0;
	free_result();
	return n == sizeof(LEDGER_TABLES) / sizeof(LEDGER_TABLES[0]);
}

uint32 boot_id() {
	if (g_boot_id != 0 || g_in_tx)
		return g_boot_id;
	if (!sq("INSERT INTO `need_marble_boot` (`started_at`) VALUES (NOW())"))
		return 0;
	g_boot_id = static_cast<uint32>(Sql_LastInsertId(mmysql_handle));
	if (g_boot_id != 0)
		ShowNotice("need_marble: boot_id %u.\n", g_boot_id);
	return g_boot_id;
}

void outv(map_session_data* sd, const char* name, int64 value) {
	if (sd != nullptr)
		pc_setreg(sd, add_str(name), value);
}

void notify(map_session_data* sd, int32 activity, int32 points) {
	if (sd == nullptr || points <= 0 || npc_name2id(NOTIFY_NPC) == nullptr)
		return;
	outv(sd, "@MB_NOTE_ACT", activity);
	outv(sd, "@MB_NOTE_PTS", points);
	npc_event(sd, NOTIFY_EVENT, 0);
}

bool idle_for(const map_session_data* sd, int32 seconds) {
	return sd->chatID != 0 || sd->state.vending || sd->state.buyingstore ||
		DIFF_TICK(last_tick, sd->idletime) >= static_cast<t_tick>(seconds);
}

// ---------------------------------------------------------------------------
// In-transaction helpers
// ---------------------------------------------------------------------------

int32 season_check_in_tx(uint32 season_id) {
	MARBLE_REQUIRE_TX();
	if (!sq("SELECT `status` FROM `need_marble_season` WHERE `season_id`='%u' LOCK IN SHARE MODE", season_id))
		return NEED_MARBLE_DB_ERROR;
	int64 status = next_row() ? col_i64(0) : 0;
	free_result();
	return status == 1 ? NEED_MARBLE_OK : NEED_MARBLE_SEASON;
}

int32 ensure_rows_in_tx(uint32 aid, uint32 season_id) {
	MARBLE_REQUIRE_TX();
	if (!sq("INSERT IGNORE INTO `need_marble_wallet` (`account_id`,`season_id`,`updated_at`) VALUES ('%u','%u',NOW())", aid, season_id) ||
		!sq("INSERT IGNORE INTO `need_marble_state` (`account_id`,`season_id`,`updated_at`) VALUES ('%u','%u',NOW())", aid, season_id))
		return NEED_MARBLE_DB_ERROR;
	return NEED_MARBLE_OK;
}

struct board_state {
	int32 state = 0, pos = 0, left = 0, die = 0;
	uint64 roll_seq = 0, step_seq = 0, run = 0, jump_seq = 0;
	uint32 start_pass = 0, laps = 0;
};

int32 load_state_in_tx(uint32 aid, board_state& s) {
	MARBLE_REQUIRE_TX();
	if (!sq("SELECT `state`,`pos`,`left_steps`,`die`,`roll_seq`,`step_seq`,`run_session`,`jump_seq`,`start_pass`,`laps` "
		"FROM `need_marble_state` WHERE `account_id`='%u' FOR UPDATE", aid))
		return NEED_MARBLE_DB_ERROR;
	if (!next_row()) {
		free_result();
		return NEED_MARBLE_NOT_FOUND;
	}
	s.state = static_cast<int32>(col_i64(0));
	s.pos = static_cast<int32>(col_i64(1));
	s.left = static_cast<int32>(col_i64(2));
	s.die = static_cast<int32>(col_i64(3));
	s.roll_seq = col_u64(4);
	s.step_seq = col_u64(5);
	s.run = col_u64(6);
	s.jump_seq = col_u64(7);
	s.start_pass = static_cast<uint32>(col_u64(8));
	s.laps = static_cast<uint32>(col_u64(9));
	free_result();
	if (s.pos < 0 || s.pos >= BOARD_TILES)
		return NEED_MARBLE_INTERNAL;
	return NEED_MARBLE_OK;
}

struct wallet_row {
	int64 score = 0, dice = 0;
	uint64 pending_ack = 0;
	int32 locked = 0;
};

int32 load_wallet_in_tx(uint32 aid, uint32 season_id, wallet_row& w) {
	MARBLE_REQUIRE_TX();
	if (!sq("SELECT `score`,`dice`,`pending_ack`,`locked` FROM `need_marble_wallet` WHERE `account_id`='%u' AND `season_id`='%u' FOR UPDATE",
		aid, season_id))
		return NEED_MARBLE_DB_ERROR;
	if (!next_row()) {
		free_result();
		return NEED_MARBLE_NOT_FOUND;
	}
	w.score = col_i64(0);
	w.dice = col_i64(1);
	w.pending_ack = col_u64(2);
	w.locked = static_cast<int32>(col_i64(3));
	free_result();
	return NEED_MARBLE_OK;
}

// Activity credit (design v0.5 sec.4.2). The activity row is the duplicate guard of the
// achievement; the period counters (EARN only) are the cap guard.
int32 credit_in_tx(uint32 aid, uint32 cid, uint32 season_id, int32 act, uint64 ka, uint64 kb, uint64 kc,
	uint32 day, int32 requested, bool row_exists, int32& credited) {
	MARBLE_REQUIRE_TX();
	credited = 0;
	if (!row_exists) {
		if (!sq("INSERT IGNORE INTO `need_marble_activity` (`account_id`,`season_id`,`activity`,`key_a`,`key_b`,`key_c`,"
			"`char_id`,`period_day`,`requested`,`credited`,`outcome`,`created_at`) "
			"VALUES ('%u','%u','%d','%" PRIu64 "','%" PRIu64 "','%" PRIu64 "','%u','%u','%d','0','0',NOW())",
			aid, season_id, act, ka, kb, kc, cid, day, requested))
			return NEED_MARBLE_DB_ERROR;
		if (affected() == 0)
			return NEED_MARBLE_ALREADY;
	}
	if (ensure_rows_in_tx(aid, season_id) != NEED_MARBLE_OK)
		return NEED_MARBLE_DB_ERROR;
	if (!sq("INSERT IGNORE INTO `need_marble_period` (`account_id`,`season_id`,`period_day`,`activity`) "
		"VALUES ('%u','%u','%u','0'),('%u','%u','%u','%d')", aid, season_id, day, aid, season_id, day, act))
		return NEED_MARBLE_DB_ERROR;
	if (!sq("SELECT `activity`,`earned` FROM `need_marble_period` WHERE `account_id`='%u' AND `season_id`='%u' "
		"AND `period_day`='%u' AND `activity` IN (0,'%d') FOR UPDATE", aid, season_id, day, act))
		return NEED_MARBLE_DB_ERROR;
	int32 earned_total = 0, earned_act = 0;
	while (next_row()) {
		if (col_i64(0) == 0)
			earned_total = static_cast<int32>(col_i64(1));
		else
			earned_act = static_cast<int32>(col_i64(1));
	}
	free_result();

	int32 cap_act = act == ACT_HUNT ? P(P_DAILY_HUNT_CAP) : act == ACT_INSTANCE ? P(P_DAILY_INSTANCE_CAP) : P(P_DAILY_BG_CAP);
	credited = credit_amount(requested, earned_act, cap_act, earned_total, P(P_DAILY_TOTAL_CAP));

	if (!sq("UPDATE `need_marble_activity` SET `credited`='%d',`outcome`='%d' WHERE `account_id`='%u' AND `season_id`='%u' "
		"AND `activity`='%d' AND `key_a`='%" PRIu64 "' AND `key_b`='%" PRIu64 "' AND `key_c`='%" PRIu64 "'",
		credited, credited > 0 ? 1 : 2, aid, season_id, act, ka, kb, kc) || affected() != 1)
		return NEED_MARBLE_DB_ERROR;
	if (credited <= 0)
		return NEED_MARBLE_OK;

	if (!sq("INSERT INTO `need_marble_wallet_tx` (`account_id`,`season_id`,`kind`,`ref_type`,`ref_a`,`ref_b`,`ref_c`,`ref_d`,"
		"`char_id`,`d_score`,`d_dice`,`period_day`,`created_at`) "
		"VALUES ('%u','%u','%d','%d','%d','%" PRIu64 "','%" PRIu64 "','%" PRIu64 "','%u','%d','0','%u',NOW())",
		aid, season_id, TX_EARN, REF_ACTIVITY, act, ka, kb, kc, cid, credited, day))
		return NEED_MARBLE_DB_ERROR;
	if (!sq("UPDATE `need_marble_wallet` SET `score`=`score`+'%d',`updated_at`=NOW() WHERE `account_id`='%u' AND `season_id`='%u' AND `locked`='0'",
		credited, aid, season_id))
		return NEED_MARBLE_DB_ERROR;
	if (affected() != 1)
		return NEED_MARBLE_LOCKED;
	if (!sq("UPDATE `need_marble_period` SET `earned`=`earned`+'%d' WHERE `account_id`='%u' AND `season_id`='%u' "
		"AND `period_day`='%u' AND `activity` IN (0,'%d')", credited, aid, season_id, day, act) || affected() != 2)
		return NEED_MARBLE_DB_ERROR;
	return NEED_MARBLE_OK;
}

// CP decision row (design v0.5 sec.16). Status 1 = DECIDED; nothing applies it in this build.
int32 payout_decide_in_tx(uint32 aid, uint32 cid, int32 src_type, uint64 a, uint64 b, int32 amount) {
	MARBLE_REQUIRE_TX();
	if (amount <= 0)
		return NEED_MARBLE_OK;
	if (!sq("INSERT INTO `need_marble_account_seq` (`account_id`,`cp_payout`) VALUES ('%u','1') "
		"ON DUPLICATE KEY UPDATE `cp_payout`=`cp_payout`+1", aid))
		return NEED_MARBLE_DB_ERROR;
	if (!sq("SELECT `cp_payout` FROM `need_marble_account_seq` WHERE `account_id`='%u'", aid))
		return NEED_MARBLE_DB_ERROR;
	uint64 payout_no = next_row() ? col_u64(0) : 0;
	free_result();
	if (payout_no == 0)
		return NEED_MARBLE_DB_ERROR;
	if (!sq("INSERT INTO `need_marble_cp_payout` (`account_id`,`src_type`,`src_a`,`src_b`,`payout_no`,`amount`,`status`,`char_id`,`decided_at`) "
		"VALUES ('%u','%d','%" PRIu64 "','%" PRIu64 "','%" PRIu64 "','%d','1','%u',NOW())",
		aid, src_type, a, b, payout_no, amount, cid))
		return NEED_MARBLE_DB_ERROR;
	return NEED_MARBLE_OK;
}

// ---------------------------------------------------------------------------
// Hunt
// ---------------------------------------------------------------------------

// One checkpoint = one transaction: batch rows, period kill count, milestones, credit and
// period counters move together (design v0.5 sec.7.3).
int32 hunt_checkpoint(uint32 aid, uint32 cid, uint64 collect_session, uint32 batch_no,
	const std::map<uint32, uint32>& batch, int32& credited_total, uint32& maxed_day) {
	credited_total = 0;
	maxed_day = 0;
	uint32 season_id = season();
	ledger_tx tx;
	if (!tx.begin())
		return NEED_MARBLE_DB_ERROR;
	int32 rc = season_check_in_tx(season_id);
	if (rc != NEED_MARBLE_OK)
		return rc;

	const int32 tier_kills = P(P_HUNT_TIER_KILLS), tier_points = P(P_HUNT_TIER_POINTS), max_tiers = P(P_HUNT_MAX_TIERS);
	for (const auto& entry : batch) {
		const uint32 day = entry.first, kills = entry.second;
		if (!sq("INSERT IGNORE INTO `need_marble_hunt_batch` (`account_id`,`collect_session`,`batch_no`,`period_day`,`season_id`,`kills`,`char_id`,`created_at`) "
			"VALUES ('%u','%" PRIu64 "','%u','%u','%u','%u','%u',NOW())", aid, collect_session, batch_no, day, season_id, kills, cid))
			return NEED_MARBLE_DB_ERROR;
		if (affected() == 0)
			return NEED_MARBLE_ALREADY;   // the whole batch was applied before (it is all or nothing)
		if (!sq("INSERT IGNORE INTO `need_marble_hunt_progress` (`account_id`,`season_id`,`period_day`,`updated_at`) VALUES ('%u','%u','%u',NOW())",
			aid, season_id, day))
			return NEED_MARBLE_DB_ERROR;
		if (!sq("SELECT `kills`,`next_tier` FROM `need_marble_hunt_progress` WHERE `account_id`='%u' AND `season_id`='%u' AND `period_day`='%u' FOR UPDATE",
			aid, season_id, day) || !next_row()) {
			free_result();
			return NEED_MARBLE_DB_ERROR;
		}
		uint64 total = col_u64(0) + kills;
		int32 next_tier = static_cast<int32>(col_i64(1));
		free_result();
		if (total > 0xFFFFFFFFull)
			total = 0xFFFFFFFFull;
		int32 new_next = hunt_next_tier(static_cast<uint32>(total), next_tier, tier_kills, max_tiers);
		for (int32 tier = next_tier; tier < new_next; tier++) {
			int32 credited = 0;
			rc = credit_in_tx(aid, cid, season_id, ACT_HUNT, day, static_cast<uint64>(tier), 0, day, tier_points, false, credited);
			if (rc == NEED_MARBLE_ALREADY)
				return NEED_MARBLE_INTERNAL;   // milestone row without matching progress: refuse, operator check
			if (rc != NEED_MARBLE_OK)
				return rc;
			credited_total += credited;
		}
		if (!sq("UPDATE `need_marble_hunt_progress` SET `kills`='%u',`next_tier`='%d',`updated_at`=NOW() "
			"WHERE `account_id`='%u' AND `season_id`='%u' AND `period_day`='%u'",
			static_cast<uint32>(total), new_next, aid, season_id, day) || affected() != 1)
			return NEED_MARBLE_DB_ERROR;
		if (new_next > max_tiers)
			maxed_day = day;
	}
	if (!tx.commit())
		return NEED_MARBLE_DB_ERROR;
	return NEED_MARBLE_OK;
}

bool hunt_issue_session(uint32 aid, uint64& out) {
	ledger_tx tx;
	if (!tx.begin())
		return false;
	if (!sq("INSERT INTO `need_marble_account_seq` (`account_id`,`collect_session`) VALUES ('%u','1') "
		"ON DUPLICATE KEY UPDATE `collect_session`=`collect_session`+1", aid) ||
		!sq("SELECT `collect_session` FROM `need_marble_account_seq` WHERE `account_id`='%u'", aid))
		return false;
	out = next_row() ? col_u64(0) : 0;
	free_result();
	return out != 0 && tx.commit();
}

void hunt_flush(map_session_data* sd, uint32 aid, hunt_buf& b) {
	if (b.session_failed) {
		b.pending.clear();
		b.frozen.clear();
		return;
	}
	if (b.collect_session == 0 && !hunt_issue_session(aid, b.collect_session)) {
		b.session_failed = true;
		b.collect_session = 0;
		ShowWarning("need_marble: hunt collect session not issued (account %u), hunt credit off for this login.\n", aid);
		return;
	}
	for (int32 round = 0; round < 2; round++) {
		if (b.frozen.empty()) {
			if (b.pending.empty())
				break;
			b.frozen.swap(b.pending);
			b.frozen_no = ++b.batch_no;
		}
		int32 credited = 0;
		uint32 maxed_day = 0;
		int32 rc = hunt_checkpoint(aid, b.char_id, b.collect_session, b.frozen_no, b.frozen, credited, maxed_day);
		b.last_flush = gettick();
		if (rc != NEED_MARBLE_OK && rc != NEED_MARBLE_ALREADY)
			break;   // keep the frozen batch, resend it unchanged later
		b.frozen.clear();
		if (maxed_day != 0 && maxed_day == today())
			b.done_day = maxed_day;
		if (credited > 0)
			notify(sd, ACT_HUNT, credited);
	}
}

bool hunt_member_ok(map_session_data* sd, mob_data* md, int32 type, bool top) {
	if (sd == nullptr || pc_isdead(sd))
		return false;
	if (P(P_HUNT_CAPTCHA_EXCLUDE) == 1 && (sd->macro_detect.cd != nullptr || sd->macro_detect.retry > 0))
		return false;
	if (!top && idle_for(sd, P(P_HUNT_ACTIVE_SEC)))
		return false;
	return need_event_hunt_normal_field_target(sd, md, type, P(P_HUNT_LEVEL_DIFF));
}

int32 collect_pc_sub(block_list* bl, va_list ap) {
	auto* out = va_arg(ap, std::vector<map_session_data*>*);
	out->push_back(reinterpret_cast<map_session_data*>(bl));
	return 1;
}

int32 bg_sample_sub(map_session_data* sd, va_list) {
	if (sd == nullptr || sd->bg_id == 0)
		return 0;
	bg_sample& s = g_bg[std::make_pair(sd->status.account_id, sd->bg_id)];
	s.total++;
	if (!idle_for(sd, P(P_BG_ACTIVE_SEC)))
		s.active++;
	return 1;
}

TIMER_FUNC(need_marble_timer) {
	if (!ready()) {
		return 0;
	}
	const t_tick now = gettick();
	const t_tick flush_ms = static_cast<t_tick>(P(P_HUNT_FLUSH_SEC)) * 1000;
	for (auto it = g_hunt.begin(); it != g_hunt.end();) {
		map_session_data* sd = map_id2sd(static_cast<int32>(it->first));
		if (sd == nullptr) {
			it = g_hunt.erase(it);   // logout hook already ran or the session is gone
			continue;
		}
		hunt_buf& b = it->second;
		if ((!b.pending.empty() || !b.frozen.empty()) && DIFF_TICK(now, b.last_flush) >= flush_ms)
			hunt_flush(sd, it->first, b);
		++it;
	}
	if (P(P_BG_ENABLED) == 1 && DIFF_TICK(now, g_last_bg_sample) >= static_cast<t_tick>(P(P_BG_SAMPLE_SEC)) * 1000) {
		g_last_bg_sample = now;
		map_foreachpc(bg_sample_sub);
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Instance / battleground
// ---------------------------------------------------------------------------

int32 instance_credit(map_session_data* sd, uint32 boot, int32 instance_id, int32 code) {
	const uint32 aid = sd->status.account_id, cid = sd->status.char_id, season_id = season(), day = today();
	const int32 points = P(P_INSTANCE_POINTS);
	ledger_tx tx;
	if (!tx.begin())
		return NEED_MARBLE_DB_ERROR;
	int32 rc = season_check_in_tx(season_id);
	if (rc != NEED_MARBLE_OK)
		return rc;
	// Run key: the same run reported twice.
	if (!sq("INSERT IGNORE INTO `need_marble_activity` (`account_id`,`season_id`,`activity`,`key_a`,`key_b`,`key_c`,"
		"`char_id`,`period_day`,`requested`,`credited`,`outcome`,`created_at`) "
		"VALUES ('%u','%u','%d','%u','%d','%d','%u','%u','%d','0','0',NOW())",
		aid, season_id, ACT_INSTANCE, boot, instance_id, code, cid, day, points))
		return NEED_MARBLE_DB_ERROR;
	if (affected() == 0)
		return NEED_MARBLE_ALREADY;
	// Eligibility key: one credited clear per account, logical day and dungeon.
	if (!sq("INSERT IGNORE INTO `need_marble_dungeon_daily` (`account_id`,`season_id`,`period_day`,`dungeon_code`,`boot_id`,`instance_id`,`created_at`) "
		"VALUES ('%u','%u','%u','%d','%u','%d',NOW())", aid, season_id, day, code, boot, instance_id))
		return NEED_MARBLE_DB_ERROR;
	if (affected() == 0) {
		if (!sq("UPDATE `need_marble_activity` SET `outcome`='3' WHERE `account_id`='%u' AND `season_id`='%u' AND `activity`='%d' "
			"AND `key_a`='%u' AND `key_b`='%d' AND `key_c`='%d'", aid, season_id, ACT_INSTANCE, boot, instance_id, code) || affected() != 1)
			return NEED_MARBLE_DB_ERROR;
		return tx.commit() ? NEED_MARBLE_USED_TODAY : NEED_MARBLE_DB_ERROR;
	}
	int32 credited = 0;
	rc = credit_in_tx(aid, cid, season_id, ACT_INSTANCE, boot, static_cast<uint64>(instance_id), static_cast<uint64>(code), day, points, true, credited);
	if (rc != NEED_MARBLE_OK)
		return rc;
	if (credited == 0 && P(P_INSTANCE_CAPPED_CONSUMES) == 0 &&
		(!sq("DELETE FROM `need_marble_dungeon_daily` WHERE `account_id`='%u' AND `season_id`='%u' AND `period_day`='%u' AND `dungeon_code`='%d'",
			aid, season_id, day, code) || affected() != 1))
		return NEED_MARBLE_DB_ERROR;
	if (!tx.commit())
		return NEED_MARBLE_DB_ERROR;
	notify(sd, ACT_INSTANCE, credited);
	return NEED_MARBLE_OK;
}

int32 bg_credit(map_session_data* sd, uint32 boot, int32 token, int32 type_code) {
	const uint32 aid = sd->status.account_id, cid = sd->status.char_id, season_id = season(), day = today();
	ledger_tx tx;
	if (!tx.begin())
		return NEED_MARBLE_DB_ERROR;
	int32 rc = season_check_in_tx(season_id);
	if (rc != NEED_MARBLE_OK)
		return rc;
	int32 credited = 0;
	rc = credit_in_tx(aid, cid, season_id, ACT_BG, boot, static_cast<uint64>(token), static_cast<uint64>(type_code), day, P(P_BG_POINTS), false, credited);
	if (rc != NEED_MARBLE_OK)
		return rc;
	if (!tx.commit())
		return NEED_MARBLE_DB_ERROR;
	notify(sd, ACT_BG, credited);
	return NEED_MARBLE_OK;
}

// ---------------------------------------------------------------------------
// Board helpers
// ---------------------------------------------------------------------------

void out_state(map_session_data* sd, const board_state& s) {
	outv(sd, "@MB_STATE", s.state);
	outv(sd, "@MB_POS", s.pos);
	outv(sd, "@MB_LEFT", s.left);
	outv(sd, "@MB_DIE", s.die);
	outv(sd, "@MB_ROLL", static_cast<int64>(s.roll_seq));
	outv(sd, "@MB_RUN", static_cast<int64>(s.run));
	outv(sd, "@MB_STEPS", static_cast<int64>(s.step_seq));
	outv(sd, "@MB_LAPS", s.laps);
	outv(sd, "@MB_SPASS", s.start_pass);
}

// Lazy "safe" auto choice for a dungeon tile left open (only when P_DUNGEON_AUTO_SAFE = 1).
void dungeon_autoresolve(map_session_data* sd) {
	if (P(P_DUNGEON_AUTO_SAFE) != 1 || g_in_tx)
		return;
	if (!sq("SELECT `s`.`roll_seq`,TIMESTAMPDIFF(SECOND,`e`.`created_at`,NOW()) FROM `need_marble_state` `s` "
		"JOIN `need_marble_event` `e` ON `e`.`account_id`=`s`.`account_id` AND `e`.`roll_seq`=`s`.`roll_seq` AND `e`.`hop`='0' "
		"WHERE `s`.`account_id`='%u' AND `s`.`state`='%d' AND `e`.`result_code`='0'", sd->status.account_id, ST_CHOICE))
		return;
	uint64 roll = 0;
	int64 age = -1;
	if (next_row()) {
		roll = col_u64(0);
		age = col_i64(1);
	}
	free_result();
	if (roll != 0 && age >= P(P_DUNGEON_AUTO_SEC))
		need_marble_choice(sd, roll, 1, true);
}

}  // namespace

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

int32 need_marble_is_ready() {
	return ready() ? 1 : 0;
}

int32 need_marble_policy_value(int32 key) {
	if (key < 0 || key >= POLICY_MAX)
		return -1;
	if (g_schema_ok && !g_fault)
		policy_refresh(false);
	return P(key);
}

void need_marble_hunt_on_kill(map_session_data* top, mob_data* md, int32 type) {
	if (top == nullptr || md == nullptr || !ready() || P(P_HUNT_ENABLED) != 1)
		return;
	if (!hunt_member_ok(top, md, type, true))
		return;
	std::vector<map_session_data*> who;
	who.push_back(top);
	if (P(P_HUNT_PARTY_MODE) == 1 && top->status.party_id != 0) {
		party_data* p = party_search(top->status.party_id);
		if (p != nullptr) {
			for (int32 i = 0; i < MAX_PARTY; i++) {
				map_session_data* member = p->data[i].sd;
				if (member == nullptr || member == top || member->m != md->m)
					continue;
				if (!check_distance_bl(md, member, AREA_SIZE))
					continue;
				if (hunt_member_ok(member, md, type, false))
					who.push_back(member);
			}
		}
	}
	const uint32 day = today();
	const uint32 flush_kills = static_cast<uint32>(P(P_HUNT_FLUSH_KILLS));
	for (map_session_data* sd : who) {
		const uint32 aid = sd->status.account_id;
		hunt_buf& b = g_hunt[aid];
		if (b.char_id != sd->status.char_id) {
			// A different character of this account without a logout flush: start a fresh collect session.
			b = hunt_buf();
			b.char_id = sd->status.char_id;
			b.last_flush = gettick();
		}
		if (b.done_day == day)
			continue;
		b.pending[day]++;
		uint32 total = 0;
		for (const auto& e : b.pending)
			total += e.second;
		if (total >= flush_kills)
			hunt_flush(sd, aid, b);
	}
}

void need_marble_on_logout(map_session_data* sd) {
	if (sd == nullptr)
		return;
	const uint32 aid = sd->status.account_id;
	auto it = g_hunt.find(aid);
	if (it != g_hunt.end()) {
		if (ready())
			hunt_flush(nullptr, aid, it->second);
		g_hunt.erase(it);
	}
	for (auto bit = g_bg.begin(); bit != g_bg.end();) {
		if (bit->first.first == aid)
			bit = g_bg.erase(bit);
		else
			++bit;
	}
}

int32 need_marble_view(map_session_data* sd) {
	if (sd == nullptr)
		return NEED_MARBLE_BAD_ARG;
	outv(sd, "@MB_READY", ready() ? 1 : 0);
	if (!ready())
		return NEED_MARBLE_DISABLED;
	dungeon_autoresolve(sd);
	const uint32 aid = sd->status.account_id, season_id = season(), day = today();

	int64 score = 0, dice = 0, pack = 0;
	if (!sq("SELECT `score`,`dice`,`pending_ack` FROM `need_marble_wallet` WHERE `account_id`='%u' AND `season_id`='%u'", aid, season_id))
		return NEED_MARBLE_DB_ERROR;
	if (next_row()) {
		score = col_i64(0);
		dice = col_i64(1);
		pack = col_i64(2);
	}
	free_result();

	board_state s;
	if (!sq("SELECT `state`,`pos`,`left_steps`,`die`,`roll_seq`,`step_seq`,`run_session`,`laps`,`start_pass` FROM `need_marble_state` WHERE `account_id`='%u'", aid))
		return NEED_MARBLE_DB_ERROR;
	if (next_row()) {
		s.state = static_cast<int32>(col_i64(0));
		s.pos = static_cast<int32>(col_i64(1));
		s.left = static_cast<int32>(col_i64(2));
		s.die = static_cast<int32>(col_i64(3));
		s.roll_seq = col_u64(4);
		s.step_seq = col_u64(5);
		s.run = col_u64(6);
		s.laps = static_cast<uint32>(col_u64(7));
		s.start_pass = static_cast<uint32>(col_u64(8));
	}
	free_result();

	int64 earned[4] = {};
	if (!sq("SELECT `activity`,`earned` FROM `need_marble_period` WHERE `account_id`='%u' AND `season_id`='%u' AND `period_day`='%u'", aid, season_id, day))
		return NEED_MARBLE_DB_ERROR;
	while (next_row()) {
		int64 a = col_i64(0);
		if (a >= 0 && a <= 3)
			earned[a] = col_i64(1);
	}
	free_result();

	int64 cp_sum = 0, cp_count = 0;
	if (!sq("SELECT COALESCE(SUM(`amount`),0),COUNT(*) FROM `need_marble_cp_payout` WHERE `account_id`='%u' AND `status`='1'", aid))
		return NEED_MARBLE_DB_ERROR;
	if (next_row()) {
		cp_sum = col_i64(0);
		cp_count = col_i64(1);
	}
	free_result();

	int64 kills = 0;
	if (!sq("SELECT `kills` FROM `need_marble_hunt_progress` WHERE `account_id`='%u' AND `season_id`='%u' AND `period_day`='%u'", aid, season_id, day))
		return NEED_MARBLE_DB_ERROR;
	if (next_row())
		kills = col_i64(0);
	free_result();
	auto hit = g_hunt.find(aid);
	if (hit != g_hunt.end()) {
		auto pit = hit->second.pending.find(day);
		if (pit != hit->second.pending.end())
			kills += pit->second;
		auto fit = hit->second.frozen.find(day);
		if (fit != hit->second.frozen.end())
			kills += fit->second;
	}

	outv(sd, "@MB_SCORE", score);
	outv(sd, "@MB_DICE", dice);
	outv(sd, "@MB_PACK", pack);
	out_state(sd, s);
	outv(sd, "@MB_TODAY_TOTAL", earned[0]);
	outv(sd, "@MB_TODAY_HUNT", earned[ACT_HUNT]);
	outv(sd, "@MB_TODAY_INST", earned[ACT_INSTANCE]);
	outv(sd, "@MB_TODAY_BG", earned[ACT_BG]);
	outv(sd, "@MB_CAP_TOTAL", P(P_DAILY_TOTAL_CAP));
	outv(sd, "@MB_CAP_HUNT", P(P_DAILY_HUNT_CAP));
	outv(sd, "@MB_CAP_INST", P(P_DAILY_INSTANCE_CAP));
	outv(sd, "@MB_CAP_BG", P(P_DAILY_BG_CAP));
	outv(sd, "@MB_COST", P(P_EXCHANGE_COST));
	outv(sd, "@MB_HOLD", P(P_DICE_HOLD_MAX));
	outv(sd, "@MB_MAXBUNDLE", P(P_EXCHANGE_MAX_BUNDLE));
	outv(sd, "@MB_CP_PENDING", cp_sum);
	outv(sd, "@MB_CP_COUNT", cp_count);
	outv(sd, "@MB_HUNT_KILLS", kills);
	outv(sd, "@MB_TIER_KILLS", P(P_HUNT_TIER_KILLS));
	outv(sd, "@MB_TIER_MAX", P(P_HUNT_MAX_TIERS));
	outv(sd, "@MB_BG_ON", P(P_BG_ENABLED));
	outv(sd, "@MB_DAY", day);
	return NEED_MARBLE_OK;
}

namespace {

void out_request(map_session_data* sd, uint64 req, int32 status, int32 bundles, int32 cost, int32 reject) {
	outv(sd, "@MB_REQ", static_cast<int64>(req));
	outv(sd, "@MB_REQ_STATUS", status);
	outv(sd, "@MB_REQ_BUNDLES", bundles);
	outv(sd, "@MB_REQ_COST", cost);
	outv(sd, "@MB_REQ_REJECT", reject);
}

// Reads one exchange request row (inside the caller's transaction).
int32 load_request_in_tx(uint32 aid, uint64 req, int32& status, int32& bundles, int32& cost, int32& reject, bool& expired) {
	MARBLE_REQUIRE_TX();
	if (!sq("SELECT `status`,`bundles`,`cost`,`reject_code`,`expires_at`<NOW() FROM `need_marble_exchange_req` "
		"WHERE `account_id`='%u' AND `request_no`='%" PRIu64 "' FOR UPDATE", aid, req))
		return NEED_MARBLE_DB_ERROR;
	if (!next_row()) {
		free_result();
		return NEED_MARBLE_NOT_FOUND;
	}
	status = static_cast<int32>(col_i64(0));
	bundles = static_cast<int32>(col_i64(1));
	cost = static_cast<int32>(col_i64(2));
	reject = static_cast<int32>(col_i64(3));
	expired = col_i64(4) != 0;
	free_result();
	return NEED_MARBLE_OK;
}

}  // namespace

// Read only: reports the request whose result is not acknowledged yet. Never issues one.
int32 need_marble_exchange_peek(map_session_data* sd) {
	if (sd == nullptr)
		return NEED_MARBLE_BAD_ARG;
	if (!ready())
		return NEED_MARBLE_DISABLED;
	const uint32 aid = sd->status.account_id;
	if (!sq("SELECT `r`.`request_no`,`r`.`status`,`r`.`bundles`,`r`.`cost`,`r`.`reject_code` FROM `need_marble_wallet` `w` "
		"JOIN `need_marble_exchange_req` `r` ON `r`.`account_id`=`w`.`account_id` AND `r`.`request_no`=`w`.`pending_ack` "
		"WHERE `w`.`account_id`='%u' AND `w`.`season_id`='%u' AND `w`.`pending_ack`<>'0'", aid, season()))
		return NEED_MARBLE_DB_ERROR;
	if (!next_row()) {
		free_result();
		return NEED_MARBLE_OK;
	}
	const uint64 req = col_u64(0);
	const int32 status = static_cast<int32>(col_i64(1)), bundles = static_cast<int32>(col_i64(2));
	const int32 cost = static_cast<int32>(col_i64(3)), reject = static_cast<int32>(col_i64(4));
	free_result();
	out_request(sd, req, status, bundles, cost, reject);
	return NEED_MARBLE_PENDING_ACK;
}

int32 need_marble_exchange_issue(map_session_data* sd, int32 bundles) {
	if (sd == nullptr)
		return NEED_MARBLE_BAD_ARG;
	if (!ready())
		return NEED_MARBLE_DISABLED;
	if (bundles < 1 || bundles > P(P_EXCHANGE_MAX_BUNDLE))
		return NEED_MARBLE_BAD_ARG;
	const uint32 aid = sd->status.account_id, cid = sd->status.char_id, season_id = season();
	ledger_tx tx;
	if (!tx.begin())
		return NEED_MARBLE_DB_ERROR;
	int32 rc = season_check_in_tx(season_id);
	if (rc != NEED_MARBLE_OK)
		return rc;
	if (ensure_rows_in_tx(aid, season_id) != NEED_MARBLE_OK)
		return NEED_MARBLE_DB_ERROR;
	wallet_row w;
	if ((rc = load_wallet_in_tx(aid, season_id, w)) != NEED_MARBLE_OK)
		return rc;
	if (w.pending_ack != 0) {
		// A previous request has no confirmed result yet: show it, never issue a second one.
		int32 status = 0, b = 0, cost = 0, reject = 0;
		bool expired = false;
		if ((rc = load_request_in_tx(aid, w.pending_ack, status, b, cost, reject, expired)) != NEED_MARBLE_OK)
			return rc;
		if (status == REQ_ISSUED && expired) {
			// Never committed (the commit changes this row in the same transaction): close it.
			if (!sq("UPDATE `need_marble_exchange_req` SET `status`='%d',`resolved_at`=NOW() WHERE `account_id`='%u' AND `request_no`='%" PRIu64 "' AND `status`='%d'",
				REQ_EXPIRED, aid, w.pending_ack, REQ_ISSUED) || affected() != 1)
				return NEED_MARBLE_DB_ERROR;
			status = REQ_EXPIRED;
		}
		if (!tx.commit())
			return NEED_MARBLE_DB_ERROR;
		out_request(sd, w.pending_ack, status, b, cost, reject);
		return NEED_MARBLE_PENDING_ACK;
	}
	if (!sq("INSERT INTO `need_marble_account_seq` (`account_id`,`exchange_req`) VALUES ('%u','1') "
		"ON DUPLICATE KEY UPDATE `exchange_req`=`exchange_req`+1", aid) ||
		!sq("SELECT `exchange_req` FROM `need_marble_account_seq` WHERE `account_id`='%u'", aid))
		return NEED_MARBLE_DB_ERROR;
	uint64 req = next_row() ? col_u64(0) : 0;
	free_result();
	if (req == 0)
		return NEED_MARBLE_DB_ERROR;
	const int32 cost = bundles * P(P_EXCHANGE_COST);
	if (!sq("INSERT INTO `need_marble_exchange_req` (`account_id`,`request_no`,`season_id`,`bundles`,`cost`,`status`,`char_id`,`issued_at`,`expires_at`) "
		"VALUES ('%u','%" PRIu64 "','%u','%d','%d','%d','%u',NOW(),NOW() + INTERVAL %d SECOND)",
		aid, req, season_id, bundles, cost, REQ_ISSUED, cid, P(P_EXCHANGE_TTL_SEC)) ||
		!sq("UPDATE `need_marble_wallet` SET `pending_ack`='%" PRIu64 "',`updated_at`=NOW() WHERE `account_id`='%u' AND `season_id`='%u' AND `pending_ack`='0'",
			req, aid, season_id) || affected() != 1)
		return NEED_MARBLE_DB_ERROR;
	if (!tx.commit())
		return NEED_MARBLE_DB_ERROR;
	out_request(sd, req, REQ_ISSUED, bundles, cost, 0);
	outv(sd, "@MB_SCORE", w.score);
	outv(sd, "@MB_DICE", w.dice);
	return NEED_MARBLE_OK;
}

int32 need_marble_exchange_commit(map_session_data* sd, uint64 req) {
	if (sd == nullptr || req == 0)
		return NEED_MARBLE_BAD_ARG;
	if (!ready())
		return NEED_MARBLE_DISABLED;
	const uint32 aid = sd->status.account_id, cid = sd->status.char_id, season_id = season(), day = today();
	ledger_tx tx;
	if (!tx.begin())
		return NEED_MARBLE_DB_ERROR;
	int32 rc = season_check_in_tx(season_id);
	if (rc != NEED_MARBLE_OK)
		return rc;
	int32 status = 0, bundles = 0, cost = 0, reject = 0;
	bool expired = false;
	if ((rc = load_request_in_tx(aid, req, status, bundles, cost, reject, expired)) != NEED_MARBLE_OK)
		return rc;
	if (status != REQ_ISSUED) {
		tx.rollback();
		out_request(sd, req, status, bundles, cost, reject);
		return status == REQ_COMMITTED ? NEED_MARBLE_ALREADY : NEED_MARBLE_BAD_STATE;
	}
	auto close_request = [&](int32 new_status, int32 code) -> bool {
		return sq("UPDATE `need_marble_exchange_req` SET `status`='%d',`reject_code`='%d',`resolved_at`=NOW() "
			"WHERE `account_id`='%u' AND `request_no`='%" PRIu64 "' AND `status`='%d'", new_status, code, aid, req, REQ_ISSUED) && affected() == 1;
	};
	if (expired) {
		if (!close_request(REQ_EXPIRED, 0) || !tx.commit())
			return NEED_MARBLE_DB_ERROR;
		out_request(sd, req, REQ_EXPIRED, bundles, cost, 0);
		return NEED_MARBLE_EXPIRED;
	}
	wallet_row w;
	if ((rc = load_wallet_in_tx(aid, season_id, w)) != NEED_MARBLE_OK)
		return rc;
	if (!sq("INSERT IGNORE INTO `need_marble_period` (`account_id`,`season_id`,`period_day`,`activity`) VALUES ('%u','%u','%u','0')", aid, season_id, day) ||
		!sq("SELECT `exchanged` FROM `need_marble_period` WHERE `account_id`='%u' AND `season_id`='%u' AND `period_day`='%u' AND `activity`='0' FOR UPDATE",
			aid, season_id, day))
		return NEED_MARBLE_DB_ERROR;
	int64 exchanged = next_row() ? col_i64(0) : 0;
	free_result();

	int32 code = 0;
	if (w.locked != 0)
		code = 3;
	else if (w.score < cost)
		code = 1;
	else if (w.dice + bundles > P(P_DICE_HOLD_MAX))
		code = 2;   // hold limit: refused, score untouched
	else if (P(P_EXCHANGE_DAILY_MAX) > 0 && exchanged + bundles > P(P_EXCHANGE_DAILY_MAX))
		code = 5;
	if (code != 0) {
		if (!close_request(REQ_REJECTED, code) || !tx.commit())
			return NEED_MARBLE_DB_ERROR;
		out_request(sd, req, REQ_REJECTED, bundles, cost, code);
		return NEED_MARBLE_REJECTED;
	}
	if (!sq("INSERT INTO `need_marble_wallet_tx` (`account_id`,`season_id`,`kind`,`ref_type`,`ref_a`,`ref_b`,`ref_c`,`ref_d`,"
		"`char_id`,`d_score`,`d_dice`,`period_day`,`created_at`) VALUES ('%u','%u','%d','%d','%" PRIu64 "','0','0','0','%u','%d','%d','%u',NOW())",
		aid, season_id, TX_EXCHANGE, REF_EXCHANGE, req, cid, -cost, bundles, day) ||
		!sq("UPDATE `need_marble_wallet` SET `score`=`score`-'%d',`dice`=`dice`+'%d',`updated_at`=NOW() "
			"WHERE `account_id`='%u' AND `season_id`='%u' AND `score`>='%d' AND `locked`='0'", cost, bundles, aid, season_id, cost) || affected() != 1 ||
		!sq("UPDATE `need_marble_period` SET `exchanged`=`exchanged`+'%d' WHERE `account_id`='%u' AND `season_id`='%u' AND `period_day`='%u' AND `activity`='0'",
			bundles, aid, season_id, day) || affected() != 1 ||
		!close_request(REQ_COMMITTED, 0))
		return NEED_MARBLE_DB_ERROR;
	if (!tx.commit())
		return NEED_MARBLE_DB_ERROR;
	out_request(sd, req, REQ_COMMITTED, bundles, cost, 0);
	outv(sd, "@MB_SCORE", w.score - cost);
	outv(sd, "@MB_DICE", w.dice + bundles);
	return NEED_MARBLE_OK;
}

int32 need_marble_exchange_ack(map_session_data* sd, uint64 req, bool cancel) {
	if (sd == nullptr || req == 0)
		return NEED_MARBLE_BAD_ARG;
	if (!ready())
		return NEED_MARBLE_DISABLED;
	const uint32 aid = sd->status.account_id, season_id = season();
	ledger_tx tx;
	if (!tx.begin())
		return NEED_MARBLE_DB_ERROR;
	int32 status = 0, bundles = 0, cost = 0, reject = 0;
	bool expired = false;
	int32 rc = load_request_in_tx(aid, req, status, bundles, cost, reject, expired);
	if (rc != NEED_MARBLE_OK)
		return rc;
	if (status == REQ_ISSUED) {
		if (!cancel && !expired)
			return NEED_MARBLE_BAD_STATE;   // still open: commit or cancel it first
		if (!sq("UPDATE `need_marble_exchange_req` SET `status`='%d',`resolved_at`=NOW() WHERE `account_id`='%u' AND `request_no`='%" PRIu64 "' AND `status`='%d'",
			REQ_EXPIRED, aid, req, REQ_ISSUED) || affected() != 1)
			return NEED_MARBLE_DB_ERROR;
		status = REQ_EXPIRED;
	}
	if (!sq("UPDATE `need_marble_exchange_req` SET `acked`='1' WHERE `account_id`='%u' AND `request_no`='%" PRIu64 "'", aid, req) ||
		!sq("UPDATE `need_marble_wallet` SET `pending_ack`='0',`updated_at`=NOW() WHERE `account_id`='%u' AND `season_id`='%u' AND `pending_ack`='%" PRIu64 "'",
			aid, season_id, req))
		return NEED_MARBLE_DB_ERROR;
	if (!tx.commit())
		return NEED_MARBLE_DB_ERROR;
	out_request(sd, req, status, bundles, cost, reject);
	return NEED_MARBLE_OK;
}

int32 need_marble_roll(map_session_data* sd, uint64 driving_run, int32 forced_die) {
	if (sd == nullptr)
		return NEED_MARBLE_BAD_ARG;
	if (!ready())
		return NEED_MARBLE_DISABLED;
	dungeon_autoresolve(sd);
	const uint32 aid = sd->status.account_id, cid = sd->status.char_id, season_id = season(), day = today();
	ledger_tx tx;
	if (!tx.begin())
		return NEED_MARBLE_DB_ERROR;
	int32 rc = season_check_in_tx(season_id);
	if (rc != NEED_MARBLE_OK)
		return rc;
	if (ensure_rows_in_tx(aid, season_id) != NEED_MARBLE_OK)
		return NEED_MARBLE_DB_ERROR;
	board_state s;
	if ((rc = load_state_in_tx(aid, s)) != NEED_MARBLE_OK)
		return rc;
	if (s.state == ST_CHOICE) {
		tx.rollback();
		out_state(sd, s);
		return NEED_MARBLE_CHOICE_PENDING;
	}
	if (s.state == ST_MOVING && driving_run != 0 && driving_run == s.run) {
		tx.rollback();
		out_state(sd, s);
		return NEED_MARBLE_BUSY;   // this login is still driving the roll
	}
	if (s.state == ST_MOVING || s.state == ST_PAUSED || s.state == ST_RECOVERY) {
		// Existing roll (any character of the account): continue it, never spend another dice.
		const uint64 new_run = s.run + 1;
		if (!sq("UPDATE `need_marble_state` SET `state`='%d',`run_session`='%" PRIu64 "',`run_char_id`='%u',`fail_reason`='0',`updated_at`=NOW() "
			"WHERE `account_id`='%u' AND `run_session`='%" PRIu64 "'", ST_MOVING, new_run, cid, aid, s.run) || affected() != 1)
			return NEED_MARBLE_DB_ERROR;
		if (!tx.commit())
			return NEED_MARBLE_DB_ERROR;
		s.state = ST_MOVING;
		s.run = new_run;
		out_state(sd, s);
		return s.left > 0 ? NEED_MARBLE_RESUME : NEED_MARBLE_RESUME_ARRIVE;
	}
	if (s.state != ST_IDLE)
		return NEED_MARBLE_INTERNAL;

	wallet_row w;
	if ((rc = load_wallet_in_tx(aid, season_id, w)) != NEED_MARBLE_OK)
		return rc;
	if (w.locked != 0)
		return NEED_MARBLE_LOCKED;
	if (w.dice < 1) {
		tx.rollback();
		out_state(sd, s);
		return NEED_MARBLE_NO_DICE;
	}
	int32 forced = 0;
	int32 die = rnd_value<int32>(1, 6);
	if (forced_die >= 1 && forced_die <= 6 && static_cast<int32>(season_id) >= TEST_SEASON_MIN && pc_get_group_level(sd) >= GM_FORCE_LEVEL) {
		die = forced_die;
		forced = 1;
	}
	const uint64 roll_seq = s.roll_seq + 1, new_run = s.run + 1;
	if (!sq("INSERT INTO `need_marble_roll` (`account_id`,`roll_seq`,`season_id`,`char_id`,`run_session`,`die`,`start_pos`,`forced`,`created_at`) "
		"VALUES ('%u','%" PRIu64 "','%u','%u','%" PRIu64 "','%d','%d','%d',NOW())", aid, roll_seq, season_id, cid, new_run, die, s.pos, forced) ||
		!sq("INSERT INTO `need_marble_wallet_tx` (`account_id`,`season_id`,`kind`,`ref_type`,`ref_a`,`ref_b`,`ref_c`,`ref_d`,"
			"`char_id`,`d_score`,`d_dice`,`period_day`,`created_at`) VALUES ('%u','%u','%d','%d','%" PRIu64 "','0','0','0','%u','0','-1','%u',NOW())",
			aid, season_id, TX_SPEND, REF_ROLL, roll_seq, cid, day) ||
		!sq("UPDATE `need_marble_wallet` SET `dice`=`dice`-1,`updated_at`=NOW() WHERE `account_id`='%u' AND `season_id`='%u' AND `dice`>='1' AND `locked`='0'",
			aid, season_id) || affected() != 1 ||
		!sq("UPDATE `need_marble_state` SET `state`='%d',`die`='%d',`left_steps`='%d',`roll_seq`='%" PRIu64 "',`run_session`='%" PRIu64 "',"
			"`run_char_id`='%u',`fail_reason`='0',`updated_at`=NOW() WHERE `account_id`='%u' AND `state`='%d' AND `run_session`='%" PRIu64 "'",
			ST_MOVING, die, die, roll_seq, new_run, cid, aid, ST_IDLE, s.run) || affected() != 1)
		return NEED_MARBLE_DB_ERROR;
	if (!tx.commit())
		return NEED_MARBLE_DB_ERROR;
	s.state = ST_MOVING;
	s.die = die;
	s.left = die;
	s.roll_seq = roll_seq;
	s.run = new_run;
	out_state(sd, s);
	outv(sd, "@MB_DICE", w.dice - 1);
	return NEED_MARBLE_OK;
}

int32 need_marble_step(map_session_data* sd, uint64 run, int32 from_pos) {
	if (sd == nullptr || run == 0)
		return NEED_MARBLE_BAD_ARG;
	if (!ready())
		return NEED_MARBLE_DISABLED;
	const uint32 aid = sd->status.account_id, cid = sd->status.char_id;
	ledger_tx tx;
	if (!tx.begin())
		return NEED_MARBLE_DB_ERROR;
	board_state s;
	int32 rc = load_state_in_tx(aid, s);
	if (rc != NEED_MARBLE_OK)
		return rc;
	if (s.state != ST_MOVING || s.run != run)
		return NEED_MARBLE_STALE;
	if (s.pos != from_pos || s.left <= 0)
		return NEED_MARBLE_BAD_STATE;
	const int32 np = (s.pos + 1) % BOARD_TILES;
	const uint64 step = s.step_seq + 1;
	const uint64 lap = lap_reached(step, P(P_LAP_STEPS));
	if (lap != 0 && (rc = payout_decide_in_tx(aid, cid, PAY_LAP, lap, 0, P(P_LAP_CP))) != NEED_MARBLE_OK)
		return rc;
	if (!sq("UPDATE `need_marble_state` SET `pos`='%d',`left_steps`=`left_steps`-1,`step_seq`='%" PRIu64 "',`start_pass`=`start_pass`+'%d',"
		"`laps`=`laps`+'%d',`updated_at`=NOW() WHERE `account_id`='%u' AND `state`='%d' AND `run_session`='%" PRIu64 "' AND `pos`='%d' AND `left_steps`>'0'",
		np, step, np == 0 ? 1 : 0, lap != 0 ? 1 : 0, aid, ST_MOVING, run, from_pos) || affected() != 1)
		return NEED_MARBLE_DB_ERROR;
	if (!tx.commit())
		return NEED_MARBLE_DB_ERROR;
	s.pos = np;
	s.left--;
	s.step_seq = step;
	s.start_pass += np == 0 ? 1 : 0;
	s.laps += lap != 0 ? 1 : 0;
	out_state(sd, s);
	outv(sd, "@MB_LAP", static_cast<int64>(lap));
	outv(sd, "@MB_LAPCP", lap != 0 ? P(P_LAP_CP) : 0);
	return NEED_MARBLE_OK;
}

int32 need_marble_arrive(map_session_data* sd, uint64 run) {
	if (sd == nullptr || run == 0)
		return NEED_MARBLE_BAD_ARG;
	if (!ready())
		return NEED_MARBLE_DISABLED;
	const uint32 aid = sd->status.account_id, cid = sd->status.char_id, season_id = season();
	ledger_tx tx;
	if (!tx.begin())
		return NEED_MARBLE_DB_ERROR;
	board_state s;
	int32 rc = load_state_in_tx(aid, s);
	if (rc != NEED_MARBLE_OK)
		return rc;
	if (s.state != ST_MOVING || s.run != run)
		return NEED_MARBLE_STALE;
	if (s.left != 0)
		return NEED_MARBLE_BAD_STATE;
	const int32 kind = BOARD[static_cast<size_t>(s.pos)];
	int32 cp = 0, slot = -1, dest = -1, new_state = ST_IDLE;

	if (kind == TILE_WARP) {
		// W1: move only. The destination tile runs no event, so no chain is possible.
		dest = warp_destination(g_pol, s.pos);
		if (dest < 0 || dest >= BOARD_TILES)
			return NEED_MARBLE_INTERNAL;
		const uint64 jump = s.jump_seq + 1;
		if (!sq("INSERT INTO `need_marble_event` (`account_id`,`roll_seq`,`hop`,`season_id`,`tile`,`tile_kind`,`char_id`,`result_code`,`created_at`,`resolved_at`) "
			"VALUES ('%u','%" PRIu64 "','0','%u','%d','%d','%u','%d',NOW(),NOW())", aid, s.roll_seq, season_id, s.pos, kind, cid, RESULT_WARP) ||
			!sq("INSERT INTO `need_marble_jump` (`account_id`,`jump_seq`,`kind`,`roll_seq`,`from_pos`,`to_pos`,`created_at`) "
				"VALUES ('%u','%" PRIu64 "','1','%" PRIu64 "','%d','%d',NOW())", aid, jump, s.roll_seq, s.pos, dest) ||
			!sq("UPDATE `need_marble_state` SET `state`='%d',`pos`='%d',`jump_seq`='%" PRIu64 "',`updated_at`=NOW() "
				"WHERE `account_id`='%u' AND `state`='%d' AND `run_session`='%" PRIu64 "' AND `left_steps`='0'",
				ST_IDLE, dest, jump, aid, ST_MOVING, run) || affected() != 1)
			return NEED_MARBLE_DB_ERROR;
		s.pos = dest;
		s.jump_seq = jump;
	} else if (kind == TILE_DUNGEON) {
		new_state = ST_CHOICE;
		if (!sq("INSERT INTO `need_marble_event` (`account_id`,`roll_seq`,`hop`,`season_id`,`tile`,`tile_kind`,`char_id`,`result_code`,`created_at`) "
			"VALUES ('%u','%" PRIu64 "','0','%u','%d','%d','%u','0',NOW())", aid, s.roll_seq, season_id, s.pos, kind, cid) ||
			!sq("UPDATE `need_marble_state` SET `state`='%d',`updated_at`=NOW() WHERE `account_id`='%u' AND `state`='%d' AND `run_session`='%" PRIu64 "' AND `left_steps`='0'",
				ST_CHOICE, aid, ST_MOVING, run) || affected() != 1)
			return NEED_MARBLE_DB_ERROR;
	} else {
		const int32 roll = rnd_value<int32>(1, RATE_SCALE);
		tile_outcome o = pick_outcome(g_pol, kind, roll);
		if (o.slot < 0)
			return NEED_MARBLE_INTERNAL;
		slot = o.slot;
		cp = o.cp;
		if (!sq("INSERT INTO `need_marble_event` (`account_id`,`roll_seq`,`hop`,`season_id`,`tile`,`tile_kind`,`char_id`,`rng_value`,`result_code`,`cp_amount`,`created_at`,`resolved_at`) "
			"VALUES ('%u','%" PRIu64 "','0','%u','%d','%d','%u','%d','%d','%d',NOW(),NOW())", aid, s.roll_seq, season_id, s.pos, kind, cid, roll, slot + 1, cp))
			return NEED_MARBLE_DB_ERROR;
		if ((rc = payout_decide_in_tx(aid, cid, PAY_TILE, s.roll_seq, 0, cp)) != NEED_MARBLE_OK)
			return rc;
		if (!sq("UPDATE `need_marble_state` SET `state`='%d',`updated_at`=NOW() WHERE `account_id`='%u' AND `state`='%d' AND `run_session`='%" PRIu64 "' AND `left_steps`='0'",
			ST_IDLE, aid, ST_MOVING, run) || affected() != 1)
			return NEED_MARBLE_DB_ERROR;
	}
	if (!tx.commit())
		return NEED_MARBLE_DB_ERROR;
	s.state = new_state;
	out_state(sd, s);
	outv(sd, "@MB_KIND", kind);
	outv(sd, "@MB_SLOT", slot);
	outv(sd, "@MB_CP", cp);
	outv(sd, "@MB_WARP", dest);
	outv(sd, "@MB_AUTO_SAFE", P(P_DUNGEON_AUTO_SAFE));
	outv(sd, "@MB_AUTO_SEC", P(P_DUNGEON_AUTO_SEC));
	return NEED_MARBLE_OK;
}

int32 need_marble_choice(map_session_data* sd, uint64 roll_seq, int32 choice, bool automatic) {
	if (sd == nullptr || roll_seq == 0 || (choice != 1 && choice != 2))
		return NEED_MARBLE_BAD_ARG;
	if (!ready())
		return NEED_MARBLE_DISABLED;
	if (automatic && (choice != 1 || P(P_DUNGEON_AUTO_SAFE) != 1))
		return NEED_MARBLE_BAD_ARG;
	const uint32 aid = sd->status.account_id, cid = sd->status.char_id;
	ledger_tx tx;
	if (!tx.begin())
		return NEED_MARBLE_DB_ERROR;
	board_state s;
	int32 rc = load_state_in_tx(aid, s);
	if (rc != NEED_MARBLE_OK)
		return rc;
	if (s.state != ST_CHOICE || s.roll_seq != roll_seq) {
		tx.rollback();
		out_state(sd, s);
		return NEED_MARBLE_ALREADY;
	}
	if (!sq("SELECT `result_code`,TIMESTAMPDIFF(SECOND,`created_at`,NOW()) FROM `need_marble_event` "
		"WHERE `account_id`='%u' AND `roll_seq`='%" PRIu64 "' AND `hop`='0' FOR UPDATE", aid, roll_seq))
		return NEED_MARBLE_DB_ERROR;
	if (!next_row()) {
		free_result();
		return NEED_MARBLE_INTERNAL;
	}
	const int64 result = col_i64(0), age = col_i64(1);
	free_result();
	if (result != 0)
		return NEED_MARBLE_INTERNAL;
	if (automatic && age < P(P_DUNGEON_AUTO_SEC))
		return NEED_MARBLE_BAD_STATE;   // a stale or early timer does nothing
	const int32 roll = choice == 2 ? rnd_value<int32>(1, RATE_SCALE) : 0;
	const int32 cp = dungeon_cp(g_pol, choice, roll);
	const int32 code = choice == 1 ? 1 : (cp > 0 ? 2 : 3);
	if (!sq("UPDATE `need_marble_event` SET `choice`='%d',`auto_choice`='%d',`rng_value`='%d',`result_code`='%d',`cp_amount`='%d',`resolved_at`=NOW() "
		"WHERE `account_id`='%u' AND `roll_seq`='%" PRIu64 "' AND `hop`='0' AND `result_code`='0'",
		choice, automatic ? 1 : 0, roll, code, cp, aid, roll_seq) || affected() != 1)
		return NEED_MARBLE_DB_ERROR;
	if ((rc = payout_decide_in_tx(aid, cid, PAY_TILE, roll_seq, 0, cp)) != NEED_MARBLE_OK)
		return rc;
	if (!sq("UPDATE `need_marble_state` SET `state`='%d',`updated_at`=NOW() WHERE `account_id`='%u' AND `state`='%d' AND `roll_seq`='%" PRIu64 "'",
		ST_IDLE, aid, ST_CHOICE, roll_seq) || affected() != 1)
		return NEED_MARBLE_DB_ERROR;
	if (!tx.commit())
		return NEED_MARBLE_DB_ERROR;
	s.state = ST_IDLE;
	out_state(sd, s);
	outv(sd, "@MB_CHOICE", choice);
	outv(sd, "@MB_CP", cp);
	return NEED_MARBLE_OK;
}

int32 need_marble_pause(map_session_data* sd, uint64 run, bool on_tile, int32 reason) {
	if (sd == nullptr || run == 0)
		return NEED_MARBLE_BAD_ARG;
	if (!ready())
		return NEED_MARBLE_DISABLED;
	if (g_in_tx)
		return NEED_MARBLE_INTERNAL;
	// Single statement: only the run that is still current can be paused.
	if (!sq("UPDATE `need_marble_state` SET `state`='%d',`fail_reason`='%d',`updated_at`=NOW() "
		"WHERE `account_id`='%u' AND `state`='%d' AND `run_session`='%" PRIu64 "'",
		on_tile ? ST_PAUSED : ST_RECOVERY, reason & 0xFF, sd->status.account_id, ST_MOVING, run))
		return NEED_MARBLE_DB_ERROR;
	return affected() == 1 ? NEED_MARBLE_OK : NEED_MARBLE_STALE;
}

int32 need_marble_instance_clear(const char* dungeon_key, const char* map_name) {
	if (dungeon_key == nullptr || map_name == nullptr || !ready() || P(P_INSTANCE_ENABLED) != 1)
		return 0;
	int32 code = 0;
	for (const dungeon_entry& d : GRADE_A_DUNGEONS) {
		if (strcmp(d.key, dungeon_key) == 0) {
			code = d.code;
			break;
		}
	}
	if (code == 0)
		return 0;   // grade B / C or unknown: not part of the first scope
	int16 m = map_mapname2mapid(map_name);
	if (m < 0)
		return 0;
	map_data* mapdata = map_getmapdata(m);
	if (mapdata == nullptr || mapdata->instance_id <= 0)
		return 0;
	const uint32 boot = boot_id();
	if (boot == 0)
		return 0;
	std::vector<map_session_data*> pcs;
	map_foreachinmap(collect_pc_sub, m, BL_PC, &pcs);
	int32 credited = 0;
	for (map_session_data* sd : pcs) {
		if (P(P_INSTANCE_REQUIRE_ACTIVE) == 1 && idle_for(sd, P(P_INSTANCE_ACTIVE_SEC)))
			continue;
		if (instance_credit(sd, boot, mapdata->instance_id, code) == NEED_MARBLE_OK)
			credited++;
		if (g_fault)
			break;
	}
	return credited;
}

int32 need_marble_bg_match(const char* map_name, const char* bg_type, int32 match_token) {
	if (map_name == nullptr || !ready() || P(P_BG_ENABLED) != 1)
		return 0;
	int32 type_code = 3;
	if (bg_type != nullptr && strcmp(bg_type, "Tierra") == 0)
		type_code = 1;
	else if (bg_type != nullptr && strcmp(bg_type, "Flavius") == 0)
		type_code = 2;
	int16 m = map_mapname2mapid(map_name);
	if (m < 0 || match_token <= 0)
		return 0;
	const uint32 boot = boot_id();
	if (boot == 0)
		return 0;
	std::vector<map_session_data*> pcs;
	map_foreachinmap(collect_pc_sub, m, BL_PC, &pcs);
	int32 credited = 0;
	for (map_session_data* sd : pcs) {
		if (sd->bg_id == 0)
			continue;
		auto it = g_bg.find(std::make_pair(sd->status.account_id, sd->bg_id));
		if (it == g_bg.end())
			continue;
		const bg_sample s = it->second;
		g_bg.erase(it);
		if (s.total < static_cast<uint32>(P(P_BG_MIN_SAMPLES)) ||
			static_cast<int64>(s.active) * 1000 < static_cast<int64>(P(P_BG_MIN_ACTIVE_PERMIL)) * s.total)
			continue;
		if (idle_for(sd, P(P_BG_ACTIVE_SEC)))
			continue;
		if (bg_credit(sd, boot, match_token, type_code) == NEED_MARBLE_OK)
			credited++;
		if (g_fault)
			break;
	}
	return credited;
}

void need_marble_init() {
	g_fault = false;
	g_in_tx = false;
	g_boot_id = 0;
	g_hunt.clear();
	g_bg.clear();
	g_schema_ok = schema_check();
	policy_refresh(true);
	if (!g_schema_ok)
		ShowNotice("need_marble: ledger schema not installed, marble board disabled.\n");
	else
		ShowNotice("need_marble: ledger ready, enabled=%d, policy day %u.\n", g_pol_ok ? P(P_ENABLED) : 0, g_pol_day);
	if (g_timer == INVALID_TIMER) {
		add_timer_func_list(need_marble_timer, "need_marble_timer");
		g_timer = add_timer_interval(gettick() + TIMER_INTERVAL_MS, need_marble_timer, 0, 0, TIMER_INTERVAL_MS);
	}
}

void need_marble_final() {
	if (g_timer != INVALID_TIMER) {
		delete_timer(g_timer, need_marble_timer);
		g_timer = INVALID_TIMER;
	}
	g_hunt.clear();
	g_bg.clear();
	g_schema_ok = false;
}
