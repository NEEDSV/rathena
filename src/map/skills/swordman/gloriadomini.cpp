// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "gloriadomini.hpp"

#include <config/core.hpp>

#include "map/status.hpp"

SkillGloriaDomini::SkillGloriaDomini() : SkillImpl(PA_PRESSURE) {
}

void SkillGloriaDomini::castendDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	skill_attack(BF_MISC,src,src,target,getSkillId(),skill_lv,tick,flag);
}

void SkillGloriaDomini::applyAdditionalEffects(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	status_percent_damage(src, target, 0, 15+5*skill_lv, false);
	//Pressure can trigger physical autospells
	attack_type |= BF_NORMAL;
	attack_type |= BF_WEAPON;
}
