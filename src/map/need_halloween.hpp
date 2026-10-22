// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 halloween event - field hunting coin drop and the jack game daily reward.

#ifndef MAP_NEED_HALLOWEEN_HPP
#define MAP_NEED_HALLOWEEN_HPP

#include <common/cbasetypes.hpp>

#include "need_chuseok_hunt.hpp"

class map_session_data;
struct mob_data;

/// Field hunting hook: halloween coin world drop.
void need_halloween_hunt_on_kill(map_session_data* sd, mob_data* md, int32 type);

/// Jack game clear reward through the shared daily ledger (account + IP, 04:00).
/// Returns the e_need_chuseok_honey_result values; the calling script shows the messages.
e_need_chuseok_honey_result need_halloween_jack_grant(map_session_data* sd);

/// Read only probe for the jack game NPC: 1 = account or IP already rewarded today,
/// 0 = not yet, -1 = ledger unavailable.
int32 need_halloween_jack_claimed_today(map_session_data* sd);

void need_halloween_init();
void need_halloween_final();

#endif  // MAP_NEED_HALLOWEEN_HPP
