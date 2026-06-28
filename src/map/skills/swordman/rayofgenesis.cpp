// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "rayofgenesis.hpp"

#include <config/core.hpp>

#include "map/battle.hpp"
#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillRayOfGenesis::SkillRayOfGenesis() : SkillImplRecursiveDamageSplash(LG_RAYOFGENESIS) {
}

void SkillRayOfGenesis::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
	map_session_data* sd = BL_CAST(BL_PC, src);

	// 2017: ground-targeted; consumes 3% * skill level of max HP, fails if not enough.
	if (!status_charge(src, status_get_max_hp(src) * 3 * skill_lv / 100, 0)) {
		if (sd)
			clif_skill_fail(*sd, getSkillId());
		return;
	}

	SkillImplRecursiveDamageSplash::castendPos2(src, x, y, skill_lv, tick, flag);
}

void SkillRayOfGenesis::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& skillratio, int32 mflag) const {
	if (wd != nullptr && (wd->flag & BF_MAGIC)) {
		// 2017: magic-side ratio for the magic damage added on top of the weapon hit.
		const status_change* sc = status_get_sc(src);
		const map_session_data* sd = BL_CAST(BL_PC, src);

		if (sc) {
			if (sc->getSCE(SC_INSPIRATION))
				skillratio += 1400;
			if (sc->getSCE(SC_BANDING))
				skillratio += -100 + 300 * skill_lv + 200 * sc->getSCE(SC_BANDING)->val2;
			skillratio = skillratio * (sd ? sd->status.job_level / 25 : 1);
		}
	} else {
		// 2017: weapon-side ratio.
		skillratio += 200 + 300 * skill_lv;
		RE_LVL_DMOD(100);
	}
}

void SkillRayOfGenesis::applyAdditionalEffects(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	status_data* tstatus = status_get_status_data(*target);

	// 50% chance to cause Blind on Undead and Demon monsters.
	if ( battle_check_undead(tstatus->race, tstatus->def_ele) || tstatus->race == RC_DEMON )
		sc_start(src,target, SC_BLIND, 50, skill_lv, skill_get_time(getSkillId(),skill_lv));
}

void SkillRayOfGenesis::modifyElement(const Damage& dmg, const block_list& src, const block_list& target, uint16 skill_lv, int32& element, int32 flag) const {
	const status_change* sc = status_get_sc(&src);

	if (sc != nullptr && sc->hasSCE(SC_INSPIRATION))
		element = ELE_NEUTRAL;
}
