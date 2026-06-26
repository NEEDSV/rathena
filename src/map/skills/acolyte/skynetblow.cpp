// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "skynetblow.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/status.hpp"

SkillSkyNetBlow::SkillSkyNetBlow() : SkillImplRecursiveDamageSplash(SR_SKYNETBLOW) {
}

void SkillSkyNetBlow::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	const status_data* sstatus = status_get_status_data(*src);

#ifndef NEED_2017_SKILL_FORMULA
	//ATK [{(Skill Level x 200) + (Caster AGI)} x Caster Base Level / 100] %
	skillratio += -100 + 200 * skill_lv + sstatus->agi / 6; // !TODO: Confirm AGI bonus
	RE_LVL_DMOD(100);
#else
	if (wd->miscflag & 8)
		//ATK [{(Skill Level x 100) + (Caster AGI) + 150} x Caster Base Level / 100] %
		skillratio += -100 + 100 * skill_lv + sstatus->agi + 150;
	else
		//ATK [{(Skill Level x 80) + (Caster AGI)} x Caster Base Level / 100] %
		skillratio += -100 + 80 * skill_lv + sstatus->agi;
	RE_LVL_DMOD(100);
#endif
}

void SkillSkyNetBlow::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
#ifdef NEED_2017_SKILL_BEHAVIOR
	// 2017: while in the Dragon Combo chain, Sky Net Blow gets the combo damage bonus (miscflag & 8).
	status_change* sc = status_get_sc(src);

	if (sc != nullptr && sc->getSCE(SC_COMBO) && sc->getSCE(SC_COMBO)->val1 == SR_DRAGONCOMBO)
		flag |= 8;
#endif
	clif_skill_nodamage(src,*target,getSkillId(),skill_lv);
	skill_castend_damage_id(src, target, getSkillId(), skill_lv, tick, flag);

	if (skill_area_temp[2] == 0) {
		clif_skill_damage( *src, *src, tick, status_get_amotion(src), 0, DMGVAL_IGNORE, 1, getSkillId(), skill_lv, DMG_SINGLE );
	}
}
