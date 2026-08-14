// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 summer field hunting rewards.

#ifndef MAP_NEED_SUMMER_HUNT_HPP
#define MAP_NEED_SUMMER_HUNT_HPP

#include <common/cbasetypes.hpp>

class map_session_data;
struct mob_data;

void need_summer_hunt_on_kill(map_session_data* sd, mob_data* md, int32 type);
void need_summer_hunt_init();
void need_summer_hunt_final();

#endif  // MAP_NEED_SUMMER_HUNT_HPP
