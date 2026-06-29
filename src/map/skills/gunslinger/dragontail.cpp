// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "dragontail.hpp"

#include "map/clif.hpp"
#include "map/map.hpp"
#include "map/pc.hpp"
#include "map/skill.hpp"
#include "map/status.hpp"

SkillDragonTail::SkillDragonTail() : SkillImpl(RL_D_TAIL) {
}

void SkillDragonTail::castendNoDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	// 2017: self-cast. Hits every Crimson Marker'd enemy within splash range around the caster.
	clif_skill_nodamage(src, *target, getSkillId(), skill_lv);
	map_foreachinrange(skill_area_sub, src, skill_get_splash(getSkillId(), skill_lv), BL_CHAR, src, getSkillId(), skill_lv, tick, flag | BCT_ENEMY | SD_SPLASH | SD_ANIMATION | 1, skill_castend_damage_id);
}

void SkillDragonTail::castendDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	map_session_data* sd = BL_CAST(BL_PC, src);
	status_change* tsc = status_get_sc(target);

	// Players only hit Crimson Marker'd targets; the marker is consumed on hit.
	if (sd == nullptr || (tsc != nullptr && tsc->getSCE(SC_C_MARKER))) {
		skill_attack(skill_get_type(getSkillId()), src, src, target, getSkillId(), skill_lv, tick, flag);
		if (sd != nullptr)
			status_change_end(target, SC_C_MARKER);
	}
}

void SkillDragonTail::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& skillratio, int32 mflag) const {
	skillratio += -100 + 4000 + 1000 * skill_lv;
}
