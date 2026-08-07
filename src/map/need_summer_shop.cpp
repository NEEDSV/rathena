// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 summer exchange and token shop limits.

#include "need_summer_shop.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>

#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>
#include <common/strlib.hpp>

#include "clif.hpp"
#include "map.hpp"
#include "mapreg.hpp"
#include "npc.hpp"
#include "pc.hpp"
#include "script.hpp"

namespace {

constexpr uint32 EVENT_ID = 202608;
constexpr int32 EVENT_START = 20260815;
constexpr int32 SHOP_END = 20260919;

constexpr int32 MSG_DISABLED = 1943;
constexpr int32 MSG_OUTSIDE_PERIOD = 1944;
constexpr int32 MSG_DATABASE_ERROR = 1945;
constexpr int32 MSG_ACCOUNT_DAILY = 1946;
constexpr int32 MSG_IP_DAILY = 1947;
constexpr int32 MSG_ACCOUNT_WEEKLY = 1948;
constexpr int32 MSG_IP_WEEKLY = 1949;
constexpr int32 MSG_ACCOUNT_SEASON = 1950;
constexpr int32 MSG_IP_SEASON = 1951;
constexpr int32 MSG_BARTER_FAILED = 1952;
constexpr int32 MSG_FAIL_CLOSED = 1953;
constexpr int32 MSG_CONSOLE_LOG = 1954;
constexpr int32 MSG_CONFIG_INVALID = 1955;

constexpr const char* EXCHANGE_NAME = "need_summer_fragment_exchange";
constexpr const char* SHOP_NAME = "need_summer_token_shop";
constexpr const char* RESULT_DELIVERED = "DELIVERED";
constexpr const char* RESULT_REJECTED = "REJECTED";
constexpr const char* RESULT_FAILED = "FAILED";

enum class product_index : size_t {
	EXCHANGE,
	ZENY_POUCH,
	CASH_TICKET,
	SHADOW_BOX,
	ARIA_BOX,
	COUNT,
};

struct product_spec {
	const char* type;
	t_itemid output_item;
	uint32 output_amount;
	t_itemid input_item;
	uint32 input_amount;
	uint32 daily_limit;
	uint32 weekly_limit;
	uint32 season_limit;
};

constexpr std::array<product_spec, static_cast<size_t>(product_index::COUNT)> PRODUCTS = {{
	{ "FRAGMENT_EXCHANGE", 399925, 10, 399926, 20, 3, 0, 0 },
	{ "ZENY_POUCH", 399929, 1, 399925, 100, 0, 2, 8 },
	{ "CASH_TICKET", 399930, 1, 399925, 250, 0, 0, 2 },
	{ "SHADOW_BOX", 399931, 1, 399925, 100, 0, 2, 8 },
	{ "ARIA_BOX", 399935, 1, 399925, 300, 0, 0, 1 },
}};

struct product_usage {
	uint32 quantity = 0;
	uint32 account_period = 0;
	uint32 ip_period = 0;
	uint32 account_season = 0;
	uint32 ip_season = 0;
	bool family_exception = false;
};

struct transaction_state {
	std::array<product_usage, static_cast<size_t>(product_index::COUNT)> usage = {};
	char ip[16] = {};
	char logical_date[11] = {};
	int32 logical_key = 0;
	uint32 week = 0;
	uint32 family_group_id = 0;
	bool sql_active = false;
};

bool schema_checked = false;
bool schema_available = false;
bool fail_closed = false;

bool feature_enabled(const char* variable) {
	return mapreg_readreg(add_str(variable)) != 0;
}

bool logical_now(transaction_state& state) {
	time_t shifted = time(nullptr) - (4 * 60 * 60);
	struct tm* local = localtime(&shifted);
	char key[9] = {};
	if (local == nullptr || strftime(state.logical_date, sizeof(state.logical_date), "%Y-%m-%d", local) != 10 ||
		strftime(key, sizeof(key), "%Y%m%d", local) != 8)
		return false;
	state.logical_key = atoi(key);
	if (state.logical_key < 20260822)
		state.week = 1;
	else if (state.logical_key < 20260829)
		state.week = 2;
	else if (state.logical_key < 20260905)
		state.week = 3;
	else
		state.week = 4;
	return true;
}

bool client_ip(map_session_data& sd, char (&ip)[16]) {
	if (sd.fd <= 0 || !session_isActive(sd.fd) || session[sd.fd]->client_addr == 0)
		return false;
	snprintf(ip, sizeof(ip), "%u.%u.%u.%u", CONVIP(session[sd.fd]->client_addr));
	return true;
}

bool sql_uint32(uint32 column, uint32& value) {
	char* data = nullptr;
	if (SQL_SUCCESS != Sql_GetData(mmysql_handle, column, &data, nullptr) || data == nullptr)
		return false;
	value = static_cast<uint32>(strtoul(data, nullptr, 10));
	return true;
}

void close_feature(map_session_data* sd) {
	if (!fail_closed)
		ShowError("%s\n", msg_txt(nullptr, MSG_FAIL_CLOSED));
	fail_closed = true;
	schema_available = false;
	if (sd != nullptr)
		clif_displaymessage(sd->fd, msg_txt(sd, MSG_DATABASE_ERROR));
}

bool schema_ready() {
	if (fail_closed)
		return false;
	if (schema_checked)
		return schema_available;
	schema_checked = true;
	if (mmysql_handle == nullptr)
		return false;

	static const char* checks[] = {
		"SELECT `event_id`,`family_group_id`,`active` FROM `need_summer_attendance_family_group` LIMIT 0",
		"SELECT `event_id`,`account_id`,`family_group_id`,`active` FROM `need_summer_attendance_family_member` LIMIT 0",
		"SELECT `event_id`,`purchase_type`,`scope_type`,`period_type`,`period_key`,`used_count` FROM `need_summer_shop_counter` LIMIT 0",
		"SELECT `event_id`,`account_id`,`char_id`,`purchase_type`,`result`,`failure_code` FROM `need_summer_shop_log` LIMIT 0",
	};
	for (const char* query : checks) {
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, query)) {
			Sql_ShowDebug(mmysql_handle);
			return false;
		}
		Sql_FreeResult(mmysql_handle);
	}
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle,
		"SELECT COUNT(*) FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA`=DATABASE() "
		"AND `TABLE_NAME` IN ('need_summer_attendance_family_group','need_summer_attendance_family_member',"
		"'need_summer_shop_counter','need_summer_shop_log') AND `ENGINE`='InnoDB'") ||
		SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_FreeResult(mmysql_handle);
		return false;
	}
	uint32 count = 0;
	bool ready = sql_uint32(0, count) && count == 4;
	Sql_FreeResult(mmysql_handle);
	schema_available = ready;
	return ready;
}

