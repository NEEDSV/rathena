// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL

#include "need_equipment_build.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <common/md5calc.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>
#include <common/timer.hpp>

#include "clif.hpp"
#include "atcommand.hpp"
#include "itemdb.hpp"
#include "log.hpp"
#include "map.hpp"
#include "pc.hpp"

namespace {

constexpr int32 NEED_BUILD_CATEGORY_MIN = 1;
constexpr int32 NEED_BUILD_CATEGORY_MAX = 6;
constexpr size_t NEED_BUILD_TITLE_MAX_CHARS = 30;
constexpr size_t NEED_BUILD_DESCRIPTION_MAX_CHARS = 200;
constexpr size_t NEED_BUILD_CHAR_ACTIVE_LIMIT = 5;
constexpr size_t NEED_BUILD_ACCOUNT_PENDING_LIMIT = 3;
constexpr int32 NEED_BUILD_PAGE_SIZE = 10;
constexpr uint32 NEED_BUILD_REWARD_AMOUNT_MAX = 100;
constexpr t_tick NEED_BUILD_REGISTER_COOLDOWN_MS = 2000;
constexpr t_tick NEED_BUILD_LIKE_COOLDOWN_MS = 1000;
constexpr t_tick NEED_BUILD_REWARD_COOLDOWN_MS = 2000;

const char* const need_build_category_names[] = {
	"Unknown", "General hunting", "Boss hunting", "Material / Zeny farming",
	"Instance dungeon", "Battleground / WoE", "Other"
};

const char* const need_build_status_names[] = {
	"Pending review", "Approved / Public", "Rejected", "Admin deleted", "Owner cancelled"
};

struct need_build_item_snapshot {
	int32 inventory_index;
	uint32 equip_position;
	item data;
};

static bool need_build_utf8_length(const unsigned char* data, size_t bytes, size_t& characters)
{
	characters = 0;
	for (size_t i = 0; i < bytes; ++characters) {
		const unsigned char c = data[i++];
		uint32 codepoint = 0;
		size_t continuation = 0;
		if (c < 0x80) {
			codepoint = c;
		} else if (c >= 0xc2 && c <= 0xdf) {
			codepoint = c & 0x1f;
			continuation = 1;
		} else if (c >= 0xe0 && c <= 0xef) {
			codepoint = c & 0x0f;
			continuation = 2;
		} else if (c >= 0xf0 && c <= 0xf4) {
			codepoint = c & 0x07;
			continuation = 3;
		} else {
			return false;
		}
		if (i + continuation > bytes)
			return false;
		for (size_t j = 0; j < continuation; ++j) {
			const unsigned char next = data[i++];
			if ((next & 0xc0) != 0x80)
				return false;
			codepoint = (codepoint << 6) | (next & 0x3f);
		}
		if ((continuation == 2 && codepoint < 0x800) ||
			(continuation == 3 && codepoint < 0x10000) ||
			(codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff ||
			codepoint < 0x20 || (codepoint >= 0x7f && codepoint <= 0x9f))
			return false;
	}
	return true;
}

static bool need_build_euckr_length(const unsigned char* data, size_t bytes, size_t& characters)
{
	characters = 0;
	for (size_t i = 0; i < bytes; ++characters) {
		const unsigned char c = data[i++];
		if (c < 0x20 || c == 0x7f)
			return false;
		if (c < 0x80)
			continue;
		if (i >= bytes)
			return false;
		const unsigned char trail = data[i++];
		if (trail < 0x41 || trail == 0x7f)
			return false;
	}
	return true;
}

static bool need_build_text_length(const char* text, size_t& characters)
{
	if (text == nullptr)
		return false;
	const auto* data = reinterpret_cast<const unsigned char*>(text);
	const size_t bytes = std::strlen(text);
	return need_build_utf8_length(data, bytes, characters) || need_build_euckr_length(data, bytes, characters);
}

static bool need_build_validate_input(int32 category, const char* title, const char* description)
{
	if (category < NEED_BUILD_CATEGORY_MIN || category > NEED_BUILD_CATEGORY_MAX || title == nullptr || description == nullptr)
		return false;

	size_t title_length = 0;
	size_t description_length = 0;
	return std::strchr(title, ':') == nullptr && std::strchr(title, '^') == nullptr &&
		need_build_text_length(title, title_length) && title_length > 0 && title_length <= NEED_BUILD_TITLE_MAX_CHARS &&
		need_build_text_length(description, description_length) && description_length <= NEED_BUILD_DESCRIPTION_MAX_CHARS;
}

static bool need_build_request_allowed(t_tick& available_tick, t_tick cooldown)
{
	const t_tick now = gettick();
	if (available_tick != 0 && DIFF_TICK(available_tick, now) > 0)
		return false;
	available_tick = now + cooldown;
	return true;
}

static std::vector<need_build_item_snapshot> need_build_collect_items(map_session_data* sd)
{
	std::vector<need_build_item_snapshot> result;
	std::set<int32> seen_inventory_indexes;

	for (int32 equip_index = 0; equip_index < EQI_MAX; ++equip_index) {
		if ((equip_index >= EQI_COSTUME_HEAD_TOP && equip_index <= EQI_COSTUME_GARMENT) || equip_index == EQI_AMMO)
			continue;

		const int32 inventory_index = sd->equip_index[equip_index];
		if (inventory_index < 0 || !seen_inventory_indexes.insert(inventory_index).second)
			continue;

		const item& equipped_item = sd->inventory.u.items_inventory[inventory_index];
		if (equipped_item.nameid == 0 || equipped_item.equip == 0)
			continue;

		result.push_back({ inventory_index, equipped_item.equip, equipped_item });
	}

	std::sort(result.begin(), result.end(), [](const need_build_item_snapshot& left, const need_build_item_snapshot& right) {
		if (left.equip_position != right.equip_position)
			return left.equip_position < right.equip_position;
		return left.inventory_index < right.inventory_index;
	});
	return result;
}

static std::string need_build_snapshot_hash(const map_session_data* sd, int32 category, const std::vector<need_build_item_snapshot>& items)
{
	std::ostringstream canonical;
	canonical << sd->status.account_id << '|' << sd->status.class_ << '|' << category << '|'
		<< sd->status.str << '|' << sd->status.agi << '|' << sd->status.vit << '|'
		<< sd->status.int_ << '|' << sd->status.dex << '|' << sd->status.luk;

	for (const need_build_item_snapshot& snapshot : items) {
		const item& current = snapshot.data;
		canonical << '|' << snapshot.equip_position << ':' << current.nameid << ':'
			<< static_cast<uint32>(current.refine) << ':' << static_cast<uint32>(current.enchantgrade);
		for (size_t slot = 0; slot < MAX_SLOTS; ++slot)
			canonical << ':' << current.card[slot];
		for (size_t option = 0; option < MAX_ITEM_RDM_OPT; ++option)
			canonical << ':' << current.option[option].id << ':' << current.option[option].value << ':' << static_cast<int32>(current.option[option].param);
	}

	char hash[33] = {};
	MD5_String(canonical.str().c_str(), hash);
	return hash;
}

static void need_build_log(const map_session_data* sd, int32 category, uint64 build_id, size_t item_count, int64 result, const char* stage)
{
	ShowInfo("NEED equipment build: account_id=%u char_id=%u char_name='%s' category=%d build_id=%" PRIu64 " item_count=%zu result=%" PRId64 " stage=%s\n",
		sd != nullptr ? sd->status.account_id : 0, sd != nullptr ? sd->status.char_id : 0,
		sd != nullptr ? sd->status.name : "", category, build_id, item_count, result, stage);
}

static int64 need_build_fail(map_session_data* sd, int32 category, uint64 build_id, size_t item_count, int64 result, const char* stage, bool rollback)
{
	if (rollback && mmysql_handle != nullptr && SQL_ERROR == Sql_QueryStr(mmysql_handle, "ROLLBACK"))
		Sql_ShowDebug(mmysql_handle);
	need_build_log(sd, category, build_id, item_count, result, stage);
	return result;
}

static bool need_build_get_column(Sql* handle, size_t column, char*& value)
{
	value = nullptr;
	return SQL_SUCCESS == Sql_GetData(handle, column, &value, nullptr) && value != nullptr;
}

static std::string need_build_column_string(Sql* handle, size_t column)
{
	char* value = nullptr;
	return need_build_get_column(handle, column, value) ? value : "";
}

static int32 need_build_column_int(Sql* handle, size_t column)
{
	const std::string value = need_build_column_string(handle, column);
	return value.empty() ? 0 : atoi(value.c_str());
}

static uint64 need_build_column_uint64(Sql* handle, size_t column)
{
	const std::string value = need_build_column_string(handle, column);
	return value.empty() ? 0 : strtoull(value.c_str(), nullptr, 10);
}

static std::string need_build_safe_job_name(map_session_data* sd, int32 job_id)
{
	if (job_db.find(job_id) == nullptr || pc_jobid2mapid(static_cast<uint16>(job_id)) == MAPID_ALL) {
		ShowWarning("NEED equipment build: unknown job_id=%d.\n", job_id);
		if (sd != nullptr) {
			char output[CHAT_SIZE_MAX] = {};
			safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_UNKNOWN_JOB), job_id);
			return output;
		}
		return "Unknown job";
	}
	return job_name(job_id);
}

static int64 need_build_read_page(Sql* handle, bool include_owner, need_equipment_build_page& result, map_session_data* sd)
{
	result.entries.clear();
	result.has_more = false;
	while (SQL_SUCCESS == Sql_NextRow(handle)) {
		need_equipment_build_list_entry entry;
		entry.build_id = need_build_column_uint64(handle, 0);
		entry.job_id = need_build_column_int(handle, 1);
		entry.category = need_build_column_int(handle, 2);
		entry.status = need_build_column_int(handle, 3);
		entry.job_name = need_build_safe_job_name(sd, entry.job_id);
		entry.title = need_build_column_string(handle, 4);
		entry.like_count = static_cast<uint32>(need_build_column_uint64(handle, 5));
		if (include_owner)
			entry.owner_name = need_build_column_string(handle, 6);
		if (result.entries.size() == NEED_BUILD_PAGE_SIZE) {
			result.has_more = true;
			break;
		}
		result.entries.push_back(std::move(entry));
	}
	Sql_FreeResult(handle);
	return static_cast<int64>(result.entries.size());
}

static bool need_build_reason_valid(const char* reason)
{
	size_t length = 0;
	return reason != nullptr && need_build_text_length(reason, length) && length > 0 && length <= NEED_BUILD_DESCRIPTION_MAX_CHARS;
}

static const char* need_build_position_name(map_session_data* sd, uint32 position)
{
	if (position & EQP_HEAD_TOP) return msg_txt(sd, NEED_BUILD_MSG_POSITION_HEAD_TOP);
	if (position & EQP_HEAD_MID) return msg_txt(sd, NEED_BUILD_MSG_POSITION_HEAD_MID);
	if (position & EQP_HEAD_LOW) return msg_txt(sd, NEED_BUILD_MSG_POSITION_HEAD_LOW);
	if (position & EQP_ARMOR) return msg_txt(sd, NEED_BUILD_MSG_POSITION_ARMOR);
	if (position & EQP_HAND_R) return msg_txt(sd, NEED_BUILD_MSG_POSITION_RIGHT_HAND);
	if (position & EQP_HAND_L) return msg_txt(sd, NEED_BUILD_MSG_POSITION_LEFT_HAND);
	if (position & EQP_GARMENT) return msg_txt(sd, NEED_BUILD_MSG_POSITION_GARMENT);
	if (position & EQP_SHOES) return msg_txt(sd, NEED_BUILD_MSG_POSITION_SHOES);
	if (position & EQP_ACC_L) return msg_txt(sd, NEED_BUILD_MSG_POSITION_ACC_LEFT);
	if (position & EQP_ACC_R) return msg_txt(sd, NEED_BUILD_MSG_POSITION_ACC_RIGHT);
	if (position & EQP_SHADOW_ARMOR) return msg_txt(sd, NEED_BUILD_MSG_POSITION_SHADOW_ARMOR);
	if (position & EQP_SHADOW_WEAPON) return msg_txt(sd, NEED_BUILD_MSG_POSITION_SHADOW_WEAPON);
	if (position & EQP_SHADOW_SHIELD) return msg_txt(sd, NEED_BUILD_MSG_POSITION_SHADOW_SHIELD);
	if (position & EQP_SHADOW_SHOES) return msg_txt(sd, NEED_BUILD_MSG_POSITION_SHADOW_SHOES);
	if (position & EQP_SHADOW_ACC_R) return msg_txt(sd, NEED_BUILD_MSG_POSITION_SHADOW_EARRING);
	if (position & EQP_SHADOW_ACC_L) return msg_txt(sd, NEED_BUILD_MSG_POSITION_SHADOW_PENDANT);
	return msg_txt(sd, NEED_BUILD_MSG_POSITION_UNKNOWN);
}

