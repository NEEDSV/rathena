// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "moonslasher.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillMoonSlasher::SkillMoonSlasher() : SkillImplRecursiveDamageSplash(LG_MOONSLASHER) {
}

void SkillMoonSlasher::castendNoDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	skill_castend_damage_id(src, src, getSkillId(), skill_lv, tick, flag);
}

void SkillMoonSlasher::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& skillratio, int32 mflag) const {
	const map_session_data* sd = BL_CAST(BL_PC, src);

	skillratio += -100 + 120 * skill_lv + ((sd) ? pc_checkskill(sd, LG_OVERBRAND) * 80 : 0);
	RE_LVL_DMOD(100);
}

void SkillMoonSlasher::applyAdditionalEffects(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	// 2017: PC targets are forced to sit (delayed ~500ms via timer), monsters get SC_STOP.
	map_session_data* dstsd = BL_CAST(BL_PC, target);
	int32 rate = 32 + 8 * skill_lv;

	if (rnd() % 100 < rate && dstsd != nullptr)
		skill_addtimerskill(src, tick + 500, target->id, 0, 0, getSkillId(), skill_lv, BF_WEAPON, 0);
	else if (target->type == BL_MOB)
		sc_start(src, target, SC_STOP, 100, skill_lv, skill_get_time(getSkillId(), skill_lv) + 1000 * (rnd() % 3));
}

void SkillMoonSlasher::splashSearch(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 flag) const {
	clif_skill_damage(*src, *target, tick, status_get_amotion(src), 0, DMGVAL_IGNORE, 1, getSkillId(), skill_lv, DMG_SINGLE);

	SkillImplRecursiveDamageSplash::splashSearch(src, target, skill_lv, tick, flag);
}
