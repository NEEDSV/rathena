// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "abundance.hpp"

#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillAbundance::SkillAbundance() : SkillImpl(RK_ABUNDANCE) {
}

void SkillAbundance::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	if (map_session_data* sd = BL_CAST(BL_PC, src); sd != nullptr) {
		if (pc_checkskill(sd, RK_RUNEMASTERY) >= 6) {
			// NEED (Uruz rune refresh-while-active): mirror the Hagalaz / Stone Hard Skin recast behavior.
			// SC_ABUNDANCE has "Fail: Abundance" in status_db, so re-casting while it is active makes sc_start
			// fail (no refresh). End the existing status first so the recast fully replaces it (fresh full
			// duration + val4 SP-tick counter) with no stacking: status_change_end removes the old entry/timer
			// and SC_ABUNDANCE has no onstart heal (first SP tick only after 10s), so no double recovery. The
			// rune/SP/cooldown are consumed by the skill condition path before castend, so reaching here is a
			// legal recast.
			if (sd->sc.getSCE(SC_ABUNDANCE))
				status_change_end(src, SC_ABUNDANCE);
			if (sc_start(src, target, skill_get_sc(getSkillId()), 100, skill_lv, skill_get_time(getSkillId(), skill_lv)))
				clif_skill_nodamage(src, *target, getSkillId(), skill_lv);
		} else
			clif_skill_fail( *sd, getSkillId() );
 	}
}