static int32 need_build_position_order(uint32 position)
{
	const uint32 order[] = { EQP_HEAD_TOP, EQP_HEAD_MID, EQP_HEAD_LOW, EQP_ARMOR, EQP_HAND_R, EQP_HAND_L,
		EQP_GARMENT, EQP_SHOES, EQP_ACC_L, EQP_ACC_R, EQP_SHADOW_ARMOR, EQP_SHADOW_WEAPON,
		EQP_SHADOW_SHIELD, EQP_SHADOW_SHOES, EQP_SHADOW_ACC_R, EQP_SHADOW_ACC_L };
	for (size_t index = 0; index < ARRAYLENGTH(order); ++index) {
		if (position & order[index])
			return static_cast<int32>(index);
	}
	return static_cast<int32>(ARRAYLENGTH(order));
}

static const char* need_equipment_build_claim_status_name(int32 status, map_session_data* sd)
{
	switch (status) {
		case 0: return msg_txt(sd, NEED_BUILD_MSG_CLAIM_UNCLAIMED);
		case 1: return msg_txt(sd, NEED_BUILD_MSG_CLAIM_PROCESSING);
		case 2: return msg_txt(sd, NEED_BUILD_MSG_CLAIM_COMPLETE);
		default: return msg_txt(sd, NEED_BUILD_MSG_CLAIM_UNKNOWN);
	}
}

struct need_build_stored_item {
	uint64 build_item_id = 0;
	uint32 equip_position = 0;
	item data{};
};

static bool need_build_load_items(uint64 build_id, std::vector<need_build_stored_item>& items)
{
	items.clear();
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `build_item_id`,`equip_position`,`item_id`,`refine`,`grade`,`attribute`,`identify`,"
		"`card_0`,`card_1`,`card_2`,`card_3`,"
		"`random_option_0`,`random_option_value_0`,`random_option_param_0`,`random_option_1`,`random_option_value_1`,`random_option_param_1`,"
		"`random_option_2`,`random_option_value_2`,`random_option_param_2`,`random_option_3`,`random_option_value_3`,`random_option_param_3`,"
		"`random_option_4`,`random_option_value_4`,`random_option_param_4` FROM `need_equipment_build_item` WHERE `build_id`='%" PRIu64 "' "
		"ORDER BY `equip_position`,`build_item_id`", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		return false;
	}
	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		need_build_stored_item stored;
		stored.build_item_id = need_build_column_uint64(mmysql_handle, 0);
		stored.equip_position = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 1));
		stored.data.nameid = static_cast<t_itemid>(need_build_column_uint64(mmysql_handle, 2));
		stored.data.refine = static_cast<char>(need_build_column_int(mmysql_handle, 3));
		stored.data.enchantgrade = static_cast<uint8>(need_build_column_int(mmysql_handle, 4));
		stored.data.attribute = static_cast<char>(need_build_column_int(mmysql_handle, 5));
		stored.data.identify = static_cast<char>(need_build_column_int(mmysql_handle, 6));
		for (size_t slot = 0; slot < MAX_SLOTS; ++slot)
			stored.data.card[slot] = static_cast<t_itemid>(need_build_column_uint64(mmysql_handle, 7 + slot));
		for (size_t option = 0; option < MAX_ITEM_RDM_OPT; ++option) {
			const size_t base = 11 + option * 3;
			stored.data.option[option].id = static_cast<int16>(need_build_column_int(mmysql_handle, base));
			stored.data.option[option].value = static_cast<int16>(need_build_column_int(mmysql_handle, base + 1));
			stored.data.option[option].param = static_cast<char>(need_build_column_int(mmysql_handle, base + 2));
		}
		items.push_back(stored);
	}
	Sql_FreeResult(mmysql_handle);
	std::sort(items.begin(), items.end(), [](const need_build_stored_item& left, const need_build_stored_item& right) {
		const int32 left_order = need_build_position_order(left.equip_position);
		const int32 right_order = need_build_position_order(right.equip_position);
		return left_order == right_order ? left.build_item_id < right.build_item_id : left_order < right_order;
	});
	return true;
}

static std::string need_build_random_option_name(const std::string& database_name)
{
	std::string result = database_name;
	if (result.compare(0, 4, "VAR_") == 0)
		result.erase(0, 4);
	const std::string amount_suffix = "AMOUNT";
	if (result.size() > amount_suffix.size() && result.compare(result.size() - amount_suffix.size(), amount_suffix.size(), amount_suffix) == 0)
		result.erase(result.size() - amount_suffix.size());
	if (result == "MAXHP") return "MaxHP";
	if (result == "MAXSP") return "MaxSP";
	return result;
}

static bool need_build_display_items(map_session_data* sd, uint64 build_id, bool admin_detail)
{
	std::vector<need_build_stored_item> items;
	if (!need_build_load_items(build_id, items))
		return false;
	if (items.empty()) {
		clif_displaymessage(sd->fd, msg_txt(sd, NEED_BUILD_MSG_NO_ITEMS));
		ShowWarning("NEED equipment build: build_id=%" PRIu64 " has no item snapshot rows.\n", build_id);
		return true;
	}
	char output[CHAT_SIZE_MAX] = {};
	clif_displaymessage(sd->fd, msg_txt(sd, NEED_BUILD_MSG_ITEM_HEADER));
	for (need_build_stored_item& stored : items) {
		std::shared_ptr<item_data> data = item_db.find(stored.data.nameid);
		const std::string item_text = data != nullptr ? item_db.create_item_link(stored.data) : msg_txt(sd, NEED_BUILD_MSG_UNKNOWN_ITEM);
		if (admin_detail) {
			safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_ITEM_ADMIN), need_build_position_name(sd, stored.equip_position),
				item_text.c_str(), stored.data.nameid, static_cast<uint32>(stored.data.refine), static_cast<uint32>(stored.data.enchantgrade),
				static_cast<int32>(stored.data.attribute), static_cast<int32>(stored.data.identify));
		} else if (stored.data.refine > 0) {
			safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_ITEM_REFINED), need_build_position_name(sd, stored.equip_position),
				item_text.c_str(), static_cast<uint32>(stored.data.refine));
		} else {
			safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_ITEM), need_build_position_name(sd, stored.equip_position), item_text.c_str());
		}
		clif_displaymessage(sd->fd, output);
		const bool special_card_data = itemdb_isspecial(stored.data.card[0]);
		if (special_card_data) {
			if (admin_detail) {
				safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_SPECIAL_CARD_ADMIN), stored.data.card[0], stored.data.card[1],
					stored.data.card[2], stored.data.card[3]);
				clif_displaymessage(sd->fd, output);
			}
		}
		uint32 displayed_cards = 0;
		for (size_t slot = 0; !special_card_data && slot < MAX_SLOTS; ++slot) {
			const t_itemid card_id = stored.data.card[slot];
			if (card_id == 0)
				continue;
			std::shared_ptr<item_data> card_data = item_db.find(card_id);
			char card_name[CHAT_SIZE_MAX] = {};
			if (card_data != nullptr && card_data->type == IT_CARD)
				safesnprintf(card_name, sizeof(card_name), "%s", item_db.create_item_link(card_id).c_str());
			else
				safesnprintf(card_name, sizeof(card_name), msg_txt(sd, NEED_BUILD_MSG_UNKNOWN_CARD), card_id);
			++displayed_cards;
			if (admin_detail)
				safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_CARD_ADMIN), displayed_cards, card_name, card_id);
			else
				safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_CARD), displayed_cards, card_name);
			clif_displaymessage(sd->fd, output);
		}
		for (size_t index = 0; index < MAX_ITEM_RDM_OPT; ++index) {
			if (stored.data.option[index].id == 0)
				continue;
			const int16 option_id = stored.data.option[index].id;
			std::shared_ptr<s_random_opt_data> option_data = random_option_db.find(static_cast<uint16>(option_id));
			char option_name[CHAT_SIZE_MAX] = {};
			if (option_data != nullptr) {
				const std::string display_name = need_build_random_option_name(option_data->name);
				safesnprintf(option_name, sizeof(option_name), "%s", display_name.c_str());
				if (admin_detail)
					safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_OPTION_ADMIN), static_cast<uint32>(index + 1), option_name,
						option_id, stored.data.option[index].value, static_cast<int32>(stored.data.option[index].param));
				else
					safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_OPTION), static_cast<uint32>(index + 1), option_name,
						stored.data.option[index].value);
			} else {
				safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_UNKNOWN_OPTION), option_id, stored.data.option[index].value);
			}
			clif_displaymessage(sd->fd, output);
		}
	}
	return true;
}

static void need_build_state_log(const map_session_data* sd, uint64 build_id, const char* action, int32 previous_status, int32 target_status, int64 result, const char* stage)
{
	ShowInfo("NEED equipment build state: account_id=%u char_id=%u build_id=%" PRIu64 " action=%s previous_status=%d target_status=%d result=%" PRId64 " stage=%s\n",
		sd != nullptr ? sd->status.account_id : 0, sd != nullptr ? sd->status.char_id : 0, build_id,
		action != nullptr ? action : "", previous_status, target_status, result, stage != nullptr ? stage : "");
}

static bool need_build_insert_admin_log(map_session_data* sd, uint64 build_id, const char* action, const char* reason)
{
	SqlStmt statement{ *mmysql_handle };
	return SQL_SUCCESS == statement.PrepareStr(
		"INSERT INTO `need_equipment_build_admin_log` (`build_id`,`admin_account_id`,`admin_char_id`,`action`,`reason`,`created_at`) VALUES (?,?,?,?,?,NOW())") &&
		SQL_SUCCESS == statement.BindParam(0, SQLDT_UINT64, &build_id, sizeof(build_id)) &&
		SQL_SUCCESS == statement.BindParam(1, SQLDT_UINT32, &sd->status.account_id, sizeof(sd->status.account_id)) &&
		SQL_SUCCESS == statement.BindParam(2, SQLDT_UINT32, &sd->status.char_id, sizeof(sd->status.char_id)) &&
		SQL_SUCCESS == statement.BindParam(3, SQLDT_STRING, const_cast<char*>(action), std::strlen(action)) &&
		SQL_SUCCESS == statement.BindParam(4, SQLDT_STRING, const_cast<char*>(reason), std::strlen(reason)) &&
		SQL_SUCCESS == statement.Execute();
}

} // namespace

