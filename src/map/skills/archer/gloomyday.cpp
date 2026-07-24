// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "gloomyday.hpp"

#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillGloomyDay::SkillGloomyDay() : SkillImpl(WM_GLOOMYDAY) {
}

void SkillGloomyDay::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	sc_type type = skill_get_sc(getSkillId());
	map_session_data* dstsd = BL_CAST(BL_PC, target);

	clif_skill_nodamage(src,*target,getSkillId(),skill_lv);
	// NEED common-penalty fix: apply the base SC_GLOOMYDAY penalty (Flee/Aspd/slow/dismount) to ALL valid
	// targets, then additionally grant SC_GLOOMYDAY_SK (spear/shield damage bonus) to targets holding the
	// listed skills. Previously listed-skill targets received only SC_GLOOMYDAY_SK and missed the base
	// penalty. The two statuses do not EndOnStart each other (status.yml) and share duration/icon; the
	// damage bonus lives only in SC_GLOOMYDAY_SK (battle.cpp), so it is applied exactly once.
	sc_start(src,target,type,100,skill_lv,skill_get_time(getSkillId(),skill_lv));
	if( dstsd && ( pc_checkskill(dstsd,KN_BRANDISHSPEAR) || pc_checkskill(dstsd,LK_SPIRALPIERCE) ||
			pc_checkskill(dstsd,CR_SHIELDCHARGE) || pc_checkskill(dstsd,CR_SHIELDBOOMERANG) ||
			pc_checkskill(dstsd,PA_SHIELDCHAIN) || pc_checkskill(dstsd,LG_SHIELDPRESS) ) )
	{ // !TODO: Which skills aren't boosted anymore?
		sc_start(src,target,SC_GLOOMYDAY_SK,100,skill_lv,skill_get_time(getSkillId(),skill_lv));
	}
}
