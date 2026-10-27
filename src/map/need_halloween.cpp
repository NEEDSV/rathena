// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 halloween event - field hunting coin drop, Jack's Candy and the jack game daily reward.
//
// Nothing here is new machinery: the world drops use the shared seasonal target
// test (need_event_hunt.hpp), and the jack game reward and Jack's Candy run through the
// shared daily ledger transaction (need_event_daily_grant) under their own event ids.
// Script side (npc/NEED/need_halloween.txt) owns the switches, the jack game,
// the battleground coins and the exchange.

#include "need_halloween.hpp"

#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/sql.hpp>

#include "battle.hpp"
#include "clif.hpp"
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

// Jack's Candy: item 6532, formerly the chuseok honey songpyun, converted for good.
// Rate is need_halloween_candy_rate / 10000 (conf 5 = 0.05%), one per account and per IP a day.
constexpr t_itemid NEED_HALLOWEEN_CANDY_ITEM_ID = 6532;
constexpr uint32 NEED_HALLOWEEN_CANDY_EVENT_ID = 202611;

constexpr int32 MSG_HALLOWEEN_LOG = 1981;
constexpr int32 MSG_HALLOWEEN_FAIL_CLOSED = 1982;
constexpr int32 MSG_HALLOWEEN_ITEM_UNAVAILABLE = 1983;
constexpr int32 MSG_HALLOWEEN_SCHEMA_UNAVAILABLE = 1984;
constexpr int32 MSG_CANDY_SCHEMA_UNAVAILABLE = 1547;
constexpr int32 MSG_CANDY_INVENTORY_FULL = 1548;
constexpr int32 MSG_CANDY_ACCOUNT_LIMIT = 1549;
constexpr int32 MSG_CANDY_IP_LIMIT = 1550;
constexpr int32 MSG_CANDY_DATABASE_ERROR = 1551;
constexpr int32 MSG_CANDY_SUCCESS = 1552;
constexpr int32 MSG_CANDY_LOG = 1553;
constexpr int32 MSG_CANDY_FAIL_CLOSED = 1554;

// No family exception: the jack game reward is one per account and one per IP, strictly.
// No player messages from here either: need_halloween.txt explains every result itself.
constexpr s_need_event_daily_spec NEED_HALLOWEEN_JACK_SPEC = {
	NEED_HALLOWEEN_EVENT_ID, NEED_HALLOWEEN_COIN_ITEM_ID, NEED_HALLOWEEN_JACK_REWARD_AMOUNT, false, false,
	MSG_HALLOWEEN_ITEM_UNAVAILABLE, MSG_HALLOWEEN_SCHEMA_UNAVAILABLE, MSG_HALLOWEEN_FAIL_CLOSED,
	MSG_HALLOWEEN_LOG, 0,
	0, 0, 0, 0, 0,
};

// No family exception and no messages from the ledger: need_halloween_candy_try below sends only
// the found / inventory / error lines, a daily limit hit stays silent (it is a world drop).
constexpr s_need_event_daily_spec NEED_HALLOWEEN_CANDY_SPEC = {
	NEED_HALLOWEEN_CANDY_EVENT_ID, NEED_HALLOWEEN_CANDY_ITEM_ID, 1, false, false,
	MSG_HALLOWEEN_ITEM_UNAVAILABLE, MSG_CANDY_SCHEMA_UNAVAILABLE, MSG_CANDY_FAIL_CLOSED,
	MSG_CANDY_LOG, 0,
	MSG_CANDY_INVENTORY_FULL, MSG_CANDY_ACCOUNT_LIMIT, MSG_CANDY_IP_LIMIT,
	MSG_CANDY_DATABASE_ERROR, MSG_CANDY_SUCCESS,
};

bool coin_item_checked = false;
bool coin_item_ready = false;
s_need_event_daily_state jack_state;
s_need_event_daily_state candy_state;

