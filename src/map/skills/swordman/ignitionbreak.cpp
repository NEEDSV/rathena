// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "ignitionbreak.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/map.hpp"
#include "map/status.hpp"

SkillIgnitionBreak::SkillIgnitionBreak() : SkillImplRecursiveDamageSplash(RK_IGNITIONBREAK) {
}

void SkillIgnitionBreak::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	// 3x3 cell Damage = ATK [{(Skill Level x 300) x (1 + [(Caster's Base Level - 100) / 100])}] %
	// 7x7 cell Damage = ATK [{(Skill Level x 250) x (1 + [(Caster's Base Level - 100) / 100])}] %
	// 11x11 cell Damage = ATK [{(Skill Level x 200) x (1 + [(Caster's Base Level - 100) / 100])}] %

	const status_data* sstatus = status_get_status_data(*src);

	int i = distance_bl(src, target);
	if (i < 2)
		skillratio += -100 + 300 * skill_lv;
	else if (i < 4)
		skillratio += -100 + 250 * skill_lv;
	else
		skillratio += -100 + 200 * skill_lv;
	skillratio = skillratio * status_get_lv(src) / 100;
	// Elemental check, 1.5x damage if your weapon element is fire.
	if (sstatus && sstatus->rhw.ele == ELE_FIRE)
		skillratio += 100 * skill_lv;
}

void SkillIgnitionBreak::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	skill_area_temp[1] = 0;

#if PACKETVER >= 20180207
	clif_skill_nodamage(src,*target,getSkillId(),skill_lv);
#else
	clif_skill_damage( *src, *src, tick, status_get_amotion(src), 0, DMGVAL_IGNORE, 1, getSkillId(), skill_lv, DMG_SINGLE );
#endif
	map_foreachinrange(skill_area_sub, target, skill_get_splash(getSkillId(), skill_lv), BL_CHAR|BL_SKILL, src, getSkillId(), skill_lv, tick, flag|BCT_ENEMY|SD_SPLASH|1, skill_castend_damage_id);
}
