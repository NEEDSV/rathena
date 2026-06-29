// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "souldestroyer.hpp"

#include <config/core.hpp>

#include "map/status.hpp"

SkillSoulDestroyer::SkillSoulDestroyer() : SkillImpl(ASC_BREAKER) {
}

void SkillSoulDestroyer::castendDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	// 2017 Soul Breaker is a BF_MISC skill: combined weapon + magic(INT) damage in battle_calc_misc_attack
	skill_attack(BF_MISC,src,src,target,getSkillId(),skill_lv,tick,flag);
}
