// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 summer attendance SQL provider.

#include "need_summer_attendance.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <string>
#include <unordered_map>
#include <utility>

#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>
#include <common/timer.hpp>

#include "battle.hpp"
#include "clif.hpp"
#include "map.hpp"
#include "pc.hpp"

namespace {

constexpr uint32 NEED_SUMMER_EVENT_ID = 202608;
constexpr int32 NEED_SUMMER_ATTENDANCE_TIMER_INTERVAL = 5000;
constexpr uint32 NEED_SUMMER_ATTENDANCE_FLUSH_SECONDS = 30;

constexpr int32 MSG_ATTENDANCE_READY = 1904;
constexpr int32 MSG_ATTENDANCE_REMAINING = 1905;
constexpr int32 MSG_ATTENDANCE_ALREADY_CLAIMED = 1906;
constexpr int32 MSG_ATTENDANCE_IP_USED = 1907;
constexpr int32 MSG_ATTENDANCE_MAX_CLAIMS = 1908;
constexpr int32 MSG_ATTENDANCE_DB_ERROR = 1909;
constexpr int32 MSG_ATTENDANCE_SUCCESS = 1910;
constexpr int32 MSG_ATTENDANCE_INVALID_SESSION = 1911;
constexpr int32 MSG_ATTENDANCE_CLAIM_LOG = 1912;
constexpr int32 MSG_ATTENDANCE_INVALID_CONFIG = 1913;
constexpr int32 MSG_ATTENDANCE_SCHEMA_UNAVAILABLE = 1914;

struct need_summer_logical_date {
	char sql_date[11] = {};
	uint32 numeric = 0;
};

struct need_summer_attendance_status {
	uint32 claimed_count = 0;
	uint32 online_seconds = 0;
	bool claimed_today = false;
};

struct need_summer_attendance_session {
	uint32 account_id = 0;
	std::string logical_date;
	time_t last_seen = 0;
	uint32 persisted_seconds = 0;
	uint32 pending_seconds = 0;
	bool ready_notified = false;
};

enum class need_summer_claim_result : uint8 {
	SUCCESS,
	NOT_ACTIVE,
	INVALID_SESSION,
	NOT_ENOUGH_TIME,
	ALREADY_CLAIMED,
	IP_ALREADY_USED,
	MAX_CLAIMS,
	DATABASE_ERROR,
};

std::unordered_map<uint32, need_summer_attendance_session> attendance_sessions;
int32 attendance_timer_id = INVALID_TIMER;
bool attendance_schema_checked = false;
bool attendance_schema_available = false;

void need_summer_attendance_sql_error() {
	if (mmysql_handle != nullptr)
		Sql_ShowDebug(mmysql_handle);
	attendance_schema_checked = true;
	attendance_schema_available = false;
}

bool need_summer_attendance_schema_ready() {
	if (!battle_config.feature_need_summer_attendance)
		return false;
	if (attendance_schema_checked)
		return attendance_schema_available;

	attendance_schema_checked = true;
	attendance_schema_available = false;
	if (mmysql_handle == nullptr)
		return false;

	static const char* checks[] = {
		"SELECT `event_id`,`account_id`,`claimed_count` FROM `need_summer_attendance_account` LIMIT 0",
		"SELECT `event_id`,`logical_date`,`account_id`,`online_seconds` FROM `need_summer_attendance_online` LIMIT 0",
		"SELECT `event_id`,`family_group_id`,`active` FROM `need_summer_attendance_family_group` LIMIT 0",
		"SELECT `event_id`,`account_id`,`family_group_id`,`active` FROM `need_summer_attendance_family_member` LIMIT 0",
		"SELECT `event_id`,`family_group_id`,`account_id`,`action` FROM `need_summer_attendance_family_audit` LIMIT 0",
		"SELECT `event_id`,`logical_date`,`ip`,`family_group_id` FROM `need_summer_attendance_ip_daily` LIMIT 0",
		"SELECT `claim_id`,`event_id`,`logical_date`,`account_id`,`claim_no`,`status` FROM `need_summer_attendance_claim` LIMIT 0",
		"SELECT `outbox_id`,`claim_id`,`event_id`,`account_id`,`char_id`,`status` FROM `need_summer_attendance_reward_outbox` LIMIT 0",
	};

	for (const char* query : checks) {
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, query)) {
			need_summer_attendance_sql_error();
			ShowError("%s\n", msg_txt(nullptr, MSG_ATTENDANCE_SCHEMA_UNAVAILABLE));
			return false;
		}
		Sql_FreeResult(mmysql_handle);
	}

	attendance_schema_available = true;
	return true;
}

