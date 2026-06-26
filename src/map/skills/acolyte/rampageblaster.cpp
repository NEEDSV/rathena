// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "rampageblaster.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillRampageBlaster::SkillRampageBlaster() : SkillImplRecursiveDamageSplash(SR_RAMPAGEBLASTER) {
}

void SkillRampageBlaster::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& skillratio, int32 mflag) const {
	const status_change* sc = status_get_sc(src);

#ifdef NEED_2017_SKILL_FORMULA
	// 2017: damage scales with the consumed spheres (spiritball_old) and SC_EXPLOSIONSPIRITS level.
	const map_session_data* sd = BL_CAST(BL_PC, src);
	int32 spheres = (sd != nullptr) ? sd->spiritball_old : 1;

	if (sc != nullptr && sc->getSCE(SC_EXPLOSIONSPIRITS)) {
		skillratio += -100 + (20 * sc->getSCE(SC_EXPLOSIONSPIRITS)->val1 + 20 * skill_lv) * spheres;
		RE_LVL_DMOD(120);
	} else {
		skillratio += -100 + (20 * skill_lv) * spheres;
		RE_LVL_DMOD(150);
	}
#else
	const status_change* tsc = status_get_sc(target);

	if (tsc && tsc->getSCE(SC_EARTHSHAKER)) {
		skillratio += 1400 + 550 * skill_lv;
		RE_LVL_DMOD(120);
	} else {
		skillratio += 900 + 350 * skill_lv;
		RE_LVL_DMOD(150);
	}

	if (sc != nullptr && sc->hasSCE(SC_GT_CHANGE))
		skillratio += skillratio * 30 / 100;
#endif
}

void SkillRampageBlaster::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	clif_skill_nodamage(src,*target,getSkillId(),skill_lv);
	skill_castend_damage_id(src, target, getSkillId(), skill_lv, tick, flag);
}
