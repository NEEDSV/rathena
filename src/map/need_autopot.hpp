// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL

#ifndef NEED_AUTOPOT_HPP
#define NEED_AUTOPOT_HPP

#include <common/cbasetypes.hpp>

class map_session_data;

constexpr uint16 NEED_AUTOPOT_MIN_INTERVAL = 300;
constexpr uint16 NEED_AUTOPOT_DEFAULT_INTERVAL = 500;
constexpr uint16 NEED_AUTOPOT_MAX_INTERVAL = 2000;
constexpr uint8 NEED_AUTOPOT_MAX_PRESETS = 5;

enum e_need_autopot_message : int32 {
	NEED_AUTOPOT_MSG_HELP_HP = 1783,
	NEED_AUTOPOT_MSG_HELP_SP,
	NEED_AUTOPOT_MSG_HELP_CONTROL,
	NEED_AUTOPOT_MSG_HELP_PRESET,
	NEED_AUTOPOT_MSG_HELP_KOREAN,
	NEED_AUTOPOT_MSG_INFO_TITLE,
	NEED_AUTOPOT_MSG_INFO_HP,
	NEED_AUTOPOT_MSG_INFO_SP,
	NEED_AUTOPOT_MSG_INFO_INTERVAL_MAP,
	NEED_AUTOPOT_MSG_INFO_PRESET,
	NEED_AUTOPOT_MSG_INFO_PRESET_NONE,
	NEED_AUTOPOT_MSG_STATE_ON,
	NEED_AUTOPOT_MSG_STATE_OFF,
	NEED_AUTOPOT_MSG_MAP_ALLOWED,
	NEED_AUTOPOT_MSG_MAP_BLOCKED,
	NEED_AUTOPOT_MSG_UNKNOWN_ITEM,
	NEED_AUTOPOT_MSG_ITEM_LIST_TITLE,
	NEED_AUTOPOT_MSG_ITEM_LIST_ROW,
	NEED_AUTOPOT_MSG_ITEM_LIST_EMPTY,
	NEED_AUTOPOT_MSG_ITEM_LIST_LIMIT,
	NEED_AUTOPOT_MSG_MAP_RESTRICTED,
	NEED_AUTOPOT_MSG_DISABLED,
	NEED_AUTOPOT_MSG_INVALID_ITEM_ID,
	NEED_AUTOPOT_MSG_ITEM_NOT_FOUND,
	NEED_AUTOPOT_MSG_ITEM_NOT_HEALING,
	NEED_AUTOPOT_MSG_ITEM_LEVEL,
	NEED_AUTOPOT_MSG_ITEM_NOT_OWNED,
	NEED_AUTOPOT_MSG_HP_ITEM_INVALID,
	NEED_AUTOPOT_MSG_SP_ITEM_INVALID,
	NEED_AUTOPOT_MSG_HP_OUT_OF_STOCK,
	NEED_AUTOPOT_MSG_SP_OUT_OF_STOCK,
	NEED_AUTOPOT_MSG_PRESET_MAP_BLOCKED,
	NEED_AUTOPOT_MSG_PRESET_TITLE,
	NEED_AUTOPOT_MSG_PRESET_EMPTY,
	NEED_AUTOPOT_MSG_PRESET_SLOT,
	NEED_AUTOPOT_MSG_PRESET_HP,
	NEED_AUTOPOT_MSG_PRESET_SP,
	NEED_AUTOPOT_MSG_HP_SP_DISABLED,
	NEED_AUTOPOT_MSG_BLACKLIST_EMPTY,
	NEED_AUTOPOT_MSG_PRESET_LIST_FAILED,
	NEED_AUTOPOT_MSG_DELAY_RANGE,
	NEED_AUTOPOT_MSG_DELAY_UPDATED,
	NEED_AUTOPOT_MSG_PRESET_SLOT_RANGE,
	NEED_AUTOPOT_MSG_PRESET_DELETED,
	NEED_AUTOPOT_MSG_PRESET_LOADED,
	NEED_AUTOPOT_MSG_PRESET_OPERATION_FAILED,
	NEED_AUTOPOT_MSG_PRESET_SAVE_USAGE,
	NEED_AUTOPOT_MSG_PRESET_NAME_INVALID,
	NEED_AUTOPOT_MSG_PRESET_SAVED,
	NEED_AUTOPOT_MSG_PRESET_SAVE_FAILED,
	NEED_AUTOPOT_MSG_HP_ENABLED,
	NEED_AUTOPOT_MSG_SP_ENABLED,
	NEED_AUTOPOT_MSG_ENABLE_FAILED,
	NEED_AUTOPOT_MSG_HP_DISABLED,
	NEED_AUTOPOT_MSG_SP_DISABLED,
	NEED_AUTOPOT_MSG_PERCENT_RANGE,
	NEED_AUTOPOT_MSG_SETTING_ENABLED,
	NEED_AUTOPOT_MSG_SETTING_NOT_ENABLED,
};

void need_autopot_init();
void need_autopot_final();
void need_autopot_logout(map_session_data* sd);
void need_autopot_map_changed(map_session_data* sd);

bool need_autopot_enable(map_session_data* sd, bool hp);
void need_autopot_disable(map_session_data* sd, bool hp);
void need_autopot_disable_all(map_session_data* sd, bool notify = false);
void need_autopot_process(map_session_data* sd);

bool need_autopot_validate_item(map_session_data* sd, uint32 item_id, bool notify);
bool need_autopot_is_map_allowed(const map_session_data* sd);
bool need_autopot_is_status_blocked(map_session_data* sd);
bool need_autopot_use_item(map_session_data* sd, uint32 item_id, bool hp);

bool need_autopot_load_preset(map_session_data* sd, uint8 slot);
bool need_autopot_save_preset(map_session_data* sd, uint8 slot, const char* name);
bool need_autopot_delete_preset(map_session_data* sd, uint8 slot);
bool need_autopot_list_presets(map_session_data* sd);

int32 need_autopot_atcommand(int32 fd, map_session_data* sd, const char* message);

#endif
