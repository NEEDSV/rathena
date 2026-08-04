// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "skill_vending.hpp"

#include "map/pc.hpp"
#include "map/vending.hpp"

SkillVending::SkillVending() : SkillImpl(MC_VENDING) {
}

void SkillVending::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	map_session_data *sd = BL_CAST(BL_PC, src);
	if (sd) {
		// Prevent vending of GMs with unnecessary Level to trade/drop. [Skotlex]
		if (!pc_can_give_items(sd))
			clif_skill_fail(*sd, MC_VENDING);
		else
			vending_prepare(*sd, skill_lv);
	}
}
