// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "luxanima.hpp"

#include "map/clif.hpp"
#include "map/party.hpp"
#include "map/pc.hpp"
#include "map/skill.hpp"
#include "map/status.hpp"

SkillLuxAnima::SkillLuxAnima() : SkillImpl(RK_LUXANIMA) {
}

void SkillLuxAnima::castendNoDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	static constexpr sc_type runes[] = {
		SC_MILLENNIUMSHIELD,
		SC_REFRESH,
		SC_GIANTGROWTH,
		SC_STONEHARDSKIN,
		SC_VITALITYACTIVATION,
		SC_ABUNDANCE,
	};
	map_session_data* sd = BL_CAST(BL_PC, src);

	if (sd != nullptr && pc_checkskill(sd, RK_RUNEMASTERY) < 10) {
		clif_skill_fail(*sd, getSkillId(), USESKILL_FAIL_LEVEL);
		return;
	}

	if (sd == nullptr || sd->status.party_id == 0 || flag & 1) {
		if (src->id == target->id)
			return;

		sc_start(src, target, runes[skill_area_temp[5]], 100, skill_lv, skill_get_time(getSkillId(), skill_lv));
		status_change_clear_buffs(target, SCCB_LUXANIMA);
	} else {
		int32 recent = 0;
		int32 result = -1;

		for (int32 i = 0; i < static_cast<int32>(ARRAYLENGTH(runes)); ++i) {
			status_change_entry* sce = sd->sc.getSCE(runes[i]);

			if (sce != nullptr && (sce->timer * (runes[i] == SC_REFRESH ? 3 : 1) > recent || recent == 0)) {
				recent = sce->timer;
				result = i;
			}
		}

		if (result != -1) {
			skill_area_temp[5] = result;
			status_change_end(src, runes[result], INVALID_TIMER);
			party_foreachsamemap(skill_area_sub, sd, skill_get_splash(getSkillId(), skill_lv), src, getSkillId(), skill_lv, tick, flag | BCT_PARTY | 1, skill_castend_nodamage_id);
			clif_skill_nodamage(src, *src, getSkillId(), skill_lv);
		}
	}
}
