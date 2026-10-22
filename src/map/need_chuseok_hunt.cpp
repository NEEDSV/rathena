// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 chuseok event - field hunting rewards and the shared honey songpyun ledger.
//
// Mirrors the summer golden watermelon flow (need_summer_hunt.cpp) and shares
// its world drop targeting through need_event_hunt.hpp. The difference is that
// the honey songpyun ledger is reachable from script as well, so the hunting
// drop and the battleground full-cycle roll consume the same daily allowance.
//
// The ledger transaction itself takes a s_need_event_daily_spec, so the 2026
// halloween jack game reward (need_halloween.cpp) runs through the same code
// with its own event id, item and amount.

#include "need_chuseok_hunt.hpp"

#include <cinttypes>
#include <cstdlib>
#include <cstring>

#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/sql.hpp>

#include "battle.hpp"
#include "clif.hpp"
#include "itemdb.hpp"
#include "log.hpp"
#include "map.hpp"
#include "mob.hpp"
#include "need_event_hunt.hpp"
#include "pc.hpp"

namespace {

constexpr t_itemid NEED_CHUSEOK_DOUGH_ITEM_ID = 7613;      // 작은 찹쌀반죽
constexpr t_itemid NEED_CHUSEOK_PINE_ITEM_ID = 1001827;    // 솔잎
constexpr t_itemid NEED_CHUSEOK_HONEY_ITEM_ID = 6532;      // 꿀송편

constexpr int32 NEED_CHUSEOK_RATE_SCALE = 10000;
constexpr int32 NEED_CHUSEOK_DOUGH_RATE = 400;   // 4.00%
constexpr uint16 NEED_CHUSEOK_DOUGH_AMOUNT = 4;
constexpr int32 NEED_CHUSEOK_PINE_RATE = 400;    // 4.00%
constexpr uint16 NEED_CHUSEOK_PINE_AMOUNT = 1;
constexpr int32 NEED_CHUSEOK_HONEY_RATE = 10;     // 0.10% (10 / 10000)
constexpr int32 NEED_CHUSEOK_LEVEL_DIFFERENCE = 30;		// 2026-10-19 간담회: ±15 -> ±30 (추석 드롭 전용, 경계 포함)
constexpr uint32 NEED_CHUSEOK_EVENT_ID = 202609;

constexpr int32 MSG_CHUSEOK_ITEM_UNAVAILABLE = 1546;
constexpr int32 MSG_CHUSEOK_SCHEMA_UNAVAILABLE = 1547;
constexpr int32 MSG_CHUSEOK_INVENTORY_FULL = 1548;
constexpr int32 MSG_CHUSEOK_ACCOUNT_LIMIT = 1549;
constexpr int32 MSG_CHUSEOK_IP_LIMIT = 1550;
constexpr int32 MSG_CHUSEOK_DATABASE_ERROR = 1551;
constexpr int32 MSG_CHUSEOK_HONEY_SUCCESS = 1552;
constexpr int32 MSG_CHUSEOK_HONEY_LOG = 1553;
constexpr int32 MSG_CHUSEOK_FAIL_CLOSED = 1554;
constexpr int32 MSG_CHUSEOK_FAMILY_UNAVAILABLE = 1555;

constexpr s_need_event_daily_spec NEED_CHUSEOK_HONEY_SPEC = {
	NEED_CHUSEOK_EVENT_ID, NEED_CHUSEOK_HONEY_ITEM_ID, 1, true, true,
	MSG_CHUSEOK_ITEM_UNAVAILABLE, MSG_CHUSEOK_SCHEMA_UNAVAILABLE, MSG_CHUSEOK_FAIL_CLOSED,
	MSG_CHUSEOK_HONEY_LOG, MSG_CHUSEOK_FAMILY_UNAVAILABLE,
	MSG_CHUSEOK_INVENTORY_FULL, MSG_CHUSEOK_ACCOUNT_LIMIT, MSG_CHUSEOK_IP_LIMIT,
	MSG_CHUSEOK_DATABASE_ERROR, MSG_CHUSEOK_HONEY_SUCCESS,
};

constexpr const char* RESULT_DELIVERED = "DELIVERED";
constexpr const char* RESULT_REJECTED = "REJECTED";
constexpr const char* RESULT_FAILED = "FAILED";
constexpr const char* ERROR_NONE = "";
constexpr const char* ERROR_INVENTORY = "INVENTORY_UNAVAILABLE";
constexpr const char* ERROR_ACCOUNT_LIMIT = "ACCOUNT_DAILY_LIMIT";
constexpr const char* ERROR_IP_LIMIT = "IP_DAILY_LIMIT";
constexpr const char* ERROR_INVALID_SESSION = "INVALID_SESSION";
constexpr const char* ERROR_ITEM_GRANT = "ITEM_GRANT_FAILED";
constexpr const char* ERROR_TRANSACTION = "SQL_TRANSACTION_FAILED";
constexpr const char* ERROR_COMMIT_UNCERTAIN = "COMMIT_UNCERTAIN";

bool material_items_checked = false;
bool material_items_ready = false;
s_need_event_daily_state honey_state;

bool need_chuseok_material_ready() {
	if (material_items_checked)
		return material_items_ready;

	material_items_checked = true;
	material_items_ready = true;
	if (!item_db.exists(NEED_CHUSEOK_DOUGH_ITEM_ID)) {
		ShowError(msg_txt(nullptr, MSG_CHUSEOK_ITEM_UNAVAILABLE), NEED_CHUSEOK_DOUGH_ITEM_ID);
		material_items_ready = false;
	}
	if (!item_db.exists(NEED_CHUSEOK_PINE_ITEM_ID)) {
		ShowError(msg_txt(nullptr, MSG_CHUSEOK_ITEM_UNAVAILABLE), NEED_CHUSEOK_PINE_ITEM_ID);
		material_items_ready = false;
	}
	return material_items_ready;
}

void need_event_daily_notify(const s_need_event_daily_spec& spec, map_session_data* sd, int32 msg_id) {
	if (spec.notify && sd != nullptr)
		clif_displaymessage(sd->fd, msg_txt(sd, msg_id));
}

// console_silent hides ShowInfo on the live server, so operational lines use ShowNotice.
void need_event_daily_console_log(const s_need_event_daily_spec& spec, const map_session_data* sd, const mob_data* md,
	const char* source, const char* ip, const char* logical_date, uint64 claim_id, const char* result, const char* error_code,
	uint32 family_group_id = 0, uint32 family_exception = 0) {
	char claim_id_text[32] = {};
	char output[1024] = {};
	safesnprintf(claim_id_text, sizeof(claim_id_text), "%" PRIu64, claim_id);
	const map_data* mapdata = md != nullptr ? map_getmapdata(md->m) : nullptr;
	safesnprintf(output, sizeof(output), msg_txt(nullptr, spec.msg_log),
		result, error_code, source != nullptr ? source : "",
		sd != nullptr ? sd->status.account_id : 0, sd != nullptr ? sd->status.char_id : 0,
		sd != nullptr ? sd->status.name : "", ip != nullptr ? ip : "", logical_date != nullptr ? logical_date : "",
		mapdata != nullptr ? mapdata->name : "", md != nullptr ? md->x : 0, md != nullptr ? md->y : 0,
		md != nullptr ? md->mob_id : 0, sd != nullptr ? sd->status.base_level : 0,
		family_group_id, family_exception, claim_id_text);
	ShowNotice("%s\n", output);
}

void need_event_daily_fail_closed(const s_need_event_daily_spec& spec, s_need_event_daily_state& state, map_session_data* sd) {
	if (!state.fail_closed)
		ShowError("%s\n", msg_txt(nullptr, spec.msg_fail_closed));
	state.fail_closed = true;
	state.schema_available = false;
	need_event_daily_notify(spec, sd, spec.msg_database_error);
}

// Operator approved family group for this account, or 0 when there is none.
//
// The list lives in the summer event tables (need_summer_attendance_family_*),
// reused here with event_id = spec.event_id so no extra schema has to be
// imported. Never fail-closed on this: when the tables are missing or the query
// errors, the account is simply treated as "no family" and the ip limit applies
// as usual. A broken family list must not take the whole honey songpyun down.
uint32 need_event_daily_family_group(const s_need_event_daily_spec& spec, s_need_event_daily_state& state, uint32 account_id) {
	if (!spec.family_exception || state.family_lookup_disabled || mmysql_handle == nullptr)
		return 0;

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `m`.`family_group_id` FROM `need_summer_attendance_family_member` `m` "
		"JOIN `need_summer_attendance_family_group` `g` "
		"ON `g`.`event_id`=`m`.`event_id` AND `g`.`family_group_id`=`m`.`family_group_id` "
		"WHERE `m`.`event_id`='%u' AND `m`.`account_id`='%u' AND `m`.`active`='1' AND `g`.`active`='1' LIMIT 1",
		spec.event_id, account_id)) {
		Sql_ShowDebug(mmysql_handle);
		state.family_lookup_disabled = true;
		ShowWarning("%s\n", msg_txt(nullptr, spec.msg_family_unavailable));
		return 0;
	}

	uint32 group_id = 0;
	if (Sql_NumRows(mmysql_handle) > 0 && SQL_SUCCESS == Sql_NextRow(mmysql_handle))
		need_event_hunt_sql_uint32(0, group_id);
	Sql_FreeResult(mmysql_handle);
	return group_id;
}

