// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED storage supply shop: bulk purchase delivered straight to the personal storage.

#ifndef MAP_NEED_STORAGE_SHOP_HPP
#define MAP_NEED_STORAGE_SHOP_HPP

#include <common/cbasetypes.hpp>
#include <common/mmo.hpp>

class map_session_data;

/// Hard cap of a single purchase, used when an item does not define its own limit.
constexpr int32 NEED_STORAGE_SHOP_MAX_QUANTITY = 30000;

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
};

int32 need_storage_shop_category_count();
int32 need_storage_shop_item_count( int32 category );
t_itemid need_storage_shop_item_at( int32 category, int32 index );
int32 need_storage_shop_price( t_itemid nameid );
int32 need_storage_shop_max_quantity( t_itemid nameid );
e_need_storage_shop_result need_storage_shop_buy( map_session_data* sd, t_itemid nameid, int32 amount, int32* out_price, int32* out_total, int32* out_amount );

#endif  // MAP_NEED_STORAGE_SHOP_HPP
