// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 summer field hunting rewards.

#include "need_summer_hunt.hpp"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>

#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>

#include "battle.hpp"
#include "clif.hpp"
#include "itemdb.hpp"
#include "log.hpp"
#include "map.hpp"
#include "mob.hpp"
#include "pc.hpp"

namespace {

constexpr t_itemid NEED_SUMMER_FRAGMENT_ITEM_ID = 399926;
constexpr t_itemid NEED_SUMMER_GOLDEN_ITEM_ID = 399927;
// Watermelon fragment drop rate scales with the reward owner's base level (fising.md).
// Rate is per NEED_SUMMER_RATE_SCALE. Base (<=159) stays at the previous 5%.
constexpr int32 NEED_SUMMER_FRAGMENT_RATE_LOW = 500; // 5%  base level <= 159
constexpr int32 NEED_SUMMER_FRAGMENT_RATE_MID = 600; // 6%  base level 160 ~ 174
constexpr int32 NEED_SUMMER_FRAGMENT_RATE_MAX = 700; // 7%  base level >= 175
constexpr int32 NEED_SUMMER_FRAGMENT_LEVEL_MID = 160;
constexpr int32 NEED_SUMMER_FRAGMENT_LEVEL_MAX = 175;
constexpr int32 NEED_SUMMER_GOLDEN_RATE = 10;
constexpr int32 NEED_SUMMER_RATE_SCALE = 10000;
constexpr int32 NEED_SUMMER_LEVEL_DIFFERENCE = 15;
constexpr uint32 NEED_SUMMER_EVENT_ID = 202608;

constexpr int32 MSG_SUMMER_HUNT_ITEM_UNAVAILABLE = 1934;
constexpr int32 MSG_SUMMER_HUNT_SCHEMA_UNAVAILABLE = 1935;
constexpr int32 MSG_SUMMER_HUNT_INVENTORY_FULL = 1936;
constexpr int32 MSG_SUMMER_HUNT_ACCOUNT_LIMIT = 1937;
constexpr int32 MSG_SUMMER_HUNT_IP_LIMIT = 1938;
constexpr int32 MSG_SUMMER_HUNT_DATABASE_ERROR = 1939;
constexpr int32 MSG_SUMMER_HUNT_GOLDEN_SUCCESS = 1940;
constexpr int32 MSG_SUMMER_HUNT_GOLDEN_LOG = 1941;
constexpr int32 MSG_SUMMER_HUNT_FAIL_CLOSED = 1942;

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

bool fragment_item_ready = false;
bool fragment_item_checked = false;
bool golden_schema_checked = false;
bool golden_schema_available = false;
bool golden_fail_closed = false;

struct need_summer_hunt_date {
	char sql_date[11] = {};
};

bool need_summer_hunt_fragment_ready() {
	if (fragment_item_checked)
		return fragment_item_ready;

	fragment_item_checked = true;
	fragment_item_ready = item_db.exists(NEED_SUMMER_FRAGMENT_ITEM_ID);
	if (!fragment_item_ready)
		ShowError(msg_txt(nullptr, MSG_SUMMER_HUNT_ITEM_UNAVAILABLE), NEED_SUMMER_FRAGMENT_ITEM_ID);

	return fragment_item_ready;
}

bool need_summer_hunt_sql_uint32(uint32 column, uint32& value) {
	char* data = nullptr;
	if (SQL_SUCCESS != Sql_GetData(mmysql_handle, column, &data, nullptr) || data == nullptr)
		return false;
	value = static_cast<uint32>(strtoul(data, nullptr, 10));
	return true;
}

bool need_summer_hunt_logical_date(need_summer_hunt_date& result) {
	time_t shifted = time(nullptr) - (4 * 60 * 60);
	struct tm* local = localtime(&shifted);
	return local != nullptr && strftime(result.sql_date, sizeof(result.sql_date), "%Y-%m-%d", local) == 10;
}

bool need_summer_hunt_client_ip(map_session_data* sd, char (&ip)[16]) {
	if (sd == nullptr || sd->fd <= 0 || !session_isActive(sd->fd) || session[sd->fd]->client_addr == 0)
		return false;
	snprintf(ip, sizeof(ip), "%u.%u.%u.%u", CONVIP(session[sd->fd]->client_addr));
	return true;
}

void need_summer_hunt_console_log(const map_session_data* sd, const mob_data* md, const char* ip,
	const char* logical_date, uint32 family_exception, uint64 claim_id, const char* result, const char* error_code) {
	char claim_id_text[32] = {};
	char output[1024] = {};
	safesnprintf(claim_id_text, sizeof(claim_id_text), "%" PRIu64, claim_id);
	const map_data* mapdata = md != nullptr ? map_getmapdata(md->m) : nullptr;
	safesnprintf(output, sizeof(output), msg_txt(nullptr, MSG_SUMMER_HUNT_GOLDEN_LOG),
		result, error_code, sd != nullptr ? sd->status.account_id : 0, sd != nullptr ? sd->status.char_id : 0,
		sd != nullptr ? sd->status.name : "", ip != nullptr ? ip : "", logical_date != nullptr ? logical_date : "",
		mapdata != nullptr ? mapdata->name : "", md != nullptr ? md->x : 0, md != nullptr ? md->y : 0,
		md != nullptr ? md->mob_id : 0, sd != nullptr ? sd->status.base_level : 0, md != nullptr ? md->level : 0,
		family_exception, claim_id_text);
	ShowInfo("%s\n", output);
}

void need_summer_hunt_fail_closed(map_session_data* sd) {
	if (!golden_fail_closed)
		ShowError("%s\n", msg_txt(nullptr, MSG_SUMMER_HUNT_FAIL_CLOSED));
	golden_fail_closed = true;
	golden_schema_available = false;
	if (sd != nullptr)
		clif_displaymessage(sd->fd, msg_txt(sd, MSG_SUMMER_HUNT_DATABASE_ERROR));
}

bool need_summer_hunt_golden_schema_ready() {
	if (golden_fail_closed)
		return false;
	if (golden_schema_checked)
		return golden_schema_available;

	golden_schema_checked = true;
	golden_schema_available = false;
	if (mmysql_handle == nullptr) {
		ShowError("%s\n", msg_txt(nullptr, MSG_SUMMER_HUNT_SCHEMA_UNAVAILABLE));
		golden_fail_closed = true;
		return false;
	}

	static const char* checks[] = {
		"SELECT `event_id`,`family_group_id`,`active` FROM `need_summer_attendance_family_group` LIMIT 0",
		"SELECT `event_id`,`account_id`,`family_group_id`,`active` FROM `need_summer_attendance_family_member` LIMIT 0",
		"SELECT `event_id`,`family_group_id`,`account_id`,`action` FROM `need_summer_attendance_family_audit` LIMIT 0",
		"SELECT `claim_id`,`event_id`,`logical_date`,`account_id`,`status` FROM `need_summer_hunt_golden_claim` LIMIT 0",
		"SELECT `event_id`,`logical_date`,`ip`,`family_group_id` FROM `need_summer_hunt_golden_ip_daily` LIMIT 0",
		"SELECT `log_id`,`claim_id`,`event_id`,`logical_date`,`account_id`,`result`,`failure_code` FROM `need_summer_hunt_golden_log` LIMIT 0",
	};
	for (const char* query : checks) {
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, query)) {
			Sql_ShowDebug(mmysql_handle);
			ShowError("%s\n", msg_txt(nullptr, MSG_SUMMER_HUNT_SCHEMA_UNAVAILABLE));
			golden_fail_closed = true;
			return false;
		}
		Sql_FreeResult(mmysql_handle);
	}

	if (SQL_ERROR == Sql_QueryStr(mmysql_handle,
		"SELECT COUNT(*) FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA`=DATABASE() "
		"AND `TABLE_NAME` IN ('need_summer_attendance_family_group','need_summer_attendance_family_member',"
		"'need_summer_attendance_family_audit','need_summer_hunt_golden_claim','need_summer_hunt_golden_ip_daily',"
		"'need_summer_hunt_golden_log') AND `ENGINE`='InnoDB'") || SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_FreeResult(mmysql_handle);
		ShowError("%s\n", msg_txt(nullptr, MSG_SUMMER_HUNT_SCHEMA_UNAVAILABLE));
		golden_fail_closed = true;
		return false;
	}
	uint32 innodb_tables = 0;
	bool ready = need_summer_hunt_sql_uint32(0, innodb_tables) && innodb_tables == 6;
	Sql_FreeResult(mmysql_handle);
	if (!ready) {
		ShowError("%s\n", msg_txt(nullptr, MSG_SUMMER_HUNT_SCHEMA_UNAVAILABLE));
		golden_fail_closed = true;
		return false;
	}

	golden_schema_available = true;
	return true;
}

