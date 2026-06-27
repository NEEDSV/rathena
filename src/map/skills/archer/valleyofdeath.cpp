// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "valleyofdeath.hpp"

#include <config/core.hpp>

#include <common/random.hpp>

#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillValleyOfDeath::SkillValleyOfDeath() : SkillImpl(WM_DEADHILLHERE) {
}

void SkillValleyOfDeath::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	status_data* tstatus = status_get_status_data(*target);

	if( target->type == BL_PC ) {
		if( !status_isdead(*target) )
			return;

#ifdef NEED_2017_SKILL_BEHAVIOR
		// 2017: Valley of Death revives with chance 88 + 2 * skill_lv %.
		if( rnd()%100 >= 88 + 2 * skill_lv )
			return;
#endif

		tstatus->hp = max(tstatus->sp, 1);
		tstatus->sp -= tstatus->sp * ( 60 - 10 * skill_lv ) / 100;
		clif_skill_nodamage(src,*target,getSkillId(),skill_lv);
		pc_revive(reinterpret_cast<map_session_data*>(target),true,true);
		clif_resurrection( *target );
	}
}
