// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "windcutter.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillWindCutter::SkillWindCutter() : SkillImplRecursiveDamageSplash(RK_WINDCUTTER) {
}

void SkillWindCutter::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	skillratio += -100 + (skill_lv + 2) * 50;
	RE_LVL_DMOD(100);
}

void SkillWindCutter::applyAdditionalEffects(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	sc_start(src, target, SC_FEAR, 3 + 2 * skill_lv, skill_lv, skill_get_time(getSkillId(), skill_lv));
}

void SkillWindCutter::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
	clif_skill_damage(*src, *src, tick, status_get_amotion(src), 0, DMGVAL_IGNORE, 1, getSkillId(), skill_lv, DMG_SINGLE);
	SkillImplRecursiveDamageSplash::castendPos2(src, x, y, skill_lv, tick, flag);
}
