// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED storage supply shop: bulk purchase delivered straight to a personal storage.
// The target storage can be storage 1 (stor_id 0, sd->storage), storage 2
// (stor_id 1) or storage 3 (stor_id 2). Storage 2 and 3 are premium storages that
// each account has to unlock separately, see need_storage_shop_is_unlocked().

#ifndef MAP_NEED_STORAGE_SHOP_HPP
#define MAP_NEED_STORAGE_SHOP_HPP

#include <common/cbasetypes.hpp>
#include <common/mmo.hpp>

class map_session_data;

/// Hard cap of a single purchase, used when an item does not define its own limit.
constexpr int32 NEED_STORAGE_SHOP_MAX_QUANTITY = 30000;

/// Highest storage id the shop may deliver to (0 = storage 1 ... 2 = storage 3).
constexpr int32 NEED_STORAGE_SHOP_STORAGE_MAX = 2;

/// Account variable holding the storage the shop delivers to.
constexpr const char* NEED_STORAGE_SHOP_TARGET_VAR = "#NEED_STORAGE_SHOP_TARGET";

/// Result of need_storage_shop_buy(), also pushed back to the NPC script as-is.
enum e_need_storage_shop_result : int32 {
	NEED_STORAGE_SHOP_OK = 1,             ///< Purchase completed
	NEED_STORAGE_SHOP_FAIL = 0,           ///< Generic failure (no player, bad item data)
	NEED_STORAGE_SHOP_NOT_SOLD = -1,      ///< Not registered or currently disabled
	NEED_STORAGE_SHOP_BAD_AMOUNT = -2,    ///< Amount below 1 or above the allowed maximum
	NEED_STORAGE_SHOP_NO_ZENY = -3,       ///< Not enough zeny
	NEED_STORAGE_SHOP_NO_ROOM = -4,       ///< Personal storage cannot hold the whole amount
	NEED_STORAGE_SHOP_BUSY = -5,          ///< Storage unavailable (trading, storage window open, ...)
	NEED_STORAGE_SHOP_NOT_STORABLE = -6,  ///< Item cannot be kept in the storage
	NEED_STORAGE_SHOP_OVERFLOW = -7,      ///< Total price does not fit into the zeny range
	NEED_STORAGE_SHOP_STORAGE_LOCKED = -8, ///< Target storage is not unlocked for this account
	NEED_STORAGE_SHOP_NOT_READY = -9,     ///< Target storage is not loaded yet
};

/// Result of need_storage_shop_prepare(), also pushed back to the NPC script as-is.
enum e_need_storage_shop_prepare : int32 {
	NEED_STORAGE_SHOP_PREPARE_READY = 0,    ///< Target storage is loaded and writable
	NEED_STORAGE_SHOP_PREPARE_WAITING = 1,  ///< Load/save in flight, ask again shortly
	NEED_STORAGE_SHOP_PREPARE_LOCKED = -1,  ///< Target storage is not unlocked for this account
	NEED_STORAGE_SHOP_PREPARE_BUSY = -2,    ///< Trading, vending, a storage window is open, ...
	NEED_STORAGE_SHOP_PREPARE_FAILED = -3,  ///< Load request failed or timed out
};

int32 need_storage_shop_category_count();
int32 need_storage_shop_item_count( int32 category );
t_itemid need_storage_shop_item_at( int32 category, int32 index );
int32 need_storage_shop_price( t_itemid nameid );
int32 need_storage_shop_max_quantity( t_itemid nameid );

bool need_storage_shop_is_unlocked( map_session_data* sd, int32 stor_id );
int32 need_storage_shop_get_target( map_session_data* sd, bool* out_reset = nullptr );
bool need_storage_shop_set_target( map_session_data* sd, int32 stor_id );
e_need_storage_shop_prepare need_storage_shop_prepare( map_session_data* sd, int32 stor_id );
bool need_storage_shop_consume_silent_load( map_session_data& sd, uint8 stor_id );
bool need_storage_shop_load_in_flight( const map_session_data& sd );
void need_storage_shop_session_end( map_session_data& sd );

e_need_storage_shop_result need_storage_shop_buy( map_session_data* sd, int32 stor_id, t_itemid nameid, int32 amount, int32* out_price, int32* out_total, int32* out_amount );

#endif  // MAP_NEED_STORAGE_SHOP_HPP
