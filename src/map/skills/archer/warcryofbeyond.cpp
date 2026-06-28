// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "warcryofbeyond.hpp"

#include <config/core.hpp>

#include <common/random.hpp>

#include "map/clif.hpp"
#include "map/map.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillWarcryOfBeyond::SkillWarcryOfBeyond() : SkillImpl(WM_BEYOND_OF_WARCRY) {
}

void SkillWarcryOfBeyond::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	sc_type type = skill_get_sc(getSkillId());
	map_session_data* sd = BL_CAST(BL_PC, src);

	if( flag&1 ) {
		sc_start2(src, target, type, 100, skill_lv, battle_calc_chorusbonus(sd), skill_get_time(getSkillId(), skill_lv));
	} else {	// These affect to all targets around the caster.
		// 2017: success chance = 15 + 5 * skill_lv * 5 * chorus bonus.
		if( rnd()%100 < 15 + 5 * skill_lv * 5 * battle_calc_chorusbonus(sd) ) {
			map_foreachinallrange(skill_area_sub, src, skill_get_splash(getSkillId(),skill_lv),BL_PC, src, getSkillId(), skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_nodamage_id);
			clif_skill_nodamage(src,*target,getSkillId(),skill_lv);
		}
	}
}