bool need_summer_attendance_logical_date(time_t now, need_summer_logical_date& result) {
	time_t shifted = now - (4 * 60 * 60);
	struct tm* local = localtime(&shifted);

	if (local == nullptr || strftime(result.sql_date, sizeof(result.sql_date), "%Y-%m-%d", local) != 10)
		return false;

	result.numeric = static_cast<uint32>((local->tm_year + 1900) * 10000 + (local->tm_mon + 1) * 100 + local->tm_mday);
	return true;
}

time_t need_summer_attendance_next_boundary(time_t now) {
	time_t shifted = now - (4 * 60 * 60);
	struct tm* local = localtime(&shifted);

	if (local == nullptr)
		return now + NEED_SUMMER_ATTENDANCE_TIMER_INTERVAL / 1000;

	local->tm_mday += 1;
	local->tm_hour = 0;
	local->tm_min = 0;
	local->tm_sec = 0;
	local->tm_isdst = -1;

	time_t next_midnight = mktime(local);
	return next_midnight == static_cast<time_t>(-1) ? now + NEED_SUMMER_ATTENDANCE_TIMER_INTERVAL / 1000 : next_midnight + (4 * 60 * 60);
}

bool need_summer_attendance_config_valid() {
	return battle_config.need_summer_attendance_start_date <= battle_config.need_summer_attendance_end_date &&
		battle_config.need_summer_attendance_required_seconds > 0 &&
		battle_config.need_summer_attendance_max_claims > 0 &&
		battle_config.need_summer_attendance_max_claims <= 20;
}

bool need_summer_attendance_active_on(uint32 logical_date) {
	return battle_config.feature_attendance && battle_config.feature_need_summer_attendance &&
		need_summer_attendance_schema_ready() &&
		need_summer_attendance_config_valid() &&
		logical_date >= static_cast<uint32>(battle_config.need_summer_attendance_start_date) &&
		logical_date <= static_cast<uint32>(battle_config.need_summer_attendance_end_date);
}

bool need_summer_attendance_client_ip(map_session_data* sd, char (&ip)[16]) {
	if (sd == nullptr || sd->fd <= 0 || !session_isActive(sd->fd) || session[sd->fd]->client_addr == 0)
		return false;

	snprintf(ip, sizeof(ip), "%u.%u.%u.%u", CONVIP(session[sd->fd]->client_addr));
	return true;
}

bool need_summer_sql_uint32(uint32 column, uint32& value) {
	char* data = nullptr;

	if (SQL_SUCCESS != Sql_GetData(mmysql_handle, column, &data, nullptr) || data == nullptr)
		return false;

	value = static_cast<uint32>(strtoul(data, nullptr, 10));
	return true;
}

bool need_summer_attendance_load_seconds(uint32 account_id, const char* logical_date, uint32& seconds) {
	seconds = 0;

	if (mmysql_handle == nullptr || SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `online_seconds` FROM `need_summer_attendance_online` "
		"WHERE `event_id`='%u' AND `logical_date`='%s' AND `account_id`='%u' LIMIT 1",
		NEED_SUMMER_EVENT_ID, logical_date, account_id)) {
		need_summer_attendance_sql_error();
		return false;
	}

	if (Sql_NumRows(mmysql_handle) == 0) {
		Sql_FreeResult(mmysql_handle);
		return true;
	}

	bool result = SQL_SUCCESS == Sql_NextRow(mmysql_handle) && need_summer_sql_uint32(0, seconds);
	Sql_FreeResult(mmysql_handle);
	return result;
}

