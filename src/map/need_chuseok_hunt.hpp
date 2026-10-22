// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 chuseok event - field hunting rewards and the shared honey songpyun ledger.

#ifndef MAP_NEED_CHUSEOK_HUNT_HPP
#define MAP_NEED_CHUSEOK_HUNT_HPP

#include <common/cbasetypes.hpp>
#include <common/mmo.hpp>

class map_session_data;
struct mob_data;

/// Result of a honey songpyun claim. Script side reads these values directly.
enum e_need_chuseok_honey_result : int32 {
	NEED_CHUSEOK_HONEY_DELIVERED = 1,
	NEED_CHUSEOK_HONEY_ACCOUNT_LIMIT = 0,
	NEED_CHUSEOK_HONEY_IP_LIMIT = -1,
	NEED_CHUSEOK_HONEY_INVENTORY = -2,
	NEED_CHUSEOK_HONEY_ERROR = -3,
	NEED_CHUSEOK_HONEY_DISABLED = -4,
};

/// One daily ledger product: at most one delivery per account AND per client IP per
/// logical day (04:00). Rows live in the need_chuseok_honey_* tables under event_id,
/// so a later event passes its own spec instead of copying the transaction.
struct s_need_event_daily_spec {
	uint32 event_id;
	t_itemid item_id;
	uint16 amount;
	bool family_exception;          // operator approved family groups skip the IP side
	bool notify;                    // send the player messages below (false: the calling script reports)
	int32 msg_item_unavailable;     // console, "%d" = item id
	int32 msg_schema_unavailable;   // console
	int32 msg_fail_closed;          // console
	int32 msg_log;                  // console, one line per claim
	int32 msg_family_unavailable;   // console
	int32 msg_inventory_full;       // player (notify only)
	int32 msg_account_limit;        // player (notify only)
	int32 msg_ip_limit;             // player (notify only)
	int32 msg_database_error;       // player (notify only)
	int32 msg_success;              // player (notify only)
};

/// Runtime flags of one spec. The owner resets them in its init / final.
struct s_need_event_daily_state {
	bool schema_checked = false;
	bool schema_available = false;
	bool fail_closed = false;
	bool family_lookup_disabled = false;
};

/// Field hunting hook: songpyun materials plus the honey songpyun roll.
void need_chuseok_hunt_on_kill(map_session_data* sd, mob_data* md, int32 type);

/// Single entry point for the honey songpyun daily ledger.
/// source is a short ascii tag recorded in the ledger, e.g. "HUNT" or "BG".
/// md may be null when the claim does not come from a monster kill.
/// The account + IP daily limit is shared by every source.
e_need_chuseok_honey_result need_chuseok_honey_grant(map_session_data* sd, const char* source, const mob_data* md);

/// The ledger transaction behind need_chuseok_honey_grant, for any spec.
e_need_chuseok_honey_result need_event_daily_grant(const s_need_event_daily_spec& spec, s_need_event_daily_state& state,
	map_session_data* sd, const char* source, const mob_data* md);

/// Checks the ledger tables once (InnoDB, columns). False closes the spec.
bool need_event_daily_schema_ready(const s_need_event_daily_spec& spec, s_need_event_daily_state& state);

void need_chuseok_hunt_init();
void need_chuseok_hunt_final();

#endif  // MAP_NEED_CHUSEOK_HUNT_HPP