bool need_halloween_coin_ready() {
	if (coin_item_checked)
		return coin_item_ready;

	coin_item_checked = true;
	coin_item_ready = item_db.exists(NEED_HALLOWEEN_COIN_ITEM_ID);
	if (!coin_item_ready)
		ShowError(msg_txt(nullptr, MSG_HALLOWEEN_ITEM_UNAVAILABLE), NEED_HALLOWEEN_COIN_ITEM_ID);
	return coin_item_ready;
}

// One Jack's Candy roll after a valid kill. The ledger checks account + IP inside its
// transaction, so a roll after today's candy only ends in a silent limit result.
void need_halloween_candy_try(map_session_data* sd, mob_data* md) {
	if (candy_state.fail_closed ||
		!rnd_chance<int32>(battle_config.need_halloween_candy_rate, NEED_HALLOWEEN_RATE_SCALE))
		return;

	switch (need_event_daily_grant(NEED_HALLOWEEN_CANDY_SPEC, candy_state, sd, "HUNT", md)) {
		case NEED_CHUSEOK_HONEY_DELIVERED:
			clif_displaymessage(sd->fd, msg_txt(sd, MSG_CANDY_SUCCESS));
			break;
		case NEED_CHUSEOK_HONEY_INVENTORY:
			clif_displaymessage(sd->fd, msg_txt(sd, MSG_CANDY_INVENTORY_FULL));
			break;
		case NEED_CHUSEOK_HONEY_ERROR:
			clif_displaymessage(sd->fd, msg_txt(sd, MSG_CANDY_DATABASE_ERROR));
			break;
		default:	// account / IP limit already used today, or the candy is switched off
			break;
	}
}

}  // namespace

void need_halloween_hunt_on_kill(map_session_data* sd, mob_data* md, int32 type) {
	if ((battle_config.need_halloween_hunt_enable == 0 && battle_config.need_halloween_candy_enable == 0) ||
		!need_event_hunt_normal_field_target(sd, md, type, NEED_HALLOWEEN_LEVEL_DIFFERENCE))
		return;

	// Inventory failures stay silent like the chuseok materials; pc_additem logs the grant.
	if (battle_config.need_halloween_hunt_enable != 0 && need_halloween_coin_ready() &&
		rnd_chance<int32>(NEED_HALLOWEEN_COIN_RATE, NEED_HALLOWEEN_RATE_SCALE))
		need_event_hunt_add_item(sd, NEED_HALLOWEEN_COIN_ITEM_ID, NEED_HALLOWEEN_COIN_AMOUNT);

	if (battle_config.need_halloween_candy_enable != 0)
		need_halloween_candy_try(sd, md);
}

e_need_chuseok_honey_result need_halloween_jack_grant(map_session_data* sd) {
	return need_event_daily_grant(NEED_HALLOWEEN_JACK_SPEC, jack_state, sd, "JACK", nullptr);
}

int32 need_halloween_jack_claimed_today(map_session_data* sd) {
	return need_event_daily_claimed_today(NEED_HALLOWEEN_JACK_SPEC, jack_state, sd);
}

int32 need_halloween_candy_claimed_today(map_session_data* sd) {
	return need_event_daily_claimed_today(NEED_HALLOWEEN_CANDY_SPEC, candy_state, sd);
}

void need_halloween_init() {
	coin_item_checked = false;
	coin_item_ready = false;
	jack_state = {};
	candy_state = {};
	need_halloween_coin_ready();
	if (!item_db.exists(NEED_HALLOWEEN_CANDY_ITEM_ID)) {
		ShowError(msg_txt(nullptr, MSG_HALLOWEEN_ITEM_UNAVAILABLE), NEED_HALLOWEEN_CANDY_ITEM_ID);
		candy_state.fail_closed = true;
	}
}

void need_halloween_final() {
	coin_item_checked = false;
	coin_item_ready = false;
	jack_state = {};
	candy_state = {};
}
