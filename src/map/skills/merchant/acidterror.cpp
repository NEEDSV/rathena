// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "acidterror.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillAcidTerror::SkillAcidTerror() : WeaponSkillImpl(AM_ACIDTERROR) {
}

void SkillAcidTerror::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& base_skillratio, int32 mflag) const {
#ifdef RENEWAL
		base_skillratio += 40 * skill_lv;
#else
	base_skillratio += -50 + 50 * skill_lv;
#endif
}

void SkillAcidTerror::applyAdditionalEffects(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	sc_start2(src,target,SC_BLEEDING,(skill_lv*3),skill_lv,src->id,skill_get_time2(getSkillId(),skill_lv));
	// NEED 2017: armor break = 3/7/10/12/13% (= 100 * Duration1). The RENEWAL branch had raised this to
	// 5/15/25/35/45% ((1000*skill_lv+500)-1000); restore the 2017 rate unconditionally.
	if (skill_break_equip(src,target, EQP_ARMOR, 100*skill_get_time(getSkillId(),skill_lv), BCT_ENEMY))
		clif_emotion( *target, ET_HUK );
}
