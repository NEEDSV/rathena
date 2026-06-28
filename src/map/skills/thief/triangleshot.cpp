// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "triangleshot.hpp"

#include <config/core.hpp>

#include "map/status.hpp"

SkillTriangleShot::SkillTriangleShot() : WeaponSkillImpl(SC_TRIANGLESHOT) {
}

void SkillTriangleShot::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	const status_data* sstatus = status_get_status_data(*src);

	skillratio += 200 + (skill_lv - 1) * sstatus->agi / 2;
	RE_LVL_DMOD(120);
}
