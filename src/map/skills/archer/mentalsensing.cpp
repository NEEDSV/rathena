// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "mentalsensing.hpp"

#include <config/core.hpp>

SkillMentalSensing::SkillMentalSensing() : SkillImpl(BD_RICHMANKIM) {
}

void SkillMentalSensing::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
#ifdef RENEWAL
	// NEED: BD_RICHMANKIM uses the current rAthena timed party EXP buff. Route through the common song
	// handler so the shared ensemble handling runs (skill_id_dance / skill_lv_dance, Encore linkage,
	// skill_check_pc_partner cast_flag=1, partner post-processing + SC_ENSEMBLEFATIGUE, averaged ensemble
	// level). skill_apply_songs sc_start()s SC_RICHMANKIM (BCT_PARTY, SplashArea 15, Duration1 180s).
	skill_castend_song(src, getSkillId(), skill_lv, tick);
#endif
}

void SkillMentalSensing::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
#ifndef RENEWAL
	flag|=1;//Set flag to 1 to prevent deleting ammo (it will be deleted on group-delete).
	skill_unitsetting(src,getSkillId(),skill_lv,x,y,0);
#endif
}