bool need_event_daily_record_log(const s_need_event_daily_spec& spec, map_session_data* sd, const mob_data* md,
	const char* source, const char* ip, const char* logical_date, uint64 claim_id, const char* result, const char* failure_code,
	uint32 family_group_id = 0, uint32 family_exception = 0) {
	char escaped_name[NAME_LENGTH * 2 + 1] = {};
	char escaped_map[MAP_NAME_LENGTH_EXT * 2 + 1] = {};
	char escaped_source[33] = {};
	char escaped_result[33] = {};
	char escaped_failure[129] = {};
	const map_data* mapdata = md != nullptr ? map_getmapdata(md->m) : nullptr;
	Sql_EscapeStringLen(mmysql_handle, escaped_name, sd->status.name, strnlen(sd->status.name, NAME_LENGTH));
	Sql_EscapeStringLen(mmysql_handle, escaped_map, mapdata != nullptr ? mapdata->name : "",
		mapdata != nullptr ? strnlen(mapdata->name, MAP_NAME_LENGTH) : 0);
	Sql_EscapeStringLen(mmysql_handle, escaped_source, source, strnlen(source, 16));
	Sql_EscapeStringLen(mmysql_handle, escaped_result, result, strnlen(result, 16));
	Sql_EscapeStringLen(mmysql_handle, escaped_failure, failure_code, strnlen(failure_code, 64));

	return SQL_ERROR != Sql_Query(mmysql_handle,
		"INSERT INTO `need_chuseok_honey_log` "
		"(`claim_id`,`event_id`,`logical_date`,`account_id`,`char_id`,`char_name`,`ip`,`source`,"
		"`family_group_id`,`family_exception`,`map_name`,`x`,`y`,`mob_id`,`player_level`,`result`,`failure_code`) "
		"VALUES ('%" PRIu64 "','%u','%s','%u','%u','%s',INET6_ATON('%s'),'%s','%u','%u','%s','%d','%d','%d','%u','%s','%s')",
		claim_id, spec.event_id, logical_date, sd->status.account_id, sd->status.char_id, escaped_name, ip,
		escaped_source, family_group_id, family_exception, escaped_map, md != nullptr ? md->x : 0, md != nullptr ? md->y : 0,
		md != nullptr ? md->mob_id : 0, sd->status.base_level, escaped_result, escaped_failure);
}

