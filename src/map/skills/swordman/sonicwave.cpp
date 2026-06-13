// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "sonicwave.hpp"

#include <config/core.hpp>

#include "map/status.hpp"

SkillSonicWave::SkillSonicWave() : WeaponSkillImpl(RK_SONICWAVE) {
}

void SkillSonicWave::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& skillratio, int32 mflag) const {
#ifdef NEED_2017_SKILL_FORMULA
	skillratio += -100 + (skill_lv + 5) * 100; // ATK = {((Skill Level + 5) x 100) x (1 + [(Caster's Base Level - 100) / 200])} %
	skillratio = skillratio * (100 + (status_get_lv(src) - 100) / 2) / 100;
#else
	skillratio += -100 + 1050 + 150 * skill_lv;
	RE_LVL_DMOD(100);
#endif	
}

void SkillSonicWave::modifyHitRate(int16& hit_rate, const block_list* src, const block_list* target, uint16 skill_lv) const {
	hit_rate += hit_rate * 3 * skill_lv / 100; // !TODO: Confirm the hitrate bonus
}
