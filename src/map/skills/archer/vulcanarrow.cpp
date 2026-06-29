// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "vulcanarrow.hpp"

#include <config/core.hpp>

#include "map/status.hpp"

SkillVulcanArrow::SkillVulcanArrow() : WeaponSkillImpl(CG_ARROWVULCAN) {
}

void SkillVulcanArrow::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	skillratio += 100 + 100 * skill_lv;
}
