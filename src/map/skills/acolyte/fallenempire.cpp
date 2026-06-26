// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "fallenempire.hpp"

#include <config/core.hpp>

#include "map/status.hpp"

SkillFallenEmpire::SkillFallenEmpire() : WeaponSkillImpl(SR_FALLENEMPIRE) {
}

void SkillFallenEmpire::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& skillratio, int32 mflag) const {

#ifdef NEED_2017_SKILL_FORMULA
	skillratio += 150 * skill_lv;
	RE_LVL_DMOD(150);
#else
	// ATK [(Skill Level x 300 + 100) x Caster Base Level / 150] %
	skillratio += 300 * skill_lv;
	RE_LVL_DMOD(150);
#endif
}

void SkillFallenEmpire::applyAdditionalEffects(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
#ifdef NEED_2017_SKILL_BEHAVIOR
	// 2017: Fallen Empire immobilizes the target (SC_STOP) on hit.
	sc_start(src, target, SC_STOP, 100, skill_lv, skill_get_time(getSkillId(), skill_lv));
#endif
}