bool need_summer_hunt_normal_field_target(const map_session_data* sd, const mob_data* md, int32 type) {
	if (sd == nullptr || md == nullptr || (type & 1) != 0 || md->state.npc_killmonster)
		return false;
	if (md->db == nullptr || md->mob_id <= 0 || md->level <= 0)
		return false;

	const map_data* mapdata = map_getmapdata(md->m);
	if (mapdata == nullptr || mapdata->instance_id > 0 || mapdata_flag_vs2(mapdata))
		return false;

	// Boss-type / boss-mode field mobs are allowed to drop the fragment (fising.md sec.3),
	// e.g. lhz_dun_n. WoE guardians and battleground mobs remain excluded (not field mobs).
	if (md->guardian_data != nullptr || md->bg_id != 0)
		return false;
	if (md->master_id != 0 || md->special_state.ai != AI_NONE || md->special_state.clone)
		return false;
	if (md->spawn == nullptr || md->deletetimer != INVALID_TIMER)
		return false;

	const int64 level_difference = static_cast<int64>(sd->status.base_level) - static_cast<int64>(md->level);
	return level_difference >= -NEED_SUMMER_LEVEL_DIFFERENCE && level_difference <= NEED_SUMMER_LEVEL_DIFFERENCE;
}

bool need_summer_hunt_add_fragment(map_session_data* sd) {
	std::shared_ptr<item_data> item_data = item_db.find(NEED_SUMMER_FRAGMENT_ITEM_ID);
	if (item_data == nullptr)
		return false;

	if (sd->weight + item_data->weight > sd->max_weight)
		return false;

	char add_check = pc_checkadditem(sd, NEED_SUMMER_FRAGMENT_ITEM_ID, 1);
	if (add_check == CHKADDITEM_OVERAMOUNT || (add_check == CHKADDITEM_NEW && pc_inventoryblank(sd) == 0))
		return false;

	struct item reward = {};
	reward.nameid = NEED_SUMMER_FRAGMENT_ITEM_ID;
	return pc_additem(sd, &reward, 1, LOG_TYPE_PICKDROP_MONSTER) == ADDITEM_SUCCESS;
}

