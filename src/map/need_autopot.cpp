// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL

#include "need_autopot.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <common/mmo.hpp>
#include <common/showmsg.hpp>
#include <common/sql.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "clif.hpp"
#include "itemdb.hpp"
#include "map.hpp"
#include "pc.hpp"
#include "status.hpp"

namespace {

constexpr int32 NEED_AUTOPOT_TIMER_INTERVAL = 100;
constexpr size_t NEED_AUTOPOT_LIST_LIMIT = 20;
constexpr const char* NEED_AUTOPOT_TABLE = "need_autopot_preset";

struct active_session {
	uint32 char_id;
};

struct autopot_preset {
	uint8 slot = 0;
	std::string name;
	bool hp_enabled = false;
	t_itemid hp_item_id = 0;
	uint8 hp_percent = 90;
	bool sp_enabled = false;
	t_itemid sp_item_id = 0;
	uint8 sp_percent = 50;
	uint16 interval_ms = NEED_AUTOPOT_DEFAULT_INTERVAL;
};

std::unordered_map<int32, active_session> active_sessions;
int32 autopot_timer_id = INVALID_TIMER;

void message(map_session_data* sd, int32 message_id)
{
	if (sd != nullptr && sd->fd >= 0)
		clif_displaymessage(sd->fd, msg_txt(sd, message_id));
}

void formatted_message(map_session_data* sd, const char* text)
{
	if (sd != nullptr && sd->fd >= 0)
		clif_displaymessage(sd->fd, text);
}

bool token_is(const std::string& token, const char* english, const char* korean_cp949, const char* korean_utf8)
{
	return token == english || token == korean_cp949 || token == korean_utf8;
}

bool token_help(const std::string& token)
{
	return token_is(token, "help", "\265\265\277\362\270\273", "\353\217\204\354\233\200\353\247\220");
}

bool token_info(const std::string& token)
{
	return token_is(token, "info", "\301\244\272\270", "\354\240\225\353\263\264");
}

bool token_off(const std::string& token)
{
	return token == "off" || token_is(token, "disable", "\262\364\261\342", "\353\201\204\352\270\260");
}

bool token_list(const std::string& token)
{
	return token_is(token, "list", "\270\361\267\317", "\353\252\251\353\241\235");
}

bool token_delay(const std::string& token)
{
	return token_is(token, "delay", "\301\366\277\254", "\354\247\200\354\227\260");
}

bool token_save(const std::string& token)
{
	return token_is(token, "save", "\300\372\300\345", "\354\240\200\354\236\245");
}

bool token_load(const std::string& token)
{
	return token_is(token, "load", "\272\322\267\257\277\300\261\342", "\353\266\210\353\237\254\354\230\244\352\270\260");
}

bool token_preset(const std::string& token)
{
	return token_is(token, "preset", "\307\301\270\256\274\302", "\355\224\204\353\246\254\354\205\213");
}

bool token_delete(const std::string& token)
{
	return token_is(token, "delete", "\273\350\301\246", "\354\202\255\354\240\234");
}

bool token_blacklist(const std::string& token)
{
	return token_is(token, "blacklist", "\272\355\267\242\270\256\275\272\306\256", "\353\270\224\353\236\231\353\246\254\354\212\244\355\212\270");
}

bool parse_uint(const std::string& text, uint32& value)
{
	if (text.empty() || text.front() == '-')
		return false;

	char* end = nullptr;
	const unsigned long parsed = strtoul(text.c_str(), &end, 10);
	if (end == text.c_str() || *end != '\0' || parsed > UINT32_MAX)
		return false;

	value = static_cast<uint32>(parsed);
	return true;
}

bool valid_slot(uint32 slot)
{
	return slot >= 1 && slot <= NEED_AUTOPOT_MAX_PRESETS;
}

bool valid_percent(uint32 percent)
{
	return percent >= 1 && percent <= 100;
}

bool valid_interval(uint32 interval)
{
	return interval >= NEED_AUTOPOT_MIN_INTERVAL && interval <= NEED_AUTOPOT_MAX_INTERVAL;
}

std::shared_ptr<item_data> healing_item(t_itemid item_id)
{
	std::shared_ptr<item_data> data = item_db.find(item_id);
	return data != nullptr && data->type == IT_HEALING ? data : nullptr;
}

const char* item_name(map_session_data* sd, t_itemid item_id, std::string& storage)
{
	std::shared_ptr<item_data> data = item_db.find(item_id);
	if (data == nullptr) {
		storage = msg_txt(sd, NEED_AUTOPOT_MSG_UNKNOWN_ITEM);
		return storage.c_str();
	}
	storage = data->ename.empty() ? data->name : data->ename;
	return storage.c_str();
}

void update_active(map_session_data* sd)
{
	if (sd == nullptr)
		return;

	if (sd->autopot.hp_enabled || sd->autopot.sp_enabled)
		active_sessions[sd->id] = { sd->status.char_id };
	else
		active_sessions.erase(sd->id);
}

void show_help(map_session_data* sd)
{
	message(sd, NEED_AUTOPOT_MSG_HELP_HP);
	message(sd, NEED_AUTOPOT_MSG_HELP_SP);
	message(sd, NEED_AUTOPOT_MSG_HELP_CONTROL);
	message(sd, NEED_AUTOPOT_MSG_HELP_PRESET);
	message(sd, NEED_AUTOPOT_MSG_HELP_KOREAN);
}

void show_info(map_session_data* sd)
{
	char output[CHAT_SIZE_MAX] = {};
	std::string name;
	message(sd, NEED_AUTOPOT_MSG_INFO_TITLE);

	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_AUTOPOT_MSG_INFO_HP),
		msg_txt(sd, sd->autopot.hp_enabled ? NEED_AUTOPOT_MSG_STATE_ON : NEED_AUTOPOT_MSG_STATE_OFF),
		item_name(sd, sd->autopot.hp_item_id, name),
		static_cast<uint32>(sd->autopot.hp_item_id), static_cast<uint32>(sd->autopot.hp_percent));
	formatted_message(sd, output);

	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_AUTOPOT_MSG_INFO_SP),
		msg_txt(sd, sd->autopot.sp_enabled ? NEED_AUTOPOT_MSG_STATE_ON : NEED_AUTOPOT_MSG_STATE_OFF),
		item_name(sd, sd->autopot.sp_item_id, name),
		static_cast<uint32>(sd->autopot.sp_item_id), static_cast<uint32>(sd->autopot.sp_percent));
	formatted_message(sd, output);

	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_AUTOPOT_MSG_INFO_INTERVAL_MAP),
		static_cast<uint32>(sd->autopot.interval_ms),
		msg_txt(sd, need_autopot_is_map_allowed(sd) ? NEED_AUTOPOT_MSG_MAP_ALLOWED : NEED_AUTOPOT_MSG_MAP_BLOCKED));
	formatted_message(sd, output);

	if (sd->autopot.active_preset_slot > 0) {
		safesnprintf(output, sizeof(output), msg_txt(sd, NEED_AUTOPOT_MSG_INFO_PRESET),
			sd->autopot.active_preset_slot, sd->autopot.active_preset_name);
		formatted_message(sd, output);
	} else {
		message(sd, NEED_AUTOPOT_MSG_INFO_PRESET_NONE);
	}
}

