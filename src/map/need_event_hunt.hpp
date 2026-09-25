// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED seasonal event field hunting - shared helpers.
//
// Extracted from need_summer_hunt.cpp so that later seasonal events
// (chuseok, ...) reuse the exact same world drop targeting, logical date
// and inventory checks instead of copying them.

#ifndef MAP_NEED_EVENT_HUNT_HPP
#define MAP_NEED_EVENT_HUNT_HPP

#include <common/cbasetypes.hpp>
#include <common/mmo.hpp>

class map_session_data;
struct mob_data;

// Logical date buffer for the 04:00 daily boundary ("YYYY-MM-DD").
struct need_event_hunt_date {
	char sql_date[11] = {};
};

/// Shared world drop target test.
/// Normal field mob, no instance / vs map, no guardian / bg / slave / clone /
/// summoned mob, and the reward owner is within +-level_difference of the mob.
bool need_event_hunt_normal_field_target(const map_session_data* sd, const mob_data* md, int32 type, int32 level_difference);

/// Current logical date with the 04:00 boundary applied.
bool need_event_hunt_logical_date(need_event_hunt_date& result);

/// Client IP of the session as dotted quad. False when the session is gone.
bool need_event_hunt_client_ip(map_session_data* sd, char (&ip)[16]);

/// True when amount of item_id can be added right now (weight and slots).
bool need_event_hunt_inventory_ready(map_session_data* sd, t_itemid item_id, uint16 amount);

/// Inventory-checked pc_additem. Returns false when the grant did not happen.
bool need_event_hunt_add_item(map_session_data* sd, t_itemid item_id, uint16 amount);

/// Reads one unsigned column of the current result row.
bool need_event_hunt_sql_uint32(uint32 column, uint32& value);

#endif  // MAP_NEED_EVENT_HUNT_HPP
