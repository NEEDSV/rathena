// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "homunculus_lavaslide.hpp"

#include "map/status.hpp"

SkillLavaSlide::SkillLavaSlide() : SkillImpl(MH_LAVA_SLIDE) {
}

void SkillLavaSlide::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
	//Set flag to 1 to prevent deleting ammo (it will be deleted on group-delete).
	flag |= 1;
	// Ammo should be deleted right away.
	skill_unitsetting(src, getSkillId(), skill_lv, x, y, 0);
}

void SkillLavaSlide::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& base_skillratio, int32 mflag) const {
#ifdef NEED_2017_SKILL_FORMULA
	base_skillratio += -100 + 70 * skill_lv;
#else
	base_skillratio += -100 + 50 * skill_lv;
#endif
}

#ifdef NEED_2017_HOMUNCULUS_S
void SkillLavaSlide::applyAdditionalEffects(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	// 2017: burning chance 10% per skill level
	sc_start4(src, target, SC_BURNING, 10 * skill_lv, skill_lv, 1000, src->id, 0, skill_get_time2(getSkillId(), skill_lv));
}
#endif