void list_inventory(map_session_data* sd)
{
	message(sd, NEED_AUTOPOT_MSG_ITEM_LIST_TITLE);
	size_t displayed = 0;
	for (int32 index = 0; index < MAX_INVENTORY && displayed < NEED_AUTOPOT_LIST_LIMIT; ++index) {
		const item& inventory_item = sd->inventory.u.items_inventory[index];
		if (inventory_item.nameid == 0 || inventory_item.amount <= 0)
			continue;
		std::shared_ptr<item_data> data = healing_item(inventory_item.nameid);
		if (data == nullptr)
			continue;

		char output[CHAT_SIZE_MAX] = {};
		const std::string& name = data->ename.empty() ? data->name : data->ename;
		safesnprintf(output, sizeof(output), msg_txt(sd, NEED_AUTOPOT_MSG_ITEM_LIST_ROW),
			static_cast<uint32>(inventory_item.nameid), name.c_str(), inventory_item.amount);
		formatted_message(sd, output);
		++displayed;
	}
	if (displayed == 0)
		message(sd, NEED_AUTOPOT_MSG_ITEM_LIST_EMPTY);
	else if (displayed == NEED_AUTOPOT_LIST_LIMIT)
		message(sd, NEED_AUTOPOT_MSG_ITEM_LIST_LIMIT);
}