bool need_summer_attendance_query_status(uint32 account_id, const need_summer_logical_date& logical_date, need_summer_attendance_status& status) {
	if (mmysql_handle == nullptr || SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT "
		"COALESCE((SELECT `claimed_count` FROM `need_summer_attendance_account` WHERE `event_id`='%u' AND `account_id`='%u'),0),"
		"EXISTS(SELECT 1 FROM `need_summer_attendance_claim` WHERE `event_id`='%u' AND `logical_date`='%s' AND `account_id`='%u'),"
		"COALESCE((SELECT `online_seconds` FROM `need_summer_attendance_online` WHERE `event_id`='%u' AND `logical_date`='%s' AND `account_id`='%u'),0)",
		NEED_SUMMER_EVENT_ID, account_id, NEED_SUMMER_EVENT_ID, logical_date.sql_date, account_id,
		NEED_SUMMER_EVENT_ID, logical_date.sql_date, account_id)) {
		need_summer_attendance_sql_error();
		return false;
	}

	uint32 claimed_today = 0;
	bool result = SQL_SUCCESS == Sql_NextRow(mmysql_handle) &&
		need_summer_sql_uint32(0, status.claimed_count) &&
		need_summer_sql_uint32(1, claimed_today) &&
		need_summer_sql_uint32(2, status.online_seconds);
	Sql_FreeResult(mmysql_handle);
	status.claimed_today = claimed_today != 0;
	return result;
}

bool need_summer_attendance_flush(map_session_data* sd, need_summer_attendance_session& state) {
	if (state.pending_seconds == 0)
		return true;
	if (mmysql_handle == nullptr) {
		need_summer_attendance_sql_error();
		return false;
	}

	char ip[16] = {};
	bool has_ip = need_summer_attendance_client_ip(sd, ip);
	int32 result;

	if (has_ip) {
		result = Sql_Query(mmysql_handle,
			"INSERT INTO `need_summer_attendance_online` "
			"(`event_id`,`logical_date`,`account_id`,`online_seconds`,`last_char_id`,`last_ip`,`updated_at`) "
			"VALUES ('%u','%s','%u','%u','%u',INET6_ATON('%s'),NOW()) "
			"ON DUPLICATE KEY UPDATE `online_seconds`=LEAST('%d',`online_seconds`+VALUES(`online_seconds`)),"
			"`last_char_id`=VALUES(`last_char_id`),`last_ip`=VALUES(`last_ip`),`updated_at`=NOW()",
			NEED_SUMMER_EVENT_ID, state.logical_date.c_str(), state.account_id, state.pending_seconds,
			sd != nullptr ? sd->status.char_id : 0, ip, battle_config.need_summer_attendance_required_seconds);
	} else {
		result = Sql_Query(mmysql_handle,
			"INSERT INTO `need_summer_attendance_online` "
			"(`event_id`,`logical_date`,`account_id`,`online_seconds`,`last_char_id`,`last_ip`,`updated_at`) "
			"VALUES ('%u','%s','%u','%u','%u',NULL,NOW()) "
			"ON DUPLICATE KEY UPDATE `online_seconds`=LEAST('%d',`online_seconds`+VALUES(`online_seconds`)),"
			"`last_char_id`=VALUES(`last_char_id`),`updated_at`=NOW()",
			NEED_SUMMER_EVENT_ID, state.logical_date.c_str(), state.account_id, state.pending_seconds,
			sd != nullptr ? sd->status.char_id : 0, battle_config.need_summer_attendance_required_seconds);
	}

	if (result == SQL_ERROR) {
		need_summer_attendance_sql_error();
		return false;
	}

	state.persisted_seconds = std::min<uint32>(battle_config.need_summer_attendance_required_seconds,
		state.persisted_seconds + state.pending_seconds);
	state.pending_seconds = 0;
	return true;
}

