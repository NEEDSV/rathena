// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "fightingspirit.hpp"

#include "map/clif.hpp"
#include "map/party.hpp"
#include "map/pc.hpp"
#include "map/skill.hpp"
#include "map/status.hpp"

SkillFightingSpirit::SkillFightingSpirit() : SkillImpl(RK_FIGHTINGSPIRIT) {
}

void SkillFightingSpirit::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	map_session_data* sd = BL_CAST(BL_PC, src);

	sc_type type = skill_get_sc(getSkillId());
	int32 runemastery_skill_lv = (sd ? pc_checkskill(sd, RK_RUNEMASTERY) : skill_get_max(RK_RUNEMASTERY));

	if (sd != nullptr && runemastery_skill_lv < 3) {
		clif_skill_fail(*sd, getSkillId(), USESKILL_FAIL_LEVEL);
		return;
	}

	if (flag & 1) {
		if (skill_area_temp[1] == target->id)
			sc_start2(src, target, type, 100, 70 + 7 * skill_area_temp[0], 4 * runemastery_skill_lv, skill_area_temp[4]);
		else
			sc_start(src, target, type, 100, skill_area_temp[3], skill_area_temp[4]);
	} else {
		if (sd != nullptr && sd->status.party_id != 0) {
			skill_area_temp[0] = party_foreachsamemap(skill_area_sub, sd, skill_get_splash(getSkillId(), skill_lv), src, getSkillId(), skill_lv, tick, BCT_PARTY, skill_area_sub_count);
			skill_area_temp[1] = src->id;
			skill_area_temp[3] = 70 + 7 * skill_area_temp[0] / 2;
			skill_area_temp[4] = skill_get_time(getSkillId(), skill_lv);
			party_foreachsamemap(skill_area_sub, sd, skill_get_splash(getSkillId(), skill_lv), src, getSkillId(), skill_lv, tick, flag | BCT_PARTY | 1, skill_castend_nodamage_id);
		} else
			sc_start2(src, target, type, 100, 77, 4 * runemastery_skill_lv, skill_get_time(getSkillId(), skill_lv));
		clif_skill_nodamage(src, *target, getSkillId(), 1);
	}
}
