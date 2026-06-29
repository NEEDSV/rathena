// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "shieldreflect.hpp"

#include "map/clif.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillShieldReflect::SkillShieldReflect() : StatusSkillImpl(CR_REFLECTSHIELD) {
}

void SkillShieldReflect::castendNoDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {

	StatusSkillImpl::castendNoDamageId(src, target, skill_lv, tick, flag);
}
