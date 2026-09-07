// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "firedance.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillFireDance::SkillFireDance() : SkillImplRecursiveDamageSplash(RL_FIREDANCE) {
}

void SkillFireDance::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& skillratio, int32 mflag) const {
	// NEED balance: pre-2016 limit-break Fire Dance. The 2016 Awakening patch (rAthena PR #1692)
	// raised the base to 200 * skill_lv and added the GS_DESPERADO bonus; both are removed here.
	// Restored formula is the one rAthena carried unchanged from 2013-11-05 to 2017-10-11:
	//   100 * skill_lv, then + (that ratio * caster BaseLv) / 300.
	skillratio += -100 + 100 * skill_lv;
	skillratio += skillratio * status_get_lv(src) / 300;
}

void SkillFireDance::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	clif_skill_nodamage(src,*target,getSkillId(),skill_lv);
	skill_castend_damage_id(src, target, getSkillId(), skill_lv, tick, flag);
}
