// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "bloodsucker.hpp"

#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillBloodSucker::SkillBloodSucker() : SkillImpl(GN_BLOOD_SUCKER) {
}

void SkillBloodSucker::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	map_session_data* sd = BL_CAST(BL_PC, src);
	status_change* sc = status_get_sc(src);
	status_change* tsc = status_get_sc(target);
	sc_type type = skill_get_sc(getSkillId());

	// 2017: a caster can keep up to skill_get_maxcount active Blood Suckers (sc->bs_counter).
	if (sc && sc->bs_counter < skill_get_maxcount(getSkillId(), skill_lv)) {
		// If the target already has a Blood Sucker (from ANY caster), end it first so the new cast
		// resets the timer. status_change_end ends it on the target; the SC_BLOODSUCKER end handler
		// (status.cpp) decrements the *owning* caster's bs_counter via val2. We therefore do NOT
		// decrement here, which avoids a double decrement for our own re-cast and avoids wrongly
		// decrementing this caster when the existing Blood Sucker belongs to another caster.
		if (tsc && tsc->getSCE(type))
			status_change_end(target, type);
		clif_skill_nodamage(src, *target, getSkillId(), skill_lv);
		// val2 = caster id: used by the SC_BLOODSUCKER tick (drain target / heal caster) and by the
		// end handler to free this caster's bs_counter slot.
		sc_start2(src, target, type, 100, skill_lv, src->id, skill_get_time(getSkillId(), skill_lv));
		(sc->bs_counter)++;
	} else if (sd)
		clif_skill_fail(*sd, getSkillId());
}
