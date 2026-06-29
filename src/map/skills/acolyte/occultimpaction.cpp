// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "occultimpaction.hpp"

#include "map/status.hpp"

SkillOccultImpaction::SkillOccultImpaction() : WeaponSkillImpl(MO_INVESTIGATE) {
}

void SkillOccultImpaction::castendDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	WeaponSkillImpl::castendDamageId(src, target, skill_lv, tick, flag);
	status_change_end(src, SC_BLADESTOP);
}

void SkillOccultImpaction::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& base_skillratio, int32 mflag) const {

	base_skillratio += 75 * skill_lv;
}
