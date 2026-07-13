// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "cannonspear.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/status.hpp"

SkillCannonSpear::SkillCannonSpear() : SkillImplRecursiveDamageSplash(LG_CANNONSPEAR) {
}

void SkillCannonSpear::castendDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	// NEED range fix: Cannon Spear is a directional path attack from the caster toward the target
	// (width = SplashArea radius 1 -> 3 cells, forward length = MaxCount/ActiveInstance = 11 cells),
	// not the target-centered splash that SkillImplRecursiveDamageSplash performs by default.
	// LG_CANNONSPEAR is a CAST_DAMAGE skill (no NK_NODAMAGE), so this castendDamageId is the entry point;
	// clif_skill_nodamage here is the skill-use animation (always shown), matching sibling path skills
	// (AG_STORM_CANNON / AG_CRIMSON_ARROW). No dummy self-packet / skill_area_temp[2] check is needed.
	clif_skill_nodamage(src, *target, getSkillId(), skill_lv);
	skill_area_temp[1] = target->id;

	if (battle_config.skill_eightpath_algorithm) {
		// map_foreachindir's search box only reaches distance (length - 1), so pass maxcount + 1 to cover
		// the cell exactly at max range (11th cell). Diagonal length stays capped at 11 (does not reach 12).
		// NEED bugfix beyond 2017: 2017 passed plain maxcount here, which reached only 10 forward cells.
		map_foreachindir(skill_attack_area, src->m, src->x, src->y, target->x, target->y,
			skill_get_splash(getSkillId(), skill_lv), skill_get_maxcount(getSkillId(), skill_lv) + 1, 0, splash_target(src),
			skill_get_type(getSkillId()), src, src, getSkillId(), skill_lv, tick, flag, BCT_ENEMY);
	} else {
		// map_foreachinpath scales the endpoint to 'length' cells, so plain maxcount already reaches the 11th cell.
		map_foreachinpath(skill_attack_area, src->m, src->x, src->y, target->x, target->y,
			skill_get_splash(getSkillId(), skill_lv), skill_get_maxcount(getSkillId(), skill_lv), splash_target(src),
			skill_get_type(getSkillId()), src, src, getSkillId(), skill_lv, tick, flag, BCT_ENEMY);
	}
}

void SkillCannonSpear::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& skillratio, int32 mflag) const {
	skillratio += -100 + skill_lv * (50 + status_get_str(src));
	RE_LVL_DMOD(100);
}