bool need_summer_hunt_inventory_ready(map_session_data* sd, t_itemid item_id) {
	std::shared_ptr<item_data> item_data = item_db.find(item_id);
	if (item_data == nullptr)
		return false;
	if (static_cast<uint64>(sd->weight) + static_cast<uint64>(item_data->weight) > static_cast<uint64>(sd->max_weight))
		return false;
	char add_check = pc_checkadditem(sd, item_id, 1);
	return add_check != CHKADDITEM_OVERAMOUNT && (add_check != CHKADDITEM_NEW || pc_inventoryblank(sd) > 0);
}

bool need_summer_hunt_record_log(map_session_data* sd, mob_data* md, const char* ip,
	const char* logical_date, uint32 family_group_id, uint32 family_exception, uint64 claim_id,
	const char* result, const char* failure_code) {
	char escaped_name[NAME_LENGTH * 2 + 1] = {};
	char escaped_map[MAP_NAME_LENGTH_EXT * 2 + 1] = {};
	char escaped_result[33] = {};
	char escaped_failure[129] = {};
	const map_data* mapdata = map_getmapdata(md->m);
	Sql_EscapeStringLen(mmysql_handle, escaped_name, sd->status.name, strnlen(sd->status.name, NAME_LENGTH));
	Sql_EscapeStringLen(mmysql_handle, escaped_map, mapdata != nullptr ? mapdata->name : "",
		mapdata != nullptr ? strnlen(mapdata->name, MAP_NAME_LENGTH) : 0);
	Sql_EscapeStringLen(mmysql_handle, escaped_result, result, strnlen(result, 16));
	Sql_EscapeStringLen(mmysql_handle, escaped_failure, failure_code, strnlen(failure_code, 64));

	return SQL_ERROR != Sql_Query(mmysql_handle,
		"INSERT INTO `need_summer_hunt_golden_log` "
		"(`claim_id`,`event_id`,`logical_date`,`account_id`,`char_id`,`char_name`,`ip`,`family_group_id`,`family_exception`,"
		"`map_name`,`x`,`y`,`mob_id`,`player_level`,`mob_level`,`result`,`failure_code`) "
		"VALUES ('%" PRIu64 "','%u','%s','%u','%u','%s',INET6_ATON('%s'),'%u','%u','%s','%d','%d','%d','%u','%d','%s','%s')",
		claim_id, NEED_SUMMER_EVENT_ID, logical_date, sd->status.account_id, sd->status.char_id, escaped_name, ip,
		family_group_id, family_exception, escaped_map, md->x, md->y, md->mob_id, sd->status.base_level, md->level,
		escaped_result, escaped_failure);
}