int64 need_equipment_build_register(map_session_data* sd, int32 category, const char* title, const char* description)
{
	if (sd == nullptr || sd->fd <= 0 || !session_isActive(sd->fd))
		return need_build_fail(sd, category, 0, 0, NEED_BUILD_ERROR_NO_PLAYER, "player_session", false);
	if (!need_build_request_allowed(sd->need_equipment_build_register_tick, NEED_BUILD_REGISTER_COOLDOWN_MS))
		return need_build_fail(sd, category, 0, 0, NEED_BUILD_ERROR_COOLDOWN, "cooldown", false);
	if (!need_build_validate_input(category, title, description))
		return need_build_fail(sd, category, 0, 0, NEED_BUILD_ERROR_INPUT, "input_validation", false);
	const uint32 base_level = static_cast<uint32>(pc_readparam(sd, SP_BASELEVEL));
	const uint32 job_level = static_cast<uint32>(pc_readparam(sd, SP_JOBLEVEL));
	if (base_level == 0 || job_level == 0)
		return need_build_fail(sd, category, 0, 0, NEED_BUILD_ERROR_INTERNAL, "invalid_character_level", false);

	const std::vector<need_build_item_snapshot> items = need_build_collect_items(sd);
	if (items.empty())
		return need_build_fail(sd, category, 0, 0, NEED_BUILD_ERROR_NO_EQUIPMENT, "equipment_collection", false);

	const std::string snapshot_hash = need_build_snapshot_hash(sd, category, items);
	if (mmysql_handle == nullptr)
		return need_build_fail(sd, category, 0, items.size(), NEED_BUILD_ERROR_DATABASE, "database_connection", false);
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
		Sql_ShowDebug(mmysql_handle);
		return need_build_fail(sd, category, 0, items.size(), NEED_BUILD_ERROR_DATABASE, "transaction_start", false);
	}

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `char_id`,`status`,`snapshot_hash` FROM `need_equipment_build` WHERE `account_id`='%u' AND `status` IN (0,1) FOR UPDATE",
		sd->status.account_id)) {
		Sql_ShowDebug(mmysql_handle);
		return need_build_fail(sd, category, 0, items.size(), NEED_BUILD_ERROR_DATABASE, "limit_query", true);
	}

	size_t character_active = 0;
	size_t account_pending = 0;
	bool duplicate = false;
	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		char* char_id_text = nullptr;
		char* status_text = nullptr;
		char* hash_text = nullptr;
		if (!need_build_get_column(mmysql_handle, 0, char_id_text) || !need_build_get_column(mmysql_handle, 1, status_text) || !need_build_get_column(mmysql_handle, 2, hash_text))
			continue;
		if (strtoul(char_id_text, nullptr, 10) == sd->status.char_id)
			++character_active;
		if (atoi(status_text) == 0)
			++account_pending;
		if (snapshot_hash == hash_text)
			duplicate = true;
	}
	Sql_FreeResult(mmysql_handle);

	if (character_active >= NEED_BUILD_CHAR_ACTIVE_LIMIT || account_pending >= NEED_BUILD_ACCOUNT_PENDING_LIMIT)
		return need_build_fail(sd, category, 0, items.size(), NEED_BUILD_ERROR_LIMIT, "registration_limit", true);
	if (duplicate)
		return need_build_fail(sd, category, 0, items.size(), NEED_BUILD_ERROR_DUPLICATE, "duplicate_snapshot", true);

	uint64 build_id = 0;
	{
		SqlStmt statement{ *mmysql_handle };
		if (SQL_SUCCESS != statement.Prepare(
			"INSERT INTO `need_equipment_build` (`account_id`,`char_id`,`char_name`,`job_id`,`base_level`,`job_level`,`str`,`agi`,`vit`,`int`,`dex`,`luk`,`category`,`title`,`description`,`status`,`snapshot_hash`,`created_at`,`updated_at`) "
			"VALUES ('%u','%u',?,'%d','%u','%u','%hu','%hu','%hu','%hu','%hu','%hu','%d',?,?,0,?,NOW(),NOW())",
			sd->status.account_id, sd->status.char_id, sd->status.class_, base_level, job_level,
			sd->status.str, sd->status.agi, sd->status.vit, sd->status.int_, sd->status.dex, sd->status.luk, category) ||
			SQL_SUCCESS != statement.BindParam(0, SQLDT_STRING, sd->status.name, strnlen(sd->status.name, NAME_LENGTH)) ||
			SQL_SUCCESS != statement.BindParam(1, SQLDT_STRING, const_cast<char*>(title), std::strlen(title)) ||
			SQL_SUCCESS != statement.BindParam(2, SQLDT_STRING, const_cast<char*>(description), std::strlen(description)) ||
			SQL_SUCCESS != statement.BindParam(3, SQLDT_STRING, const_cast<char*>(snapshot_hash.c_str()), snapshot_hash.size()) ||
			SQL_SUCCESS != statement.Execute()) {
			SqlStmt_ShowDebug(statement);
			return need_build_fail(sd, category, 0, items.size(), NEED_BUILD_ERROR_BUILD_INSERT, "build_insert", true);
		}
		build_id = statement.LastInsertId();
	}

	if (build_id == 0)
		return need_build_fail(sd, category, 0, items.size(), NEED_BUILD_ERROR_BUILD_INSERT, "build_id", true);

	for (const need_build_item_snapshot& snapshot : items) {
		const item& current = snapshot.data;
		if (SQL_ERROR == Sql_Query(mmysql_handle,
			"INSERT INTO `need_equipment_build_item` (`build_id`,`equip_position`,`inventory_index`,`item_id`,`refine`,`grade`,`attribute`,`identify`,"
			"`card_0`,`card_1`,`card_2`,`card_3`,"
			"`random_option_0`,`random_option_value_0`,`random_option_param_0`,`random_option_1`,`random_option_value_1`,`random_option_param_1`,"
			"`random_option_2`,`random_option_value_2`,`random_option_param_2`,`random_option_3`,`random_option_value_3`,`random_option_param_3`,"
			"`random_option_4`,`random_option_value_4`,`random_option_param_4`) VALUES ("
			"'%" PRIu64 "','%u','%d','%u','%u','%u','%u','%u','%u','%u','%u','%u',"
			"'%d','%d','%d','%d','%d','%d','%d','%d','%d','%d','%d','%d','%d','%d','%d')",
			build_id, snapshot.equip_position, snapshot.inventory_index, current.nameid,
			static_cast<uint32>(current.refine), static_cast<uint32>(current.enchantgrade), static_cast<uint32>(current.attribute), static_cast<uint32>(current.identify),
			current.card[0], current.card[1], current.card[2], current.card[3],
			current.option[0].id, current.option[0].value, static_cast<int32>(current.option[0].param),
			current.option[1].id, current.option[1].value, static_cast<int32>(current.option[1].param),
			current.option[2].id, current.option[2].value, static_cast<int32>(current.option[2].param),
			current.option[3].id, current.option[3].value, static_cast<int32>(current.option[3].param),
			current.option[4].id, current.option[4].value, static_cast<int32>(current.option[4].param))) {
			Sql_ShowDebug(mmysql_handle);
			return need_build_fail(sd, category, build_id, items.size(), NEED_BUILD_ERROR_ITEM_INSERT, "item_insert", true);
		}
	}

	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		Sql_ShowDebug(mmysql_handle);
		return need_build_fail(sd, category, build_id, items.size(), NEED_BUILD_ERROR_COMMIT, "transaction_commit", true);
	}

	need_build_log(sd, category, build_id, items.size(), static_cast<int64>(build_id), "complete");
	return static_cast<int64>(build_id);
}

int64 need_equipment_build_count(uint32 account_id)
{
	if (mmysql_handle == nullptr || SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT COUNT(*) FROM `need_equipment_build` WHERE `account_id`='%u'", account_id)) {
		if (mmysql_handle != nullptr)
			Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	char* value = nullptr;
	int64 count = NEED_BUILD_ERROR_DATABASE;
	if (SQL_SUCCESS == Sql_NextRow(mmysql_handle) && need_build_get_column(mmysql_handle, 0, value))
		count = strtoll(value, nullptr, 10);
	Sql_FreeResult(mmysql_handle);
	return count;
}

int64 need_equipment_build_last_id(uint32 account_id)
{
	if (mmysql_handle == nullptr || SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `build_id` FROM `need_equipment_build` WHERE `account_id`='%u' ORDER BY `build_id` DESC LIMIT 1", account_id)) {
		if (mmysql_handle != nullptr)
			Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	char* value = nullptr;
	int64 build_id = 0;
	if (SQL_SUCCESS == Sql_NextRow(mmysql_handle) && need_build_get_column(mmysql_handle, 0, value))
		build_id = strtoll(value, nullptr, 10);
	Sql_FreeResult(mmysql_handle);
	return build_id;
}

const char* need_equipment_build_category_name(int32 category, map_session_data* sd)
{
	if (category < NEED_BUILD_CATEGORY_MIN || category > NEED_BUILD_CATEGORY_MAX) {
		ShowWarning("NEED equipment build: unknown category=%d.\n", category);
		return sd != nullptr ? msg_txt(sd, NEED_BUILD_MSG_CATEGORY_UNKNOWN) : need_build_category_names[0];
	}
	if (sd != nullptr)
		return msg_txt(sd, NEED_BUILD_MSG_CATEGORY_GENERAL + category - 1);
	return need_build_category_names[category];
}

const char* need_equipment_build_status_name(int32 status, map_session_data* sd)
{
	if (status < 0 || status >= static_cast<int32>(ARRAYLENGTH(need_build_status_names)))
		return sd != nullptr ? msg_txt(sd, NEED_BUILD_MSG_STATUS_UNKNOWN) : "Unknown";
	if (sd != nullptr)
		return msg_txt(sd, NEED_BUILD_MSG_STATUS_PENDING + status);
	return need_build_status_names[status];
}

bool need_equipment_build_is_admin(const map_session_data* sd)
{
	return sd != nullptr && pc_can_use_command(sd, "needbuildcheck", COMMAND_ATCOMMAND);
}

int64 need_equipment_build_admin_list(map_session_data* sd, int32 status, int32 page, need_equipment_build_page& result)
{
	result = {};
	if (!need_equipment_build_is_admin(sd))
		return NEED_BUILD_ERROR_UNAUTHORIZED;
	if (status < 0 || status > 4 || page < 0)
		return NEED_BUILD_ERROR_INPUT;
	if (mmysql_handle == nullptr)
		return NEED_BUILD_ERROR_DATABASE;
	const char* order = status == 0 ? "`created_at` ASC,`build_id` ASC" : "`updated_at` DESC,`build_id` DESC";
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `build_id`,`job_id`,`category`,`status`,`title`,`like_count`,`char_name` FROM `need_equipment_build` WHERE `status`='%d' ORDER BY %s LIMIT %d OFFSET %d",
		status, order, NEED_BUILD_PAGE_SIZE + 1, page * NEED_BUILD_PAGE_SIZE)) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	return need_build_read_page(mmysql_handle, true, result, sd);
}

int64 need_equipment_build_admin_search(map_session_data* sd, int32 search_type, const char* value, int32 page, need_equipment_build_page& result)
{
	result = {};
	if (!need_equipment_build_is_admin(sd))
		return NEED_BUILD_ERROR_UNAUTHORIZED;
	if (value == nullptr || *value == '\0' || page < 0 || search_type < 1 || search_type > 3)
		return NEED_BUILD_ERROR_INPUT;
	if (mmysql_handle == nullptr)
		return NEED_BUILD_ERROR_DATABASE;

	int32 query_result = SQL_ERROR;
	if (search_type == 2) {
		char escaped[NAME_LENGTH * 2 + 1] = {};
		Sql_EscapeStringLen(mmysql_handle, escaped, value, strnlen(value, NAME_LENGTH));
		query_result = Sql_Query(mmysql_handle,
			"SELECT `build_id`,`job_id`,`category`,`status`,`title`,`like_count`,`char_name` FROM `need_equipment_build` WHERE `char_name`='%s' ORDER BY `build_id` DESC LIMIT %d OFFSET %d",
			escaped, NEED_BUILD_PAGE_SIZE + 1, page * NEED_BUILD_PAGE_SIZE);
	} else {
		char* end = nullptr;
		const uint64 numeric_value = strtoull(value, &end, 10);
		if (numeric_value == 0 || end == value || *end != '\0')
			return NEED_BUILD_ERROR_INPUT;
		if (search_type == 1)
			query_result = Sql_Query(mmysql_handle,
				"SELECT `build_id`,`job_id`,`category`,`status`,`title`,`like_count`,`char_name` FROM `need_equipment_build` WHERE `build_id`='%" PRIu64 "' LIMIT %d OFFSET %d",
				numeric_value, NEED_BUILD_PAGE_SIZE + 1, page * NEED_BUILD_PAGE_SIZE);
		else
			query_result = Sql_Query(mmysql_handle,
				"SELECT `build_id`,`job_id`,`category`,`status`,`title`,`like_count`,`char_name` FROM `need_equipment_build` WHERE `account_id`='%" PRIu64 "' ORDER BY `build_id` DESC LIMIT %d OFFSET %d",
				numeric_value, NEED_BUILD_PAGE_SIZE + 1, page * NEED_BUILD_PAGE_SIZE);
	}
	if (SQL_ERROR == query_result) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	return need_build_read_page(mmysql_handle, true, result, sd);
}