bool need_summer_attendance_prepare_session(map_session_data* sd, time_t now, need_summer_attendance_session*& state) {
	if (sd == nullptr)
		return false;

	need_summer_logical_date logical_date;
	if (!need_summer_attendance_logical_date(now, logical_date))
		return false;

	auto iterator = attendance_sessions.find(sd->status.char_id);
	if (iterator == attendance_sessions.end() || iterator->second.account_id != sd->status.account_id) {
		uint32 persisted_seconds = 0;
		if (!need_summer_attendance_load_seconds(sd->status.account_id, logical_date.sql_date, persisted_seconds))
			return false;

		need_summer_attendance_session fresh;
		fresh.account_id = sd->status.account_id;
		fresh.logical_date = logical_date.sql_date;
		fresh.last_seen = now;
		fresh.persisted_seconds = persisted_seconds;
		iterator = attendance_sessions.insert_or_assign(sd->status.char_id, std::move(fresh)).first;
	}

	state = &iterator->second;
	return true;
}

bool need_summer_attendance_switch_day(map_session_data* sd, need_summer_attendance_session& state, const need_summer_logical_date& logical_date) {
	if (state.logical_date == logical_date.sql_date)
		return true;

	if (!need_summer_attendance_flush(sd, state))
		return false;

	uint32 persisted_seconds = 0;
	if (!need_summer_attendance_load_seconds(state.account_id, logical_date.sql_date, persisted_seconds))
		return false;

	state.logical_date = logical_date.sql_date;
	state.persisted_seconds = persisted_seconds;
	state.pending_seconds = 0;
	state.ready_notified = false;
	return true;
}

bool need_summer_attendance_countable(map_session_data* sd) {
	return sd != nullptr && sd->state.pc_loaded && !sd->state.autotrade && sd->fd > 0 &&
		session_isActive(sd->fd) && session[sd->fd]->client_addr != 0;
}

void need_summer_attendance_notify_ready(map_session_data* sd, need_summer_attendance_session& state) {
	if (state.ready_notified || sd == nullptr || !pc_has_permission(sd, PC_PERM_ATTENDANCE))
		return;

	need_summer_logical_date logical_date;
	need_summer_attendance_status status;
	if (!need_summer_attendance_logical_date(time(nullptr), logical_date) ||
		!need_summer_attendance_active_on(logical_date.numeric) ||
		!need_summer_attendance_query_status(sd->status.account_id, logical_date, status))
		return;

	if (status.online_seconds < static_cast<uint32>(battle_config.need_summer_attendance_required_seconds))
		return;

	state.ready_notified = true;
	if (status.claimed_today || status.claimed_count >= static_cast<uint32>(battle_config.need_summer_attendance_max_claims))
		return;

	clif_displaymessage(sd->fd, msg_txt(sd, MSG_ATTENDANCE_READY));
	clif_ui_open(*sd, OUT_UI_ATTENDANCE, 10 * status.claimed_count);
}

bool need_summer_attendance_update_session(map_session_data* sd, time_t now, bool notify_ready) {
	need_summer_attendance_session* state = nullptr;
	if (!need_summer_attendance_prepare_session(sd, now, state))
		return false;

	if (now < state->last_seen) {
		state->last_seen = now;
		return true;
	}

	bool countable = need_summer_attendance_countable(sd);
	while (state->last_seen < now) {
		need_summer_logical_date segment_date;
		if (!need_summer_attendance_logical_date(state->last_seen, segment_date) ||
			!need_summer_attendance_switch_day(sd, *state, segment_date))
			return false;

		time_t segment_end = std::min(now, need_summer_attendance_next_boundary(state->last_seen));
		if (segment_end <= state->last_seen)
			segment_end = now;

		if (countable && need_summer_attendance_active_on(segment_date.numeric)) {
			uint64 elapsed = static_cast<uint64>(segment_end - state->last_seen);
			uint32 current = state->persisted_seconds + state->pending_seconds;
			uint32 required = static_cast<uint32>(battle_config.need_summer_attendance_required_seconds);
			if (current < required)
				state->pending_seconds += static_cast<uint32>(std::min<uint64>(elapsed, required - current));
		}

		state->last_seen = segment_end;
	}

	uint32 total = state->persisted_seconds + state->pending_seconds;
	if (state->pending_seconds >= NEED_SUMMER_ATTENDANCE_FLUSH_SECONDS ||
		total >= static_cast<uint32>(battle_config.need_summer_attendance_required_seconds)) {
		if (!need_summer_attendance_flush(sd, *state))
			return false;
		total = state->persisted_seconds;
	}

	if (notify_ready && total >= static_cast<uint32>(battle_config.need_summer_attendance_required_seconds))
		need_summer_attendance_notify_ready(sd, *state);

	return true;
}