void need_summer_hunt_log_after_rollback(map_session_data* sd, mob_data* md, const char* ip,
	const char* logical_date, uint32 family_group_id, uint32 family_exception, const char* result, const char* error_code) {
	if (!need_summer_hunt_record_log(sd, md, ip, logical_date, family_group_id, family_exception, 0, result, error_code)) {
		Sql_ShowDebug(mmysql_handle);
		need_summer_hunt_fail_closed(sd);
	}
	need_summer_hunt_console_log(sd, md, ip, logical_date, family_exception, 0, result, error_code);
}

void need_summer_hunt_try_golden(map_session_data* sd, mob_data* md) {
	if (!item_db.exists(NEED_SUMMER_GOLDEN_ITEM_ID)) {
		ShowError(msg_txt(nullptr, MSG_SUMMER_HUNT_ITEM_UNAVAILABLE), NEED_SUMMER_GOLDEN_ITEM_ID);
		golden_fail_closed = true;
		return;
	}

	char ip[16] = {};
	need_summer_hunt_date logical_date;
	if (!need_summer_hunt_client_ip(sd, ip) || !need_summer_hunt_logical_date(logical_date)) {
		if (need_summer_hunt_golden_schema_ready())
			need_summer_hunt_log_after_rollback(sd, md, "0.0.0.0", "1970-01-01", 0, 0, RESULT_FAILED, ERROR_INVALID_SESSION);
		return;
	}

	if (!need_summer_hunt_inventory_ready(sd, NEED_SUMMER_GOLDEN_ITEM_ID)) {
		clif_displaymessage(sd->fd, msg_txt(sd, MSG_SUMMER_HUNT_INVENTORY_FULL));
		if (need_summer_hunt_golden_schema_ready())
			need_summer_hunt_log_after_rollback(sd, md, ip, logical_date.sql_date, 0, 0, RESULT_FAILED, ERROR_INVENTORY);
		return;
	}

	if (!need_summer_hunt_golden_schema_ready()) {
		need_summer_hunt_fail_closed(sd);
		return;
	}

	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
		Sql_ShowDebug(mmysql_handle);
		need_summer_hunt_fail_closed(sd);
		return;
	}
	uint32 family_group_id = 0;
	uint32 family_exception = 0;
	auto rollback = []() {
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "ROLLBACK"))
			Sql_ShowDebug(mmysql_handle);
	};
	auto transaction_failure = [&]() {
		Sql_ShowDebug(mmysql_handle);
		rollback();
		if (!need_summer_hunt_record_log(sd, md, ip, logical_date.sql_date, family_group_id, family_exception,
			0, RESULT_FAILED, ERROR_TRANSACTION))
			Sql_ShowDebug(mmysql_handle);
		need_summer_hunt_console_log(sd, md, ip, logical_date.sql_date, family_exception, 0, RESULT_FAILED, ERROR_TRANSACTION);
		need_summer_hunt_fail_closed(sd);
	};

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `claim_id` FROM `need_summer_hunt_golden_claim` WHERE `event_id`='%u' AND `logical_date`='%s' "
		"AND `account_id`='%u' LIMIT 1 FOR UPDATE",
		NEED_SUMMER_EVENT_ID, logical_date.sql_date, sd->status.account_id)) {
		transaction_failure();
		return;
	}
	bool account_used = Sql_NumRows(mmysql_handle) > 0;
	Sql_FreeResult(mmysql_handle);
	if (account_used) {
		rollback();
		need_summer_hunt_log_after_rollback(sd, md, ip, logical_date.sql_date, 0, 0, RESULT_REJECTED, ERROR_ACCOUNT_LIMIT);
		clif_displaymessage(sd->fd, msg_txt(sd, MSG_SUMMER_HUNT_ACCOUNT_LIMIT));
		return;
	}

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `m`.`family_group_id` FROM `need_summer_attendance_family_member` `m` "
		"JOIN `need_summer_attendance_family_group` `g` ON `g`.`event_id`=`m`.`event_id` AND `g`.`family_group_id`=`m`.`family_group_id` "
		"WHERE `m`.`event_id`='%u' AND `m`.`account_id`='%u' AND `m`.`active`='1' AND `g`.`active`='1' LIMIT 1 FOR UPDATE",
		NEED_SUMMER_EVENT_ID, sd->status.account_id)) {
		transaction_failure();
		return;
	}
	if (Sql_NumRows(mmysql_handle) > 0 &&
		(SQL_SUCCESS != Sql_NextRow(mmysql_handle) || !need_summer_hunt_sql_uint32(0, family_group_id))) {
		Sql_FreeResult(mmysql_handle);
		transaction_failure();
		return;
	}
	Sql_FreeResult(mmysql_handle);

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT IGNORE INTO `need_summer_hunt_golden_ip_daily` "
		"(`event_id`,`logical_date`,`ip`,`family_group_id`,`first_account_id`,`first_char_id`) "
		"VALUES ('%u','%s',INET6_ATON('%s'),'%u','%u','%u')",
		NEED_SUMMER_EVENT_ID, logical_date.sql_date, ip, family_group_id, sd->status.account_id, sd->status.char_id)) {
		transaction_failure();
		return;
	}

	if (Sql_NumRowsAffected(mmysql_handle) == 0) {
		uint32 occupied_group = 0;
		uint32 first_account_id = 0;
		if (SQL_ERROR == Sql_Query(mmysql_handle,
			"SELECT `family_group_id`,`first_account_id` FROM `need_summer_hunt_golden_ip_daily` "
			"WHERE `event_id`='%u' AND `logical_date`='%s' AND `ip`=INET6_ATON('%s') FOR UPDATE",
			NEED_SUMMER_EVENT_ID, logical_date.sql_date, ip) || SQL_SUCCESS != Sql_NextRow(mmysql_handle) ||
			!need_summer_hunt_sql_uint32(0, occupied_group) || !need_summer_hunt_sql_uint32(1, first_account_id)) {
			Sql_FreeResult(mmysql_handle);
			transaction_failure();
			return;
		}
		Sql_FreeResult(mmysql_handle);
		if (first_account_id != sd->status.account_id && (family_group_id == 0 || occupied_group != family_group_id)) {
			rollback();
			need_summer_hunt_log_after_rollback(sd, md, ip, logical_date.sql_date, family_group_id, 0, RESULT_REJECTED, ERROR_IP_LIMIT);
			clif_displaymessage(sd->fd, msg_txt(sd, MSG_SUMMER_HUNT_IP_LIMIT));
			return;
		}
		family_exception = first_account_id != sd->status.account_id ? 1 : 0;
	}

	char escaped_name[NAME_LENGTH * 2 + 1] = {};
	char escaped_map[MAP_NAME_LENGTH_EXT * 2 + 1] = {};
	const map_data* mapdata = map_getmapdata(md->m);
	Sql_EscapeStringLen(mmysql_handle, escaped_name, sd->status.name, strnlen(sd->status.name, NAME_LENGTH));
	Sql_EscapeStringLen(mmysql_handle, escaped_map, mapdata != nullptr ? mapdata->name : "",
		mapdata != nullptr ? strnlen(mapdata->name, MAP_NAME_LENGTH) : 0);
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT INTO `need_summer_hunt_golden_claim` "
		"(`event_id`,`logical_date`,`account_id`,`char_id`,`char_name`,`ip`,`family_group_id`,`family_exception`,"
		"`map_name`,`x`,`y`,`mob_id`,`player_level`,`mob_level`,`status`) "
		"VALUES ('%u','%s','%u','%u','%s',INET6_ATON('%s'),'%u','%u','%s','%d','%d','%d','%u','%d','0')",
		NEED_SUMMER_EVENT_ID, logical_date.sql_date, sd->status.account_id, sd->status.char_id, escaped_name, ip,
		family_group_id, family_exception, escaped_map, md->x, md->y, md->mob_id, sd->status.base_level, md->level)) {
		transaction_failure();
		return;
	}
	uint64 claim_id = Sql_LastInsertId(mmysql_handle);
	if (claim_id == 0) {
		transaction_failure();
		return;
	}

	struct item reward = {};
	reward.nameid = NEED_SUMMER_GOLDEN_ITEM_ID;
	if (pc_additem(sd, &reward, 1, LOG_TYPE_PICKDROP_MONSTER) != ADDITEM_SUCCESS) {
		rollback();
		need_summer_hunt_log_after_rollback(sd, md, ip, logical_date.sql_date, family_group_id, family_exception, RESULT_FAILED, ERROR_ITEM_GRANT);
		clif_displaymessage(sd->fd, msg_txt(sd, MSG_SUMMER_HUNT_INVENTORY_FULL));
		return;
	}

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `need_summer_hunt_golden_claim` SET `status`='1',`delivered_at`=NOW(),`updated_at`=NOW() "
		"WHERE `claim_id`='%" PRIu64 "' AND `status`='0'", claim_id) || Sql_NumRowsAffected(mmysql_handle) != 1 ||
		!need_summer_hunt_record_log(sd, md, ip, logical_date.sql_date, family_group_id, family_exception,
			claim_id, RESULT_DELIVERED, ERROR_NONE)) {
		Sql_ShowDebug(mmysql_handle);
		rollback();
		need_summer_hunt_console_log(sd, md, ip, logical_date.sql_date, family_exception, claim_id, RESULT_FAILED, ERROR_COMMIT_UNCERTAIN);
		need_summer_hunt_fail_closed(sd);
		return;
	}
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		Sql_ShowDebug(mmysql_handle);
		rollback();
		need_summer_hunt_console_log(sd, md, ip, logical_date.sql_date, family_exception, claim_id, RESULT_FAILED, ERROR_COMMIT_UNCERTAIN);
		need_summer_hunt_fail_closed(sd);
		return;
	}

	clif_displaymessage(sd->fd, msg_txt(sd, MSG_SUMMER_HUNT_GOLDEN_SUCCESS));
	need_summer_hunt_console_log(sd, md, ip, logical_date.sql_date, family_exception, claim_id, RESULT_DELIVERED, ERROR_NONE);
}

