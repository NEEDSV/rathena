// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 summer event reward origin log (item side).

#include "need_summer_reward_log.hpp"

#include <cinttypes>
#include <cstring>

#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>

#include "map.hpp"
#include "pc.hpp"

namespace {

constexpr uint32 NEED_SUMMER_EVENT_ID = 202608;
constexpr uint32 NEED_SUMMER_REWARD_TYPE_ITEM = 3; // 1=ZENY, 2=CASH, 3=ITEM

bool reward_log_checked = false;
bool reward_log_available = false;

// One-time schema probe. Never runs on a hot path (only the shadow box grant reaches here).
bool need_summer_reward_log_ready() {
	if (reward_log_checked)
		return reward_log_available;
	reward_log_checked = true;
	reward_log_available = false;
	if (mmysql_handle == nullptr)
		return false;
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "SELECT `id` FROM `need_summer_reward_log` LIMIT 0")) {
		ShowWarning("need_summer_reward_log: table `need_summer_reward_log` not found; item origin logging disabled.\n");
		return false;
	}
	Sql_FreeResult(mmysql_handle);
	reward_log_available = true;
	return true;
}

} // namespace

void need_summer_reward_log_item( map_session_data* sd, uint32 source_item_id, uint32 reward_item_id, uint32 amount, uint64 unique_id ) {
	if (sd == nullptr || mmysql_handle == nullptr)
		return;
	if (!need_summer_reward_log_ready())
		return;

	char ip[16] = "";
	if (sd->fd > 0 && session_isActive(sd->fd) && session[sd->fd]->client_addr != 0)
		snprintf(ip, sizeof(ip), "%u.%u.%u.%u", CONVIP(session[sd->fd]->client_addr));
	char escaped_ip[sizeof(ip) * 2 + 1] = {};
	Sql_EscapeStringLen(mmysql_handle, escaped_ip, ip, strnlen(ip, sizeof(ip)));

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT INTO `need_summer_reward_log` "
		"(`event_id`,`source_item_id`,`account_id`,`char_id`,`ip`,`reward_type`,`reward_item_id`,`reward_amount`,`unique_id`,`created_at`) "
		"VALUES ('%u','%u','%u','%u','%s','%u','%u','%u','%" PRIu64 "',NOW())",
		NEED_SUMMER_EVENT_ID, source_item_id, sd->status.account_id, sd->status.char_id, escaped_ip,
		NEED_SUMMER_REWARD_TYPE_ITEM, reward_item_id, amount, unique_id)) {
		Sql_ShowDebug(mmysql_handle);
		ShowWarning("need_summer_reward_log: origin-log insert failed (source %u, item %u); reward is kept.\n",
			source_item_id, reward_item_id);
		// Intentionally swallow the error: a log failure must not affect the granted reward.
	}
}