int32 need_summer_attendance_timer_sub(map_session_data* sd, va_list) {
	if (sd == nullptr)
		return 0;

	if (sd->state.autotrade) {
		need_summer_attendance_session_end(sd);
		return 0;
	}

	need_summer_logical_date logical_date;
	if (!need_summer_attendance_logical_date(time(nullptr), logical_date) ||
		(!need_summer_attendance_active_on(logical_date.numeric) && attendance_sessions.find(sd->status.char_id) == attendance_sessions.end()))
		return 0;

	need_summer_attendance_update_session(sd, time(nullptr), true);
	return 0;
}

TIMER_FUNC(need_summer_attendance_timer) {
	map_foreachpc(need_summer_attendance_timer_sub);
	return 0;
}

need_summer_claim_result need_summer_attendance_reserve(map_session_data* sd, uint32& claimed_count, uint32& remaining_seconds, uint32& family_group_id) {
	claimed_count = 0;
	remaining_seconds = 0;
	family_group_id = 0;

	need_summer_logical_date logical_date;
	if (!need_summer_attendance_logical_date(time(nullptr), logical_date) || !need_summer_attendance_active_on(logical_date.numeric))
		return need_summer_claim_result::NOT_ACTIVE;

	char ip[16] = {};
	if (sd == nullptr || sd->state.autotrade || !need_summer_attendance_client_ip(sd, ip))
		return need_summer_claim_result::INVALID_SESSION;

	if (!need_summer_attendance_update_session(sd, time(nullptr), false))
		return need_summer_claim_result::DATABASE_ERROR;
	auto session_iterator = attendance_sessions.find(sd->status.char_id);
	if (session_iterator == attendance_sessions.end() || !need_summer_attendance_flush(sd, session_iterator->second))
		return need_summer_claim_result::DATABASE_ERROR;

	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
		need_summer_attendance_sql_error();
		return need_summer_claim_result::DATABASE_ERROR;
	}

	auto rollback = []() {
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "ROLLBACK"))
			need_summer_attendance_sql_error();
	};

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT IGNORE INTO `need_summer_attendance_account` (`event_id`,`account_id`,`claimed_count`) VALUES ('%u','%u','0')",
		NEED_SUMMER_EVENT_ID, sd->status.account_id) ||
		SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `claimed_count` FROM `need_summer_attendance_account` WHERE `event_id`='%u' AND `account_id`='%u' FOR UPDATE",
		NEED_SUMMER_EVENT_ID, sd->status.account_id) || SQL_SUCCESS != Sql_NextRow(mmysql_handle) ||
		!need_summer_sql_uint32(0, claimed_count)) {
		need_summer_attendance_sql_error();
		Sql_FreeResult(mmysql_handle);
		rollback();
		return need_summer_claim_result::DATABASE_ERROR;
	}
	Sql_FreeResult(mmysql_handle);

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `claim_id` FROM `need_summer_attendance_claim` WHERE `event_id`='%u' AND `logical_date`='%s' "
		"AND `account_id`='%u' LIMIT 1 FOR UPDATE",
		NEED_SUMMER_EVENT_ID, logical_date.sql_date, sd->status.account_id)) {
		need_summer_attendance_sql_error();
		rollback();
		return need_summer_claim_result::DATABASE_ERROR;
	}
	bool already_claimed = Sql_NumRows(mmysql_handle) > 0;
	Sql_FreeResult(mmysql_handle);
	if (already_claimed) {
		rollback();
		return need_summer_claim_result::ALREADY_CLAIMED;
	}

	if (claimed_count >= static_cast<uint32>(battle_config.need_summer_attendance_max_claims)) {
		rollback();
		return need_summer_claim_result::MAX_CLAIMS;
	}

	uint32 online_seconds = 0;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `online_seconds` FROM `need_summer_attendance_online` WHERE `event_id`='%u' AND `logical_date`='%s' "
		"AND `account_id`='%u' FOR UPDATE",
		NEED_SUMMER_EVENT_ID, logical_date.sql_date, sd->status.account_id)) {
		need_summer_attendance_sql_error();
		rollback();
		return need_summer_claim_result::DATABASE_ERROR;
	}
	if (Sql_NumRows(mmysql_handle) > 0 &&
		(SQL_SUCCESS != Sql_NextRow(mmysql_handle) || !need_summer_sql_uint32(0, online_seconds))) {
		Sql_FreeResult(mmysql_handle);
		rollback();
		return need_summer_claim_result::DATABASE_ERROR;
	}
	Sql_FreeResult(mmysql_handle);

	uint32 required_seconds = static_cast<uint32>(battle_config.need_summer_attendance_required_seconds);
	if (online_seconds < required_seconds) {
		remaining_seconds = required_seconds - online_seconds;
		rollback();
		return need_summer_claim_result::NOT_ENOUGH_TIME;
	}

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `m`.`family_group_id` FROM `need_summer_attendance_family_member` `m` "
		"JOIN `need_summer_attendance_family_group` `g` ON `g`.`family_group_id`=`m`.`family_group_id` AND `g`.`event_id`=`m`.`event_id` "
		"WHERE `m`.`event_id`='%u' AND `m`.`account_id`='%u' AND `m`.`active`='1' AND `g`.`active`='1' LIMIT 1 FOR UPDATE",
		NEED_SUMMER_EVENT_ID, sd->status.account_id)) {
		need_summer_attendance_sql_error();
		rollback();
		return need_summer_claim_result::DATABASE_ERROR;
	}
	if (Sql_NumRows(mmysql_handle) > 0 &&
		(SQL_SUCCESS != Sql_NextRow(mmysql_handle) || !need_summer_sql_uint32(0, family_group_id))) {
		Sql_FreeResult(mmysql_handle);
		rollback();
		return need_summer_claim_result::DATABASE_ERROR;
	}
	Sql_FreeResult(mmysql_handle);

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT IGNORE INTO `need_summer_attendance_ip_daily` "
		"(`event_id`,`logical_date`,`ip`,`family_group_id`,`first_account_id`,`first_char_id`) "
		"VALUES ('%u','%s',INET6_ATON('%s'),'%u','%u','%u')",
		NEED_SUMMER_EVENT_ID, logical_date.sql_date, ip, family_group_id, sd->status.account_id, sd->status.char_id)) {
		need_summer_attendance_sql_error();
		rollback();
		return need_summer_claim_result::DATABASE_ERROR;
	}

	if (Sql_NumRowsAffected(mmysql_handle) == 0) {
		uint32 occupied_family_group_id = 0;
		uint32 first_account_id = 0;
		if (SQL_ERROR == Sql_Query(mmysql_handle,
			"SELECT `family_group_id`,`first_account_id` FROM `need_summer_attendance_ip_daily` "
			"WHERE `event_id`='%u' AND `logical_date`='%s' AND `ip`=INET6_ATON('%s') FOR UPDATE",
			NEED_SUMMER_EVENT_ID, logical_date.sql_date, ip) || SQL_SUCCESS != Sql_NextRow(mmysql_handle) ||
			!need_summer_sql_uint32(0, occupied_family_group_id) || !need_summer_sql_uint32(1, first_account_id)) {
			need_summer_attendance_sql_error();
			Sql_FreeResult(mmysql_handle);
			rollback();
			return need_summer_claim_result::DATABASE_ERROR;
		}
		Sql_FreeResult(mmysql_handle);

		if (first_account_id != sd->status.account_id &&
			(family_group_id == 0 || occupied_family_group_id != family_group_id)) {
			rollback();
			return need_summer_claim_result::IP_ALREADY_USED;
		}
	}

	uint32 next_claim = claimed_count + 1;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT INTO `need_summer_attendance_claim` "
		"(`event_id`,`logical_date`,`account_id`,`char_id`,`ip`,`family_group_id`,`claim_no`,`online_seconds`,`status`) "
		"VALUES ('%u','%s','%u','%u',INET6_ATON('%s'),'%u','%u','%u','0')",
		NEED_SUMMER_EVENT_ID, logical_date.sql_date, sd->status.account_id, sd->status.char_id, ip,
		family_group_id, next_claim, online_seconds)) {
		need_summer_attendance_sql_error();
		rollback();
		return need_summer_claim_result::DATABASE_ERROR;
	}

	uint64 claim_id = Sql_LastInsertId(mmysql_handle);
	if (claim_id == 0 || SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT INTO `need_summer_attendance_reward_outbox` "
		"(`claim_id`,`event_id`,`account_id`,`char_id`,`token_item_id`,`token_amount`,`box_item_id`,`box_amount`,`status`) "
		"VALUES ('%" PRIu64 "','%u','%u','%u','399925','10','399928','1','0')",
		claim_id, NEED_SUMMER_EVENT_ID, sd->status.account_id, sd->status.char_id) ||
		SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `need_summer_attendance_account` SET `claimed_count`='%u',`updated_at`=NOW() "
		"WHERE `event_id`='%u' AND `account_id`='%u'",
		next_claim, NEED_SUMMER_EVENT_ID, sd->status.account_id) || Sql_NumRowsAffected(mmysql_handle) != 1) {
		need_summer_attendance_sql_error();
		rollback();
		return need_summer_claim_result::DATABASE_ERROR;
	}

	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		need_summer_attendance_sql_error();
		rollback();
		return need_summer_claim_result::DATABASE_ERROR;
	}

	claimed_count = next_claim;
	return need_summer_claim_result::SUCCESS;
}

}  // namespace