bool preset_from_current_row(autopot_preset& preset)
{
	char* data = nullptr;
	unsigned long values[8] = {};

	Sql_GetData(mmysql_handle, 0, &data, nullptr);
	if (data == nullptr)
		return false;
	values[0] = strtoul(data, nullptr, 10);

	size_t name_length = 0;
	Sql_GetData(mmysql_handle, 1, &data, &name_length);
	preset.name.assign(data != nullptr ? data : "", name_length);

	for (size_t column = 2; column <= 8; ++column) {
		Sql_GetData(mmysql_handle, static_cast<uint32>(column), &data, nullptr);
		if (data == nullptr)
			return false;
		values[column - 1] = strtoul(data, nullptr, 10);
	}

	if (!valid_slot(values[0]) || !valid_percent(values[3]) || !valid_percent(values[6]) ||
		!valid_interval(values[7]) || values[1] > 1 || values[4] > 1)
		return false;

	preset.slot = static_cast<uint8>(values[0]);
	preset.hp_enabled = values[1] != 0;
	preset.hp_item_id = static_cast<t_itemid>(values[2]);
	preset.hp_percent = static_cast<uint8>(values[3]);
	preset.sp_enabled = values[4] != 0;
	preset.sp_item_id = static_cast<t_itemid>(values[5]);
	preset.sp_percent = static_cast<uint8>(values[6]);
	preset.interval_ms = static_cast<uint16>(values[7]);
	return true;
}

bool valid_preset_name(const char* name)
{
	if (name == nullptr)
		return false;
	const size_t length = strlen(name);
	if (length == 0 || length > 24)
		return false;
	for (size_t i = 0; i < length; ++i) {
		const unsigned char c = static_cast<unsigned char>(name[i]);
		if (c < 0x20 || c == 0x7f)
			return false;
	}
	return true;
}

class processing_guard {
public:
	explicit processing_guard(bool& flag) : flag_(flag) { flag_ = true; }
	~processing_guard() { flag_ = false; }
private:
	bool& flag_;
};

TIMER_FUNC(need_autopot_timer)
{
	for (auto it = active_sessions.begin(); it != active_sessions.end();) {
		const int32 session_id = it->first;
		const uint32 char_id = it->second.char_id;
		++it;

		map_session_data* sd = map_id2sd(session_id);
		if (sd == nullptr || !sd->state.active || sd->status.char_id != char_id) {
			active_sessions.erase(session_id);
			continue;
		}
		need_autopot_process(sd);
	}
	return 0;
}

} // namespace

void need_autopot_init()
{
	add_timer_func_list(need_autopot_timer, "need_autopot_timer");
	autopot_timer_id = add_timer_interval(gettick() + NEED_AUTOPOT_TIMER_INTERVAL,
		need_autopot_timer, 0, 0, NEED_AUTOPOT_TIMER_INTERVAL);
}

void need_autopot_final()
{
	if (autopot_timer_id != INVALID_TIMER) {
		delete_timer(autopot_timer_id, need_autopot_timer);
		autopot_timer_id = INVALID_TIMER;
	}
	active_sessions.clear();
}

void need_autopot_logout(map_session_data* sd)
{
	if (sd == nullptr)
		return;
	active_sessions.erase(sd->id);
	sd->autopot = {};
}

void need_autopot_map_changed(map_session_data* sd)
{
	if (sd != nullptr && (sd->autopot.hp_enabled || sd->autopot.sp_enabled) && !need_autopot_is_map_allowed(sd))
		need_autopot_disable_all(sd, true);
}

