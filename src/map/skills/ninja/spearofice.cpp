// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "spearofice.hpp"

#include <config/core.hpp>

#include "map/pc.hpp"
#include "map/status.hpp"

SkillSpearOfIce::SkillSpearOfIce() : SkillImpl(NJ_HYOUSENSOU) {
}

void SkillSpearOfIce::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &base_skillratio, int32 mflag) const {
	const map_session_data* sd = BL_CAST( BL_PC, src );

#ifdef RENEWAL

	base_skillratio -= 30;
	if (sd && sd->spiritcharm_type == CHARM_TYPE_WATER && sd->spiritcharm > 0)
		base_skillratio += 5 * sd->spiritcharm;
#endif
}

void SkillSpearOfIce::castendDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	skill_attack(BF_MAGIC, src, src, target, getSkillId(), skill_lv, tick, flag);
}
