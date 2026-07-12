// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#pragma once

#include "map/pc.hpp"
#include "map/status.hpp"

inline bool soul_linker_ka_target_allowed(map_session_data* sd, map_session_data* dstsd) {
	if (sd == nullptr)
		return true;
	if (dstsd == nullptr)
		return false;

	status_change_entry* spirit = sd->sc.getSCE(SC_SPIRIT);

	return (spirit != nullptr && spirit->val2 == SL_SOULLINKER) ||
		(dstsd->class_&MAPID_SECONDMASK) == MAPID_SOUL_LINKER ||
		dstsd == sd ||
		pc_get_partner(sd) == dstsd ||
		pc_get_partner(dstsd) == sd ||
		pc_get_child(sd) == dstsd;
}
