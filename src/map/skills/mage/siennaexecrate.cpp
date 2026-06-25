// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "siennaexecrate.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillSiennaExecrate::SkillSiennaExecrate() : SkillImpl(WL_SIENNAEXECRATE) {
}

void SkillSiennaExecrate::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	sc_type type = skill_get_sc(getSkillId());
	map_session_data* sd = BL_CAST(BL_PC, src);
	status_change *tsc = status_get_sc(target);

	if( status_isimmune(target) || !tsc )
		return;

	if( flag&1 ) {
		if( target->id == skill_area_temp[1] )
			return; // Already work on this target

#ifdef NEED_2017_SKILL_BEHAVIOR
		// 2017: re-cast on an already-stoned target removes the Stone (toggle); duration uses skill_get_time (Duration1).
		if( tsc->getSCE(type) )
			status_change_end(target, type);
		else
			status_change_start(src,target,type,10000,skill_lv,src->id,0,0,skill_get_time(getSkillId(),skill_lv), SCSTART_NOTICKDEF);
#else
		status_change_start(src,target,type,10000,skill_lv,src->id,0,0,skill_get_time2(getSkillId(),skill_lv), SCSTART_NOTICKDEF, skill_get_time(getSkillId(), skill_lv));
#endif
	} else {
		int32 rate = 45 + 5 * skill_lv + ( sd? sd->status.job_level : 50 ) / 4;
		// IroWiki says Rate should be reduced by target stats, but currently unknown
		if( rnd()%100 < rate ) { // Success on First Target
#ifdef NEED_2017_SKILL_BEHAVIOR
			// 2017: toggle Stone on the first target, then spread to splash (AoE retained).
			bool applied;
			if( !tsc->getSCE(type) )
				applied = status_change_start(src,target,type,10000,skill_lv,src->id,0,0,skill_get_time(getSkillId(),skill_lv), SCSTART_NOTICKDEF);
			else {
				applied = true;
				status_change_end(target, type);
			}
			if( applied ) {
				clif_skill_nodamage(src,*target,getSkillId(),skill_lv);
				skill_area_temp[1] = target->id;
				map_foreachinallrange(skill_area_sub,target,skill_get_splash(getSkillId(),skill_lv),BL_CHAR,src,getSkillId(),skill_lv,tick,flag|BCT_ENEMY|1,skill_castend_nodamage_id);
			}
#else
			if( status_change_start(src,target,type,10000,skill_lv,src->id,0,0,skill_get_time2(getSkillId(),skill_lv), SCSTART_NOTICKDEF, skill_get_time(getSkillId(), skill_lv)) ) {
				clif_skill_nodamage(src,*target,getSkillId(),skill_lv);
				skill_area_temp[1] = target->id;
				map_foreachinallrange(skill_area_sub,target,skill_get_splash(getSkillId(),skill_lv),BL_CHAR,src,getSkillId(),skill_lv,tick,flag|BCT_ENEMY|1,skill_castend_nodamage_id);
			}
			// Doesn't send failure packet if it fails on defense.
#endif
		}
		else if( sd ) // Failure on Rate
			clif_skill_fail( *sd, getSkillId() );
	}
}
