// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
#ifndef NEED_PVP_DUMMY_HPP
#define NEED_PVP_DUMMY_HPP

#include <cstring>

#include "map.hpp"
#include "mob.hpp"

// Dedicated test target, never a player. Do not use this for BL_PC/PK checks.
constexpr int16 NEED_PVP_DUMMY_ID = 31900;

inline bool mob_is_need_pvp_dummy(const block_list* target) {
	if (target == nullptr || target->type != BL_MOB)
		return false;
	const auto* md = static_cast<const mob_data*>(target);
	if (md->mob_id != NEED_PVP_DUMMY_ID || md->master_id != 0 || md->bg_id != 0)
		return false;
	const auto* mapdata = map_getmapdata(target->m);
	return mapdata != nullptr && std::strcmp(mapdata->name, "hero_tra") == 0 && !mapdata_flag_vs2(mapdata);
}

// Only bAddRace and bIgnoreDefRaceRate opt in to this race substitution.
inline uint8 need_pvp_dummy_attack_race(const block_list* target, uint8 race) {
	return mob_is_need_pvp_dummy(target) ? static_cast<uint8>(RC_PLAYER_HUMAN) : race;
}

#endif