bool is_summer_barter(const std::shared_ptr<s_npc_barter>& barter, bool& exchange) {
	if (barter == nullptr)
		return false;
	exchange = barter->name == EXCHANGE_NAME;
	return exchange || barter->name == SHOP_NAME;
}

const product_spec* find_product(t_itemid item_id, size_t& index) {
	for (size_t i = 0; i < PRODUCTS.size(); ++i) {
		if (PRODUCTS[i].output_item == item_id) {
			index = i;
			return &PRODUCTS[i];
		}
	}
	return nullptr;
}

bool valid_configuration(bool exchange, const std::vector<s_barter_purchase>& purchases,
	std::array<product_usage, static_cast<size_t>(product_index::COUNT)>& usage) {
	for (const s_barter_purchase& purchase : purchases) {
		if (purchase.item == nullptr || purchase.amount == 0)
			return false;
		if (!exchange && purchase.item->nameid == 12610) {
			if (purchase.item->outputAmount != 1 || purchase.item->price != 0 || purchase.item->requirements.size() != 1)
				return false;
			auto requirement = purchase.item->requirements.begin()->second;
			if (requirement == nullptr || requirement->nameid != 399925 || requirement->amount != 10)
				return false;
			continue;
		}
		size_t index = 0;
		const product_spec* spec = find_product(purchase.item->nameid, index);
		if (spec == nullptr || exchange != (index == static_cast<size_t>(product_index::EXCHANGE)) ||
			purchase.item->outputAmount != spec->output_amount || purchase.item->price != 0 ||
			purchase.item->requirements.size() != 1)
			return false;
		auto requirement = purchase.item->requirements.begin()->second;
		if (requirement == nullptr || requirement->nameid != spec->input_item || requirement->amount != spec->input_amount)
			return false;
		if (UINT32_MAX - usage[index].quantity < purchase.amount)
			return false;
		usage[index].quantity += purchase.amount;
	}
	return true;
}