int64 need_equipment_build_public_jobs(int32 family, std::vector<need_equipment_build_job_entry>& result)
{
	result.clear();
	if (family < 1 || family > 8 || mmysql_handle == nullptr)
		return NEED_BUILD_ERROR_INPUT;
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "SELECT DISTINCT `job_id` FROM `need_equipment_build` WHERE `status`=1 ORDER BY `job_id`")) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		const int32 job_id = need_build_column_int(mmysql_handle, 0);
		if (job_db.find(job_id) == nullptr) {
			ShowWarning("NEED equipment build: unknown public job_id=%d.\n", job_id);
			continue;
		}
		const uint64 map_id = pc_jobid2mapid(static_cast<uint16>(job_id));
		const uint64 first_job = map_id & MAPID_FIRSTMASK;
		int32 job_family = 8;
		switch (first_job) {
			case MAPID_NOVICE: job_family = 1; break;
			case MAPID_SWORDMAN: job_family = 2; break;
			case MAPID_MAGE: job_family = 3; break;
			case MAPID_ARCHER: job_family = 4; break;
			case MAPID_ACOLYTE: job_family = 5; break;
			case MAPID_MERCHANT: job_family = 6; break;
			case MAPID_THIEF: job_family = 7; break;
			default: break;
		}
		if (job_family == family)
			result.push_back({ job_id, job_name(job_id) });
	}
	Sql_FreeResult(mmysql_handle);
	std::sort(result.begin(), result.end(), [](const need_equipment_build_job_entry& left, const need_equipment_build_job_entry& right) {
		return left.name < right.name;
	});
	return static_cast<int64>(result.size());
}

int64 need_equipment_build_public_list(int32 job_id, int32 category, int32 sort_type, int32 page, need_equipment_build_page& result)
{
	result = {};
	if (job_db.find(job_id) == nullptr || category < NEED_BUILD_CATEGORY_MIN || category > NEED_BUILD_CATEGORY_MAX || page < 0)
		return NEED_BUILD_ERROR_INPUT;
	if (sort_type != 1 && sort_type != 2)
		sort_type = 1;
	if (mmysql_handle == nullptr)
		return NEED_BUILD_ERROR_DATABASE;
	// Privacy boundary: this query intentionally selects no owner identifiers.
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `build_id`,`job_id`,`category`,`status`,`title`,`like_count` FROM `need_equipment_build` WHERE `status`=1 AND `job_id`='%d' AND `category`='%d' ORDER BY %s LIMIT %d OFFSET %d",
		job_id, category, sort_type == 1 ? "`like_count` DESC,`approved_at` DESC,`build_id` DESC" : "`approved_at` DESC,`build_id` DESC",
		NEED_BUILD_PAGE_SIZE + 1, page * NEED_BUILD_PAGE_SIZE)) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	return need_build_read_page(mmysql_handle, false, result, nullptr);
}

int64 need_equipment_build_owner_list(map_session_data* sd, int32 page, need_equipment_build_page& result)
{
	result = {};
	if (sd == nullptr || page < 0)
		return NEED_BUILD_ERROR_NO_PLAYER;
	if (mmysql_handle == nullptr)
		return NEED_BUILD_ERROR_DATABASE;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `build_id`,`job_id`,`category`,`status`,`title`,`like_count` FROM `need_equipment_build` WHERE `account_id`='%u' AND `char_id`='%u' ORDER BY `build_id` DESC LIMIT %d OFFSET %d",
		sd->status.account_id, sd->status.char_id, NEED_BUILD_PAGE_SIZE + 1, page * NEED_BUILD_PAGE_SIZE)) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	return need_build_read_page(mmysql_handle, false, result, sd);
}

int64 need_equipment_build_admin_detail(map_session_data* sd, uint64 build_id)
{
	if (!need_equipment_build_is_admin(sd))
		return NEED_BUILD_ERROR_UNAUTHORIZED;
	if (build_id == 0 || mmysql_handle == nullptr)
		return NEED_BUILD_ERROR_INPUT;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `account_id`,`char_id`,`char_name`,`job_id`,`base_level`,`job_level`,`category`,`title`,`description`,"
		"`str`,`agi`,`vit`,`int`,`dex`,`luk`,`status`,`snapshot_hash`,`created_at`,`updated_at`,`approved_at`,`approved_by`,"
		"`deleted_at`,`deleted_by`,`review_reason`,`delete_reason`,`reward_eligible`,`reward_claimed`,`reward_claimed_at`,`cancelled_at`,`like_count`,"
		"`reward_item_id`,`reward_amount`,`reward_approved_by`,`reward_claim_status`,`reward_claim_started_at` "
		"FROM `need_equipment_build` WHERE `build_id`='%" PRIu64 "' LIMIT 1", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		return NEED_BUILD_ERROR_NOT_FOUND;
	}
	std::array<std::string, 35> value;
	for (size_t index = 0; index < value.size(); ++index)
		value[index] = need_build_column_string(mmysql_handle, index);
	Sql_FreeResult(mmysql_handle);
	char output[CHAT_SIZE_MAX] = {};
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_ADMIN_HEADER), static_cast<unsigned long long>(build_id));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_ADMIN_OWNER), value[0].c_str(), value[1].c_str(), value[2].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_JOB), need_build_safe_job_name(sd, atoi(value[3].c_str())).c_str(), atoi(value[3].c_str()));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_LEVEL), static_cast<uint32>(strtoul(value[4].c_str(), nullptr, 10)),
		static_cast<uint32>(strtoul(value[5].c_str(), nullptr, 10)));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_CATEGORY), need_equipment_build_category_name(atoi(value[6].c_str()), sd));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_STATUS), need_equipment_build_status_name(atoi(value[15].c_str()), sd));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_TITLE), value[7].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_DESCRIPTION), value[8].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_STATS), atoi(value[9].c_str()), atoi(value[10].c_str()), atoi(value[11].c_str()),
		atoi(value[12].c_str()), atoi(value[13].c_str()), atoi(value[14].c_str()));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_SNAPSHOT_HASH), value[16].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_CREATED_UPDATED), value[17].c_str(), value[18].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_APPROVED_ADMIN), value[19].c_str(), value[20].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_DELETED_ADMIN), value[21].c_str(), value[22].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_CANCELLED), value[28].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_REVIEW_REASON), value[23].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_DELETE_REASON), value[24].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_REWARD_STATE), atoi(value[25].c_str()) ? msg_txt(sd, NEED_BUILD_MSG_YES) : msg_txt(sd, NEED_BUILD_MSG_NO),
		atoi(value[26].c_str()) ? msg_txt(sd, NEED_BUILD_MSG_YES) : msg_txt(sd, NEED_BUILD_MSG_NO), value[27].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_ADMIN_LIKES), value[29].c_str());
	clif_displaymessage(sd->fd, output);
	if (atoi(value[25].c_str()) != 0) {
		const t_itemid reward_item_id = static_cast<t_itemid>(strtoul(value[30].c_str(), nullptr, 10));
		std::shared_ptr<item_data> reward_data = item_db.find(reward_item_id);
		safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_REWARD_ADMIN),
			reward_data != nullptr ? reward_data->name.c_str() : msg_txt(sd, NEED_BUILD_MSG_UNKNOWN_ITEM), reward_item_id, value[31].c_str(), value[32].c_str(),
			need_equipment_build_claim_status_name(atoi(value[33].c_str()), sd), value[34].c_str());
	} else {
		safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_REWARD_NONE));
	}
	clif_displaymessage(sd->fd, output);
	if (!need_build_display_items(sd, build_id, true))
		return NEED_BUILD_ERROR_DATABASE;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `admin_account_id`,`admin_char_id`,`action`,`reason`,`created_at` FROM `need_equipment_build_admin_log` WHERE `build_id`='%" PRIu64 "' ORDER BY `log_id`", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_ADMIN_LOG),
			need_build_column_string(mmysql_handle, 0).c_str(), need_build_column_string(mmysql_handle, 1).c_str(),
			need_build_column_string(mmysql_handle, 2).c_str(), need_build_column_string(mmysql_handle, 3).c_str(), need_build_column_string(mmysql_handle, 4).c_str());
		clif_displaymessage(sd->fd, output);
	}
	Sql_FreeResult(mmysql_handle);
	return 1;
}

int64 need_equipment_build_public_detail(map_session_data* sd, uint64 build_id, uint32* like_count, bool* liked, bool* is_owner)
{
	if (like_count != nullptr) *like_count = 0;
	if (liked != nullptr) *liked = false;
	if (is_owner != nullptr) *is_owner = false;
	if (sd == nullptr || build_id == 0 || mmysql_handle == nullptr)
		return NEED_BUILD_ERROR_INPUT;
	// Privacy boundary: owner identifiers are reduced to a boolean inside SQL and never returned to the caller.
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT b.`job_id`,b.`base_level`,b.`job_level`,b.`category`,b.`title`,b.`description`,b.`str`,b.`agi`,b.`vit`,b.`int`,b.`dex`,b.`luk`,b.`created_at`,b.`like_count`,"
		"(b.`account_id`='%u'),EXISTS(SELECT 1 FROM `need_equipment_build_like` l WHERE l.`build_id`=b.`build_id` AND l.`account_id`='%u') "
		"FROM `need_equipment_build` b WHERE b.`build_id`='%" PRIu64 "' AND b.`status`=1 LIMIT 1",
		sd->status.account_id, sd->status.account_id, build_id)) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		return NEED_BUILD_ERROR_NOT_FOUND;
	}
	std::array<std::string, 16> value;
	for (size_t index = 0; index < value.size(); ++index)
		value[index] = need_build_column_string(mmysql_handle, index);
	Sql_FreeResult(mmysql_handle);
	if (like_count != nullptr) *like_count = static_cast<uint32>(strtoul(value[13].c_str(), nullptr, 10));
	if (is_owner != nullptr) *is_owner = atoi(value[14].c_str()) != 0;
	if (liked != nullptr) *liked = atoi(value[15].c_str()) != 0;
	char output[CHAT_SIZE_MAX] = {};
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_DETAIL_HEADER), static_cast<unsigned long long>(build_id));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_JOB), need_build_safe_job_name(sd, atoi(value[0].c_str())).c_str(), atoi(value[0].c_str()));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_LEVEL), static_cast<uint32>(strtoul(value[1].c_str(), nullptr, 10)),
		static_cast<uint32>(strtoul(value[2].c_str(), nullptr, 10)));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_CATEGORY), need_equipment_build_category_name(atoi(value[3].c_str()), sd));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_TITLE), value[4].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_DESCRIPTION), value[5].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_CREATED), value[12].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_STATS), atoi(value[6].c_str()), atoi(value[7].c_str()), atoi(value[8].c_str()),
		atoi(value[9].c_str()), atoi(value[10].c_str()), atoi(value[11].c_str()));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_LIKES), value[13].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_ACCOUNT_LIKED), atoi(value[15].c_str()) ? msg_txt(sd, NEED_BUILD_MSG_YES) : msg_txt(sd, NEED_BUILD_MSG_NO));
	clif_displaymessage(sd->fd, output);
	if (!need_build_display_items(sd, build_id, false))
		return NEED_BUILD_ERROR_DATABASE;
	clif_displaymessage(sd->fd, msg_txt(sd, NEED_BUILD_MSG_SNAPSHOT_NOTICE_1));
	clif_displaymessage(sd->fd, msg_txt(sd, NEED_BUILD_MSG_SNAPSHOT_NOTICE_2));
	return 1;
}

