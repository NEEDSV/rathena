// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "inspiration.hpp"

#include "map/battle.hpp"
#include "map/map.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillInspiration::SkillInspiration() : StatusSkillImpl(LG_INSPIRATION) {
}

void SkillInspiration::castendNoDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	map_session_data* sd = BL_CAST(BL_PC, src);

	// 2017: casting Inspiration costs 1% of base EXP (when enabled and the map allows EXP penalty).
	if (sd != nullptr && !map_getmapflag(src->m, MF_NOEXPPENALTY) && battle_config.exp_cost_inspiration)
		pc_lostexp(sd, u64min(sd->status.base_exp, pc_nextbaseexp(sd) * battle_config.exp_cost_inspiration / 100), 0);

	StatusSkillImpl::castendNoDamageId(src, target, skill_lv, tick, flag);
}
