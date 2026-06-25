// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "hellinferno.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/status.hpp"

// TODO: refactor to SkillImplRecursiveDamageSplash
SkillHellInferno::SkillHellInferno() : SkillImpl(WL_HELLINFERNO) {
}

void SkillHellInferno::modifyDamageData(Damage& dmg, const block_list& src, const block_list& target, uint16 skill_lv) const {
	if (dmg.miscflag & 2) { // ELE_DARK
		dmg.div_ = -3;
	}
}

void SkillHellInferno::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
#ifdef NEED_2017_SKILL_FORMULA
	skillratio += -100 + 300 * skill_lv;
	RE_LVL_DMOD(100);
	// Shadow: MATK [{( Skill Level x 300 ) x ( Caster Base Level / 100 ) x 4/5 }] %
	// Fire : MATK [{( Skill Level x 300 ) x ( Caster Base Level / 100 ) /5 }] %
	if (mflag & ELE_DARK)
		skillratio *= 4;
	skillratio /= 5;
#else
	skillratio += -100 + 400 * skill_lv;
	if (mflag & 2) // ELE_DARK
		skillratio += 200 * skill_lv;
	RE_LVL_DMOD(100);
#endif
}

void SkillHellInferno::castendDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	if (flag & 1) {
		skill_attack(BF_MAGIC, src, src, target, getSkillId(), skill_lv, tick, flag);
#ifdef NEED_2017_SKILL_BEHAVIOR
		skill_addtimerskill(src, tick + 200, target->id, 0, 0, getSkillId(), skill_lv, BF_MAGIC, flag | 2);
#else
		skill_addtimerskill(src, tick + 300, target->id, 0, 0, getSkillId(), skill_lv, BF_MAGIC, flag | 2);
#endif
	} else {
		clif_skill_nodamage(src, *target, getSkillId(), skill_lv);
		map_foreachinrange(skill_area_sub, target, skill_get_splash(getSkillId(), skill_lv), BL_CHAR, src, getSkillId(), skill_lv, tick, flag | BCT_ENEMY | SD_SPLASH | 1, skill_castend_damage_id);
	}
}

void SkillHellInferno::modifyElement(const Damage& dmg, const block_list& src, const block_list& target, uint16 skill_lv, int32& element, int32 flag) const {
	if (dmg.miscflag & 2) {
		element = ELE_DARK;
	}
}