int64 need_equipment_build_owner_detail(map_session_data* sd, uint64 build_id)
{
	if (sd == nullptr || build_id == 0 || mmysql_handle == nullptr)
		return NEED_BUILD_ERROR_INPUT;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `job_id`,`base_level`,`job_level`,`category`,`title`,`description`,`str`,`agi`,`vit`,`int`,`dex`,`luk`,`status`,"
		"`review_reason`,`delete_reason`,`reward_eligible`,`reward_claimed`,`reward_claimed_at`,`created_at`,`updated_at`,`cancelled_at`,`like_count`,"
		"`reward_item_id`,`reward_amount`,`reward_claim_status` "
		"FROM `need_equipment_build` WHERE `build_id`='%" PRIu64 "' AND `account_id`='%u' AND `char_id`='%u' LIMIT 1",
		build_id, sd->status.account_id, sd->status.char_id)) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		return NEED_BUILD_ERROR_NOT_OWNER;
	}
	std::array<std::string, 25> value;
	for (size_t index = 0; index < value.size(); ++index)
		value[index] = need_build_column_string(mmysql_handle, index);
	Sql_FreeResult(mmysql_handle);
	char output[CHAT_SIZE_MAX] = {};
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_OWNER_HEADER), static_cast<unsigned long long>(build_id));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_STATUS), need_equipment_build_status_name(atoi(value[12].c_str()), sd));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_JOB), need_build_safe_job_name(sd, atoi(value[0].c_str())).c_str(), atoi(value[0].c_str()));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_LEVEL), static_cast<uint32>(strtoul(value[1].c_str(), nullptr, 10)),
		static_cast<uint32>(strtoul(value[2].c_str(), nullptr, 10)));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_CATEGORY), need_equipment_build_category_name(atoi(value[3].c_str()), sd));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_TITLE), value[4].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_DESCRIPTION), value[5].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_STATS), atoi(value[6].c_str()), atoi(value[7].c_str()), atoi(value[8].c_str()),
		atoi(value[9].c_str()), atoi(value[10].c_str()), atoi(value[11].c_str()));
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_REVIEW_REASON), value[13].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_DELETE_REASON), value[14].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_REWARD_STATE), atoi(value[15].c_str()) ? msg_txt(sd, NEED_BUILD_MSG_YES) : msg_txt(sd, NEED_BUILD_MSG_NO),
		atoi(value[16].c_str()) ? msg_txt(sd, NEED_BUILD_MSG_YES) : msg_txt(sd, NEED_BUILD_MSG_NO), value[17].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_OWNER_TIMES), value[18].c_str(), value[19].c_str(), value[20].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_LIKES), value[21].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_CAN_CANCEL), atoi(value[12].c_str()) == 0 ? msg_txt(sd, NEED_BUILD_MSG_YES) : msg_txt(sd, NEED_BUILD_MSG_NO));
	clif_displaymessage(sd->fd, output);
	if (atoi(value[15].c_str()) != 0) {
		const t_itemid reward_item_id = static_cast<t_itemid>(strtoul(value[22].c_str(), nullptr, 10));
		std::shared_ptr<item_data> reward_data = item_db.find(reward_item_id);
		safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_REWARD_OWNER),
			reward_data != nullptr ? reward_data->name.c_str() : msg_txt(sd, NEED_BUILD_MSG_UNKNOWN_ITEM), reward_item_id, value[23].c_str(),
			need_equipment_build_claim_status_name(atoi(value[24].c_str()), sd));
	} else {
		safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_REWARD_NONE));
	}
	clif_displaymessage(sd->fd, output);
	return need_build_display_items(sd, build_id, false) ? 1 : NEED_BUILD_ERROR_DATABASE;
}

static int64 need_build_row_count()
{
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "SELECT ROW_COUNT()") || SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_FreeResult(mmysql_handle);
		return -1;
	}
	const int64 rows = static_cast<int64>(need_build_column_uint64(mmysql_handle, 0));
	Sql_FreeResult(mmysql_handle);
	return rows;
}

int64 need_equipment_build_approve(map_session_data* sd, uint64 build_id, uint32 reward_item_id, uint32 reward_amount)
{
	if (!need_equipment_build_is_admin(sd)) {
		need_build_state_log(sd, build_id, "APPROVE", -1, 1, NEED_BUILD_ERROR_UNAUTHORIZED, "permission");
		return NEED_BUILD_ERROR_UNAUTHORIZED;
	}
	const bool with_reward = reward_item_id != 0 || reward_amount != 0;
	if (build_id == 0 || (with_reward && (reward_item_id == 0 || reward_amount == 0 || reward_amount > NEED_BUILD_REWARD_AMOUNT_MAX || item_db.find(reward_item_id) == nullptr))) {
		need_build_state_log(sd, build_id, "APPROVE", -1, 1, NEED_BUILD_ERROR_INPUT, "reward_validation");
		return NEED_BUILD_ERROR_INPUT;
	}
	if (mmysql_handle == nullptr || SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
		if (mmysql_handle != nullptr) Sql_ShowDebug(mmysql_handle);
		need_build_state_log(sd, build_id, "APPROVE", -1, 1, NEED_BUILD_ERROR_DATABASE, "transaction_start");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle, "SELECT `status` FROM `need_equipment_build` WHERE `build_id`='%" PRIu64 "' FOR UPDATE", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, "APPROVE", -1, 1, NEED_BUILD_ERROR_DATABASE, "state_select");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_NOT_FOUND;
	}
	const int32 previous_status = need_build_column_int(mmysql_handle, 0);
	Sql_FreeResult(mmysql_handle);
	if (previous_status != 0) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, "APPROVE", previous_status, 1, NEED_BUILD_ERROR_STATE_CONFLICT, "state_check");
		return NEED_BUILD_ERROR_STATE_CONFLICT;
	}
	const int32 update_result = with_reward ? Sql_Query(mmysql_handle,
		"UPDATE `need_equipment_build` SET `status`=1,`approved_at`=NOW(),`approved_by`='%u',`reward_eligible`=1,"
		"`reward_item_id`='%u',`reward_amount`='%u',`reward_claimed`=0,`reward_claimed_at`=NULL,`reward_approved_by`='%u',"
		"`reward_claim_status`=0,`reward_claim_started_at`=NULL,`review_reason`=NULL,`updated_at`=NOW() WHERE `build_id`='%" PRIu64 "' AND `status`=0",
		sd->status.char_id, reward_item_id, reward_amount, sd->status.char_id, build_id) : Sql_Query(mmysql_handle,
		"UPDATE `need_equipment_build` SET `status`=1,`approved_at`=NOW(),`approved_by`='%u',`reward_eligible`=0,"
		"`reward_item_id`=0,`reward_amount`=0,`reward_claimed`=0,`reward_claimed_at`=NULL,`reward_approved_by`=NULL,"
		"`reward_claim_status`=0,`reward_claim_started_at`=NULL,`review_reason`=NULL,`updated_at`=NOW() WHERE `build_id`='%" PRIu64 "' AND `status`=0",
		sd->status.char_id, build_id);
	if (update_result == SQL_ERROR) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (Sql_NumRowsAffected(mmysql_handle) != 1) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_STATE_CONFLICT;
	}
	const char* action_name = with_reward ? "APPROVE_WITH_REWARD" : "APPROVE_WITHOUT_REWARD";
	char reason[100] = {};
	if (with_reward)
		snprintf(reason, sizeof(reason), "Reward item: %u, amount: %u", reward_item_id, reward_amount);
	else
		safesnprintf(reason, sizeof(reason), "Approved without reward");
	SqlStmt log_statement{ *mmysql_handle };
	if (SQL_SUCCESS != log_statement.PrepareStr("INSERT INTO `need_equipment_build_admin_log` (`build_id`,`admin_account_id`,`admin_char_id`,`action`,`reason`,`created_at`) VALUES (?,?,?,?,?,NOW())") ||
		SQL_SUCCESS != log_statement.BindParam(0, SQLDT_UINT64, &build_id, sizeof(build_id)) ||
		SQL_SUCCESS != log_statement.BindParam(1, SQLDT_UINT32, &sd->status.account_id, sizeof(sd->status.account_id)) ||
		SQL_SUCCESS != log_statement.BindParam(2, SQLDT_UINT32, &sd->status.char_id, sizeof(sd->status.char_id)) ||
		SQL_SUCCESS != log_statement.BindParam(3, SQLDT_STRING, const_cast<char*>(action_name), std::strlen(action_name)) ||
		SQL_SUCCESS != log_statement.BindParam(4, SQLDT_STRING, reason, std::strlen(reason)) || SQL_SUCCESS != log_statement.Execute()) {
		SqlStmt_ShowDebug(log_statement);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_COMMIT;
	}
	need_build_state_log(sd, build_id, action_name, previous_status, 1, 1, "complete");
	return 1;
}

int64 need_equipment_build_review(map_session_data* sd, uint64 build_id, int32 action, const char* reason)
{
	if (action == NEED_BUILD_REVIEW_APPROVE)
		return need_equipment_build_approve(sd, build_id, 0, 0);
	if (!need_equipment_build_is_admin(sd)) {
		need_build_state_log(sd, build_id, "UNAUTHORIZED", -1, -1, NEED_BUILD_ERROR_UNAUTHORIZED, "permission");
		return NEED_BUILD_ERROR_UNAUTHORIZED;
	}
	if (build_id == 0 || action < NEED_BUILD_REVIEW_REJECT || action > NEED_BUILD_REVIEW_DELETE) {
		need_build_state_log(sd, build_id, "INVALID", -1, -1, NEED_BUILD_ERROR_INPUT, "input_validation");
		return NEED_BUILD_ERROR_INPUT;
	}
	const int32 target_status = action == NEED_BUILD_REVIEW_REJECT ? 2 : 3;
	const char* action_name = action == NEED_BUILD_REVIEW_REJECT ? "REJECT" : "DELETE";
	if (!need_build_reason_valid(reason)) {
		need_build_state_log(sd, build_id, action_name, -1, target_status, NEED_BUILD_ERROR_REASON, "reason_validation");
		return NEED_BUILD_ERROR_REASON;
	}
	if (mmysql_handle == nullptr || SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
		if (mmysql_handle != nullptr) Sql_ShowDebug(mmysql_handle);
		need_build_state_log(sd, build_id, action_name, -1, target_status, NEED_BUILD_ERROR_DATABASE, "transaction_start");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle, "SELECT `status`,`reward_claim_status` FROM `need_equipment_build` WHERE `build_id`='%" PRIu64 "' FOR UPDATE", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, action_name, -1, target_status, NEED_BUILD_ERROR_DATABASE, "state_select");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, action_name, -1, target_status, NEED_BUILD_ERROR_NOT_FOUND, "not_found");
		return NEED_BUILD_ERROR_NOT_FOUND;
	}
	const int32 previous_status = need_build_column_int(mmysql_handle, 0);
	const int32 reward_claim_status = need_build_column_int(mmysql_handle, 1);
	Sql_FreeResult(mmysql_handle);
	if ((action != NEED_BUILD_REVIEW_DELETE && previous_status != 0) || (action == NEED_BUILD_REVIEW_DELETE && (previous_status == 3 || reward_claim_status == 1))) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, action_name, previous_status, target_status, NEED_BUILD_ERROR_STATE_CONFLICT, "state_check");
		return NEED_BUILD_ERROR_STATE_CONFLICT;
	}

	SqlStmt statement{ *mmysql_handle };
	const char* query = action == NEED_BUILD_REVIEW_REJECT
		? "UPDATE `need_equipment_build` SET `status`=2,`review_reason`=?,`updated_at`=NOW() WHERE `build_id`=? AND `status`=0"
		: "UPDATE `need_equipment_build` SET `status`=3,`deleted_at`=NOW(),`deleted_by`=?,`delete_reason`=?,`updated_at`=NOW() WHERE `build_id`=? AND `status`=? AND `status`<>3";
	uint64 bound_build_id = build_id;
	uint32 admin_char_id = sd->status.char_id;
	int32 bound_previous = previous_status;
	if (SQL_SUCCESS != statement.PrepareStr(query)) {
		SqlStmt_ShowDebug(statement);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, action_name, previous_status, target_status, NEED_BUILD_ERROR_DATABASE, "update_prepare");
		return NEED_BUILD_ERROR_DATABASE;
	}
	int32 update_result = SQL_ERROR;
	if (action == NEED_BUILD_REVIEW_REJECT) {
		update_result = statement.BindParam(0, SQLDT_STRING, const_cast<char*>(reason), std::strlen(reason)) == SQL_SUCCESS &&
			statement.BindParam(1, SQLDT_UINT64, &bound_build_id, sizeof(bound_build_id)) == SQL_SUCCESS && statement.Execute() == SQL_SUCCESS ? SQL_SUCCESS : SQL_ERROR;
	} else {
		update_result = statement.BindParam(0, SQLDT_UINT32, &admin_char_id, sizeof(admin_char_id)) == SQL_SUCCESS &&
			statement.BindParam(1, SQLDT_STRING, const_cast<char*>(reason), std::strlen(reason)) == SQL_SUCCESS &&
			statement.BindParam(2, SQLDT_UINT64, &bound_build_id, sizeof(bound_build_id)) == SQL_SUCCESS &&
			statement.BindParam(3, SQLDT_INT32, &bound_previous, sizeof(bound_previous)) == SQL_SUCCESS && statement.Execute() == SQL_SUCCESS ? SQL_SUCCESS : SQL_ERROR;
	}
	if (update_result == SQL_ERROR)
		SqlStmt_ShowDebug(statement);
	if (update_result == SQL_ERROR) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, action_name, previous_status, target_status, NEED_BUILD_ERROR_DATABASE, "update");
		return NEED_BUILD_ERROR_DATABASE;
	}
	const int64 affected = need_build_row_count();
	if (affected != 1) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, action_name, previous_status, target_status, NEED_BUILD_ERROR_STATE_CONFLICT, "conditional_update");
		return NEED_BUILD_ERROR_STATE_CONFLICT;
	}

	SqlStmt log_statement{ *mmysql_handle };
	char* log_reason = const_cast<char*>(reason);
	if (SQL_SUCCESS != log_statement.PrepareStr("INSERT INTO `need_equipment_build_admin_log` (`build_id`,`admin_account_id`,`admin_char_id`,`action`,`reason`,`created_at`) VALUES (?,?,?,?,?,NOW())") ||
		SQL_SUCCESS != log_statement.BindParam(0, SQLDT_UINT64, &build_id, sizeof(build_id)) ||
		SQL_SUCCESS != log_statement.BindParam(1, SQLDT_UINT32, &sd->status.account_id, sizeof(sd->status.account_id)) ||
		SQL_SUCCESS != log_statement.BindParam(2, SQLDT_UINT32, &sd->status.char_id, sizeof(sd->status.char_id)) ||
		SQL_SUCCESS != log_statement.BindParam(3, SQLDT_STRING, const_cast<char*>(action_name), std::strlen(action_name)) ||
		SQL_SUCCESS != log_statement.BindParam(4, SQLDT_STRING, log_reason, std::strlen(log_reason)) || SQL_SUCCESS != log_statement.Execute()) {
		SqlStmt_ShowDebug(log_statement);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, action_name, previous_status, target_status, NEED_BUILD_ERROR_DATABASE, "admin_log_insert");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, action_name, previous_status, target_status, NEED_BUILD_ERROR_COMMIT, "transaction_commit");
		return NEED_BUILD_ERROR_COMMIT;
	}
	need_build_state_log(sd, build_id, action_name, previous_status, target_status, 1, "complete");
	return 1;
}

