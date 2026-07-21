// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL

#ifndef NEED_EQUIPMENT_BUILD_HPP
#define NEED_EQUIPMENT_BUILD_HPP

#include <common/cbasetypes.hpp>

#include <string>
#include <vector>

class map_session_data;

enum e_need_equipment_build_result : int64 {
	NEED_BUILD_ERROR_INTERNAL = -10,
	NEED_BUILD_ERROR_COMMIT = -9,
	NEED_BUILD_ERROR_ITEM_INSERT = -8,
	NEED_BUILD_ERROR_BUILD_INSERT = -7,
	NEED_BUILD_ERROR_DATABASE = -6,
	NEED_BUILD_ERROR_NO_EQUIPMENT = -5,
	NEED_BUILD_ERROR_DUPLICATE = -4,
	NEED_BUILD_ERROR_LIMIT = -3,
	NEED_BUILD_ERROR_INPUT = -2,
	NEED_BUILD_ERROR_NO_PLAYER = -1,
	NEED_BUILD_ERROR_UNAUTHORIZED = -11,
	NEED_BUILD_ERROR_NOT_FOUND = -12,
	NEED_BUILD_ERROR_STATE_CONFLICT = -13,
	NEED_BUILD_ERROR_REASON = -14,
	NEED_BUILD_ERROR_NOT_OWNER = -15,
	NEED_BUILD_ERROR_COOLDOWN = -16,
};

enum e_need_equipment_build_like_result : int64 {
	NEED_BUILD_LIKE_ADDED = 1,
	NEED_BUILD_LIKE_REMOVED = 2,
	NEED_BUILD_LIKE_FAILED = 0,
	NEED_BUILD_LIKE_NOT_FOUND = -1,
	NEED_BUILD_LIKE_NOT_PUBLIC = -2,
	NEED_BUILD_LIKE_OWN_BUILD = -3,
	NEED_BUILD_LIKE_DATABASE = -4,
	NEED_BUILD_LIKE_COOLDOWN = -5,
};

enum e_need_equipment_build_reward_result : int64 {
	NEED_BUILD_REWARD_SUCCESS = 1,
	NEED_BUILD_REWARD_FAILED = 0,
	NEED_BUILD_REWARD_NO_PLAYER = -1,
	NEED_BUILD_REWARD_NOT_OWNER = -2,
	NEED_BUILD_REWARD_NOT_ELIGIBLE = -3,
	NEED_BUILD_REWARD_ALREADY_PROCESSED = -4,
	NEED_BUILD_REWARD_INVALID_ITEM = -5,
	NEED_BUILD_REWARD_INVENTORY = -6,
	NEED_BUILD_REWARD_DATABASE = -7,
	NEED_BUILD_REWARD_FINALIZE = -8,
	NEED_BUILD_REWARD_COOLDOWN = -9,
};

enum e_need_equipment_build_reward_recovery_action : int32 {
	NEED_BUILD_REWARD_RECOVERY_COMPLETE = 1,
	NEED_BUILD_REWARD_RECOVERY_RETRY = 2,
};

enum e_need_equipment_build_review_action : int32 {
	NEED_BUILD_REVIEW_APPROVE = 1,
	NEED_BUILD_REVIEW_REJECT = 2,
	NEED_BUILD_REVIEW_DELETE = 3,
};

enum e_need_equipment_build_view_mode : int32 {
	NEED_BUILD_VIEW_PUBLIC = 1,
	NEED_BUILD_VIEW_OWNER = 2,
	NEED_BUILD_VIEW_ADMIN = 3,
};

enum e_need_equipment_build_view_result : int64 {
	NEED_BUILD_VIEW_SUCCESS = 1,
	NEED_BUILD_VIEW_FAILED = 0,
	NEED_BUILD_VIEW_NOT_FOUND = -1,
	NEED_BUILD_VIEW_NOT_PUBLIC = -2,
	NEED_BUILD_VIEW_NOT_OWNER = -3,
	NEED_BUILD_VIEW_NOT_ADMIN = -4,
	NEED_BUILD_VIEW_NO_ITEMS = -5,
	NEED_BUILD_VIEW_INVALID_DATA = -6,
	NEED_BUILD_VIEW_UNAVAILABLE = -7,
	NEED_BUILD_VIEW_COOLDOWN = -8,
};