bool need_summer_attendance_enabled() {
	need_summer_logical_date logical_date;
	return need_summer_attendance_logical_date(time(nullptr), logical_date) && need_summer_attendance_active_on(logical_date.numeric);
}

int32 need_summer_attendance_ui_progress(map_session_data* sd) {
	need_summer_logical_date logical_date;
	need_summer_attendance_status status;

	if (sd == nullptr || !need_summer_attendance_logical_date(time(nullptr), logical_date) ||
		!need_summer_attendance_active_on(logical_date.numeric) ||
		!need_summer_attendance_query_status(sd->status.account_id, logical_date, status))
		return 0;

	uint32 claimed_count = std::min<uint32>(status.claimed_count, battle_config.need_summer_attendance_max_claims);
	return static_cast<int32>(10 * claimed_count + (status.claimed_today ? 1 : 0));
}

bool need_summer_attendance_should_auto_open(map_session_data* sd) {
	if (sd == nullptr || !need_summer_attendance_enabled())
		return false;

	if (!need_summer_attendance_update_session(sd, time(nullptr), false))
		return false;
	auto iterator = attendance_sessions.find(sd->status.char_id);
	if (iterator != attendance_sessions.end()) {
		if (!need_summer_attendance_flush(sd, iterator->second))
			return false;
	}

	need_summer_logical_date logical_date;
	need_summer_attendance_status status;
	if (!need_summer_attendance_logical_date(time(nullptr), logical_date) ||
		!need_summer_attendance_query_status(sd->status.account_id, logical_date, status))
		return false;

	bool ready = status.online_seconds >= static_cast<uint32>(battle_config.need_summer_attendance_required_seconds) &&
		!status.claimed_today && status.claimed_count < static_cast<uint32>(battle_config.need_summer_attendance_max_claims);
	if (ready && iterator != attendance_sessions.end())
		iterator->second.ready_notified = true;
	return ready;
}