int64 need_equipment_build_cancel(map_session_data* sd, uint64 build_id)
{
	if (sd == nullptr || build_id == 0)
		return NEED_BUILD_ERROR_NO_PLAYER;
	if (mmysql_handle == nullptr || SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
		if (mmysql_handle != nullptr) Sql_ShowDebug(mmysql_handle);
		need_build_state_log(sd, build_id, "CANCEL", -1, 4, NEED_BUILD_ERROR_DATABASE, "transaction_start");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `account_id`,`char_id`,`status` FROM `need_equipment_build` WHERE `build_id`='%" PRIu64 "' FOR UPDATE", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, "CANCEL", -1, 4, NEED_BUILD_ERROR_DATABASE, "owner_select");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, "CANCEL", -1, 4, NEED_BUILD_ERROR_NOT_FOUND, "not_found");
		return NEED_BUILD_ERROR_NOT_FOUND;
	}
	const uint32 owner_account = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 0));
	const uint32 owner_char = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 1));
	const int32 previous_status = need_build_column_int(mmysql_handle, 2);
	Sql_FreeResult(mmysql_handle);
	if (owner_account != sd->status.account_id || owner_char != sd->status.char_id) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, "CANCEL", previous_status, 4, NEED_BUILD_ERROR_NOT_OWNER, "ownership");
		return NEED_BUILD_ERROR_NOT_OWNER;
	}
	if (previous_status != 0) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, "CANCEL", previous_status, 4, NEED_BUILD_ERROR_STATE_CONFLICT, "state_check");
		return NEED_BUILD_ERROR_STATE_CONFLICT;
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `need_equipment_build` SET `status`=4,`cancelled_at`=NOW(),`updated_at`=NOW() WHERE `build_id`='%" PRIu64 "' AND `account_id`='%u' AND `char_id`='%u' AND `status`=0",
		build_id, sd->status.account_id, sd->status.char_id)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, "CANCEL", previous_status, 4, NEED_BUILD_ERROR_DATABASE, "update");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (Sql_NumRowsAffected(mmysql_handle) != 1) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, "CANCEL", previous_status, 4, NEED_BUILD_ERROR_STATE_CONFLICT, "conditional_update");
		return NEED_BUILD_ERROR_STATE_CONFLICT;
	}
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		need_build_state_log(sd, build_id, "CANCEL", previous_status, 4, NEED_BUILD_ERROR_COMMIT, "transaction_commit");
		return NEED_BUILD_ERROR_COMMIT;
	}
	need_build_state_log(sd, build_id, "CANCEL", previous_status, 4, 1, "complete");
	return 1;
}

static int64 need_build_like_fail(map_session_data* sd, uint64 build_id, int64 result, const char* stage, bool rollback)
{
	if (rollback && mmysql_handle != nullptr && SQL_ERROR == Sql_QueryStr(mmysql_handle, "ROLLBACK"))
		Sql_ShowDebug(mmysql_handle);
	ShowInfo("NEED equipment build like: account_id=%u char_id=%u build_id=%" PRIu64 " result=%" PRId64 " stage=%s\n",
		sd != nullptr ? sd->status.account_id : 0, sd != nullptr ? sd->status.char_id : 0, build_id, result, stage);
	return result;
}

int64 need_equipment_build_toggle_like(map_session_data* sd, uint64 build_id)
{
	if (sd == nullptr || sd->fd <= 0 || !session_isActive(sd->fd) || build_id == 0)
		return NEED_BUILD_LIKE_FAILED;
	if (!need_build_request_allowed(sd->need_equipment_build_like_tick, NEED_BUILD_LIKE_COOLDOWN_MS))
		return need_build_like_fail(sd, build_id, NEED_BUILD_LIKE_COOLDOWN, "cooldown", false);
	if (mmysql_handle == nullptr || SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
		if (mmysql_handle != nullptr) Sql_ShowDebug(mmysql_handle);
		return need_build_like_fail(sd, build_id, NEED_BUILD_LIKE_DATABASE, "transaction_start", false);
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `account_id`,`status` FROM `need_equipment_build` WHERE `build_id`='%" PRIu64 "' FOR UPDATE", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		return need_build_like_fail(sd, build_id, NEED_BUILD_LIKE_DATABASE, "build_lock", true);
	}
	if (SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		return need_build_like_fail(sd, build_id, NEED_BUILD_LIKE_NOT_FOUND, "not_found", true);
	}
	const uint32 owner_account_id = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 0));
	const int32 status = need_build_column_int(mmysql_handle, 1);
	Sql_FreeResult(mmysql_handle);
	if (status != 1)
		return need_build_like_fail(sd, build_id, NEED_BUILD_LIKE_NOT_PUBLIC, "status_check", true);
	if (owner_account_id == sd->status.account_id)
		return need_build_like_fail(sd, build_id, NEED_BUILD_LIKE_OWN_BUILD, "ownership_check", true);

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `like_id` FROM `need_equipment_build_like` WHERE `build_id`='%" PRIu64 "' AND `account_id`='%u' FOR UPDATE",
		build_id, sd->status.account_id)) {
		Sql_ShowDebug(mmysql_handle);
		return need_build_like_fail(sd, build_id, NEED_BUILD_LIKE_DATABASE, "like_lock", true);
	}
	const bool already_liked = SQL_SUCCESS == Sql_NextRow(mmysql_handle);
	Sql_FreeResult(mmysql_handle);
	int64 result = NEED_BUILD_LIKE_FAILED;
	if (already_liked) {
		if (SQL_ERROR == Sql_Query(mmysql_handle,
			"DELETE FROM `need_equipment_build_like` WHERE `build_id`='%" PRIu64 "' AND `account_id`='%u'",
			build_id, sd->status.account_id) || Sql_NumRowsAffected(mmysql_handle) != 1) {
			Sql_ShowDebug(mmysql_handle);
			return need_build_like_fail(sd, build_id, NEED_BUILD_LIKE_DATABASE, "like_delete", true);
		}
		if (SQL_ERROR == Sql_Query(mmysql_handle,
			"UPDATE `need_equipment_build` SET `like_count`=GREATEST(`like_count`-1,0) WHERE `build_id`='%" PRIu64 "'", build_id)) {
			Sql_ShowDebug(mmysql_handle);
			return need_build_like_fail(sd, build_id, NEED_BUILD_LIKE_DATABASE, "count_decrement", true);
		}
		result = NEED_BUILD_LIKE_REMOVED;
	} else {
		if (SQL_ERROR == Sql_Query(mmysql_handle,
			"INSERT INTO `need_equipment_build_like` (`build_id`,`account_id`,`char_id`,`created_at`) VALUES ('%" PRIu64 "','%u','%u',NOW())",
			build_id, sd->status.account_id, sd->status.char_id)) {
			Sql_ShowDebug(mmysql_handle);
			return need_build_like_fail(sd, build_id, NEED_BUILD_LIKE_DATABASE, "like_insert", true);
		}
		if (SQL_ERROR == Sql_Query(mmysql_handle,
			"UPDATE `need_equipment_build` SET `like_count`=`like_count`+1 WHERE `build_id`='%" PRIu64 "'", build_id)) {
			Sql_ShowDebug(mmysql_handle);
			return need_build_like_fail(sd, build_id, NEED_BUILD_LIKE_DATABASE, "count_increment", true);
		}
		result = NEED_BUILD_LIKE_ADDED;
	}
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		Sql_ShowDebug(mmysql_handle);
		return need_build_like_fail(sd, build_id, NEED_BUILD_LIKE_DATABASE, "transaction_commit", true);
	}
	return need_build_like_fail(sd, build_id, result, "complete", false);
}

int64 need_equipment_build_sync_likes(map_session_data* sd, uint64 build_id, uint32& like_count)
{
	like_count = 0;
	if (!need_equipment_build_is_admin(sd))
		return NEED_BUILD_ERROR_UNAUTHORIZED;
	if (build_id == 0 || mmysql_handle == nullptr)
		return NEED_BUILD_ERROR_INPUT;
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `build_id` FROM `need_equipment_build` WHERE `build_id`='%" PRIu64 "' FOR UPDATE", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_NOT_FOUND;
	}
	Sql_FreeResult(mmysql_handle);
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `need_equipment_build` SET `like_count`=(SELECT COUNT(*) FROM `need_equipment_build_like` WHERE `build_id`='%" PRIu64 "') WHERE `build_id`='%" PRIu64 "'",
		build_id, build_id)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle, "SELECT `like_count` FROM `need_equipment_build` WHERE `build_id`='%" PRIu64 "'", build_id) ||
		SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_FreeResult(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_DATABASE;
	}
	like_count = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 0));
	Sql_FreeResult(mmysql_handle);
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_COMMIT;
	}
	ShowInfo("NEED equipment build like sync: admin_account_id=%u admin_char_id=%u build_id=%" PRIu64 " like_count=%u\n",
		sd->status.account_id, sd->status.char_id, build_id, like_count);
	return 1;
}

int64 need_equipment_build_reward_list(map_session_data* sd, int32 page, need_equipment_build_reward_page& result)
{
	result = {};
	if (sd == nullptr || page < 0)
		return NEED_BUILD_REWARD_NO_PLAYER;
	if (mmysql_handle == nullptr)
		return NEED_BUILD_REWARD_DATABASE;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `build_id`,`title`,`reward_item_id`,`reward_amount` FROM `need_equipment_build` "
		"WHERE `account_id`='%u' AND `char_id`='%u' AND `status`=1 AND `reward_eligible`=1 AND `reward_claimed`=0 "
		"AND `reward_claim_status`=0 AND `reward_item_id`>0 AND `reward_amount`>0 ORDER BY `build_id` DESC LIMIT %d OFFSET %d",
		sd->status.account_id, sd->status.char_id, NEED_BUILD_PAGE_SIZE + 1, page * NEED_BUILD_PAGE_SIZE)) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_REWARD_DATABASE;
	}
	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		if (result.entries.size() == NEED_BUILD_PAGE_SIZE) {
			result.has_more = true;
			break;
		}
		need_equipment_build_reward_entry entry;
		entry.build_id = need_build_column_uint64(mmysql_handle, 0);
		entry.title = need_build_column_string(mmysql_handle, 1);
		entry.reward_item_id = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 2));
		entry.reward_amount = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 3));
		result.entries.push_back(std::move(entry));
	}
	Sql_FreeResult(mmysql_handle);
	return static_cast<int64>(result.entries.size());
}