bool load_family(map_session_data& sd, transaction_state& state) {
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `m`.`family_group_id` FROM `need_summer_attendance_family_member` `m` "
		"INNER JOIN `need_summer_attendance_family_group` `g` ON `g`.`event_id`=`m`.`event_id` "
		"AND `g`.`family_group_id`=`m`.`family_group_id` AND `g`.`active`='1' "
		"WHERE `m`.`event_id`='%u' AND `m`.`account_id`='%u' AND `m`.`active`='1' LIMIT 1 FOR UPDATE",
		EVENT_ID, sd.status.account_id))
		return false;
	if (Sql_NumRows(mmysql_handle) > 0 && SQL_SUCCESS == Sql_NextRow(mmysql_handle) && !sql_uint32(0, state.family_group_id)) {
		Sql_FreeResult(mmysql_handle);
		return false;
	}
	Sql_FreeResult(mmysql_handle);
	return true;
}

bool reserve_scope(map_session_data& sd, transaction_state& state, const product_spec& spec,
	const char* scope, const char* period_type, uint32 period_key, uint32 limit, uint32 quantity,
	uint32& final_count, bool& family_exception, const char*& failure_code, int32& message_id) {
	const bool account_scope = strcmp(scope, "ACCOUNT") == 0;
	const char* ip_value = account_scope ? "0.0.0.0" : state.ip;
	uint32 account_id = account_scope ? sd.status.account_id : 0;
	uint32 row_family = account_scope ? 0 : state.family_group_id;

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT INTO `need_summer_shop_counter` (`event_id`,`purchase_type`,`scope_type`,`account_id`,`ip`,"
		"`period_type`,`period_key`,`used_count`,`family_group_id`,`first_account_id`,`first_char_id`,`last_account_id`,`last_char_id`) "
		"VALUES ('%u','%s','%s','%u',INET6_ATON('%s'),'%s','%u','0','%u','%u','%u','%u','%u') "
		"ON DUPLICATE KEY UPDATE `updated_at`=`updated_at`",
		EVENT_ID, spec.type, scope, account_id, ip_value, period_type, period_key, row_family,
		sd.status.account_id, sd.status.char_id, sd.status.account_id, sd.status.char_id))
		return false;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `used_count`,`family_group_id`,`first_account_id` FROM `need_summer_shop_counter` "
		"WHERE `event_id`='%u' AND `purchase_type`='%s' AND `scope_type`='%s' AND `account_id`='%u' "
		"AND `ip`=INET6_ATON('%s') AND `period_type`='%s' AND `period_key`='%u' FOR UPDATE",
		EVENT_ID, spec.type, scope, account_id, ip_value, period_type, period_key))
		return false;
	uint32 used = 0, stored_family = 0, first_account = 0;
	bool valid = SQL_SUCCESS == Sql_NextRow(mmysql_handle) && sql_uint32(0, used) &&
		sql_uint32(1, stored_family) && sql_uint32(2, first_account);
	Sql_FreeResult(mmysql_handle);
	if (!valid)
		return false;

	if (quantity > limit || used > limit - quantity) {
		if (!account_scope && state.family_group_id != 0 && stored_family == state.family_group_id &&
			first_account != sd.status.account_id) {
			family_exception = true;
			final_count = used;
			return true;
		}
		failure_code = account_scope ? (strcmp(period_type, "DAILY") == 0 ? "ACCOUNT_DAILY_LIMIT" :
			(strcmp(period_type, "WEEKLY") == 0 ? "ACCOUNT_WEEKLY_LIMIT" : "ACCOUNT_SEASON_LIMIT")) :
			(strcmp(period_type, "DAILY") == 0 ? "IP_DAILY_LIMIT" :
			(strcmp(period_type, "WEEKLY") == 0 ? "IP_WEEKLY_LIMIT" : "IP_SEASON_LIMIT"));
		message_id = account_scope ? (strcmp(period_type, "DAILY") == 0 ? MSG_ACCOUNT_DAILY :
			(strcmp(period_type, "WEEKLY") == 0 ? MSG_ACCOUNT_WEEKLY : MSG_ACCOUNT_SEASON)) :
			(strcmp(period_type, "DAILY") == 0 ? MSG_IP_DAILY :
			(strcmp(period_type, "WEEKLY") == 0 ? MSG_IP_WEEKLY : MSG_IP_SEASON));
		return false;
	}
	final_count = used + quantity;
	return SQL_ERROR != Sql_Query(mmysql_handle,
		"UPDATE `need_summer_shop_counter` SET `used_count`='%u',`last_account_id`='%u',`last_char_id`='%u' "
		"WHERE `event_id`='%u' AND `purchase_type`='%s' AND `scope_type`='%s' AND `account_id`='%u' "
		"AND `ip`=INET6_ATON('%s') AND `period_type`='%s' AND `period_key`='%u'",
		final_count, sd.status.account_id, sd.status.char_id, EVENT_ID, spec.type, scope, account_id,
		ip_value, period_type, period_key);
}

