// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "poisonbuster.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillPoisonBuster::SkillPoisonBuster() : SkillImplRecursiveDamageSplash(SO_POISON_BUSTER) {
}

void SkillPoisonBuster::castendDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	if (!(flag & 1)) {
		// 2017: Poison Buster can only be cast on a poisoned target; on hit it removes the Poison.
		status_change* tsc = status_get_sc(target);
		if (!(tsc && tsc->getSCE(SC_POISON))) {
			map_session_data* sd = BL_CAST(BL_PC, src);
			if (sd != nullptr)
				clif_skill_fail(*sd, getSkillId());
			return;
		}
		SkillImplRecursiveDamageSplash::castendDamageId(src, target, skill_lv, tick, flag);
		status_change_end(target, SC_POISON);
		return;
	}
	SkillImplRecursiveDamageSplash::castendDamageId(src, target, skill_lv, tick, flag);
}

void SkillPoisonBuster::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	const status_change *sc = status_get_sc(src);
	const map_session_data* sd = BL_CAST(BL_PC, src);

	skillratio += 900 + 300 * skill_lv;
	RE_LVL_DMOD(120);
	if (sc && sc->getSCE(SC_CURSED_SOIL_OPTION))
		skillratio += (sd ? sd->status.job_level * 5 : 0);
}
