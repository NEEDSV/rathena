// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "earthshaker.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/map.hpp"
#include "map/mob.hpp"
#include "map/status.hpp"

SkillEarthShaker::SkillEarthShaker() : WeaponSkillImpl(SR_EARTHSHAKER) {
}

void SkillEarthShaker::applyAdditionalEffects(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	// 2017: Earth Shaker only stuns the target and clears Root Twist (no persistent marker status in 2017).
	sc_start(src,target,SC_STUN, 25 + 5 * skill_lv,skill_lv,skill_get_time(getSkillId(),skill_lv));
	status_change_end(target, SC_SV_ROOTTWIST);
}

void SkillEarthShaker::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	const status_change *tsc = status_get_sc(target);

	if (tsc && ((tsc->option & (OPTION_HIDE | OPTION_CLOAK | OPTION_CHASEWALK)) || tsc->getSCE(SC_CAMOUFLAGE) || tsc->getSCE(SC_STEALTHFIELD) || tsc->getSCE(SC__SHADOWFORM))) {
		//[(Skill Level x 150) x (Caster Base Level / 100) + (Caster INT x 3)] %
		skillratio += -100 + 150 * skill_lv;
		RE_LVL_DMOD(100);
		skillratio += status_get_int(src) * 3;
	}
	else { //[(Skill Level x 50) x (Caster Base Level / 100) + (Caster INT x 2)] %
		skillratio += -100 + 50 * skill_lv;
		RE_LVL_DMOD(100);
		skillratio += status_get_int(src) * 2;
	}
}

void SkillEarthShaker::castendDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	status_change *tsc = status_get_sc(target);

	if( flag&1 ) { //by default cloaking skills are remove by aoe skills so no more checking/removing except hiding and cloaking exceed.
		WeaponSkillImpl::castendDamageId(src, target, skill_lv, tick, flag);
		status_change_end(target, SC_CLOAKINGEXCEED);
		if (tsc && tsc->getSCE(SC__SHADOWFORM) && rnd() % 100 < 100 - tsc->getSCE(SC__SHADOWFORM)->val1 * 10) // [100 - (Skill Level x 10)] %
			status_change_end(target, SC__SHADOWFORM);
	} else {
		map_foreachinrange(skill_area_sub, target, skill_get_splash(getSkillId(), skill_lv), BL_CHAR|BL_SKILL, src, getSkillId(), skill_lv, tick, flag|BCT_ENEMY|SD_SPLASH|1, skill_castend_damage_id);
		clif_skill_damage( *src, *src, tick, status_get_amotion(src), 0, DMGVAL_IGNORE, 1, getSkillId(), skill_lv, DMG_SINGLE );
	}
}

void SkillEarthShaker::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	skill_castend_damage_id(src, src, getSkillId(), skill_lv, tick, flag);
}
