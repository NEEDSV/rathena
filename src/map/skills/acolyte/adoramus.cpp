// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "adoramus.hpp"

#include <config/core.hpp>

#include "map/map.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillAdoramus::SkillAdoramus() : SkillImplRecursiveDamageSplash(AB_ADORAMUS) {
}

void SkillAdoramus::applyAdditionalEffects(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	map_session_data* sd = BL_CAST( BL_PC, src );

	sc_start(src,target, SC_ADORAMUS, skill_lv * 4 + (sd ? sd->status.job_level : 50) / 2, skill_lv, skill_get_time2(getSkillId(), skill_lv));
}

void SkillAdoramus::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	skillratio += 400 + 100 * skill_lv;
	// NEED bishop rework: 5x5 splash. skill_area_temp[1] holds the original (center) target id, set by the
	// recursive-splash dispatch (SkillImplRecursiveDamageSplash::castendDamageId). The center keeps the full
	// NEED coefficient; every other target in the 5x5 takes 30% of it. Applied in the skillratio stage
	// (before RE_LVL_DMOD) so surrounding = center * 30% at every level and MDEF/element/card order is
	// unchanged. No new BaseLv multiplier is added - the RE_LVL_DMOD below is the existing NEED formula.
	if (target != nullptr && target->id != skill_area_temp[1])
		skillratio = skillratio * 30 / 100;
	RE_LVL_DMOD(100);
}

int64 SkillAdoramus::splashDamage(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 flag) const {

	return SkillImplRecursiveDamageSplash::splashDamage(src, target, skill_lv, tick, flag);
}

void SkillAdoramus::modifyElement(const Damage& dmg, const block_list& src, const block_list& target, uint16 skill_lv, int32& element, int32 flag) const {
}
