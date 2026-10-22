// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 halloween event - field hunting coin drop and the jack game daily reward.
//
// Nothing here is new machinery: the world drop uses the shared seasonal target
// test (need_event_hunt.hpp) and the jack game reward runs through the chuseok
// daily ledger transaction (need_event_daily_grant) under its own event id.
// Script side (npc/NEED/need_halloween.txt) owns the switches, the jack game,
// the battleground coins and the exchange.

#include "need_halloween.hpp"

#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/sql.hpp>

#include "battle.hpp"
#include "itemdb.hpp"
#include "map.hpp"
#include "mob.hpp"
#include "need_event_hunt.hpp"
#include "pc.hpp"

namespace {

constexpr t_itemid NEED_HALLOWEEN_COIN_ITEM_ID = 6658;     // 할로윈 코인 (Halloween_Coin)
constexpr int32 NEED_HALLOWEEN_RATE_SCALE = 10000;
constexpr int32 NEED_HALLOWEEN_COIN_RATE = 400;            // 4.00% (기대값 25마리당 1개)
constexpr uint16 NEED_HALLOWEEN_COIN_AMOUNT = 1;
constexpr int32 NEED_HALLOWEEN_LEVEL_DIFFERENCE = 30;      // 플레이어 레벨 ±30, 경계 포함
constexpr uint32 NEED_HALLOWEEN_EVENT_ID = 202610;
constexpr uint16 NEED_HALLOWEEN_JACK_REWARD_AMOUNT = 10;

constexpr int32 MSG_HALLOWEEN_LOG = 1981;
constexpr int32 MSG_HALLOWEEN_FAIL_CLOSED = 1982;
constexpr int32 MSG_HALLOWEEN_ITEM_UNAVAILABLE = 1983;
constexpr int32 MSG_HALLOWEEN_SCHEMA_UNAVAILABLE = 1984;

// No family exception: the jack game reward is one per account and one per IP, strictly.
// No player messages from here either: need_halloween.txt explains every result itself.
constexpr s_need_event_daily_spec NEED_HALLOWEEN_JACK_SPEC = {
	NEED_HALLOWEEN_EVENT_ID, NEED_HALLOWEEN_COIN_ITEM_ID, NEED_HALLOWEEN_JACK_REWARD_AMOUNT, false, false,
	MSG_HALLOWEEN_ITEM_UNAVAILABLE, MSG_HALLOWEEN_SCHEMA_UNAVAILABLE, MSG_HALLOWEEN_FAIL_CLOSED,
	MSG_HALLOWEEN_LOG, 0,
	0, 0, 0, 0, 0,
};

bool coin_item_checked = false;
bool coin_item_ready = false;
s_need_event_daily_state jack_state;

bool need_halloween_coin_ready() {
	if (coin_item_checked)
		return coin_item_ready;

	coin_item_checked = true;
	coin_item_ready = item_db.exists(NEED_HALLOWEEN_COIN_ITEM_ID);
	if (!coin_item_ready)
		ShowError(msg_txt(nullptr, MSG_HALLOWEEN_ITEM_UNAVAILABLE), NEED_HALLOWEEN_COIN_ITEM_ID);
	return coin_item_ready;
}

}  // namespace

void need_halloween_hunt_on_kill(map_session_data* sd, mob_data* md, int32 type) {
	if (battle_config.need_halloween_hunt_enable == 0 ||
		!need_event_hunt_normal_field_target(sd, md, type, NEED_HALLOWEEN_LEVEL_DIFFERENCE) ||
		!need_halloween_coin_ready())
		return;

	// Inventory failures stay silent like the chuseok materials; pc_additem logs the grant.
	if (rnd_chance<int32>(NEED_HALLOWEEN_COIN_RATE, NEED_HALLOWEEN_RATE_SCALE))
		need_event_hunt_add_item(sd, NEED_HALLOWEEN_COIN_ITEM_ID, NEED_HALLOWEEN_COIN_AMOUNT);
}

e_need_chuseok_honey_result need_halloween_jack_grant(map_session_data* sd) {
	return need_event_daily_grant(NEED_HALLOWEEN_JACK_SPEC, jack_state, sd, "JACK", nullptr);
}

int32 need_halloween_jack_claimed_today(map_session_data* sd) {
	char ip[16] = {};
	need_event_hunt_date logical_date;
	if (sd == nullptr || jack_state.fail_closed || !need_event_daily_schema_ready(NEED_HALLOWEEN_JACK_SPEC, jack_state) ||
		!need_event_hunt_client_ip(sd, ip) || !need_event_hunt_logical_date(logical_date))
		return -1;

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT 1 FROM `need_chuseok_honey_claim` WHERE `event_id`='%u' AND `logical_date`='%s' AND `account_id`='%u' "
		"UNION ALL SELECT 1 FROM `need_chuseok_honey_ip_daily` WHERE `event_id`='%u' AND `logical_date`='%s' AND `ip`=INET6_ATON('%s') "
		"LIMIT 1",
		NEED_HALLOWEEN_EVENT_ID, logical_date.sql_date, sd->status.account_id,
		NEED_HALLOWEEN_EVENT_ID, logical_date.sql_date, ip)) {
		Sql_ShowDebug(mmysql_handle);
		return -1;
	}
	int32 claimed = Sql_NumRows(mmysql_handle) > 0 ? 1 : 0;
	Sql_FreeResult(mmysql_handle);
	return claimed;
}

void need_halloween_init() {
	coin_item_checked = false;
	coin_item_ready = false;
	jack_state = {};
	need_halloween_coin_ready();
}

void need_halloween_final() {
	coin_item_checked = false;
	coin_item_ready = false;
	jack_state = {};
}
