// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED marble board - account shared activity score, dice and board ledger (Phase B/C).
//
// Every ledger operation is one C++ entry point = one transaction on mmysql_handle.
// Entry points open and close the transaction; *_in_tx helpers in need_marble.cpp
// never do (nested START TRANSACTION would implicitly commit on MariaDB).
// Rules and policy defaults: need_marble_logic.hpp. Schema: sql-files/need_marble_phase_bc_v1.sql.
//
// Cash points are DECIDED in need_marble_cp_payout. need_marble_cp_apply hands a decided row to a CP writer,
// the only code allowed to change a balance; this build registers none (V20 prep: the test build registers a
// mock). Nothing in this module changes #CASHPOINTS or any other account registry value.

#ifndef MAP_NEED_MARBLE_HPP
#define MAP_NEED_MARBLE_HPP

#include <common/cbasetypes.hpp>

class map_session_data;
struct mob_data;

enum e_need_marble_rc : int32 {
	NEED_MARBLE_OK = 0,
	NEED_MARBLE_RESUME = 1,          // paused roll continues, no dice spent
	NEED_MARBLE_RESUME_ARRIVE = 2,   // all steps were walked, only the arrival is left
	NEED_MARBLE_DISABLED = -1,
	NEED_MARBLE_DB_ERROR = -2,
	NEED_MARBLE_BUSY = -3,
	NEED_MARBLE_ALREADY = -4,
	NEED_MARBLE_NO_SCORE = -5,
	NEED_MARBLE_HOLD_LIMIT = -6,
	NEED_MARBLE_PENDING_ACK = -7,
	NEED_MARBLE_EXPIRED = -8,
	NEED_MARBLE_NOT_FOUND = -9,
	NEED_MARBLE_BAD_STATE = -10,
	NEED_MARBLE_NO_DICE = -11,
	NEED_MARBLE_CHOICE_PENDING = -12,
	NEED_MARBLE_LOCKED = -13,
	NEED_MARBLE_SEASON = -14,
	NEED_MARBLE_EXCHANGE_LIMIT = -15,
	NEED_MARBLE_BAD_ARG = -16,
	NEED_MARBLE_STALE = -17,
	NEED_MARBLE_USED_TODAY = -18,
	NEED_MARBLE_INTERNAL = -19,
	NEED_MARBLE_REJECTED = -20,
	NEED_MARBLE_CONFLICT = -21,      // a hunt batch key already holds other content (V21): nothing written
	NEED_MARBLE_UNCERTAIN = -22,     // CP payout UNCERTAIN: never retried automatically, operator reconciliation
};

void need_marble_init();
void need_marble_final();

/// Field kill hook (mob_dead). top is the highest damage dealer.
void need_marble_hunt_on_kill(map_session_data* top, mob_data* md, int32 type);
/// Flushes the hunt buffer of this login and forgets its in-memory state.
void need_marble_on_logout(map_session_data* sd);

// Script entry points. Results are also written to @MB_* temporary character variables.
int32 need_marble_view(map_session_data* sd);
int32 need_marble_exchange_peek(map_session_data* sd);
int32 need_marble_exchange_issue(map_session_data* sd, int32 bundles);
int32 need_marble_exchange_commit(map_session_data* sd, uint64 request_no);
int32 need_marble_exchange_ack(map_session_data* sd, uint64 request_no, bool cancel);
int32 need_marble_roll(map_session_data* sd, uint64 driving_run, int32 forced_die);
int32 need_marble_step(map_session_data* sd, uint64 run, int32 from_pos);
int32 need_marble_arrive(map_session_data* sd, uint64 run);
int32 need_marble_choice(map_session_data* sd, uint64 roll_seq, int32 choice, bool automatic);
int32 need_marble_pause(map_session_data* sd, uint64 run, bool on_tile, int32 reason);

/// Instance final boss hook (F_NEED_MD_ClearBonus). Credits every eligible PC on the boss map.
int32 need_marble_instance_clear(const char* dungeon_key, const char* map_name);
/// Battleground reward hook (F_BG_Badge / F_BG_RM_AutoRewardBadge / F_KvM_GrantReward, random match cycle): one finished match = BG points
/// for this player's account, once per match. NEED_MARBLE_REJECTED = obviously AFK for the whole match.
int32 need_marble_bg_credit(map_session_data* sd, const char* bg_type, int32 match_token);

/// V20 CP writer: changes the balance of one payout and answers need_marble::CPW_APPLIED / CPW_NO_EFFECT /
/// CPW_UNKNOWN. It has no idempotency key of its own (like #CASHPOINTS): the ledger calls it at most once per row.
using need_marble_cp_writer = int32 (*)(uint32 account_id, uint32 char_id, uint64 payout_no, int32 amount);
void need_marble_cp_set_writer(need_marble_cp_writer writer);
/// Applies one DECIDED payout of this account: DECIDED -> UNCERTAIN (committed before the writer runs) ->
/// APPLIED, or back to DECIDED only when the writer confirms it changed nothing. DISABLED without a writer,
/// without need_marble_cp_audit or with policy key 5 off. @MB_CP_STATUS / @MB_CP_AMOUNT.
int32 need_marble_cp_apply(map_session_data* sd, uint64 payout_no);
/// Operator reconciliation of one UNCERTAIN payout: MANUAL_APPLIED (4) or MANUAL_VOID (5) with a ticket.
int32 need_marble_cp_resolve(uint32 account_id, uint64 payout_no, int32 to_status, uint64 ticket);

/// Current value of a policy key (today's effective value), -1 when unknown.
int32 need_marble_policy_value(int32 key);
/// 1 when the module is installed, valid and switched on.
int32 need_marble_is_ready();

#endif  // MAP_NEED_MARBLE_HPP
