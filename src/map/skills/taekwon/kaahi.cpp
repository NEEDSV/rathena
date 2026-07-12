// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "kaahi.hpp"
#include "ka_target.hpp"

#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillKaahi::SkillKaahi() : StatusSkillImpl(SL_KAAHI) {
}

void SkillKaahi::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	map_session_data* sd = BL_CAST( BL_PC, src );
	map_session_data *dstsd = BL_CAST( BL_PC, target );

	if (!soul_linker_ka_target_allowed(sd, dstsd)) {
		status_change_start(src,src,SC_STUN,10000,skill_lv,0,0,0,500,SCSTART_NORATEDEF);
		clif_skill_fail( *sd, getSkillId() );
		return;
	}

	StatusSkillImpl::castendNoDamageId(src, target, skill_lv, tick, flag);
}
