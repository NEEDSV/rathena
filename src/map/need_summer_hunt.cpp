// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 summer field hunting rewards.

#include "need_summer_hunt.hpp"

#include <cstdlib>
#include <cstring>

#include <common/random.hpp>
#include <common/showmsg.hpp>

#include "battle.hpp"
#include "itemdb.hpp"
#include "log.hpp"
#include "map.hpp"
#include "mob.hpp"
#include "pc.hpp"

namespace {

constexpr t_itemid NEED_SUMMER_FRAGMENT_ITEM_ID = 399926;
constexpr int32 NEED_SUMMER_FRAGMENT_RATE = 300;
constexpr int32 NEED_SUMMER_RATE_SCALE = 10000;
constexpr int32 NEED_SUMMER_LEVEL_DIFFERENCE = 15;

constexpr int32 MSG_SUMMER_HUNT_ITEM_UNAVAILABLE = 1934;

bool fragment_item_ready = false;
bool fragment_item_checked = false;

bool need_summer_hunt_fragment_ready() {
	if (fragment_item_checked)
		return fragment_item_ready;

	fragment_item_checked = true;
	fragment_item_ready = item_db.exists(NEED_SUMMER_FRAGMENT_ITEM_ID);
	if (!fragment_item_ready)
		ShowError(msg_txt(nullptr, MSG_SUMMER_HUNT_ITEM_UNAVAILABLE), NEED_SUMMER_FRAGMENT_ITEM_ID);

	return fragment_item_ready;
}

bool need_summer_hunt_normal_field_target(const map_session_data* sd, const mob_data* md, int32 type) {
	if (sd == nullptr || md == nullptr || (type & 1) != 0 || md->state.npc_killmonster)
		return false;
	if (md->db == nullptr || md->mob_id <= 0 || md->level <= 0)
		return false;

	const map_data* mapdata = map_getmapdata(md->m);
	if (mapdata == nullptr || mapdata->instance_id > 0 || mapdata_flag_vs2(mapdata))
		return false;

	if (md->get_bosstype() != BOSSTYPE_NONE || md->state.boss || md->guardian_data != nullptr || md->bg_id != 0)
		return false;
	if (md->master_id != 0 || md->special_state.ai != AI_NONE || md->special_state.clone)
		return false;
	if (md->spawn == nullptr || md->spawn->state.dynamic || md->npc_event[0] != '\0' || md->deletetimer != INVALID_TIMER)
		return false;

	const int64 level_difference = static_cast<int64>(sd->status.base_level) - static_cast<int64>(md->level);
	return level_difference >= -NEED_SUMMER_LEVEL_DIFFERENCE && level_difference <= NEED_SUMMER_LEVEL_DIFFERENCE;
}

bool need_summer_hunt_add_fragment(map_session_data* sd) {
	std::shared_ptr<item_data> item_data = item_db.find(NEED_SUMMER_FRAGMENT_ITEM_ID);
	if (item_data == nullptr)
		return false;

	if (sd->weight + item_data->weight > sd->max_weight)
		return false;

	char add_check = pc_checkadditem(sd, NEED_SUMMER_FRAGMENT_ITEM_ID, 1);
	if (add_check == CHKADDITEM_OVERAMOUNT || (add_check == CHKADDITEM_NEW && pc_inventoryblank(sd) == 0))
		return false;

	struct item reward = {};
	reward.nameid = NEED_SUMMER_FRAGMENT_ITEM_ID;
	return pc_additem(sd, &reward, 1, LOG_TYPE_PICKDROP_MONSTER) == ADDITEM_SUCCESS;
}

}  // namespace

void need_summer_hunt_on_kill(map_session_data* sd, mob_data* md, int32 type) {
	if (battle_config.need_summer_hunt_enable == 0 || !need_summer_hunt_normal_field_target(sd, md, type))
		return;

	if (battle_config.need_summer_hunt_fragment_enable != 0 && need_summer_hunt_fragment_ready() &&
		rnd_chance<int32>(NEED_SUMMER_FRAGMENT_RATE, NEED_SUMMER_RATE_SCALE)) {
		// Frequent inventory failures are intentionally silent. pc_additem records successful grants in picklog.
		need_summer_hunt_add_fragment(sd);
	}
}

void need_summer_hunt_init() {
	fragment_item_checked = false;
	fragment_item_ready = false;
	if (battle_config.need_summer_hunt_enable != 0 && battle_config.need_summer_hunt_fragment_enable != 0)
		need_summer_hunt_fragment_ready();
}

void need_summer_hunt_final() {
	fragment_item_checked = false;
	fragment_item_ready = false;
}
