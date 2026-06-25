// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "earthstrain.hpp"

#include <config/core.hpp>

#include <common/random.hpp>

#include "map/battle.hpp"
#include "map/map.hpp"
#include "map/status.hpp"

SkillEarthStrain::SkillEarthStrain() : SkillImpl(WL_EARTHSTRAIN) {
}

void SkillEarthStrain::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
#ifdef NEED_2017_SKILL_FORMULA
	skillratio += 1900 + 100 * skill_lv;
	RE_LVL_DMOD(100);
#else
	skillratio += -100 + 1000 + 600 * skill_lv;
	RE_LVL_DMOD(100);
#endif
}

void SkillEarthStrain::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
	int32 w, wave = skill_lv + 4, dir = map_calc_dir(src,x,y);
	int32 sx = x = src->x, sy = y = src->y; // Store first caster's location to avoid glitch on unit setting

	for( w = 1; w <= wave; w++ )
	{
		switch( dir ){
			case 0: case 1: case 7: sy = y + w; break;
			case 3: case 4: case 5: sy = y - w; break;
			case 2: sx = x - w; break;
			case 6: sx = x + w; break;
		}
		skill_addtimerskill(src,gettick() + (140 * w),0,sx,sy,getSkillId(),skill_lv,dir,flag&2);
	}
}

void SkillEarthStrain::applyAdditionalEffects(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
#ifdef NEED_2017_SKILL_BEHAVIOR
	// 2017: on a successful hit, try to strip up to skill_lv equipment pieces in order
	// WEAPON, HELM, SHIELD, ARMOR, ACC - each at 5*skill_lv% chance for skill_get_time2 duration.
	if (dmg_lv != ATK_DEF)
		return;

	status_change* tsc = status_get_sc(target);

	if (tsc == nullptr || tsc->option&OPTION_MADOGEAR) // Mado Gear cannot be divested
		return;

	static const sc_type sc_atk[5] = { SC_STRIPWEAPON, SC_STRIPHELM, SC_STRIPSHIELD, SC_STRIPARMOR, SC__STRIPACCESSORY };
	static const sc_type sc_def[5] = { SC_CP_WEAPON, SC_CP_HELM, SC_CP_SHIELD, SC_CP_ARMOR, SC_NONE };
	t_tick time = skill_get_time2(getSkillId(), skill_lv);

	for (uint16 i = 0; i < skill_lv && i < 5; i++) {
		if (sc_def[i] > SC_NONE && tsc->getSCE(sc_def[i]))
			continue; // Equipment is protected from stripping.
		if (rnd()%100 < 5 * skill_lv)
			sc_start(src, target, sc_atk[i], 100, skill_lv, time);
	}
#endif
}
