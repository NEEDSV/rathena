// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "acousticrhythm.hpp"

#include <config/core.hpp>

SkillAcousticRhythm::SkillAcousticRhythm() : SkillImpl(BD_SIEGFRIED) {
}

void SkillAcousticRhythm::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	skill_unitsetting(src, getSkillId(), skill_lv, src->x, src->y, 0);
}

void SkillAcousticRhythm::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
	flag|=1;//Set flag to 1 to prevent deleting ammo (it will be deleted on group-delete).
	skill_unitsetting(src,getSkillId(),skill_lv,x,y,0);
}