int64 need_equipment_build_reward_detail(map_session_data* sd, uint64 build_id, need_equipment_build_reward_entry& result)
{
	result = {};
	if (sd == nullptr || build_id == 0)
		return NEED_BUILD_REWARD_NO_PLAYER;
	if (mmysql_handle == nullptr)
		return NEED_BUILD_REWARD_DATABASE;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `title`,`reward_item_id`,`reward_amount` FROM `need_equipment_build` WHERE `build_id`='%" PRIu64 "' "
		"AND `account_id`='%u' AND `char_id`='%u' AND `status`=1 AND `reward_eligible`=1 AND `reward_claimed`=0 "
		"AND `reward_claim_status`=0 AND `reward_item_id`>0 AND `reward_amount`>0 LIMIT 1",
		build_id, sd->status.account_id, sd->status.char_id)) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_REWARD_DATABASE;
	}
	if (SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		return NEED_BUILD_REWARD_NOT_ELIGIBLE;
	}
	result.build_id = build_id;
	result.title = need_build_column_string(mmysql_handle, 0);
	result.reward_item_id = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 1));
	result.reward_amount = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 2));
	Sql_FreeResult(mmysql_handle);
	return 1;
}

static int64 need_build_reward_log_result(map_session_data* sd, uint64 build_id, uint32 item_id, uint32 amount, int64 result, const char* stage)
{
	ShowInfo("NEED equipment build reward: account_id=%u char_id=%u char_name='%s' build_id=%" PRIu64 " item_id=%u amount=%u result=%" PRId64 " stage=%s\n",
		sd != nullptr ? sd->status.account_id : 0, sd != nullptr ? sd->status.char_id : 0, sd != nullptr ? sd->status.name : "",
		build_id, item_id, amount, result, stage);
	return result;
}

static bool need_build_reward_release_reservation(map_session_data* sd, uint64 build_id)
{
	if (mmysql_handle == nullptr || SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `need_equipment_build` SET `reward_claim_status`=0,`reward_claim_started_at`=NULL "
		"WHERE `build_id`='%" PRIu64 "' AND `account_id`='%u' AND `char_id`='%u' AND `reward_claimed`=0 AND `reward_claim_status`=1",
		build_id, sd->status.account_id, sd->status.char_id)) {
		if (mmysql_handle != nullptr) Sql_ShowDebug(mmysql_handle);
		return false;
	}
	return Sql_NumRowsAffected(mmysql_handle) == 1;
}

int64 need_equipment_build_claim_reward(map_session_data* sd, uint64 build_id)
{
	if (sd == nullptr || sd->fd <= 0 || !session_isActive(sd->fd) || build_id == 0)
		return NEED_BUILD_REWARD_NO_PLAYER;
	if (!need_build_request_allowed(sd->need_equipment_build_reward_tick, NEED_BUILD_REWARD_COOLDOWN_MS))
		return need_build_reward_log_result(sd, build_id, 0, 0, NEED_BUILD_REWARD_COOLDOWN, "cooldown");
	if (mmysql_handle == nullptr || SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
		if (mmysql_handle != nullptr) Sql_ShowDebug(mmysql_handle);
		return need_build_reward_log_result(sd, build_id, 0, 0, NEED_BUILD_REWARD_DATABASE, "reservation_start");
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `account_id`,`char_id`,`status`,`reward_eligible`,`reward_item_id`,`reward_amount`,`reward_claimed`,`reward_claim_status` "
		"FROM `need_equipment_build` WHERE `build_id`='%" PRIu64 "' FOR UPDATE", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, 0, 0, NEED_BUILD_REWARD_DATABASE, "reservation_lock");
	}
	if (SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_REWARD_NOT_OWNER;
	}
	const uint32 owner_account_id = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 0));
	const uint32 owner_char_id = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 1));
	const int32 status = need_build_column_int(mmysql_handle, 2);
	const bool reward_eligible = need_build_column_int(mmysql_handle, 3) != 0;
	const uint32 reward_item_id = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 4));
	const uint32 reward_amount = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 5));
	const bool reward_claimed = need_build_column_int(mmysql_handle, 6) != 0;
	const int32 claim_status = need_build_column_int(mmysql_handle, 7);
	Sql_FreeResult(mmysql_handle);
	if (owner_account_id != sd->status.account_id || owner_char_id != sd->status.char_id) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_NOT_OWNER, "ownership");
	}
	if (status != 1 || !reward_eligible || reward_amount == 0) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_NOT_ELIGIBLE, "eligibility");
	}
	if (reward_claimed || claim_status != 0) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_ALREADY_PROCESSED, "claim_status");
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle, "SELECT `reward_log_id` FROM `need_equipment_build_reward_log` WHERE `build_id`='%" PRIu64 "' LIMIT 1", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_DATABASE, "log_check");
	}
	if (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_ALREADY_PROCESSED, "log_duplicate");
	}
	Sql_FreeResult(mmysql_handle);
	std::shared_ptr<item_data> reward_data = item_db.find(reward_item_id);
	if (reward_data == nullptr || reward_amount > NEED_BUILD_REWARD_AMOUNT_MAX || reward_amount > MAX_AMOUNT) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_INVALID_ITEM, "item_validation");
	}
	const uint64 reward_weight = static_cast<uint64>(reward_data->weight) * reward_amount;
	bool inventory_ok = static_cast<uint64>(sd->weight) + reward_weight <= sd->max_weight;
	if (inventory_ok && itemdb_isstackable2(reward_data.get())) {
		const char check = pc_checkadditem(sd, reward_item_id, static_cast<int32>(reward_amount));
		inventory_ok = check != CHKADDITEM_OVERAMOUNT && (check != CHKADDITEM_NEW || pc_inventoryblank(sd) >= 1);
	} else if (inventory_ok) {
		inventory_ok = pc_inventoryblank(sd) >= reward_amount;
	}
	if (!inventory_ok) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_INVENTORY, "inventory_check");
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `need_equipment_build` SET `reward_claim_status`=1,`reward_claim_started_at`=NOW() "
		"WHERE `build_id`='%" PRIu64 "' AND `account_id`='%u' AND `char_id`='%u' AND `status`=1 AND `reward_eligible`=1 "
		"AND `reward_claimed`=0 AND `reward_claim_status`=0",
		build_id, sd->status.account_id, sd->status.char_id) || Sql_NumRowsAffected(mmysql_handle) != 1) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_DATABASE, "reservation_update");
	}
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_DATABASE, "reservation_commit");
	}

	if (!session_isActive(sd->fd)) {
		const bool released = need_build_reward_release_reservation(sd, build_id);
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_NO_PLAYER,
			released ? "session_lost_released" : "session_lost_release_failed");
	}
	item reward_item{};
	reward_item.nameid = reward_item_id;
	reward_item.identify = 1;
	uint32 delivered = 0;
	e_additem_result add_result = ADDITEM_SUCCESS;
	if (itemdb_isstackable2(reward_data.get())) {
		add_result = pc_additem(sd, &reward_item, static_cast<int32>(reward_amount), LOG_TYPE_SCRIPT);
		if (add_result == ADDITEM_SUCCESS) delivered = reward_amount;
	} else {
		for (; delivered < reward_amount; ++delivered) {
			item single_item = reward_item;
			add_result = pc_additem(sd, &single_item, 1, LOG_TYPE_SCRIPT);
			if (add_result != ADDITEM_SUCCESS) break;
		}
	}
	if (add_result != ADDITEM_SUCCESS) {
		const bool released = delivered == 0 && need_build_reward_release_reservation(sd, build_id);
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount,
			delivered == 0 ? NEED_BUILD_REWARD_INVENTORY : NEED_BUILD_REWARD_FINALIZE,
			released ? "item_add_failed_released" : delivered == 0 ? "item_add_failed_release_failed" : "partial_item_delivery_processing_locked");
	}

	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
		Sql_ShowDebug(mmysql_handle);
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_FINALIZE, "finalize_start_after_delivery");
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `reward_claimed`,`reward_claim_status` FROM `need_equipment_build` WHERE `build_id`='%" PRIu64 "' FOR UPDATE", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_FINALIZE, "finalize_lock_after_delivery");
	}
	if (SQL_SUCCESS != Sql_NextRow(mmysql_handle) || need_build_column_int(mmysql_handle, 0) != 0 || need_build_column_int(mmysql_handle, 1) != 1) {
		Sql_FreeResult(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_FINALIZE, "finalize_state_after_delivery");
	}
	Sql_FreeResult(mmysql_handle);
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT INTO `need_equipment_build_reward_log` (`build_id`,`account_id`,`char_id`,`reward_item_id`,`reward_amount`,`claimed_at`) "
		"VALUES ('%" PRIu64 "','%u','%u','%u','%u',NOW())",
		build_id, sd->status.account_id, sd->status.char_id, reward_item_id, reward_amount)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_FINALIZE, "log_insert_after_delivery");
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `need_equipment_build` SET `reward_claimed`=1,`reward_claimed_at`=NOW(),`reward_claim_status`=2 "
		"WHERE `build_id`='%" PRIu64 "' AND `reward_claimed`=0 AND `reward_claim_status`=1",
		build_id) || Sql_NumRowsAffected(mmysql_handle) != 1) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_FINALIZE, "claim_update_after_delivery");
	}
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_FINALIZE, "finalize_commit_after_delivery");
	}
	return need_build_reward_log_result(sd, build_id, reward_item_id, reward_amount, NEED_BUILD_REWARD_SUCCESS, "complete");
}

int64 need_equipment_build_reward_check(map_session_data* sd, uint64 build_id)
{
	if (!need_equipment_build_is_admin(sd))
		return NEED_BUILD_ERROR_UNAUTHORIZED;
	if (build_id == 0 || mmysql_handle == nullptr)
		return NEED_BUILD_ERROR_INPUT;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `status`,`account_id`,`char_id`,`reward_eligible`,`reward_item_id`,`reward_amount`,`reward_claimed`,`reward_claimed_at`,"
		"`reward_approved_by`,`reward_claim_status`,`reward_claim_started_at` FROM `need_equipment_build` WHERE `build_id`='%" PRIu64 "' LIMIT 1", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		return NEED_BUILD_ERROR_NOT_FOUND;
	}
	std::array<std::string, 11> value;
	for (size_t index = 0; index < value.size(); ++index) value[index] = need_build_column_string(mmysql_handle, index);
	Sql_FreeResult(mmysql_handle);
	char output[CHAT_SIZE_MAX] = {};
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_REWARD_CHECK_1), static_cast<unsigned long long>(build_id),
		need_equipment_build_status_name(atoi(value[0].c_str()), sd), value[1].c_str(), value[2].c_str(),
		atoi(value[3].c_str()) ? msg_txt(sd, NEED_BUILD_MSG_YES) : msg_txt(sd, NEED_BUILD_MSG_NO), value[4].c_str(), value[5].c_str());
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_REWARD_CHECK_2),
		atoi(value[6].c_str()) ? msg_txt(sd, NEED_BUILD_MSG_YES) : msg_txt(sd, NEED_BUILD_MSG_NO), value[7].c_str(), value[8].c_str(),
		need_equipment_build_claim_status_name(atoi(value[9].c_str()), sd), value[10].c_str());
	clif_displaymessage(sd->fd, output);
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `account_id`,`char_id`,`reward_item_id`,`reward_amount`,`claimed_at` FROM `need_equipment_build_reward_log` WHERE `build_id`='%" PRIu64 "' LIMIT 1", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_REWARD_LOG),
			need_build_column_string(mmysql_handle, 0).c_str(), need_build_column_string(mmysql_handle, 1).c_str(),
			need_build_column_string(mmysql_handle, 2).c_str(), need_build_column_string(mmysql_handle, 3).c_str(), need_build_column_string(mmysql_handle, 4).c_str());
		clif_displaymessage(sd->fd, output);
	} else {
		clif_displaymessage(sd->fd, msg_txt(sd, NEED_BUILD_MSG_REWARD_LOG_NONE));
	}
	Sql_FreeResult(mmysql_handle);
	return 1;
}