void need_summer_attendance_claim(map_session_data* sd) {
	uint32 claimed_count = 0;
	uint32 remaining_seconds = 0;
	uint32 family_group_id = 0;
	need_summer_claim_result result = need_summer_attendance_reserve(sd, claimed_count, remaining_seconds, family_group_id);
	char output[CHAT_SIZE_MAX] = {};

	switch (result) {
		case need_summer_claim_result::SUCCESS: {
			safesnprintf(output, sizeof(output), msg_txt(sd, MSG_ATTENDANCE_SUCCESS), claimed_count,
				battle_config.need_summer_attendance_max_claims);
			clif_displaymessage(sd->fd, output);
			clif_attendence_response(sd, claimed_count);

			need_summer_logical_date logical_date;
			if (need_summer_attendance_logical_date(time(nullptr), logical_date)) {
				safesnprintf(output, sizeof(output), msg_txt(sd, MSG_ATTENDANCE_CLAIM_LOG), sd->status.account_id,
					sd->status.char_id, logical_date.sql_date, claimed_count, family_group_id);
				ShowInfo("%s\n", output);
			}
			break;
		}
		case need_summer_claim_result::NOT_ENOUGH_TIME:
			safesnprintf(output, sizeof(output), msg_txt(sd, MSG_ATTENDANCE_REMAINING), remaining_seconds / 60, remaining_seconds % 60);
			clif_displaymessage(sd->fd, output);
			break;
		case need_summer_claim_result::ALREADY_CLAIMED:
			clif_displaymessage(sd->fd, msg_txt(sd, MSG_ATTENDANCE_ALREADY_CLAIMED));
			break;
		case need_summer_claim_result::IP_ALREADY_USED:
			clif_displaymessage(sd->fd, msg_txt(sd, MSG_ATTENDANCE_IP_USED));
			break;
		case need_summer_claim_result::MAX_CLAIMS:
			safesnprintf(output, sizeof(output), msg_txt(sd, MSG_ATTENDANCE_MAX_CLAIMS), battle_config.need_summer_attendance_max_claims);
			clif_displaymessage(sd->fd, output);
			break;
		case need_summer_claim_result::INVALID_SESSION:
			clif_displaymessage(sd->fd, msg_txt(sd, MSG_ATTENDANCE_INVALID_SESSION));
			break;
		case need_summer_claim_result::NOT_ACTIVE:
		case need_summer_claim_result::DATABASE_ERROR:
		default:
			clif_displaymessage(sd->fd, msg_txt(sd, MSG_ATTENDANCE_DB_ERROR));
			break;
	}
}

