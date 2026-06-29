// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "axetornado.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/status.hpp"

SkillAxeTornado::SkillAxeTornado() : SkillImplRecursiveDamageSplash(NC_AXETORNADO) {
}

void SkillAxeTornado::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	const status_data* sstatus = status_get_status_data(*src);

	skillratio += 100 + 100 * skill_lv + sstatus->vit;
	RE_LVL_DMOD(100);
	if (distance_bl(src, target) > 2) // Will deal 75% damage outside of 5x5 area.
		skillratio = skillratio * 75 / 100;
}

void SkillAxeTornado::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	clif_skill_nodamage(src,*target,getSkillId(),skill_lv);
	skill_castend_damage_id(src, target, getSkillId(), skill_lv, tick, flag);

	if (skill_area_temp[2] == 0) {
		clif_skill_damage( *src, *src, tick, status_get_amotion(src), 0, DMGVAL_IGNORE, 1, getSkillId(), skill_lv, DMG_SINGLE );
	}
}