bool need_autopot_enable(map_session_data* sd, bool hp)
{
	if (sd == nullptr)
		return false;
	if (!need_autopot_is_map_allowed(sd)) {
		message(sd, NEED_AUTOPOT_MSG_MAP_RESTRICTED);
		return false;
	}

	const t_itemid item_id = hp ? sd->autopot.hp_item_id : sd->autopot.sp_item_id;
	if (item_id == 0 || !need_autopot_validate_item(sd, item_id, true))
		return false;

	if (hp)
		sd->autopot.hp_enabled = true;
	else
		sd->autopot.sp_enabled = true;
	sd->autopot.next_check_tick = gettick();
	update_active(sd);
	return true;
}

void need_autopot_disable(map_session_data* sd, bool hp)
{
	if (sd == nullptr)
		return;
	if (hp)
		sd->autopot.hp_enabled = false;
	else
		sd->autopot.sp_enabled = false;
	update_active(sd);
}

void need_autopot_disable_all(map_session_data* sd, bool notify)
{
	if (sd == nullptr)
		return;
	sd->autopot.hp_enabled = false;
	sd->autopot.sp_enabled = false;
	sd->autopot.processing = false;
	update_active(sd);
	if (notify) {
		message(sd, NEED_AUTOPOT_MSG_MAP_RESTRICTED);
		message(sd, NEED_AUTOPOT_MSG_DISABLED);
	}
}

bool need_autopot_validate_item(map_session_data* sd, uint32 raw_item_id, bool notify)
{
	if (sd == nullptr || raw_item_id == 0) {
		if (notify)
			message(sd, NEED_AUTOPOT_MSG_INVALID_ITEM_ID);
		return false;
	}

	const t_itemid item_id = static_cast<t_itemid>(raw_item_id);
	std::shared_ptr<item_data> data = item_db.find(item_id);
	if (data == nullptr) {
		if (notify)
			message(sd, NEED_AUTOPOT_MSG_ITEM_NOT_FOUND);
		return false;
	}
	if (data->type != IT_HEALING) {
		if (notify)
			message(sd, NEED_AUTOPOT_MSG_ITEM_NOT_HEALING);
		return false;
	}
	if ((data->elv != 0 && sd->status.base_level < data->elv) ||
		(data->elvmax != 0 && sd->status.base_level > data->elvmax)) {
		if (notify)
			message(sd, NEED_AUTOPOT_MSG_ITEM_LEVEL);
		return false;
	}
	const int16 index = pc_search_inventory(sd, item_id);
	if (index < 0 || sd->inventory.u.items_inventory[index].amount <= 0) {
		if (notify)
			message(sd, NEED_AUTOPOT_MSG_ITEM_NOT_OWNED);
		return false;
	}
	return true;
}

bool need_autopot_is_map_allowed(const map_session_data* sd)
{
	if (sd == nullptr || sd->m < 0)
		return false;
	return !map_getmapflag(sd->m, MF_PVP) &&
		!map_getmapflag(sd->m, MF_GVG) &&
		!map_getmapflag(sd->m, MF_GVG_CASTLE) &&
		!map_getmapflag(sd->m, MF_BATTLEGROUND);
}

bool need_autopot_is_status_blocked(map_session_data* sd)
{
	if (sd == nullptr)
		return true;
	status_change& sc = sd->sc;
	return sc.getSCE(SC_BERSERK) != nullptr ||
		sc.getSCE(SC_SATURDAYNIGHTFEVER) != nullptr ||
		sc.getSCE(SC_GRAVITATION) != nullptr ||
		sc.getSCE(SC_TRICKDEAD) != nullptr ||
		sc.getSCE(SC_HIDING) != nullptr ||
		sc.getSCE(SC__SHADOWFORM) != nullptr ||
		sc.getSCE(SC__INVISIBILITY) != nullptr ||
		sc.getSCE(SC__MANHOLE) != nullptr ||
		sc.getSCE(SC_KAGEHUMI) != nullptr ||
		sc.getSCE(SC_HEAT_BARREL) != nullptr ||
		sc.getSCE(SC_STONE) != nullptr ||
		sc.getSCE(SC_FREEZE) != nullptr ||
		sc.getSCE(SC_STUN) != nullptr ||
		sc.getSCE(SC_SLEEP) != nullptr;
}