void need_summer_attendance_session_start(map_session_data* sd) {
	if (sd == nullptr || !need_summer_attendance_enabled())
		return;

	need_summer_attendance_session* state = nullptr;
	need_summer_attendance_prepare_session(sd, time(nullptr), state);
}

void need_summer_attendance_session_pause(map_session_data* sd) {
	need_summer_attendance_session_end(sd);
}

void need_summer_attendance_session_end(map_session_data* sd) {
	if (sd == nullptr)
		return;

	auto iterator = attendance_sessions.find(sd->status.char_id);
	if (iterator == attendance_sessions.end())
		return;

	need_summer_attendance_update_session(sd, time(nullptr), false);
	iterator = attendance_sessions.find(sd->status.char_id);
	if (iterator != attendance_sessions.end()) {
		need_summer_attendance_flush(sd, iterator->second);
		attendance_sessions.erase(iterator);
	}
}

void need_summer_attendance_init() {
	attendance_schema_checked = false;
	attendance_schema_available = false;

	if (!need_summer_attendance_config_valid()) {
		ShowWarning("%s\n", msg_txt(nullptr, MSG_ATTENDANCE_INVALID_CONFIG));
		return;
	}

	add_timer_func_list(need_summer_attendance_timer, "need_summer_attendance_timer");
	attendance_timer_id = add_timer_interval(gettick() + NEED_SUMMER_ATTENDANCE_TIMER_INTERVAL,
		need_summer_attendance_timer, 0, 0, NEED_SUMMER_ATTENDANCE_TIMER_INTERVAL);
}

void need_summer_attendance_final() {
	if (attendance_timer_id != INVALID_TIMER) {
		delete_timer(attendance_timer_id, need_summer_attendance_timer);
		attendance_timer_id = INVALID_TIMER;
	}
	attendance_sessions.clear();
	attendance_schema_checked = false;
	attendance_schema_available = false;
}
