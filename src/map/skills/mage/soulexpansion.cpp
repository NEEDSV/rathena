// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "soulexpansion.hpp"

#include <config/core.hpp>

#include "map/status.hpp"

SkillSoulExpansion::SkillSoulExpansion() : SkillImplRecursiveDamageSplash(WL_SOULEXPANSION) {
}

void SkillSoulExpansion::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {

#ifdef NEED_2017_SKILL_FORMULA
	skillratio += -100 + (skill_lv + 4) * 100 + status_get_int(src);
	RE_LVL_DMOD(100);
#else
	const status_data* sstatus = status_get_status_data(*src);

	skillratio += -100 + 1000 + skill_lv * 200;
	skillratio += sstatus->int_;
	RE_LVL_DMOD(100);
#endif
}