bool reserve_period(map_session_data& sd, transaction_state& state, const product_spec& spec,
	const char* period_type, uint32 period_key, uint32 limit, uint32 quantity,
	uint32& account_count, uint32& ip_count, bool& family_exception,
	const char*& failure_code, int32& message_id) {
	if (!reserve_scope(sd, state, spec, "ACCOUNT", period_type, period_key, limit, quantity,
		account_count, family_exception, failure_code, message_id))
		return false;
	return reserve_scope(sd, state, spec, "IP", period_type, period_key, limit, quantity,
		ip_count, family_exception, failure_code, message_id);
}

bool write_log(map_session_data& sd, const transaction_state& state, size_t index,
	const char* result, const char* failure_code) {
	const product_spec& spec = PRODUCTS[index];
	const product_usage& usage = state.usage[index];
	char escaped_name[NAME_LENGTH * 2 + 1] = {};
	Sql_EscapeStringLen(mmysql_handle, escaped_name, sd.status.name, strnlen(sd.status.name, NAME_LENGTH));
	const char* primary_period = spec.daily_limit != 0 ? "DAILY" : (spec.weekly_limit != 0 ? "WEEKLY" : "SEASON");
	uint32 primary_key = spec.daily_limit != 0 ? state.logical_key : (spec.weekly_limit != 0 ? state.week : 0);
	return SQL_ERROR != Sql_Query(mmysql_handle,
		"INSERT INTO `need_summer_shop_log` (`event_id`,`logical_date`,`event_week`,`account_id`,`char_id`,`char_name`,`ip`,"
		"`family_group_id`,`family_exception`,`purchase_type`,`period_type`,`period_key`,`quantity`,`account_used`,`ip_used`,"
		"`account_season_used`,`ip_season_used`,`consume_item_id`,`consume_amount`,`grant_item_id`,`grant_amount`,`result`,`failure_code`) "
		"VALUES ('%u','%s','%u','%u','%u','%s',INET6_ATON('%s'),'%u','%u','%s','%s','%u','%u','%u','%u','%u','%u','%u','%u','%u','%u','%s','%s')",
		EVENT_ID, state.logical_date, state.week, sd.status.account_id, sd.status.char_id, escaped_name, state.ip,
		state.family_group_id, usage.family_exception ? 1 : 0, spec.type, primary_period, primary_key, usage.quantity,
		usage.account_period, usage.ip_period, usage.account_season, usage.ip_season,
		spec.input_item, spec.input_amount * usage.quantity, spec.output_item, spec.output_amount * usage.quantity,
		result, failure_code);
}

void console_log(map_session_data& sd, const transaction_state& state, size_t index,
	const char* result, const char* failure_code) {
	ShowInfo(msg_txt(nullptr, MSG_CONSOLE_LOG), result, failure_code, sd.status.account_id, sd.status.char_id,
		sd.status.name, state.ip, state.logical_date, state.week, PRODUCTS[index].type, state.usage[index].quantity,
		state.usage[index].account_period, state.usage[index].ip_period, state.usage[index].family_exception ? 1 : 0);
	ShowInfo("\n");
}

bool rollback() {
	return SQL_ERROR != Sql_QueryStr(mmysql_handle, "ROLLBACK");
}

void log_after_rollback(map_session_data& sd, const transaction_state& state,
	const char* result, const char* failure_code) {
	for (size_t i = 0; i < PRODUCTS.size(); ++i) {
		if (state.usage[i].quantity == 0)
			continue;
		if (!write_log(sd, state, i, result, failure_code)) {
			Sql_ShowDebug(mmysql_handle);
			close_feature(&sd);
			return;
		}
		console_log(sd, state, i, result, failure_code);
	}
}

}  // namespace

