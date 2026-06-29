// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "aciddemonstration.hpp"

#include <config/core.hpp>

#include "map/status.hpp"

SkillAcidDemonstration::SkillAcidDemonstration() : SkillImpl(CR_ACIDDEMONSTRATION) {
}

void SkillAcidDemonstration::castendDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	skill_attack(BF_MISC,src,src,target,getSkillId(),skill_lv,tick,flag);
}

void SkillAcidDemonstration::applyAdditionalEffects(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	skill_break_equip(src,target, EQP_WEAPON|EQP_ARMOR, 100*skill_lv, BCT_ENEMY);
}