int64 need_equipment_build_reward_processing(map_session_data* sd)
{
	if (!need_equipment_build_is_admin(sd))
		return NEED_BUILD_ERROR_UNAUTHORIZED;
	if (mmysql_handle == nullptr)
		return NEED_BUILD_ERROR_DATABASE;
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle,
		"SELECT b.`build_id`,b.`account_id`,b.`char_id`,b.`char_name`,b.`reward_item_id`,b.`reward_amount`,b.`reward_claim_started_at`,"
		"EXISTS(SELECT 1 FROM `need_equipment_build_reward_log` l WHERE l.`build_id`=b.`build_id`) "
		"FROM `need_equipment_build` b WHERE b.`reward_claim_status`=1 ORDER BY b.`reward_claim_started_at` ASC,b.`build_id` ASC LIMIT 30")) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	int32 count = 0;
	char output[CHAT_SIZE_MAX] = {};
	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_REWARD_PROCESSING),
			need_build_column_string(mmysql_handle, 0).c_str(), need_build_column_string(mmysql_handle, 1).c_str(),
			need_build_column_string(mmysql_handle, 2).c_str(), need_build_column_string(mmysql_handle, 3).c_str(),
			need_build_column_string(mmysql_handle, 4).c_str(), need_build_column_string(mmysql_handle, 5).c_str(),
			need_build_column_string(mmysql_handle, 6).c_str(), need_build_column_int(mmysql_handle, 7) ? msg_txt(sd, NEED_BUILD_MSG_YES) : msg_txt(sd, NEED_BUILD_MSG_NO));
		clif_displaymessage(sd->fd, output);
		++count;
	}
	Sql_FreeResult(mmysql_handle);
	if (count == 0)
		clif_displaymessage(sd->fd, msg_txt(sd, NEED_BUILD_MSG_REWARD_PROCESSING_NONE));
	return count;
}

int64 need_equipment_build_reward_recover(map_session_data* sd, uint64 build_id, int32 action)
{
	if (!need_equipment_build_is_admin(sd))
		return NEED_BUILD_ERROR_UNAUTHORIZED;
	if (build_id == 0 || (action != NEED_BUILD_REWARD_RECOVERY_COMPLETE && action != NEED_BUILD_REWARD_RECOVERY_RETRY))
		return NEED_BUILD_ERROR_INPUT;
	if (mmysql_handle == nullptr || SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
		if (mmysql_handle != nullptr) Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `account_id`,`char_id`,`reward_eligible`,`reward_item_id`,`reward_amount`,`reward_claimed`,`reward_claim_status` "
		"FROM `need_equipment_build` WHERE `build_id`='%" PRIu64 "' FOR UPDATE", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_NOT_FOUND;
	}
	const uint32 owner_account_id = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 0));
	const uint32 owner_char_id = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 1));
	const bool eligible = need_build_column_int(mmysql_handle, 2) != 0;
	const uint32 item_id = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 3));
	const uint32 amount = static_cast<uint32>(need_build_column_uint64(mmysql_handle, 4));
	const bool claimed = need_build_column_int(mmysql_handle, 5) != 0;
	const int32 previous_status = need_build_column_int(mmysql_handle, 6);
	Sql_FreeResult(mmysql_handle);

	if (previous_status != 1 || (action == NEED_BUILD_REWARD_RECOVERY_COMPLETE && (!eligible || item_id == 0 || amount == 0))) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_STATE_CONFLICT;
	}
	if (action == NEED_BUILD_REWARD_RECOVERY_RETRY && claimed) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_STATE_CONFLICT;
	}

	bool log_exists = false;
	bool log_matches = false;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `account_id`,`char_id`,`reward_item_id`,`reward_amount` FROM `need_equipment_build_reward_log` WHERE `build_id`='%" PRIu64 "' FOR UPDATE", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		log_exists = true;
		log_matches = need_build_column_uint64(mmysql_handle, 0) == owner_account_id && need_build_column_uint64(mmysql_handle, 1) == owner_char_id &&
			need_build_column_uint64(mmysql_handle, 2) == item_id && need_build_column_uint64(mmysql_handle, 3) == amount;
	}
	Sql_FreeResult(mmysql_handle);
	if ((action == NEED_BUILD_REWARD_RECOVERY_RETRY && log_exists) || (action == NEED_BUILD_REWARD_RECOVERY_COMPLETE && log_exists && !log_matches)) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_STATE_CONFLICT;
	}

	const char* action_name = action == NEED_BUILD_REWARD_RECOVERY_COMPLETE ? "REWARD_FORCE_COMPLETE" : "REWARD_RESET_RETRY";
	if (action == NEED_BUILD_REWARD_RECOVERY_COMPLETE && !log_exists && SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT INTO `need_equipment_build_reward_log` (`build_id`,`account_id`,`char_id`,`reward_item_id`,`reward_amount`,`claimed_at`) "
		"VALUES ('%" PRIu64 "','%u','%u','%u','%u',NOW())", build_id, owner_account_id, owner_char_id, item_id, amount)) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_DATABASE;
	}
	const int32 update_result = action == NEED_BUILD_REWARD_RECOVERY_COMPLETE ? Sql_Query(mmysql_handle,
		"UPDATE `need_equipment_build` SET `reward_claimed`=1,`reward_claimed_at`=NOW(),`reward_claim_status`=2 "
		"WHERE `build_id`='%" PRIu64 "' AND `reward_claim_status`=1 AND `reward_eligible`=1 AND `reward_item_id`>0 AND `reward_amount`>0", build_id) : Sql_Query(mmysql_handle,
		"UPDATE `need_equipment_build` SET `reward_claim_status`=0,`reward_claim_started_at`=NULL "
		"WHERE `build_id`='%" PRIu64 "' AND `reward_claim_status`=1 AND `reward_claimed`=0", build_id);
	if (update_result == SQL_ERROR || Sql_NumRowsAffected(mmysql_handle) != 1) {
		if (update_result == SQL_ERROR) Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return update_result == SQL_ERROR ? NEED_BUILD_ERROR_DATABASE : NEED_BUILD_ERROR_STATE_CONFLICT;
	}
	char reason[160] = {};
	snprintf(reason, sizeof(reason), "claim_status: %d -> %d, reward item: %u, amount: %u", previous_status,
		action == NEED_BUILD_REWARD_RECOVERY_COMPLETE ? 2 : 0, item_id, amount);
	if (!need_build_insert_admin_log(sd, build_id, action_name, reason)) {
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		Sql_ShowDebug(mmysql_handle);
		Sql_QueryStr(mmysql_handle, "ROLLBACK");
		return NEED_BUILD_ERROR_COMMIT;
	}
	need_build_state_log(sd, build_id, action_name, previous_status,
		action == NEED_BUILD_REWARD_RECOVERY_COMPLETE ? 2 : 0, 1, "complete");
	ShowInfo("NEED equipment build reward recovery: admin_account_id=%u admin_char_id=%u build_id=%" PRIu64
		" action=%s previous_claim_status=%d target_claim_status=%d reward_item_id=%u reward_amount=%u sql_result=committed\n",
		sd->status.account_id, sd->status.char_id, build_id, action_name, previous_status,
		action == NEED_BUILD_REWARD_RECOVERY_COMPLETE ? 2 : 0, item_id, amount);
	return 1;
}

int64 need_equipment_build_reward_audit(map_session_data* sd, uint64 build_id)
{
	if (!need_equipment_build_is_admin(sd))
		return NEED_BUILD_ERROR_UNAUTHORIZED;
	if (build_id == 0 || mmysql_handle == nullptr)
		return NEED_BUILD_ERROR_INPUT;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `reward_eligible`,`reward_item_id`,`reward_amount`,`reward_claimed`,`reward_claimed_at`,"
		"`reward_claim_status`,`reward_claim_started_at`,`account_id`,`char_id` FROM `need_equipment_build` "
		"WHERE `build_id`='%" PRIu64 "' LIMIT 1", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	if (SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		return NEED_BUILD_ERROR_NOT_FOUND;
	}
	const bool eligible = need_build_column_int(mmysql_handle, 0) != 0;
	const uint64 item_id = need_build_column_uint64(mmysql_handle, 1);
	const uint64 amount = need_build_column_uint64(mmysql_handle, 2);
	const bool claimed = need_build_column_int(mmysql_handle, 3) != 0;
	const bool claimed_at = !need_build_column_string(mmysql_handle, 4).empty();
	const int32 claim_status = need_build_column_int(mmysql_handle, 5);
	const bool started_at = !need_build_column_string(mmysql_handle, 6).empty();
	const uint64 owner_account_id = need_build_column_uint64(mmysql_handle, 7);
	const uint64 owner_char_id = need_build_column_uint64(mmysql_handle, 8);
	Sql_FreeResult(mmysql_handle);
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `account_id`,`char_id`,`reward_item_id`,`reward_amount` FROM `need_equipment_build_reward_log` WHERE `build_id`='%" PRIu64 "'", build_id)) {
		Sql_ShowDebug(mmysql_handle);
		return NEED_BUILD_ERROR_DATABASE;
	}
	uint64 log_count = 0;
	bool log_matches = false;
	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		++log_count;
		if (log_count == 1)
			log_matches = need_build_column_uint64(mmysql_handle, 0) == owner_account_id && need_build_column_uint64(mmysql_handle, 1) == owner_char_id &&
				need_build_column_uint64(mmysql_handle, 2) == item_id && need_build_column_uint64(mmysql_handle, 3) == amount;
	}
	Sql_FreeResult(mmysql_handle);
	if (log_count != 1)
		log_matches = false;

	const bool no_reward = !eligible && item_id == 0 && amount == 0 && !claimed && !claimed_at && claim_status == 0 && !started_at && log_count == 0;
	const bool unclaimed = eligible && item_id > 0 && amount > 0 && !claimed && !claimed_at && claim_status == 0 && log_count == 0 && !started_at;
	const bool processing = eligible && item_id > 0 && amount > 0 && !claimed && !claimed_at && claim_status == 1 && started_at && log_count == 0;
	const bool complete = eligible && item_id > 0 && amount > 0 && claimed && claim_status == 2 && claimed_at && log_matches;
	const char* classification = no_reward ? msg_txt(sd, NEED_BUILD_MSG_AUDIT_NO_REWARD) : unclaimed ? msg_txt(sd, NEED_BUILD_MSG_AUDIT_UNCLAIMED) :
		processing ? msg_txt(sd, NEED_BUILD_MSG_AUDIT_PROCESSING) : complete ? msg_txt(sd, NEED_BUILD_MSG_AUDIT_COMPLETE) : msg_txt(sd, NEED_BUILD_MSG_AUDIT_INCONSISTENT);
	char output[CHAT_SIZE_MAX] = {};
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_AUDIT_HEADER), static_cast<unsigned long long>(build_id), classification);
	clif_displaymessage(sd->fd, output);
	safesnprintf(output, sizeof(output), msg_txt(sd, NEED_BUILD_MSG_AUDIT_VALUES), eligible, static_cast<unsigned long long>(item_id),
		static_cast<unsigned long long>(amount), claimed, claimed_at, need_equipment_build_claim_status_name(claim_status, sd), started_at,
		static_cast<unsigned long long>(log_count), log_matches);
	clif_displaymessage(sd->fd, output);
	if (!(no_reward || unclaimed || processing || complete)) {
		if (eligible && amount == 0) clif_displaymessage(sd->fd, msg_txt(sd, NEED_BUILD_MSG_AUDIT_ISSUE_AMOUNT));
		if (item_id == 0 && (eligible || amount > 0)) clif_displaymessage(sd->fd, msg_txt(sd, NEED_BUILD_MSG_AUDIT_ISSUE_ITEM));
		if (claimed && log_count == 0) clif_displaymessage(sd->fd, msg_txt(sd, NEED_BUILD_MSG_AUDIT_ISSUE_NO_LOG));
		if (log_count > 0 && !claimed) clif_displaymessage(sd->fd, msg_txt(sd, NEED_BUILD_MSG_AUDIT_ISSUE_NOT_CLAIMED));
		if (claim_status == 2 && !claimed_at) clif_displaymessage(sd->fd, msg_txt(sd, NEED_BUILD_MSG_AUDIT_ISSUE_NO_CLAIMED_AT));
		if (claim_status == 1 && !started_at) clif_displaymessage(sd->fd, msg_txt(sd, NEED_BUILD_MSG_AUDIT_ISSUE_NO_STARTED_AT));
		if (log_count > 1) clif_displaymessage(sd->fd, msg_txt(sd, NEED_BUILD_MSG_AUDIT_ISSUE_LOG_COUNT));
		if (log_count == 1 && !log_matches) clif_displaymessage(sd->fd, msg_txt(sd, NEED_BUILD_MSG_AUDIT_ISSUE_LOG_MISMATCH));
	}
	ShowInfo("NEED equipment build reward audit: admin_account_id=%u admin_char_id=%u build_id=%" PRIu64
		" classification=%s claim_status=%d reward_item_id=%" PRIu64 " reward_amount=%" PRIu64 " log_count=%" PRIu64 "\n",
		sd->status.account_id, sd->status.char_id, build_id, classification, claim_status, item_id, amount, log_count);
	return no_reward || unclaimed || processing || complete ? 1 : 0;
}
