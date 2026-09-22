// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 chuseok event - field hunting rewards and the shared honey songpyun ledger.

#ifndef MAP_NEED_CHUSEOK_HUNT_HPP
#define MAP_NEED_CHUSEOK_HUNT_HPP

#include <common/cbasetypes.hpp>

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

/// Field hunting hook: songpyun materials plus the honey songpyun roll.
void need_chuseok_hunt_on_kill(map_session_data* sd, mob_data* md, int32 type);

/// Single entry point for the honey songpyun daily ledger.
/// source is a short ascii tag recorded in the ledger, e.g. "HUNT" or "BG".
/// md may be null when the claim does not come from a monster kill.
/// The account + IP daily limit is shared by every source.
e_need_chuseok_honey_result need_chuseok_honey_grant(map_session_data* sd, const char* source, const mob_data* md);

void need_chuseok_hunt_init();
void need_chuseok_hunt_final();

#endif  // MAP_NEED_CHUSEOK_HUNT_HPP