void need_event_daily_log_after_rollback(const s_need_event_daily_spec& spec, s_need_event_daily_state& state,
	map_session_data* sd, const mob_data* md, const char* source, const char* ip,
	const char* logical_date, const char* result, const char* error_code,
	uint32 family_group_id = 0, uint32 family_exception = 0) {
	if (!need_event_daily_record_log(spec, sd, md, source, ip, logical_date, 0, result, error_code,
		family_group_id, family_exception)) {
		Sql_ShowDebug(mmysql_handle);
		need_event_daily_fail_closed(spec, state, sd);
	}
	need_event_daily_console_log(spec, sd, md, source, ip, logical_date, 0, result, error_code,
		family_group_id, family_exception);
}

}  // namespace

bool need_event_daily_schema_ready(const s_need_event_daily_spec& spec, s_need_event_daily_state& state) {
	if (state.fail_closed)
		return false;
	if (state.schema_checked)
		return state.schema_available;

	state.schema_checked = true;
	state.schema_available = false;
	if (mmysql_handle == nullptr) {
		ShowError("%s\n", msg_txt(nullptr, spec.msg_schema_unavailable));
		state.fail_closed = true;
		return false;
	}

	static const char* checks[] = {
		"SELECT `claim_id`,`event_id`,`logical_date`,`account_id`,`source`,`family_group_id`,`family_exception`,`status` FROM `need_chuseok_honey_claim` LIMIT 0",
		"SELECT `event_id`,`logical_date`,`ip`,`first_account_id`,`family_group_id` FROM `need_chuseok_honey_ip_daily` LIMIT 0",
		"SELECT `log_id`,`claim_id`,`event_id`,`logical_date`,`account_id`,`source`,`family_group_id`,`family_exception`,`result`,`failure_code` FROM `need_chuseok_honey_log` LIMIT 0",
	};
	for (const char* query : checks) {
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, query)) {
			Sql_ShowDebug(mmysql_handle);
			ShowError("%s\n", msg_txt(nullptr, spec.msg_schema_unavailable));
			state.fail_closed = true;
			return false;
		}
		Sql_FreeResult(mmysql_handle);
	}

	if (SQL_ERROR == Sql_QueryStr(mmysql_handle,
		"SELECT COUNT(*) FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA`=DATABASE() "
		"AND `TABLE_NAME` IN ('need_chuseok_honey_claim','need_chuseok_honey_ip_daily','need_chuseok_honey_log') "
		"AND `ENGINE`='InnoDB'") || SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_FreeResult(mmysql_handle);
		ShowError("%s\n", msg_txt(nullptr, spec.msg_schema_unavailable));
		state.fail_closed = true;
		return false;
	}
	uint32 innodb_tables = 0;
	bool ready = need_event_hunt_sql_uint32(0, innodb_tables) && innodb_tables == 3;
	Sql_FreeResult(mmysql_handle);
	if (!ready) {
		ShowError("%s\n", msg_txt(nullptr, spec.msg_schema_unavailable));
		state.fail_closed = true;
		return false;
	}

	state.schema_available = true;
	return true;
}

