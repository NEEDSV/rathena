// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "stonehardskin.hpp"

#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillStoneHardSkin::SkillStoneHardSkin() : SkillImpl(RK_STONEHARDSKIN) {
}

void SkillStoneHardSkin::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	if (map_session_data* sd = BL_CAST(BL_PC, src); sd != nullptr) {
		if (pc_checkskill(sd, RK_RUNEMASTERY) >= 4) {
			// NEED (Hagalaz rune refresh-while-active): allow recasting while Stone Hard Skin is still active
			// and fully REPLACE the old status with a new one (no stacking). All use conditions and costs
			// (rune item, SP, cooldown, map, status ailments) are validated and consumed by the skill
			// condition/consume path BEFORE castend, so reaching here means the recast is legal; on any of
			// those failures castend is never entered and the existing status is left untouched. Ending the
			// existing status first avoids the sc_start overlap no-op (status_change_start returns early when
			// the existing val1 > the new val1) so val1-val4, durability (20% of CURRENT HP) and the full
			// duration are recomputed. The 20% HP charged by the SC onstart is the only post-end cost and
			// cannot fail for a living player, so the buff is never dropped without a replacement.
			if (sd->sc.getSCE(SC_STONEHARDSKIN))
				status_change_end(src, SC_STONEHARDSKIN);
			if (sc_start(src, target, skill_get_sc(getSkillId()), 100, skill_lv, skill_get_time(getSkillId(), skill_lv)))
				clif_skill_nodamage(src, *target, getSkillId(), skill_lv);
			else
				clif_skill_fail( *sd, getSkillId(), USESKILL_FAIL_HP_INSUFFICIENT );
		} else
			clif_skill_fail( *sd, getSkillId() );
	}
}