need_summer_shop_begin_result need_summer_shop_begin(map_session_data& sd,
	const std::shared_ptr<s_npc_barter>& barter, const std::vector<s_barter_purchase>& purchases,
	need_summer_shop_transaction& transaction) {
	bool exchange = false;
	if (!is_summer_barter(barter, exchange))
		return need_summer_shop_begin_result::NOT_APPLICABLE;

	std::unique_ptr<transaction_state> state = std::make_unique<transaction_state>();
	if (!feature_enabled("$NS_SHOP_ON") ||
		!feature_enabled(exchange ? "$NS_EXCHANGE_ON" : "$NS_TOKEN_SHOP_ON")) {
		clif_displaymessage(sd.fd, msg_txt(&sd, MSG_DISABLED));
		return need_summer_shop_begin_result::REJECTED;
	}
	if (!logical_now(*state) || state->logical_key < EVENT_START || state->logical_key >= SHOP_END) {
		clif_displaymessage(sd.fd, msg_txt(&sd, MSG_OUTSIDE_PERIOD));
		return need_summer_shop_begin_result::REJECTED;
	}
	if (!client_ip(sd, state->ip)) {
		clif_displaymessage(sd.fd, msg_txt(&sd, MSG_DATABASE_ERROR));
		return need_summer_shop_begin_result::REJECTED;
	}
	if (!valid_configuration(exchange, purchases, state->usage)) {
		ShowError("%s\n", msg_txt(nullptr, MSG_CONFIG_INVALID));
		close_feature(&sd);
		return need_summer_shop_begin_result::REJECTED;
	}
	if (!schema_ready()) {
		close_feature(&sd);
		return need_summer_shop_begin_result::REJECTED;
	}

	bool has_limited_product = false;
	for (const product_usage& usage : state->usage)
		has_limited_product |= usage.quantity != 0;
	if (!has_limited_product)
		return need_summer_shop_begin_result::ALLOWED;

	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
		Sql_ShowDebug(mmysql_handle);
		close_feature(&sd);
		return need_summer_shop_begin_result::REJECTED;
	}
	state->sql_active = true;
	if (!load_family(sd, *state)) {
		Sql_ShowDebug(mmysql_handle);
		rollback();
		close_feature(&sd);
		return need_summer_shop_begin_result::REJECTED;
	}

	const char* failure_code = "SQL_TRANSACTION_FAILED";
	int32 message_id = MSG_DATABASE_ERROR;
	for (size_t i = 0; i < PRODUCTS.size(); ++i) {
		product_usage& usage = state->usage[i];
		const product_spec& spec = PRODUCTS[i];
		if (usage.quantity == 0)
			continue;
		bool reserved = true;
		if (spec.daily_limit != 0)
			reserved = reserve_period(sd, *state, spec, "DAILY", state->logical_key, spec.daily_limit, usage.quantity,
				usage.account_period, usage.ip_period, usage.family_exception, failure_code, message_id);
		if (reserved && spec.weekly_limit != 0)
			reserved = reserve_period(sd, *state, spec, "WEEKLY", state->week, spec.weekly_limit, usage.quantity,
				usage.account_period, usage.ip_period, usage.family_exception, failure_code, message_id);
		if (reserved && spec.season_limit != 0)
			reserved = reserve_period(sd, *state, spec, "SEASON", 0, spec.season_limit, usage.quantity,
				usage.account_season, usage.ip_season, usage.family_exception, failure_code, message_id);
		if (!reserved) {
			if (strcmp(failure_code, "SQL_TRANSACTION_FAILED") == 0)
				Sql_ShowDebug(mmysql_handle);
			rollback();
			state->sql_active = false;
			log_after_rollback(sd, *state, strcmp(failure_code, "SQL_TRANSACTION_FAILED") == 0 ? RESULT_FAILED : RESULT_REJECTED,
				failure_code);
			clif_displaymessage(sd.fd, msg_txt(&sd, message_id));
			if (strcmp(failure_code, "SQL_TRANSACTION_FAILED") == 0)
				close_feature(&sd);
			return need_summer_shop_begin_result::REJECTED;
		}
	}

	transaction.state = state.release();
	return need_summer_shop_begin_result::ALLOWED;
}

bool need_summer_shop_finish(map_session_data& sd, need_summer_shop_transaction& transaction, bool delivered) {
	std::unique_ptr<transaction_state> state(static_cast<transaction_state*>(transaction.state));
	transaction.state = nullptr;
	if (state == nullptr || !state->sql_active)
		return true;

	if (!delivered) {
		rollback();
		state->sql_active = false;
		log_after_rollback(sd, *state, RESULT_FAILED, "BARTER_DELIVERY_FAILED");
		clif_displaymessage(sd.fd, msg_txt(&sd, MSG_BARTER_FAILED));
		return true;
	}
	for (size_t i = 0; i < PRODUCTS.size(); ++i) {
		if (state->usage[i].quantity == 0)
			continue;
		if (!write_log(sd, *state, i, RESULT_DELIVERED, "")) {
			Sql_ShowDebug(mmysql_handle);
			rollback();
			close_feature(&sd);
			return false;
		}
	}
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		Sql_ShowDebug(mmysql_handle);
		close_feature(&sd);
		return false;
	}
	state->sql_active = false;
	for (size_t i = 0; i < PRODUCTS.size(); ++i) {
		if (state->usage[i].quantity != 0)
			console_log(sd, *state, i, RESULT_DELIVERED, "");
	}
	return true;
}
