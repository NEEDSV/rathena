// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "homunculus_erasercutter.hpp"

#include "map/status.hpp"

SkillEraserCutter::SkillEraserCutter() : SkillImpl(MH_ERASER_CUTTER) {
}

void SkillEraserCutter::castendDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	skill_attack(BF_MAGIC, src, src, target, getSkillId(), skill_lv, tick, flag);
}

void SkillEraserCutter::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& base_skillratio, int32 mflag) const {
	const status_data* sstatus = status_get_status_data(*src);

#ifdef NEED_2017_SKILL_FORMULA
	base_skillratio += 400 + 100 * skill_lv + (skill_lv % 2 > 0 ? 0 : 300);
#else
	base_skillratio += -100 + 450 * skill_lv * status_get_lv(src) / 100 + sstatus->int_; // !TODO: Confirm Base Level and INT bonus
#endif
}