enum e_need_equipment_build_message : int32 {
	NEED_BUILD_MSG_DETAIL_HEADER = 1651,
	NEED_BUILD_MSG_JOB,
	NEED_BUILD_MSG_LEVEL,
	NEED_BUILD_MSG_CATEGORY,
	NEED_BUILD_MSG_TITLE,
	NEED_BUILD_MSG_DESCRIPTION,
	NEED_BUILD_MSG_CREATED,
	NEED_BUILD_MSG_STATS,
	NEED_BUILD_MSG_LIKES,
	NEED_BUILD_MSG_ACCOUNT_LIKED,
	NEED_BUILD_MSG_YES,
	NEED_BUILD_MSG_NO,
	NEED_BUILD_MSG_SNAPSHOT_NOTICE_1,
	NEED_BUILD_MSG_SNAPSHOT_NOTICE_2,
	NEED_BUILD_MSG_ITEM_HEADER,
	NEED_BUILD_MSG_NO_ITEMS,
	NEED_BUILD_MSG_ITEM,
	NEED_BUILD_MSG_CARD,
	NEED_BUILD_MSG_NAMED_ID,
	NEED_BUILD_MSG_UNKNOWN_CARD,
	NEED_BUILD_MSG_OPTION,
	NEED_BUILD_MSG_UNKNOWN_OPTION,
	NEED_BUILD_MSG_UNKNOWN_ITEM,
	NEED_BUILD_MSG_UNKNOWN_JOB,
	NEED_BUILD_MSG_POSITION_HEAD_TOP,
	NEED_BUILD_MSG_POSITION_HEAD_MID,
	NEED_BUILD_MSG_POSITION_HEAD_LOW,
	NEED_BUILD_MSG_POSITION_ARMOR,
	NEED_BUILD_MSG_POSITION_RIGHT_HAND,
	NEED_BUILD_MSG_POSITION_LEFT_HAND,
	NEED_BUILD_MSG_POSITION_GARMENT,
	NEED_BUILD_MSG_POSITION_SHOES,
	NEED_BUILD_MSG_POSITION_ACC_LEFT,
	NEED_BUILD_MSG_POSITION_ACC_RIGHT,
	NEED_BUILD_MSG_POSITION_SHADOW_ARMOR,
	NEED_BUILD_MSG_POSITION_SHADOW_WEAPON,
	NEED_BUILD_MSG_POSITION_SHADOW_SHIELD,
	NEED_BUILD_MSG_POSITION_SHADOW_SHOES,
	NEED_BUILD_MSG_POSITION_SHADOW_EARRING,
	NEED_BUILD_MSG_POSITION_SHADOW_PENDANT,
	NEED_BUILD_MSG_POSITION_UNKNOWN,
	NEED_BUILD_MSG_CATEGORY_GENERAL,
	NEED_BUILD_MSG_CATEGORY_BOSS,
	NEED_BUILD_MSG_CATEGORY_FARMING,
	NEED_BUILD_MSG_CATEGORY_INSTANCE,
	NEED_BUILD_MSG_CATEGORY_WOE,
	NEED_BUILD_MSG_CATEGORY_OTHER,
	NEED_BUILD_MSG_CATEGORY_UNKNOWN,
	NEED_BUILD_MSG_STATUS_PENDING,
	NEED_BUILD_MSG_STATUS_APPROVED,
	NEED_BUILD_MSG_STATUS_REJECTED,
	NEED_BUILD_MSG_STATUS_DELETED,
	NEED_BUILD_MSG_STATUS_CANCELLED,
	NEED_BUILD_MSG_STATUS_UNKNOWN,
	NEED_BUILD_MSG_ADMIN_HEADER,
	NEED_BUILD_MSG_ADMIN_OWNER,
	NEED_BUILD_MSG_STATUS,
	NEED_BUILD_MSG_SNAPSHOT_HASH,
	NEED_BUILD_MSG_CREATED_UPDATED,
	NEED_BUILD_MSG_APPROVED_ADMIN,
	NEED_BUILD_MSG_DELETED_ADMIN,
	NEED_BUILD_MSG_CANCELLED,
	NEED_BUILD_MSG_REVIEW_REASON,
	NEED_BUILD_MSG_DELETE_REASON,
	NEED_BUILD_MSG_REWARD_STATE,
	NEED_BUILD_MSG_REWARD_ADMIN,
	NEED_BUILD_MSG_REWARD_NONE,
	NEED_BUILD_MSG_ADMIN_LIKES,
	NEED_BUILD_MSG_ADMIN_LOG,
	NEED_BUILD_MSG_OWNER_HEADER,
	NEED_BUILD_MSG_OWNER_TIMES,
	NEED_BUILD_MSG_CAN_CANCEL,
	NEED_BUILD_MSG_REWARD_OWNER,
	NEED_BUILD_MSG_CLAIM_UNCLAIMED,
	NEED_BUILD_MSG_CLAIM_PROCESSING,
	NEED_BUILD_MSG_CLAIM_COMPLETE,
	NEED_BUILD_MSG_CLAIM_UNKNOWN,
	NEED_BUILD_MSG_REWARD_CHECK_1,
	NEED_BUILD_MSG_REWARD_CHECK_2,
	NEED_BUILD_MSG_REWARD_LOG,
	NEED_BUILD_MSG_REWARD_LOG_NONE,
	NEED_BUILD_MSG_REWARD_PROCESSING,
	NEED_BUILD_MSG_REWARD_PROCESSING_NONE,
	NEED_BUILD_MSG_AUDIT_HEADER,
	NEED_BUILD_MSG_AUDIT_VALUES,
	NEED_BUILD_MSG_AUDIT_NO_REWARD,
	NEED_BUILD_MSG_AUDIT_UNCLAIMED,
	NEED_BUILD_MSG_AUDIT_PROCESSING,
	NEED_BUILD_MSG_AUDIT_COMPLETE,
	NEED_BUILD_MSG_AUDIT_INCONSISTENT,
	NEED_BUILD_MSG_AUDIT_ISSUE_AMOUNT,
	NEED_BUILD_MSG_AUDIT_ISSUE_ITEM,
	NEED_BUILD_MSG_AUDIT_ISSUE_NO_LOG,
	NEED_BUILD_MSG_AUDIT_ISSUE_NOT_CLAIMED,
	NEED_BUILD_MSG_AUDIT_ISSUE_NO_CLAIMED_AT,
	NEED_BUILD_MSG_AUDIT_ISSUE_NO_STARTED_AT,
	NEED_BUILD_MSG_AUDIT_ISSUE_LOG_COUNT,
	NEED_BUILD_MSG_AUDIT_ISSUE_LOG_MISMATCH,
	NEED_BUILD_MSG_USAGE_CHECK,
	NEED_BUILD_MSG_INVALID_ID,
	NEED_BUILD_MSG_USAGE_LIKE_SYNC,
	NEED_BUILD_MSG_INVALID_SYNC_ID,
	NEED_BUILD_MSG_LIKE_SYNCED,
	NEED_BUILD_MSG_NO_PERMISSION,
	NEED_BUILD_MSG_NOT_FOUND,
	NEED_BUILD_MSG_LIKE_SYNC_FAILED,
	NEED_BUILD_MSG_USAGE_REWARD,
	NEED_BUILD_MSG_USAGE_REWARD_PROCESSING,
	NEED_BUILD_MSG_REWARD_PROCESSING_FAILED,
	NEED_BUILD_MSG_TOO_MANY_PARAMS,
	NEED_BUILD_MSG_USAGE_REWARD_CHECK,
	NEED_BUILD_MSG_REWARD_CHECK_FAILED,
	NEED_BUILD_MSG_REWARD_COMPLETE_WARNING,
	NEED_BUILD_MSG_REWARD_RETRY_WARNING,
	NEED_BUILD_MSG_REWARD_CONFIRM_USAGE,
	NEED_BUILD_MSG_REWARD_COMPLETED,
	NEED_BUILD_MSG_REWARD_RETRIED,
	NEED_BUILD_MSG_RECOVERY_CONFLICT,
	NEED_BUILD_MSG_RECOVERY_FAILED,
	NEED_BUILD_MSG_USAGE_AUDIT,
	NEED_BUILD_MSG_AUDIT_FAILED,
	NEED_BUILD_MSG_ITEM_REFINED,
	NEED_BUILD_MSG_ITEM_ADMIN,
	NEED_BUILD_MSG_CARD_ADMIN,
	NEED_BUILD_MSG_OPTION_ADMIN,
	NEED_BUILD_MSG_SPECIAL_CARD_ADMIN,
	NEED_BUILD_MSG_VIEW_OPENED,
	NEED_BUILD_MSG_VIEW_FAILED,
	NEED_BUILD_MSG_VIEW_NOT_PUBLIC,
	NEED_BUILD_MSG_VIEW_NO_ITEMS,
	NEED_BUILD_MSG_VIEW_NAME,
	NEED_BUILD_MSG_VIEW_COOLDOWN,
};

