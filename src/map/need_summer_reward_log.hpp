// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 summer event reward origin log (item side).
//
// Records a single origin row when a valuable event reward is actually granted.
// The Zeny / Cash origin rows are inserted from the item-use scripts via query_sql;
// this helper is only used for the coconut shadow box (399931) so that the real
// granted item_id + unique_id can be captured from the getgroupitem grant context.

#ifndef NEED_SUMMER_REWARD_LOG_HPP
#define NEED_SUMMER_REWARD_LOG_HPP

#include <common/cbasetypes.hpp>

class map_session_data;

// Insert one ITEM origin-log row (reward_type = 3). Fire-and-forget: a log failure
// never affects the reward grant. Safe to call with any sd; validates internally.
void need_summer_reward_log_item( map_session_data* sd, uint32 source_item_id, uint32 reward_item_id, uint32 amount, uint64 unique_id );

#endif /* NEED_SUMMER_REWARD_LOG_HPP */
