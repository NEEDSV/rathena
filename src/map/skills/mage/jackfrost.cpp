// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "jackfrost.hpp"

#include <config/core.hpp>

#include "map/status.hpp"

SkillJackFrost::SkillJackFrost() : SkillImplRecursiveDamageSplash(WL_JACKFROST) {
}

void SkillJackFrost::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
#ifdef NEED_2017_SKILL_FORMULA
	const status_change* tsc = status_get_sc(target);

	if (tsc && tsc->getSCE(SC_FREEZING)) {
		skillratio += 900 + 300 * skill_lv;
		RE_LVL_DMOD(100);
	}
	else {
		skillratio += 400 + 100 * skill_lv;
		RE_LVL_DMOD(150);
	}
#else

	if (tsc && tsc->getSCE(SC_MISTY_FROST))
		skillratio += -100 + 1200 + 600 * skill_lv;
	else
		skillratio += -100 + 1000 + 300 * skill_lv;
	RE_LVL_DMOD(100);
#endif
}

void SkillJackFrost::splashSearch(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 flag) const {
#ifdef NEED_2017_SKILL_BEHAVIOR
	// 2017: Jack Frost is a self-targeted skill - AoE centers on the caster.
	SkillImplRecursiveDamageSplash::splashSearch(src, src, skill_lv, tick, flag);
#else
	SkillImplRecursiveDamageSplash::splashSearch(src, target, skill_lv, tick, flag);
#endif
}

int16 SkillJackFrost::getSplashSearchSize(block_list* src, uint16 skill_lv) const {
#ifdef NEED_2017_SKILL_BEHAVIOR
	// 2017 splash radius: 5:6:7:8:9 (4 + skill level).
	return 4 + skill_lv;
#else
	return SkillImplRecursiveDamageSplash::getSplashSearchSize(src, skill_lv);
#endif
}

void SkillJackFrost::applyAdditionalEffects(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
#ifdef NEED_2017_SKILL_BEHAVIOR
	// 2017: Jack Frost applies SC_FREEZE (200% chance) for skill_get_time (Duration1).
	sc_start(src, target, SC_FREEZE, 200, skill_lv, skill_get_time(getSkillId(), skill_lv));
#endif
}
