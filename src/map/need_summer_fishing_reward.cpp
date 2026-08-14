// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 summer fishing weekly-ranking reward delivery.
//
// This module mirrors the proven attendance reward outbox consumer
// (need_summer_attendance.cpp). It ONLY delivers rewards: the ranking rows are
// produced by the fishing NPC scripts into need_summer_fishing_rank_reward_outbox,
// and this consumer turns each PENDING/RETRY row into a system mail inside a single
// InnoDB transaction (mail + mail_attachments + outbox DELIVERED). It never touches
// the fishing state machine, weekly_best or weekly_result.

#include "need_summer_fishing_reward.hpp"

#include <cinttypes>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <common/showmsg.hpp>
#include <common/mmo.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>
#include <common/timer.hpp>

#include "battle.hpp"
#include "clif.hpp"
#include "intif.hpp"
#include "itemdb.hpp"
#include "map.hpp"
#include "pc.hpp"

namespace {

constexpr uint32 NEED_FISHING_EVENT_ID = 202608;
constexpr int32 NEED_FISHING_REWARD_TIMER_INTERVAL = 5000; // ms, mirrors attendance
constexpr uint32 NEED_FISHING_REWARD_TOKEN_ITEM_ID = 399925; // coconut token
constexpr uint32 NEED_FISHING_REWARD_MAX_ATTEMPTS = 5;
constexpr int32 NEED_FISHING_REWARD_BATCH_SIZE = 20;
constexpr uint32 NEED_FISHING_REWARD_MAX_RANK = 10;
constexpr uint32 NEED_FISHING_REWARD_MAX_AMOUNT = 1000; // sanity ceiling for a single mail

// Korean user-facing mail strings live in conf/msg_conf/map_msg_need_summer.conf (CP949).
constexpr int32 MSG_FISH_RANK_SENDER = 1985;
constexpr int32 MSG_FISH_RANK_TITLE = 1986;
constexpr int32 MSG_FISH_RANK_BODY = 1987;

// Error codes stored in outbox.last_error_code (ascii). Console logs stay ASCII English.
constexpr const char* REWARD_ERROR_PARSE = "OUTBOX_ROW_PARSE_FAILED";
constexpr const char* REWARD_ERROR_PAYLOAD = "INVALID_REWARD_PAYLOAD";
constexpr const char* REWARD_ERROR_PROCESSING = "PROCESSING_STATE_UPDATE_FAILED";
constexpr const char* REWARD_ERROR_DESTINATION_LOOKUP = "DESTINATION_LOOKUP_FAILED";
constexpr const char* REWARD_ERROR_DESTINATION_MISSING = "INVALID_CHARACTER";
constexpr const char* REWARD_ERROR_DESTINATION_NAME = "INVALID_CHARACTER";
constexpr const char* REWARD_ERROR_MAIL_INSERT = "MAIL_INSERT_FAILED";
constexpr const char* REWARD_ERROR_MAIL_ID = "MAIL_ID_OUT_OF_RANGE";
constexpr const char* REWARD_ERROR_ATTACHMENT = "ATTACHMENT_INSERT_FAILED";
constexpr const char* REWARD_ERROR_COMMIT = "SQL_TRANSACTION_FAILED";

enum need_fishing_reward_status : uint8 {
	NEED_FISHING_REWARD_PENDING = 0,
	NEED_FISHING_REWARD_PROCESSING = 1,
	NEED_FISHING_REWARD_DELIVERED = 2,
	NEED_FISHING_REWARD_RETRY = 3,
	NEED_FISHING_REWARD_REVIEW = 4,
};

enum class need_fishing_consume_result : uint8 {
	IDLE,
	PROCESSED,
	FAILED,
};

struct need_fishing_reward_row {
	uint64 outbox_id = 0;
	uint32 event_id = 0;
	uint32 week_no = 0;
	uint32 rank_no = 0;
	uint32 account_id = 0;
	uint32 char_id = 0;
	uint32 reward_item_id = 0;
	uint32 reward_amount = 0;
	uint32 attempts = 0;
	uint32 result_account_id = 0; // from weekly_result cross-join
	uint32 result_char_id = 0;
};

int32 fishing_reward_timer_id = INVALID_TIMER;
bool fishing_reward_schema_checked = false;
bool fishing_reward_schema_available = false;
bool fishing_reward_fail_closed = false;
char fishing_reward_char_table[64] = "char";
char fishing_reward_mail_table[64] = "mail";
char fishing_reward_mail_attachment_table[64] = "mail_attachments";

void need_fishing_reward_consume_batch();
bool need_fishing_reward_sql_uint32(uint32 column, uint32& value);

void need_fishing_reward_sql_error() {
	if (mmysql_handle != nullptr)
		Sql_ShowDebug(mmysql_handle);
	fishing_reward_schema_checked = false;
	fishing_reward_schema_available = false;
}

bool need_fishing_reward_schema_ready() {
	if (!battle_config.need_summer_fishing_rank_reward_enable)
		return false;
	if (fishing_reward_schema_checked)
		return fishing_reward_schema_available;

	fishing_reward_schema_checked = true;
	fishing_reward_schema_available = false;
	if (mmysql_handle == nullptr)
		return false;
	if (!item_db.exists(NEED_FISHING_REWARD_TOKEN_ITEM_ID)) {
		ShowError("need_summer_fishing_reward: coconut token item %u is not loaded.\n", NEED_FISHING_REWARD_TOKEN_ITEM_ID);
		return false;
	}

	static const char* checks[] = {
		"SELECT `event_id`,`week_no`,`rank_no`,`account_id`,`char_id`,`length_mm`,`weight_g` FROM `need_summer_fishing_weekly_result` LIMIT 0",
		"SELECT `outbox_id`,`event_id`,`week_no`,`rank_no`,`account_id`,`char_id`,`char_name`,`reward_item_id`,`reward_amount`,"
		"`status`,`attempts`,`next_attempt_at`,`last_attempt_at`,`last_error_code`,`last_error`,`mail_id`,`delivered_at` "
		"FROM `need_summer_fishing_rank_reward_outbox` LIMIT 0",
	};
	for (const char* query : checks) {
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, query)) {
			need_fishing_reward_sql_error();
			fishing_reward_schema_checked = true;
			ShowError("need_summer_fishing_reward: ranking/outbox schema is not available.\n");
			return false;
		}
		Sql_FreeResult(mmysql_handle);
	}

	char table_check[512] = {};
	const char* table_checks[] = {
		"SELECT `char_id`,`account_id`,`name` FROM `%s` LIMIT 0",
		"SELECT `id`,`send_name`,`send_id`,`dest_name`,`dest_id`,`title`,`message`,`time`,`status`,`zeny`,`type` FROM `%s` LIMIT 0",
		"SELECT `id`,`index`,`nameid`,`amount`,`identify` FROM `%s` LIMIT 0",
	};
	const char* table_names[] = {
		fishing_reward_char_table,
		fishing_reward_mail_table,
		fishing_reward_mail_attachment_table,
	};
	for (size_t i = 0; i < ARRAYLENGTH(table_checks); ++i) {
		safesnprintf(table_check, sizeof(table_check), table_checks[i], table_names[i]);
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, table_check)) {
			need_fishing_reward_sql_error();
			fishing_reward_schema_checked = true;
			ShowError("need_summer_fishing_reward: char/mail schema is not available.\n");
			return false;
		}
		Sql_FreeResult(mmysql_handle);
	}

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT COUNT(*) FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA`=DATABASE() "
		"AND `TABLE_NAME` IN ('need_summer_fishing_rank_reward_outbox','%s','%s') AND `ENGINE`='InnoDB'",
		fishing_reward_mail_table, fishing_reward_mail_attachment_table) || SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		need_fishing_reward_sql_error();
		fishing_reward_schema_checked = true;
		Sql_FreeResult(mmysql_handle);
		ShowError("need_summer_fishing_reward: InnoDB requirement check failed.\n");
		return false;
	}
	uint32 innodb_tables = 0;
	bool innodb_ready = need_fishing_reward_sql_uint32(0, innodb_tables) && innodb_tables == 3;
	Sql_FreeResult(mmysql_handle);
	if (!innodb_ready) {
		ShowError("need_summer_fishing_reward: outbox/mail tables must be InnoDB.\n");
		return false;
	}

	fishing_reward_schema_available = true;
	return true;
}

bool need_fishing_reward_sql_uint32(uint32 column, uint32& value) {
	char* data = nullptr;
	if (SQL_SUCCESS != Sql_GetData(mmysql_handle, column, &data, nullptr) || data == nullptr)
		return false;
	value = static_cast<uint32>(strtoul(data, nullptr, 10));
	return true;
}

bool need_fishing_reward_sql_uint64(uint32 column, uint64& value) {
	char* data = nullptr;
	if (SQL_SUCCESS != Sql_GetData(mmysql_handle, column, &data, nullptr) || data == nullptr)
		return false;
	value = strtoull(data, nullptr, 10);
	return true;
}

void need_fishing_reward_uint64_string(uint64 value, char (&output)[32]) {
	safesnprintf(output, sizeof(output), "%" PRIu64, value);
}

void need_fishing_reward_rollback() {
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "ROLLBACK"))
		need_fishing_reward_sql_error();
}

void need_fishing_reward_fail_closed_set() {
	if (!fishing_reward_fail_closed)
		ShowError("need_summer_fishing_reward: consumer fail-closed; halting delivery until restart.\n");
	fishing_reward_fail_closed = true;
}

void need_fishing_reward_log_retry(const need_fishing_reward_row& row, uint32 attempt, const char* error_code, bool review) {
	char outbox_id[32] = {};
	need_fishing_reward_uint64_string(row.outbox_id, outbox_id);
	if (review)
		ShowWarning("need_summer_fishing_reward: outbox %s week %u rank %u -> REVIEW (%s).\n",
			outbox_id, row.week_no, row.rank_no, error_code);
	else
		ShowWarning("need_summer_fishing_reward: outbox %s week %u rank %u retry #%u (%s).\n",
			outbox_id, row.week_no, row.rank_no, attempt, error_code);
}

// Failure after ROLLBACK: bump attempts / schedule retry (or REVIEW at the cap). Auto-commit UPDATE.
void need_fishing_reward_record_failure(const need_fishing_reward_row& row, const char* error_code) {
	char escaped_error_code[129] = {};
	Sql_EscapeStringLen(mmysql_handle, escaped_error_code, error_code, strnlen(error_code, 64));
	uint32 next_attempt = row.attempts + 1;
	uint32 next_status = next_attempt >= NEED_FISHING_REWARD_MAX_ATTEMPTS
		? NEED_FISHING_REWARD_REVIEW
		: NEED_FISHING_REWARD_RETRY;

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `need_summer_fishing_rank_reward_outbox` SET `status`='%u',`attempts`='%u',"
		"`last_attempt_at`=NOW(),`next_attempt_at`=DATE_ADD(NOW(),INTERVAL 60 SECOND),"
		"`last_error_code`='%s',`last_error`='%s',`updated_at`=NOW() "
		"WHERE `outbox_id`='%" PRIu64 "' AND `status` IN ('0','3')",
		next_status, next_attempt, escaped_error_code, escaped_error_code, row.outbox_id)) {
		need_fishing_reward_sql_error();
		need_fishing_reward_fail_closed_set();
		return;
	}
	if (Sql_NumRowsAffected(mmysql_handle) != 1) {
		need_fishing_reward_fail_closed_set();
		return;
	}
	need_fishing_reward_log_retry(row, next_attempt, error_code, next_status == NEED_FISHING_REWARD_REVIEW);
}

// Non-retryable error inside the open transaction: move to REVIEW and COMMIT that state.
bool need_fishing_reward_mark_review_locked(const need_fishing_reward_row& row, const char* error_code) {
	char escaped_error_code[129] = {};
	Sql_EscapeStringLen(mmysql_handle, escaped_error_code, error_code, strnlen(error_code, 64));
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `need_summer_fishing_rank_reward_outbox` SET `status`='%u',`attempts`=`attempts`+1,"
		"`last_attempt_at`=NOW(),`last_error_code`='%s',`last_error`='%s',`updated_at`=NOW() "
		"WHERE `outbox_id`='%" PRIu64 "'",
		NEED_FISHING_REWARD_REVIEW, escaped_error_code, escaped_error_code, row.outbox_id) ||
		Sql_NumRowsAffected(mmysql_handle) != 1 ||
		SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		need_fishing_reward_sql_error();
		need_fishing_reward_rollback();
		need_fishing_reward_fail_closed_set();
		return false;
	}
	need_fishing_reward_log_retry(row, row.attempts + 1, error_code, true);
	return true;
}

need_fishing_consume_result need_fishing_reward_consume_one() {
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "START TRANSACTION")) {
		need_fishing_reward_sql_error();
		return need_fishing_consume_result::FAILED;
	}

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `o`.`outbox_id`,`o`.`event_id`,`o`.`week_no`,`o`.`rank_no`,`o`.`account_id`,`o`.`char_id`,"
		"`o`.`reward_item_id`,`o`.`reward_amount`,`o`.`attempts`,"
		"COALESCE(`r`.`account_id`,0),COALESCE(`r`.`char_id`,0) "
		"FROM `need_summer_fishing_rank_reward_outbox` `o` "
		"LEFT JOIN `need_summer_fishing_weekly_result` `r` "
		"ON `r`.`event_id`=`o`.`event_id` AND `r`.`week_no`=`o`.`week_no` AND `r`.`rank_no`=`o`.`rank_no` "
		"WHERE `o`.`event_id`='%u' AND `o`.`status` IN ('0','3') AND `o`.`attempts`<'%u' "
		"AND `o`.`next_attempt_at`<=NOW() ORDER BY `o`.`outbox_id` LIMIT 1 FOR UPDATE",
		NEED_FISHING_EVENT_ID, NEED_FISHING_REWARD_MAX_ATTEMPTS)) {
		need_fishing_reward_sql_error();
		need_fishing_reward_rollback();
		return need_fishing_consume_result::FAILED;
	}

	if (Sql_NumRows(mmysql_handle) == 0) {
		Sql_FreeResult(mmysql_handle);
		if (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
			need_fishing_reward_sql_error();
			need_fishing_reward_rollback();
			return need_fishing_consume_result::FAILED;
		}
		return need_fishing_consume_result::IDLE;
	}

	need_fishing_reward_row row;
	bool parsed = SQL_SUCCESS == Sql_NextRow(mmysql_handle) &&
		need_fishing_reward_sql_uint64(0, row.outbox_id) &&
		need_fishing_reward_sql_uint32(1, row.event_id) && need_fishing_reward_sql_uint32(2, row.week_no) &&
		need_fishing_reward_sql_uint32(3, row.rank_no) && need_fishing_reward_sql_uint32(4, row.account_id) &&
		need_fishing_reward_sql_uint32(5, row.char_id) && need_fishing_reward_sql_uint32(6, row.reward_item_id) &&
		need_fishing_reward_sql_uint32(7, row.reward_amount) && need_fishing_reward_sql_uint32(8, row.attempts) &&
		need_fishing_reward_sql_uint32(9, row.result_account_id) && need_fishing_reward_sql_uint32(10, row.result_char_id);
	Sql_FreeResult(mmysql_handle);

	if (!parsed || row.outbox_id == 0) {
		if (row.outbox_id != 0)
			return need_fishing_reward_mark_review_locked(row, REWARD_ERROR_PARSE)
				? need_fishing_consume_result::PROCESSED : need_fishing_consume_result::FAILED;
		need_fishing_reward_rollback();
		need_fishing_reward_fail_closed_set();
		return need_fishing_consume_result::FAILED;
	}

	// Payload / source-of-truth cross validation against the frozen weekly_result snapshot.
	if (row.event_id != NEED_FISHING_EVENT_ID || row.week_no < 1 || row.week_no > 4 ||
		row.rank_no < 1 || row.rank_no > NEED_FISHING_REWARD_MAX_RANK ||
		row.reward_item_id != NEED_FISHING_REWARD_TOKEN_ITEM_ID ||
		row.reward_amount == 0 || row.reward_amount > NEED_FISHING_REWARD_MAX_AMOUNT ||
		row.result_account_id != row.account_id || row.result_char_id != row.char_id) {
		return need_fishing_reward_mark_review_locked(row, REWARD_ERROR_PAYLOAD)
			? need_fishing_consume_result::PROCESSED : need_fishing_consume_result::FAILED;
	}

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `need_summer_fishing_rank_reward_outbox` SET `status`='%u',`last_attempt_at`=NOW(),"
		"`updated_at`=NOW() WHERE `outbox_id`='%" PRIu64 "' AND `status` IN ('0','3')",
		NEED_FISHING_REWARD_PROCESSING, row.outbox_id) || Sql_NumRowsAffected(mmysql_handle) != 1) {
		need_fishing_reward_sql_error();
		need_fishing_reward_rollback();
		need_fishing_reward_record_failure(row, REWARD_ERROR_PROCESSING);
		return need_fishing_consume_result::FAILED;
	}

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `name` FROM `%s` WHERE `char_id`='%u' AND `account_id`='%u' LIMIT 1",
		fishing_reward_char_table, row.char_id, row.account_id)) {
		need_fishing_reward_sql_error();
		need_fishing_reward_rollback();
		need_fishing_reward_record_failure(row, REWARD_ERROR_DESTINATION_LOOKUP);
		return need_fishing_consume_result::FAILED;
	}

	if (Sql_NumRows(mmysql_handle) == 0 || SQL_SUCCESS != Sql_NextRow(mmysql_handle)) {
		Sql_FreeResult(mmysql_handle);
		return need_fishing_reward_mark_review_locked(row, REWARD_ERROR_DESTINATION_MISSING)
			? need_fishing_consume_result::PROCESSED : need_fishing_consume_result::FAILED;
	}

	char* name_data = nullptr;
	char destination_name[NAME_LENGTH] = {};
	if (SQL_SUCCESS != Sql_GetData(mmysql_handle, 0, &name_data, nullptr) || name_data == nullptr || name_data[0] == '\0') {
		Sql_FreeResult(mmysql_handle);
		return need_fishing_reward_mark_review_locked(row, REWARD_ERROR_DESTINATION_NAME)
			? need_fishing_consume_result::PROCESSED : need_fishing_consume_result::FAILED;
	}
	safestrncpy(destination_name, name_data, sizeof(destination_name));
	Sql_FreeResult(mmysql_handle);

	char sender[NAME_LENGTH] = {}, title[MAIL_TITLE_LENGTH] = {}, body[MAIL_BODY_LENGTH] = {};
	safestrncpy(sender, msg_txt(nullptr, MSG_FISH_RANK_SENDER), sizeof(sender));
	safesnprintf(title, sizeof(title), "%s", msg_txt(nullptr, MSG_FISH_RANK_TITLE));
	safesnprintf(body, sizeof(body), msg_txt(nullptr, MSG_FISH_RANK_BODY), row.week_no, row.rank_no, row.reward_amount);
	char escaped_sender[NAME_LENGTH * 2 + 1] = {}, escaped_destination[NAME_LENGTH * 2 + 1] = {};
	char escaped_title[MAIL_TITLE_LENGTH * 2 + 1] = {}, escaped_body[MAIL_BODY_LENGTH * 2 + 1] = {};
	Sql_EscapeStringLen(mmysql_handle, escaped_sender, sender, strnlen(sender, sizeof(sender)));
	Sql_EscapeStringLen(mmysql_handle, escaped_destination, destination_name, strnlen(destination_name, sizeof(destination_name)));
	Sql_EscapeStringLen(mmysql_handle, escaped_title, title, strnlen(title, sizeof(title)));
	Sql_EscapeStringLen(mmysql_handle, escaped_body, body, strnlen(body, sizeof(body)));

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT INTO `%s` (`send_name`,`send_id`,`dest_name`,`dest_id`,`title`,`message`,`time`,`status`,`zeny`,`type`) "
		"VALUES ('%s','0','%s','%u','%s','%s','%lu','%d','0','%d')",
		fishing_reward_mail_table, escaped_sender, escaped_destination, row.char_id, escaped_title, escaped_body,
		static_cast<unsigned long>(time(nullptr)), MAIL_NEW, MAIL_INBOX_NORMAL)) {
		need_fishing_reward_sql_error();
		need_fishing_reward_rollback();
		need_fishing_reward_record_failure(row, REWARD_ERROR_MAIL_INSERT);
		return need_fishing_consume_result::FAILED;
	}

	uint64 mail_id = Sql_LastInsertId(mmysql_handle);
	if (mail_id == 0 || mail_id > INT_MAX) {
		need_fishing_reward_rollback();
		need_fishing_reward_record_failure(row, REWARD_ERROR_MAIL_ID);
		return need_fishing_consume_result::FAILED;
	}

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT INTO `%s` (`id`,`index`,`nameid`,`amount`,`identify`) VALUES ('%" PRIu64 "','0','%u','%u','1')",
		fishing_reward_mail_attachment_table, mail_id, row.reward_item_id, row.reward_amount) ||
		Sql_NumRowsAffected(mmysql_handle) != 1) {
		need_fishing_reward_sql_error();
		need_fishing_reward_rollback();
		need_fishing_reward_record_failure(row, REWARD_ERROR_ATTACHMENT);
		return need_fishing_consume_result::FAILED;
	}

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `need_summer_fishing_rank_reward_outbox` SET `status`='%u',`attempts`=`attempts`+1,"
		"`mail_id`='%" PRIu64 "',`last_error_code`='',`last_error`='',`delivered_at`=NOW(),`updated_at`=NOW() "
		"WHERE `outbox_id`='%" PRIu64 "' AND `status`='%u'",
		NEED_FISHING_REWARD_DELIVERED, mail_id, row.outbox_id, NEED_FISHING_REWARD_PROCESSING) ||
		Sql_NumRowsAffected(mmysql_handle) != 1 ||
		SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {
		need_fishing_reward_sql_error();
		need_fishing_reward_rollback();
		need_fishing_reward_record_failure(row, REWARD_ERROR_COMMIT);
		return need_fishing_consume_result::FAILED;
	}

	map_session_data* recipient = map_charid2sd(row.char_id);
	if (recipient != nullptr) {
		recipient->mail.changed = true;
		recipient->mail.inbox.unread++;
		clif_Mail_new(recipient, static_cast<int32>(mail_id), sender, title);
		intif_Mail_requestinbox(recipient->status.char_id, 1, MAIL_INBOX_NORMAL);
	}
	char outbox_id_text[32] = {}, mail_id_text[32] = {};
	need_fishing_reward_uint64_string(row.outbox_id, outbox_id_text);
	need_fishing_reward_uint64_string(mail_id, mail_id_text);
	ShowInfo("need_summer_fishing_reward: delivered outbox %s (week %u rank %u, mail %s, account %u char %u).\n",
		outbox_id_text, row.week_no, row.rank_no, mail_id_text, row.account_id, row.char_id);
	return need_fishing_consume_result::PROCESSED;
}

void need_fishing_reward_consume_batch() {
	if (fishing_reward_fail_closed)
		return;
	for (int32 i = 0; i < NEED_FISHING_REWARD_BATCH_SIZE; ++i) {
		if (need_fishing_reward_consume_one() != need_fishing_consume_result::PROCESSED)
			break;
	}
}

TIMER_FUNC(need_fishing_reward_timer) {
	if (!battle_config.need_summer_fishing_rank_reward_enable ||
		fishing_reward_fail_closed ||
		!need_fishing_reward_schema_ready())
		return 0;

	need_fishing_reward_consume_batch();
	return 0;
}

}  // namespace

void need_summer_fishing_reward_set_char_table(const char* table) {
	if (table == nullptr || table[0] == '\0')
		return;
	safestrncpy(fishing_reward_char_table, table, sizeof(fishing_reward_char_table));
	fishing_reward_schema_checked = false;
	fishing_reward_schema_available = false;
}

void need_summer_fishing_reward_set_mail_table(const char* table) {
	if (table == nullptr || table[0] == '\0')
		return;
	safestrncpy(fishing_reward_mail_table, table, sizeof(fishing_reward_mail_table));
	fishing_reward_schema_checked = false;
	fishing_reward_schema_available = false;
}

void need_summer_fishing_reward_set_mail_attachment_table(const char* table) {
	if (table == nullptr || table[0] == '\0')
		return;
	safestrncpy(fishing_reward_mail_attachment_table, table, sizeof(fishing_reward_mail_attachment_table));
	fishing_reward_schema_checked = false;
	fishing_reward_schema_available = false;
}

void need_summer_fishing_reward_init() {
	fishing_reward_schema_checked = false;
	fishing_reward_schema_available = false;
	fishing_reward_fail_closed = false;

	add_timer_func_list(need_fishing_reward_timer, "need_fishing_reward_timer");
	fishing_reward_timer_id = add_timer_interval(gettick() + NEED_FISHING_REWARD_TIMER_INTERVAL,
		need_fishing_reward_timer, 0, 0, NEED_FISHING_REWARD_TIMER_INTERVAL);
}

void need_summer_fishing_reward_final() {
	if (fishing_reward_timer_id != INVALID_TIMER) {
		delete_timer(fishing_reward_timer_id, need_fishing_reward_timer);
		fishing_reward_timer_id = INVALID_TIMER;
	}
	fishing_reward_schema_checked = false;
	fishing_reward_schema_available = false;
	fishing_reward_fail_closed = false;
}
