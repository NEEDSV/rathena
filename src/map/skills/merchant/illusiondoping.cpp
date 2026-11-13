// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "illusiondoping.hpp"

#include <common/random.hpp>
#include <common/timer.hpp>

#include "map/map.hpp"
#include "map/need_illusiondoping_decay.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillIllusionDoping::SkillIllusionDoping() : SkillImplRecursiveDamageSplash(GN_ILLUSIONDOPING) {
}

void SkillIllusionDoping::applyAdditionalEffects(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	// NEED 2026-11-13: battleground-only repeat decay (see need_illusiondoping_decay.hpp). Player targets on
	// a battleground map only; every other map, and non-player targets, keep the original single roll.
	map_session_data* tsd = BL_CAST(BL_PC, target);
	s_need_illusiondoping_decay* decay = nullptr;
	t_tick now = 0;

	if( tsd != nullptr && map_getmapflag(target->m, MF_BATTLEGROUND) ){
		decay = &tsd->need_illusiondoping_decay;
		now = gettick();

		const uint8 stage = need_illusiondoping_decay_stage(*decay, now);
		uint32 draw = 1;

		if( stage > 0 && stage <= NEED_ILLUSIONDOPING_DECAY_MAX_SHIFT )
			draw = rnd_value<uint32>(1, UINT32_C(1) << stage);
		if( !need_illusiondoping_decay_gate(stage, draw) )
			return; // the extra 1/2^stage factor failed: same as a failed roll, the chain is left untouched
	}

	if( sc_start(src,target,SC_ILLUSIONDOPING,100 - skill_lv * 10,skill_lv,skill_get_time(getSkillId(),skill_lv)) ) {
		if( decay != nullptr )
			need_illusiondoping_decay_success(*decay, now);
		sc_start(src,target,SC_HALLUCINATION,100,skill_lv,skill_get_time(getSkillId(),skill_lv));
	}
}
