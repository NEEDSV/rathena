// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "iceboundtrap.hpp"

#include <config/core.hpp>

#include "map/status.hpp"

SkillIceboundTrap::SkillIceboundTrap() : SkillImpl(RA_ICEBOUNDTRAP) {
}

void SkillIceboundTrap::applyAdditionalEffects(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
#ifdef NEED_2017_SKILL_BEHAVIOR
	// 2017: Freezing is mutually exclusive with Burning. status_change_start
	// rejected the Freezing application while the target was already Burning.
	// 2026 dropped that cross-block (status_db Fail no longer lists Burning), so
	// restore it on the RA trap path only. Warmer is still handled by status_db Fail.
	const status_change* tsc = status_get_sc(target);
	if (tsc && tsc->getSCE(SC_BURNING))
		return;
#endif
	sc_start(src, target, SC_FREEZING, 50 + skill_lv * 10, skill_lv, skill_get_time2(getSkillId(), skill_lv));
}

void SkillIceboundTrap::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
	flag|=1;//Set flag to 1 to prevent deleting ammo (it will be deleted on group-delete).
	skill_unitsetting(src,getSkillId(),skill_lv,x,y,0);
}