bool need_autopot_use_item(map_session_data* sd, uint32 raw_item_id, bool hp)
{
	if (sd == nullptr)
		return false;
	const t_itemid item_id = static_cast<t_itemid>(raw_item_id);
	bool& missing_notified = hp ? sd->autopot.hp_missing_notified : sd->autopot.sp_missing_notified;
	std::shared_ptr<item_data> data = healing_item(item_id);
	if (data == nullptr) {
		if (!missing_notified) {
			message(sd, hp ? NEED_AUTOPOT_MSG_HP_ITEM_INVALID : NEED_AUTOPOT_MSG_SP_ITEM_INVALID);
			missing_notified = true;
		}
		return false;
	}

	const int16 index = pc_search_inventory(sd, item_id);
	if (index < 0 || sd->inventory.u.items_inventory[index].amount <= 0) {
		if (!missing_notified) {
			message(sd, hp ? NEED_AUTOPOT_MSG_HP_OUT_OF_STOCK : NEED_AUTOPOT_MSG_SP_OUT_OF_STOCK);
			missing_notified = true;
		}
		return false;
	}
	missing_notified = false;
	return pc_useitem(sd, index) != 0;
}

void need_autopot_process(map_session_data* sd)
{
	if (sd == nullptr || (!sd->autopot.hp_enabled && !sd->autopot.sp_enabled))
		return;
	const t_tick tick = gettick();
	if (DIFF_TICK(tick, sd->autopot.next_check_tick) < 0 || sd->autopot.processing)
		return;
	sd->autopot.next_check_tick = tick + sd->autopot.interval_ms;
	if (!sd->state.active || sd->battle_status.hp <= 0)
		return;
	if (!need_autopot_is_map_allowed(sd)) {
		need_autopot_disable_all(sd, true);
		return;
	}

	const bool need_hp = sd->autopot.hp_enabled &&
		static_cast<int64>(sd->battle_status.hp) * 100 <
		static_cast<int64>(sd->battle_status.max_hp) * sd->autopot.hp_percent;
	const bool need_sp = sd->autopot.sp_enabled &&
		static_cast<int64>(sd->battle_status.sp) * 100 <
		static_cast<int64>(sd->battle_status.max_sp) * sd->autopot.sp_percent;
	if (!need_hp && !need_sp)
		return;
	if (need_autopot_is_status_blocked(sd))
		return;

	processing_guard guard(sd->autopot.processing);
	const int32 session_id = sd->id;
	const uint32 char_id = sd->status.char_id;
	if (need_hp)
		need_autopot_use_item(sd, sd->autopot.hp_item_id, true);

	sd = map_id2sd(session_id);
	if (sd == nullptr || !sd->state.active || sd->status.char_id != char_id)
		return;
	if (need_sp)
		need_autopot_use_item(sd, sd->autopot.sp_item_id, false);
}

bool need_autopot_save_preset(map_session_data* sd, uint8 slot, const char* name)
{
	if (sd == nullptr || !valid_slot(slot) || !valid_preset_name(name))
		return false;

	char escaped_name[49] = {};
	Sql_EscapeString(mmysql_handle, escaped_name, name);
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT INTO `%s` (`account_id`,`slot`,`name`,`hp_enabled`,`hp_item_id`,`hp_percent`,"
		"`sp_enabled`,`sp_item_id`,`sp_percent`,`interval_ms`) "
		"VALUES ('%u','%u','%s','%u','%u','%u','%u','%u','%u','%u') "
		"ON DUPLICATE KEY UPDATE `name`=VALUES(`name`),`hp_enabled`=VALUES(`hp_enabled`),"
		"`hp_item_id`=VALUES(`hp_item_id`),`hp_percent`=VALUES(`hp_percent`),"
		"`sp_enabled`=VALUES(`sp_enabled`),`sp_item_id`=VALUES(`sp_item_id`),"
		"`sp_percent`=VALUES(`sp_percent`),`interval_ms`=VALUES(`interval_ms`)",
		NEED_AUTOPOT_TABLE, sd->status.account_id, slot, escaped_name,
		sd->autopot.hp_enabled ? 1 : 0, static_cast<uint32>(sd->autopot.hp_item_id), sd->autopot.hp_percent,
		sd->autopot.sp_enabled ? 1 : 0, static_cast<uint32>(sd->autopot.sp_item_id), sd->autopot.sp_percent,
		sd->autopot.interval_ms)) {
		Sql_ShowDebug(mmysql_handle);
		return false;
	}
	sd->autopot.active_preset_slot = static_cast<int8>(slot);
	safestrncpy(sd->autopot.active_preset_name, name, sizeof(sd->autopot.active_preset_name));
	return true;
}

