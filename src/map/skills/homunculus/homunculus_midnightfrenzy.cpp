// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "homunculus_midnightfrenzy.hpp"

#include "map/homunculus.hpp"
#include "map/status.hpp"

SkillMidnightFrenzy::SkillMidnightFrenzy() : SkillImpl(MH_MIDNIGHT_FRENZY) {
}

void SkillMidnightFrenzy::castendDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	skill_attack(skill_get_type(getSkillId()), src, src, target, getSkillId(), skill_lv, tick, flag);
}

void SkillMidnightFrenzy::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& base_skillratio, int32 mflag) const {
	const status_data* sstatus = status_get_status_data(*src);

#ifdef NEED_2017_SKILL_FORMULA
	base_skillratio += -100 + 300 * skill_lv * status_get_lv(src) / 150;
#else
	base_skillratio += -100 + 450 * skill_lv * status_get_lv(src) / 150 + sstatus->str; // !TODO: Confirm STR bonus
#endif
}

#ifdef NEED_2017_HOMUNCULUS_S
void SkillMidnightFrenzy::applyAdditionalEffects(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	// 2017: fear chance (remaining spirit spheres) * (10 + 2 * skill level) %
	const homun_data* hd = BL_CAST(BL_HOM, src);
	int32 spiritball = (hd != nullptr ? hd->homunculus.spiritball : 1);

	sc_start4(src, target, SC_FEAR, spiritball * (10 + 2 * skill_lv), skill_lv, src->id, 0, 0, skill_get_time(getSkillId(), skill_lv));
}
#endif