e_need_chuseok_honey_result need_event_daily_grant(const s_need_event_daily_spec& spec, s_need_event_daily_state& state,
	map_session_data* sd, const char* source, const mob_data* md) {
	if (sd == nullptr)
		return NEED_CHUSEOK_HONEY_ERROR;
	if (source == nullptr || source[0] == '\0')
		source = "UNKNOWN";
	if (state.fail_closed)
		return NEED_CHUSEOK_HONEY_DISABLED;

	if (!item_db.exists(spec.item_id)) {
		ShowError(msg_txt(nullptr, spec.msg_item_unavailable), spec.item_id);
		state.fail_closed = true;
		return NEED_CHUSEOK_HONEY_DISABLED;
	}

	char ip[16] = {};
	need_event_hunt_date logical_date;
	if (!need_event_hunt_client_ip(sd, ip) || !need_event_hunt_logical_date(logical_date)) {
		if (need_event_daily_schema_ready(spec, state))
			need_event_daily_log_after_rollback(spec, state, sd, md, source, "0.0.0.0", "1970-01-01", RESULT_FAILED, ERROR_INVALID_SESSION);
		return NEED_CHUSEOK_HONEY_ERROR;
	}

	if (!need_event_hunt_inventory_ready(sd, spec.item_id, spec.amount)) {
		need_event_daily_notify(spec, sd, spec.msg_inventory_full);
		if (need_event_daily_schema_ready(spec, state))
			need_event_daily_log_after_rollback(spec, state, sd, md, source, ip, logical_date.sql_date, RESULT_FAILED, ERROR_INVENTORY);
		return NEED_CHUSEOK_HONEY_INVENTORY;
	}

	if (!need_event_daily_schema_ready(spec, state)) {
		need_event_daily_fail_closed(spec, state, sd);
		return NEED_CHUSEOK_HONEY_DISABLED;
	}

	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
		Sql_ShowDebug(mmysql_handle);
		need_event_daily_fail_closed(spec, state, sd);
		return NEED_CHUSEOK_HONEY_ERROR;
	}

	auto rollback = []() {
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "ROLLBACK"))
			Sql_ShowDebug(mmysql_handle);
	};
	auto transaction_failure = [&]() {
		Sql_ShowDebug(mmysql_handle);
		rollback();
		if (!need_event_daily_record_log(spec, sd, md, source, ip, logical_date.sql_date, 0, RESULT_FAILED, ERROR_TRANSACTION))
			Sql_ShowDebug(mmysql_handle);
		need_event_daily_console_log(spec, sd, md, source, ip, logical_date.sql_date, 0, RESULT_FAILED, ERROR_TRANSACTION);
		need_event_daily_fail_closed(spec, state, sd);
	};

	// Account side of the daily limit.
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `claim_id` FROM `need_chuseok_honey_claim` WHERE `event_id`='%u' AND `logical_date`='%s' "
		"AND `account_id`='%u' LIMIT 1 FOR UPDATE",
		spec.event_id, logical_date.sql_date, sd->status.account_id)) {
		transaction_failure();
		return NEED_CHUSEOK_HONEY_ERROR;
	}
	bool account_used = Sql_NumRows(mmysql_handle) > 0;
	Sql_FreeResult(mmysql_handle);
	if (account_used) {
		rollback();
		need_event_daily_log_after_rollback(spec, state, sd, md, source, ip, logical_date.sql_date, RESULT_REJECTED, ERROR_ACCOUNT_LIMIT);
		need_event_daily_notify(spec, sd, spec.msg_account_limit);
		return NEED_CHUSEOK_HONEY_ACCOUNT_LIMIT;
	}

	// IP side of the daily limit. An operator approved family shares one household
	// ip on purpose, so those accounts skip this check and keep only the account cap.
	uint32 family_group_id = need_event_daily_family_group(spec, state, sd->status.account_id);
	uint32 family_exception = 0;
	char escaped_source[33] = {};
	Sql_EscapeStringLen(mmysql_handle, escaped_source, source, strnlen(source, 16));
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT IGNORE INTO `need_chuseok_honey_ip_daily` "
		"(`event_id`,`logical_date`,`ip`,`first_account_id`,`first_char_id`,`family_group_id`,`source`) "
		"VALUES ('%u','%s',INET6_ATON('%s'),'%u','%u','%u','%s')",
		spec.event_id, logical_date.sql_date, ip, sd->status.account_id, sd->status.char_id,
		family_group_id, escaped_source)) {
		transaction_failure();
		return NEED_CHUSEOK_HONEY_ERROR;
	}
	if (Sql_NumRowsAffected(mmysql_handle) == 0) {
		if (family_group_id == 0) {
			rollback();
			need_event_daily_log_after_rollback(spec, state, sd, md, source, ip, logical_date.sql_date, RESULT_REJECTED, ERROR_IP_LIMIT);
			need_event_daily_notify(spec, sd, spec.msg_ip_limit);
			return NEED_CHUSEOK_HONEY_IP_LIMIT;
		}
		// The ip slot is already taken today, but this account is an approved family member.
		family_exception = 1;
	}

	char escaped_name[NAME_LENGTH * 2 + 1] = {};
	char escaped_map[MAP_NAME_LENGTH_EXT * 2 + 1] = {};
	const map_data* mapdata = md != nullptr ? map_getmapdata(md->m) : nullptr;
	Sql_EscapeStringLen(mmysql_handle, escaped_name, sd->status.name, strnlen(sd->status.name, NAME_LENGTH));
	Sql_EscapeStringLen(mmysql_handle, escaped_map, mapdata != nullptr ? mapdata->name : "",
		mapdata != nullptr ? strnlen(mapdata->name, MAP_NAME_LENGTH) : 0);
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT INTO `need_chuseok_honey_claim` "
		"(`event_id`,`logical_date`,`account_id`,`char_id`,`char_name`,`ip`,`source`,"
		"`family_group_id`,`family_exception`,`map_name`,`x`,`y`,`mob_id`,`player_level`,`status`) "
		"VALUES ('%u','%s','%u','%u','%s',INET6_ATON('%s'),'%s','%u','%u','%s','%d','%d','%d','%u','0')",
		spec.event_id, logical_date.sql_date, sd->status.account_id, sd->status.char_id, escaped_name, ip,
		escaped_source, family_group_id, family_exception, escaped_map, md != nullptr ? md->x : 0, md != nullptr ? md->y : 0,
		md != nullptr ? md->mob_id : 0, sd->status.base_level)) {
		transaction_failure();
		return NEED_CHUSEOK_HONEY_ERROR;
	}
	uint64 claim_id = Sql_LastInsertId(mmysql_handle);
	if (claim_id == 0) {
		transaction_failure();
		return NEED_CHUSEOK_HONEY_ERROR;
	}

	struct item reward = {};
	reward.nameid = spec.item_id;
	if (pc_additem(sd, &reward, spec.amount, LOG_TYPE_SCRIPT) != ADDITEM_SUCCESS) {
		rollback();
		need_event_daily_log_after_rollback(spec, state, sd, md, source, ip, logical_date.sql_date, RESULT_FAILED, ERROR_ITEM_GRANT);
		need_event_daily_notify(spec, sd, spec.msg_inventory_full);
		return NEED_CHUSEOK_HONEY_INVENTORY;
	}

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `need_chuseok_honey_claim` SET `status`='1',`delivered_at`=NOW(),`updated_at`=NOW() "
		"WHERE `claim_id`='%" PRIu64 "' AND `status`='0'", claim_id) || Sql_NumRowsAffected(mmysql_handle) != 1 ||
		!need_event_daily_record_log(spec, sd, md, source, ip, logical_date.sql_date, claim_id, RESULT_DELIVERED, ERROR_NONE,
			family_group_id, family_exception)) {
		Sql_ShowDebug(mmysql_handle);
		rollback();
		need_event_daily_console_log(spec, sd, md, source, ip, logical_date.sql_date, claim_id, RESULT_FAILED, ERROR_COMMIT_UNCERTAIN);
		need_event_daily_fail_closed(spec, state, sd);
		return NEED_CHUSEOK_HONEY_ERROR;
	}
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		Sql_ShowDebug(mmysql_handle);
		rollback();
		need_event_daily_console_log(spec, sd, md, source, ip, logical_date.sql_date, claim_id, RESULT_FAILED, ERROR_COMMIT_UNCERTAIN);
		need_event_daily_fail_closed(spec, state, sd);
		return NEED_CHUSEOK_HONEY_ERROR;
	}

	need_event_daily_notify(spec, sd, spec.msg_success);
	need_event_daily_console_log(spec, sd, md, source, ip, logical_date.sql_date, claim_id, RESULT_DELIVERED, ERROR_NONE,
		family_group_id, family_exception);
	return NEED_CHUSEOK_HONEY_DELIVERED;
}