// Reward-owner base level -> fragment drop rate bucket (fising.md sec.1/5).
int32 need_summer_fragment_rate(const map_session_data* sd) {
	int32 lv = (sd != nullptr) ? sd->status.base_level : 0;
	if (lv >= NEED_SUMMER_FRAGMENT_LEVEL_MAX)
		return NEED_SUMMER_FRAGMENT_RATE_MAX;
	if (lv >= NEED_SUMMER_FRAGMENT_LEVEL_MID)
		return NEED_SUMMER_FRAGMENT_RATE_MID;
	return NEED_SUMMER_FRAGMENT_RATE_LOW;
}

}  // namespace

void need_summer_hunt_on_kill(map_session_data* sd, mob_data* md, int32 type) {
	if (battle_config.need_summer_hunt_enable == 0 || !need_summer_hunt_normal_field_target(sd, md, type))
		return;

	if (battle_config.need_summer_hunt_fragment_enable != 0 && need_summer_hunt_fragment_ready() &&
		rnd_chance<int32>(need_summer_fragment_rate(sd), NEED_SUMMER_RATE_SCALE)) {
		// Frequent inventory failures are intentionally silent. pc_additem records successful grants in picklog.
		need_summer_hunt_add_fragment(sd);
	}

	if (battle_config.need_summer_hunt_golden_enable != 0 && !golden_fail_closed &&
		rnd_chance<int32>(NEED_SUMMER_GOLDEN_RATE, NEED_SUMMER_RATE_SCALE))
		need_summer_hunt_try_golden(sd, md);
}

void need_summer_hunt_init() {
	fragment_item_checked = false;
	fragment_item_ready = false;
	golden_schema_checked = false;
	golden_schema_available = false;
	golden_fail_closed = false;
	if (battle_config.need_summer_hunt_enable != 0 && battle_config.need_summer_hunt_fragment_enable != 0)
		need_summer_hunt_fragment_ready();
	if (battle_config.need_summer_hunt_enable != 0 && battle_config.need_summer_hunt_golden_enable != 0) {
		if (!item_db.exists(NEED_SUMMER_GOLDEN_ITEM_ID)) {
			ShowError(msg_txt(nullptr, MSG_SUMMER_HUNT_ITEM_UNAVAILABLE), NEED_SUMMER_GOLDEN_ITEM_ID);
			golden_fail_closed = true;
		} else {
			need_summer_hunt_golden_schema_ready();
		}
	}
}

void need_summer_hunt_final() {
	fragment_item_checked = false;
	fragment_item_ready = false;
	golden_schema_checked = false;
	golden_schema_available = false;
	golden_fail_closed = false;
}