bool need_autopot_load_preset(map_session_data* sd, uint8 slot)
{
	if (sd == nullptr || !valid_slot(slot))
		return false;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `slot`,`name`,`hp_enabled`,`hp_item_id`,`hp_percent`,`sp_enabled`,`sp_item_id`,"
		"`sp_percent`,`interval_ms` FROM `%s` WHERE `account_id`='%u' AND `slot`='%u'",
		NEED_AUTOPOT_TABLE, sd->status.account_id, slot)) {
		Sql_ShowDebug(mmysql_handle);
		return false;
	}

	autopot_preset preset;
	const bool found = Sql_NumRows(mmysql_handle) == 1 &&
		SQL_SUCCESS == Sql_NextRow(mmysql_handle) && preset_from_current_row(preset);
	Sql_FreeResult(mmysql_handle);
	if (!found)
		return false;
	if ((preset.hp_item_id != 0 && healing_item(preset.hp_item_id) == nullptr) ||
		(preset.sp_item_id != 0 && healing_item(preset.sp_item_id) == nullptr)) {
		ShowWarning("need_autopot: invalid item data in account %u preset %u.\n", sd->status.account_id, slot);
		return false;
	}

	need_autopot_disable_all(sd);
	sd->autopot.hp_item_id = preset.hp_item_id;
	sd->autopot.hp_percent = preset.hp_percent;
	sd->autopot.sp_item_id = preset.sp_item_id;
	sd->autopot.sp_percent = preset.sp_percent;
	sd->autopot.interval_ms = preset.interval_ms;
	sd->autopot.active_preset_slot = static_cast<int8>(slot);
	safestrncpy(sd->autopot.active_preset_name, preset.name.c_str(), sizeof(sd->autopot.active_preset_name));

	if (!need_autopot_is_map_allowed(sd)) {
		message(sd, NEED_AUTOPOT_MSG_PRESET_MAP_BLOCKED);
		return true;
	}
	sd->autopot.hp_enabled = preset.hp_enabled && preset.hp_item_id != 0;
	sd->autopot.sp_enabled = preset.sp_enabled && preset.sp_item_id != 0;
	sd->autopot.next_check_tick = gettick();
	update_active(sd);
	return true;
}

bool need_autopot_delete_preset(map_session_data* sd, uint8 slot)
{
	if (sd == nullptr || !valid_slot(slot))
		return false;
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE FROM `%s` WHERE `account_id`='%u' AND `slot`='%u'",
		NEED_AUTOPOT_TABLE, sd->status.account_id, slot)) {
		Sql_ShowDebug(mmysql_handle);
		return false;
	}
	if (sd->autopot.active_preset_slot == static_cast<int8>(slot)) {
		sd->autopot.active_preset_slot = -1;
		sd->autopot.active_preset_name[0] = '\0';
	}
	return true;
}