e_need_chuseok_honey_result need_chuseok_honey_grant(map_session_data* sd, const char* source, const mob_data* md) {
	return need_event_daily_grant(NEED_CHUSEOK_HONEY_SPEC, honey_state, sd, source, md);
}

void need_chuseok_hunt_on_kill(map_session_data* sd, mob_data* md, int32 type) {
	if (battle_config.need_chuseok_hunt_enable == 0 ||
		!need_event_hunt_normal_field_target(sd, md, type, NEED_CHUSEOK_LEVEL_DIFFERENCE))
		return;

	if (battle_config.need_chuseok_hunt_material_enable != 0 && need_chuseok_material_ready()) {
		// The two materials roll independently. Inventory failures stay silent,
		// pc_additem already records successful grants in picklog.
		if (rnd_chance<int32>(NEED_CHUSEOK_DOUGH_RATE, NEED_CHUSEOK_RATE_SCALE))
			need_event_hunt_add_item(sd, NEED_CHUSEOK_DOUGH_ITEM_ID, NEED_CHUSEOK_DOUGH_AMOUNT);
		if (rnd_chance<int32>(NEED_CHUSEOK_PINE_RATE, NEED_CHUSEOK_RATE_SCALE))
			need_event_hunt_add_item(sd, NEED_CHUSEOK_PINE_ITEM_ID, NEED_CHUSEOK_PINE_AMOUNT);
	}

	if (battle_config.need_chuseok_hunt_honey_enable != 0 && !honey_state.fail_closed &&
		rnd_chance<int32>(NEED_CHUSEOK_HONEY_RATE, NEED_CHUSEOK_RATE_SCALE))
		need_chuseok_honey_grant(sd, "HUNT", md);
}

void need_chuseok_hunt_init() {
	material_items_checked = false;
	material_items_ready = false;
	honey_state = {};
	if (battle_config.need_chuseok_hunt_enable != 0 && battle_config.need_chuseok_hunt_material_enable != 0)
		need_chuseok_material_ready();
	if (battle_config.need_chuseok_hunt_enable != 0 && battle_config.need_chuseok_hunt_honey_enable != 0) {
		if (!item_db.exists(NEED_CHUSEOK_HONEY_ITEM_ID)) {
			ShowError(msg_txt(nullptr, MSG_CHUSEOK_ITEM_UNAVAILABLE), NEED_CHUSEOK_HONEY_ITEM_ID);
			honey_state.fail_closed = true;
		} else {
			need_event_daily_schema_ready(NEED_CHUSEOK_HONEY_SPEC, honey_state);
		}
	}
}

void need_chuseok_hunt_final() {
	material_items_checked = false;
	material_items_ready = false;
	honey_state = {};
}
