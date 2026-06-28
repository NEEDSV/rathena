// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "circleofnaturessound.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/map.hpp"
#include "map/party.hpp"
#include "map/pc.hpp"
#include "map/skill.hpp"
#include "map/status.hpp"

SkillCircleOfNaturesSound::SkillCircleOfNaturesSound() : SkillImpl(WM_SIRCLEOFNATURE) {
}

void SkillCircleOfNaturesSound::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	sc_type type = skill_get_sc(getSkillId());

	// 2017: affects all PCs in splash range (BCT_ALL) and starts SC_SIRCLEOFNATURE without the Voice Lesson val2.
	if( flag&1 )
		sc_start(src,target,type,100,skill_lv,skill_get_time(getSkillId(),skill_lv));
	else {
		map_foreachinallrange(skill_area_sub,src,skill_get_splash(getSkillId(),skill_lv),BL_PC,src,getSkillId(),skill_lv,tick,flag|BCT_ALL|1,skill_castend_nodamage_id);
		clif_skill_nodamage(src,*target,getSkillId(),skill_lv);
	}
}
