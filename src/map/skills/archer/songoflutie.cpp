// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "songoflutie.hpp"

#include <config/core.hpp>

#include "map/pc.hpp"

SkillSongofLutie::SkillSongofLutie() : SkillImpl(BA_APPLEIDUN) {
}

void SkillSongofLutie::castendNoDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
#ifdef RENEWAL
	// NEED @performancemode: modern mode = self+party time buff (no unit); classic = field song.
	if( map_session_data* sd = BL_CAST(BL_PC, src); sd != nullptr && need_get_solo_performance_mode(sd) == NEED_PERFORMANCE_MODERN )
		need_castend_solo_song_modern(src, getSkillId(), skill_lv, tick);
	else
		skill_unitsetting(src, getSkillId(), skill_lv, src->x, src->y, 0);
#endif
}

void SkillSongofLutie::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
#ifdef RENEWAL
	// NEED @performancemode: modern mode = self+party time buff (no unit); classic = field song.
	if (map_session_data* sd = BL_CAST(BL_PC, src); sd != nullptr && need_get_solo_performance_mode(sd) == NEED_PERFORMANCE_MODERN)
		need_castend_solo_song_modern(src, getSkillId(), skill_lv, tick);
	else
	{
		flag |= 1;//Set flag to 1 to prevent deleting ammo (it will be deleted on group-delete).
		skill_unitsetting(src, getSkillId(), skill_lv, x, y, 0);
	}
#endif
}
