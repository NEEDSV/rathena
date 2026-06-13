// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "throwkunai.hpp"

#include <config/core.hpp>

SkillThrowKunai::SkillThrowKunai() : WeaponSkillImpl(NJ_KUNAI) {
}

void SkillThrowKunai::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &base_skillratio, int32 mflag) const {
#ifdef RENEWAL
#ifdef NEED_2017_SKILL_FORMULA
	base_skillratio += 200;
#else
	base_skillratio += -100 + 100 * skill_lv;
#endif
#endif
}