struct need_equipment_build_list_entry {
	uint64 build_id = 0;
	int32 job_id = 0;
	int32 category = 0;
	int32 status = 0;
	uint32 like_count = 0;
	std::string job_name;
	std::string title;
	std::string owner_name;
};

struct need_equipment_build_page {
	std::vector<need_equipment_build_list_entry> entries;
	bool has_more = false;
};

struct need_equipment_build_job_entry {
	int32 job_id = 0;
	std::string name;
};

struct need_equipment_build_reward_entry {
	uint64 build_id = 0;
	uint32 reward_item_id = 0;
	uint32 reward_amount = 0;
	std::string title;
};

struct need_equipment_build_reward_page {
	std::vector<need_equipment_build_reward_entry> entries;
	bool has_more = false;
};

int64 need_equipment_build_register(map_session_data* sd, int32 category, const char* title, const char* description);
int64 need_equipment_build_count(uint32 account_id);
int64 need_equipment_build_last_id(uint32 account_id);
const char* need_equipment_build_category_name(int32 category, map_session_data* sd = nullptr);
const char* need_equipment_build_status_name(int32 status, map_session_data* sd = nullptr);
bool need_equipment_build_is_admin(const map_session_data* sd);
int64 need_equipment_build_admin_list(map_session_data* sd, int32 status, int32 page, need_equipment_build_page& result);
int64 need_equipment_build_admin_search(map_session_data* sd, int32 search_type, const char* value, int32 page, need_equipment_build_page& result);
int64 need_equipment_build_public_jobs(int32 family, std::vector<need_equipment_build_job_entry>& result);
int64 need_equipment_build_public_list(int32 job_id, int32 category, int32 sort_type, int32 page, need_equipment_build_page& result);
int64 need_equipment_build_owner_list(map_session_data* sd, int32 page, need_equipment_build_page& result);
int64 need_equipment_build_admin_detail(map_session_data* sd, uint64 build_id);
int64 need_equipment_build_public_detail(map_session_data* sd, uint64 build_id, uint32* like_count = nullptr, bool* liked = nullptr, bool* is_owner = nullptr);
int64 need_equipment_build_owner_detail(map_session_data* sd, uint64 build_id);
int64 need_equipment_build_open_window(map_session_data* sd, uint64 build_id, int32 view_mode);
int64 need_equipment_build_review(map_session_data* sd, uint64 build_id, int32 action, const char* reason);
int64 need_equipment_build_approve(map_session_data* sd, uint64 build_id, uint32 reward_item_id, uint32 reward_amount);
int64 need_equipment_build_cancel(map_session_data* sd, uint64 build_id);
int64 need_equipment_build_toggle_like(map_session_data* sd, uint64 build_id);
int64 need_equipment_build_sync_likes(map_session_data* sd, uint64 build_id, uint32& like_count);
int64 need_equipment_build_reward_list(map_session_data* sd, int32 page, need_equipment_build_reward_page& result);
int64 need_equipment_build_reward_detail(map_session_data* sd, uint64 build_id, need_equipment_build_reward_entry& result);
int64 need_equipment_build_claim_reward(map_session_data* sd, uint64 build_id);
int64 need_equipment_build_reward_check(map_session_data* sd, uint64 build_id);
int64 need_equipment_build_reward_processing(map_session_data* sd);
int64 need_equipment_build_reward_recover(map_session_data* sd, uint64 build_id, int32 action);
int64 need_equipment_build_reward_audit(map_session_data* sd, uint64 build_id);

#endif // NEED_EQUIPMENT_BUILD_HPP