bool need_autopot_list_presets(map_session_data* sd)
{
	if (sd == nullptr)
		return false;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `slot`,`name`,`hp_enabled`,`hp_item_id`,`hp_percent`,`sp_enabled`,`sp_item_id`,"
		"`sp_percent`,`interval_ms` FROM `%s` WHERE `account_id`='%u' ORDER BY `slot`",
		NEED_AUTOPOT_TABLE, sd->status.account_id)) {
		Sql_ShowDebug(mmysql_handle);
		return false;
	}

	std::unordered_map<uint8, autopot_preset> presets;
	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		autopot_preset preset;
		if (preset_from_current_row(preset))
			presets[preset.slot] = preset;
		else
			ShowWarning("need_autopot: invalid preset row for account %u.\n", sd->status.account_id);
	}
	Sql_FreeResult(mmysql_handle);

	message(sd, NEED_AUTOPOT_MSG_PRESET_TITLE);
	for (uint8 slot = 1; slot <= NEED_AUTOPOT_MAX_PRESETS; ++slot) {
		char output[CHAT_SIZE_MAX] = {};
		auto found = presets.find(slot);
		if (found == presets.end()) {
			safesnprintf(output, sizeof(output), msg_txt(sd, NEED_AUTOPOT_MSG_PRESET_EMPTY), static_cast<uint32>(slot));
			formatted_message(sd, output);
			continue;
		}
		const autopot_preset& preset = found->second;
		safesnprintf(output, sizeof(output), msg_txt(sd, NEED_AUTOPOT_MSG_PRESET_SLOT),
			static_cast<uint32>(slot), preset.name.c_str());
		formatted_message(sd, output);
		std::string hp_name;
		safesnprintf(output, sizeof(output), msg_txt(sd, NEED_AUTOPOT_MSG_PRESET_HP),
			item_name(sd, preset.hp_item_id, hp_name), static_cast<uint32>(preset.hp_item_id),
			msg_txt(sd, preset.hp_enabled ? NEED_AUTOPOT_MSG_STATE_ON : NEED_AUTOPOT_MSG_STATE_OFF),
			static_cast<uint32>(preset.hp_percent));
		formatted_message(sd, output);
		std::string sp_name;
		safesnprintf(output, sizeof(output), msg_txt(sd, NEED_AUTOPOT_MSG_PRESET_SP),
			item_name(sd, preset.sp_item_id, sp_name), static_cast<uint32>(preset.sp_item_id),
			msg_txt(sd, preset.sp_enabled ? NEED_AUTOPOT_MSG_STATE_ON : NEED_AUTOPOT_MSG_STATE_OFF),
			static_cast<uint32>(preset.sp_percent), static_cast<uint32>(preset.interval_ms));
		formatted_message(sd, output);
	}
	return true;
}

