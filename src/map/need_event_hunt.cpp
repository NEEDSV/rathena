// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED seasonal event field hunting - shared helpers.

#include "need_event_hunt.hpp"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>

#include <common/socket.hpp>
#include <common/sql.hpp>

#include "itemdb.hpp"
#include "log.hpp"
#include "map.hpp"
#include "mob.hpp"
#include "pc.hpp"

bool need_event_hunt_normal_field_target(const map_session_data* sd, const mob_data* md, int32 type, int32 level_difference) {
	if (sd == nullptr || md == nullptr || (type & 1) != 0 || md->state.npc_killmonster)
		return false;
	if (md->db == nullptr || md->mob_id <= 0 || md->level <= 0)
		return false;

	const map_data* mapdata = map_getmapdata(md->m);
	if (mapdata == nullptr || mapdata->instance_id > 0 || mapdata_flag_vs2(mapdata))
		return false;

	// Boss-type / boss-mode field mobs are allowed (fising.md sec.3), e.g. lhz_dun_n.
	// WoE guardians and battleground mobs stay excluded (not field mobs).
	if (md->guardian_data != nullptr || md->bg_id != 0)
		return false;
	if (md->master_id != 0 || md->special_state.ai != AI_NONE || md->special_state.clone)
		return false;
	if (md->spawn == nullptr || md->deletetimer != INVALID_TIMER)
		return false;

	const int64 difference = static_cast<int64>(sd->status.base_level) - static_cast<int64>(md->level);
	return difference >= -level_difference && difference <= level_difference;
}

bool need_event_hunt_logical_date(need_event_hunt_date& result) {
	time_t shifted = time(nullptr) - (4 * 60 * 60);
	struct tm* local = localtime(&shifted);
	return local != nullptr && strftime(result.sql_date, sizeof(result.sql_date), "%Y-%m-%d", local) == 10;
}

bool need_event_hunt_client_ip(map_session_data* sd, char (&ip)[16]) {
	if (sd == nullptr || sd->fd <= 0 || !session_isActive(sd->fd) || session[sd->fd]->client_addr == 0)
		return false;
	snprintf(ip, sizeof(ip), "%u.%u.%u.%u", CONVIP(session[sd->fd]->client_addr));
	return true;
}

bool need_event_hunt_inventory_ready(map_session_data* sd, t_itemid item_id, uint16 amount) {
	if (sd == nullptr || amount == 0)
		return false;

	std::shared_ptr<item_data> data = item_db.find(item_id);
	if (data == nullptr)
		return false;
	if (static_cast<uint64>(sd->weight) + static_cast<uint64>(data->weight) * amount > static_cast<uint64>(sd->max_weight))
		return false;

	char add_check = pc_checkadditem(sd, item_id, amount);
	return add_check != CHKADDITEM_OVERAMOUNT && (add_check != CHKADDITEM_NEW || pc_inventoryblank(sd) > 0);
}

bool need_event_hunt_add_item(map_session_data* sd, t_itemid item_id, uint16 amount) {
	if (!need_event_hunt_inventory_ready(sd, item_id, amount))
		return false;

	struct item reward = {};
	reward.nameid = item_id;
	return pc_additem(sd, &reward, amount, LOG_TYPE_PICKDROP_MONSTER) == ADDITEM_SUCCESS;
}

bool need_event_hunt_sql_uint32(uint32 column, uint32& value) {
	char* data = nullptr;
	if (SQL_SUCCESS != Sql_GetData(mmysql_handle, column, &data, nullptr) || data == nullptr)
		return false;
	value = static_cast<uint32>(strtoul(data, nullptr, 10));
	return true;
}