int32 need_autopot_atcommand(int32 fd, map_session_data* sd, const char* raw_message)
{
	if (sd == nullptr)
		return -1;
	std::string input = raw_message != nullptr ? raw_message : "";
	std::istringstream stream(input);
	std::string command;
	stream >> command;
	std::transform(command.begin(), command.end(), command.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (command.empty() || token_help(command)) {
		show_help(sd);
		return 0;
	}
	if (token_info(command)) {
		show_info(sd);
		return 0;
	}
	if (token_off(command)) {
		need_autopot_disable_all(sd);
		message(sd, NEED_AUTOPOT_MSG_HP_SP_DISABLED);
		return 0;
	}
	if (token_blacklist(command)) {
		message(sd, NEED_AUTOPOT_MSG_BLACKLIST_EMPTY);
		return 0;
	}
	if (token_preset(command)) {
		if (!need_autopot_list_presets(sd))
			message(sd, NEED_AUTOPOT_MSG_PRESET_LIST_FAILED);
		return 0;
	}
	if (token_delay(command)) {
		std::string value_text, extra;
		uint32 value = 0;
		if (!(stream >> value_text) || (stream >> extra) || !parse_uint(value_text, value) || !valid_interval(value)) {
			message(sd, NEED_AUTOPOT_MSG_DELAY_RANGE);
			return -1;
		}
		sd->autopot.interval_ms = static_cast<uint16>(value);
		sd->autopot.next_check_tick = gettick() + value;
		message(sd, NEED_AUTOPOT_MSG_DELAY_UPDATED);
		return 0;
	}
	if (token_load(command) || token_delete(command)) {
		const bool deleting = token_delete(command);
		std::string slot_text, extra;
		uint32 slot = 0;
		if (!(stream >> slot_text) || (stream >> extra) || !parse_uint(slot_text, slot) || !valid_slot(slot)) {
			message(sd, NEED_AUTOPOT_MSG_PRESET_SLOT_RANGE);
			return -1;
		}
		const bool result = deleting ?
			need_autopot_delete_preset(sd, static_cast<uint8>(slot)) :
			need_autopot_load_preset(sd, static_cast<uint8>(slot));
		message(sd, result ?
			(deleting ? NEED_AUTOPOT_MSG_PRESET_DELETED : NEED_AUTOPOT_MSG_PRESET_LOADED) :
			NEED_AUTOPOT_MSG_PRESET_OPERATION_FAILED);
		return result ? 0 : -1;
	}
	if (token_save(command)) {
		std::string slot_text;
		uint32 slot = 0;
		if (!(stream >> slot_text) || !parse_uint(slot_text, slot) || !valid_slot(slot)) {
			message(sd, NEED_AUTOPOT_MSG_PRESET_SAVE_USAGE);
			return -1;
		}
		std::string name;
		std::getline(stream, name);
		const size_t first = name.find_first_not_of(" \t");
		if (first != std::string::npos)
			name.erase(0, first);
		else
			name.clear();
		if (!valid_preset_name(name.c_str())) {
			message(sd, NEED_AUTOPOT_MSG_PRESET_NAME_INVALID);
			return -1;
		}
		const bool result = need_autopot_save_preset(sd, static_cast<uint8>(slot), name.c_str());
		message(sd, result ? NEED_AUTOPOT_MSG_PRESET_SAVED : NEED_AUTOPOT_MSG_PRESET_SAVE_FAILED);
		return result ? 0 : -1;
	}
	if (command != "hp" && command != "sp") {
		show_help(sd);
		return -1;
	}

	const bool hp = command == "hp";
	std::string action;
	stream >> action;
	std::transform(action.begin(), action.end(), action.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (action == "on") {
		const bool result = need_autopot_enable(sd, hp);
		message(sd, result ?
			(hp ? NEED_AUTOPOT_MSG_HP_ENABLED : NEED_AUTOPOT_MSG_SP_ENABLED) :
			NEED_AUTOPOT_MSG_ENABLE_FAILED);
		return result ? 0 : -1;
	}
	if (action == "off") {
		need_autopot_disable(sd, hp);
		message(sd, hp ? NEED_AUTOPOT_MSG_HP_DISABLED : NEED_AUTOPOT_MSG_SP_DISABLED);
		return 0;
	}
	if (token_list(action)) {
		list_inventory(sd);
		return 0;
	}

	uint32 item_id = 0, percent = 0, interval = sd->autopot.interval_ms;
	std::string percent_text, interval_text, extra;
	if (!parse_uint(action, item_id) || !(stream >> percent_text) || !parse_uint(percent_text, percent) ||
		!valid_percent(percent)) {
		message(sd, NEED_AUTOPOT_MSG_PERCENT_RANGE);
		return -1;
	}
	if (stream >> interval_text) {
		if (!parse_uint(interval_text, interval) || !valid_interval(interval) || (stream >> extra)) {
			message(sd, NEED_AUTOPOT_MSG_DELAY_RANGE);
			return -1;
		}
	}
	if (!need_autopot_validate_item(sd, item_id, true))
		return -1;

	if (hp) {
		sd->autopot.hp_item_id = static_cast<t_itemid>(item_id);
		sd->autopot.hp_percent = static_cast<uint8>(percent);
		sd->autopot.hp_missing_notified = false;
	} else {
		sd->autopot.sp_item_id = static_cast<t_itemid>(item_id);
		sd->autopot.sp_percent = static_cast<uint8>(percent);
		sd->autopot.sp_missing_notified = false;
	}
	sd->autopot.interval_ms = static_cast<uint16>(interval);
	sd->autopot.active_preset_slot = -1;
	sd->autopot.active_preset_name[0] = '\0';
	const bool result = need_autopot_enable(sd, hp);
	message(sd, result ? NEED_AUTOPOT_MSG_SETTING_ENABLED : NEED_AUTOPOT_MSG_SETTING_NOT_ENABLED);
	return result ? 0 : -1;
}
