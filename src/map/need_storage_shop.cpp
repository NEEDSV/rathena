// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED storage supply shop: bulk purchase delivered straight to the personal storage.

#include "need_storage_shop.hpp"

#include <cstring>
#include <memory>
#include <unordered_map>

#include <common/showmsg.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "intif.hpp"
#include "itemdb.hpp"
#include "log.hpp"
#include "map.hpp"
#include "pc.hpp"
#include "script.hpp"
#include "storage.hpp"

namespace {

/**
 * One sellable entry of the storage supply shop.
 * price   : 0 -> the Buy price of item_db is used, otherwise this exact price.
 * max_qty : 0 -> NEED_STORAGE_SHOP_MAX_QUANTITY is used.
 * enabled : false -> the item stays configured but is hidden and refused.
 *
 * The discount of the merchant classes is never applied here, see
 * need_storage_shop_price(): the price is read straight from item_db or from
 * the override, pc_modifybuyvalue() is intentionally not part of this path.
 */
struct shop_item {
	t_itemid nameid;
	bool enabled;
	int32 max_qty;
	int32 price;
};

struct shop_category {
	const char* id;
	const shop_item* items;
	size_t count;
};

// ---------------------------------------------------------------------------
// Configuration - edit the tables below to change what the shop sells.
// A price override is mandatory whenever the item_db Buy price is only a
// placeholder and the regular NPC shops sell the item for more (rune stones,
// poison herbs, shards, ...), otherwise the supply shop would undercut them.
// ---------------------------------------------------------------------------

constexpr shop_item ITEMS_RECOVERY[] = {
	{ 501, true, 0, 0 },    // Red Potion
	{ 503, true, 0, 0 },    // Yellow Potion
	{ 504, true, 0, 0 },    // White Potion
	{ 506, true, 0, 0 },    // Green Potion
	{ 601, true, 0, 0 },    // Fly Wing
};

constexpr shop_item ITEMS_PHARMACY[] = {
	{ 713, true, 0, 0 },       // Empty Bottle
	{ 717, true, 0, 0 },       // Blue Gemstone
	{ 1092, true, 0, 0 },      // Empty Test Tube
	{ 1093, true, 0, 0 },      // Empty Potion Bottle
	{ 7134, true, 0, 0 },      // Medicine Bowl
	{ 6297, true, 0, 0 },      // Bottle to Throw
	{ 7141, true, 0, 0 },      // Yggdrasilberry Dew
	{ 7143, true, 0, 0 },      // Life Force Pot
	{ 6360, true, 0, 200 },    // Scarlet Point       (Shard_Seller price)
	{ 6361, true, 0, 200 },    // Indigo Point        (Shard_Seller price)
	{ 6362, true, 0, 200 },    // Yellow Wish Point   (Shard_Seller price)
	{ 6363, true, 0, 200 },    // Lime Green Point    (Shard_Seller price)
};

constexpr shop_item ITEMS_SKILL[] = {
	{ 11513, true, 0, 0 },       // Protect Neck Candy
	{ 1065, true, 0, 0 },        // Booby Trap
	{ 7940, true, 0, 0 },        // Special Alloy Trap
	{ 12737, true, 0, 1000 },    // Ordinary Rune Stone (Runes_Seller price)
	{ 12734, true, 0, 2500 },    // Quality Rune Stone  (Runes_Seller price)
	{ 12738, true, 0, 7500 },    // Rare Rune Stone     (Runes_Seller price)
	{ 12735, true, 0, 18000 },   // Ancient Rune Stone  (Runes_Seller price)
	{ 12736, true, 0, 50000 },   // Mystic Rune Stone   (Runes_Seller price)
	{ 7938, true, 0, 10000 },    // Light Granule       (Runes_Seller price)
	{ 7939, true, 0, 20000 },    // Elder Branch        (Runes_Seller price)
	{ 7931, true, 0, 5000 },     // Poison Kit          (NEED tool shop price)
	{ 7932, true, 0, 4000 },     // Poison Herb Nerium  (Herb_Seller price)
	{ 7933, true, 0, 4000 },     // Poison Herb Rantana (Herb_Seller price)
	{ 7934, true, 0, 4000 },     // Poison Herb Makulata
	{ 7935, true, 0, 4000 },     // Poison Herb Seratum
	{ 7936, true, 0, 4000 },     // Poison Herb Scopolia
	{ 7937, true, 0, 4000 },     // Poison Herb Amoena
	{ 6512, true, 0, 0 },        // Fire Charm
	{ 6513, true, 0, 0 },        // Ice Charm
	{ 6514, true, 0, 0 },        // Wind Charm
	{ 6515, true, 0, 0 },        // Earth Charm
	{ 7521, true, 0, 0 },        // Flame Stone
	{ 7522, true, 0, 0 },        // Ice Stone
	{ 7523, true, 0, 0 },        // Wind Stone
	{ 7524, true, 0, 0 },        // Shadow Orb
	{ 1000565, true, 0, 0 },     // Pitch Black Haze
	{ 1000566, true, 0, 0 },     // Crimson Flame Haze
	{ 1000567, true, 0, 0 },     // Frost Haze
	{ 1000568, true, 0, 0 },     // Earth Haze
	{ 1000569, true, 0, 0 },     // North Wind Haze
	{ 6146, true, 0, 0 },        // Magic Gear Fuel
};

constexpr shop_item ITEMS_ARROW[] = {
	{ 1750, true, 0, 0 },    // Arrow
	{ 1770, true, 0, 0 },    // Iron Arrow
	{ 1751, true, 0, 0 },    // Silver Arrow
	{ 1752, true, 0, 0 },    // Fire Arrow
	{ 1754, true, 0, 0 },    // Crystal Arrow
	{ 1755, true, 0, 0 },    // Arrow of Wind
	{ 1756, true, 0, 0 },    // Stone Arrow
	{ 1757, true, 0, 0 },    // Immaterial Arrow
	{ 1762, true, 0, 0 },    // Rusty Arrow
	{ 1767, true, 0, 0 },    // Arrow of Shadow
	{ 1773, true, 0, 0 },    // Arrow of Elf
};

constexpr shop_item ITEMS_BULLET[] = {
	{ 13200, true, 0, 0 },    // Bullet
	{ 13221, true, 0, 0 },    // Silver Bullet
	{ 13222, true, 0, 0 },    // Shell of Blood
	{ 13215, true, 0, 0 },    // AP Ammo
	{ 13216, true, 0, 0 },    // Blaze Bullet
	{ 13217, true, 0, 0 },    // Freezing Bullet
	{ 13218, true, 0, 0 },    // Electric Shock Bullet
	{ 13219, true, 0, 0 },    // Magical Stone Bullet
	{ 13220, true, 0, 0 },    // Sanctified Bullet
	{ 13228, true, 0, 0 },    // Flare Bullet
	{ 13229, true, 0, 0 },    // Lightning Bullet
	{ 13230, true, 0, 0 },    // Ice Bullet
	{ 13231, true, 0, 0 },    // Poison Bullet
	{ 13232, true, 0, 0 },    // Blind Bullet
	{ 6145, true, 0, 0 },     // Vulcan Bullet
	{ 6147, true, 0, 0 },     // Liquid Condensed Bullet
	{ 7663, true, 0, 0 },     // Full Metal Jacket
	{ 7664, true, 0, 0 },     // Shooting Mine
	{ 7665, true, 0, 0 },     // Dragon Tail Missile
};

constexpr shop_item ITEMS_CANNONBALL[] = {
	{ 18000, true, 0, 0 },    // Cannon Ball
	{ 18001, true, 0, 0 },    // Holy Cannon Ball
	{ 18002, true, 0, 0 },    // Dark Cannon Ball
	{ 18003, true, 0, 0 },    // Soul Cannon Ball
	{ 18004, true, 0, 0 },    // Iron Cannon Ball
	{ 18005, true, 0, 0 },    // Ice Cannon Ball
	{ 18006, true, 0, 0 },    // Lightning Cannon Ball
	{ 18007, true, 0, 0 },    // Stone Cannon Ball
	{ 18008, true, 0, 0 },    // Flare Cannon Ball
};

constexpr shop_item ITEMS_MISC_AMMO[] = {
	{ 13250, true, 0, 0 },    // Shuriken
	{ 13251, true, 0, 0 },    // Nimbus Shuriken
	{ 13252, true, 0, 0 },    // Flash Shuriken
	{ 13253, true, 0, 0 },    // Sharp Leaf Shuriken
	{ 13254, true, 0, 0 },    // Thorn Needle Shuriken
	{ 13255, true, 0, 0 },    // Kunai of Icicle
	{ 13256, true, 0, 0 },    // Kunai of Black Soil
	{ 13257, true, 0, 0 },    // Kunai of Furious Wind
	{ 13258, true, 0, 0 },    // Kunai of Fierce Flame
	{ 13259, true, 0, 0 },    // Kunai of Deadly Poison
	{ 13294, true, 0, 0 },    // Explosive Kunai
	{ 6493, true, 0, 0 },     // Makibishi
};

#define SHOP_CATEGORY( id, table ) { id, table, sizeof( table ) / sizeof( table[0] ) }

// The order of this table is the order of the NPC menu, the NPC script keeps
// the display names at the very same indexes.
constexpr shop_category CATEGORIES[] = {
	SHOP_CATEGORY( "RECOVERY", ITEMS_RECOVERY ),
	SHOP_CATEGORY( "PHARMACY", ITEMS_PHARMACY ),
	SHOP_CATEGORY( "SKILL", ITEMS_SKILL ),
	SHOP_CATEGORY( "ARROW", ITEMS_ARROW ),
	SHOP_CATEGORY( "BULLET", ITEMS_BULLET ),
	SHOP_CATEGORY( "CANNONBALL", ITEMS_CANNONBALL ),
	SHOP_CATEGORY( "MISC_AMMO", ITEMS_MISC_AMMO ),
};

#undef SHOP_CATEGORY

constexpr int32 CATEGORY_COUNT = static_cast<int32>( sizeof( CATEGORIES ) / sizeof( CATEGORIES[0] ) );

bool config_checked = false;

/// Returns the configured entry of an item, nullptr when it is not registered.
const shop_item* find_item( t_itemid nameid ) {
	if( nameid == 0 )
		return nullptr;

	for( int32 i = 0; i < CATEGORY_COUNT; i++ ) {
		const shop_category& category = CATEGORIES[i];

		for( size_t j = 0; j < category.count; j++ ) {
			if( category.items[j].nameid == nameid )
				return &category.items[j];
		}
	}

	return nullptr;
}

/// Price of a configured entry, 0 when the item cannot be priced.
int32 entry_price( const shop_item& entry ) {
	if( entry.price > 0 )
		return entry.price;

	std::shared_ptr<item_data> id = item_db.find( entry.nameid );

	if( id == nullptr )
		return 0;

	// Straight from item_db, never through pc_modifybuyvalue().
	return id->value_buy;
}

int32 entry_max_quantity( const shop_item& entry ) {
	int32 max = ( entry.max_qty > 0 ) ? entry.max_qty : NEED_STORAGE_SHOP_MAX_QUANTITY;

	return ( max > NEED_STORAGE_SHOP_MAX_QUANTITY ) ? NEED_STORAGE_SHOP_MAX_QUANTITY : max;
}

/**
 * One time sanity check of the tables above, so a broken configuration shows up
 * in the console instead of silently disappearing from the menu.
 * Runs on the first access, item_db is fully loaded by then.
 */
void check_config() {
	if( config_checked )
		return;

	config_checked = true;

	for( int32 i = 0; i < CATEGORY_COUNT; i++ ) {
		const shop_category& category = CATEGORIES[i];

		for( size_t j = 0; j < category.count; j++ ) {
			const shop_item& entry = category.items[j];

			if( !entry.enabled )
				continue;

			std::shared_ptr<item_data> id = item_db.find( entry.nameid );

			if( id == nullptr ) {
				ShowWarning( "need_storage_shop: unknown item %u in category %s.\n", entry.nameid, category.id );
				continue;
			}

			if( !itemdb_isstackable2( id.get() ) )
				ShowWarning( "need_storage_shop: item %u (%s) is not stackable and cannot be sold in bulk.\n", entry.nameid, category.id );

			if( id->flag.guid )
				ShowWarning( "need_storage_shop: item %u (%s) uses unique ids and cannot be sold in bulk.\n", entry.nameid, category.id );

			if( id->flag.trade_restriction.storage )
				ShowWarning( "need_storage_shop: item %u (%s) cannot be kept in the storage.\n", entry.nameid, category.id );

			if( entry_price( entry ) <= 0 )
				ShowWarning( "need_storage_shop: item %u (%s) has no buy price, add a price override.\n", entry.nameid, category.id );
		}
	}
}

// ---------------------------------------------------------------------------
// Target storage (storage 1 / 2 / 3)
//
// storage 1 -> stor_id 0, sd->storage, always available
// storage 2 -> stor_id 1, premium storage, unlocked by #NEED_STORAGE2_UNLOCK
// storage 3 -> stor_id 2, premium storage, unlocked by #NEED_STORAGE3_UNLOCK
//
// The unlock variables are the very ones the existing NEED storage NPC
// (npc/NEED/Storage_Command.txt) sets when the account pays cash for the extra
// storage, so the shop reuses that permission instead of inventing its own.
// ---------------------------------------------------------------------------

constexpr const char* UNLOCK_VARS[] = {
	nullptr,                    // storage 1 needs no unlock
	"#NEED_STORAGE2_UNLOCK",    // storage 2
	"#NEED_STORAGE3_UNLOCK",    // storage 3
};

/**
 * A premium storage load/save the supply shop started itself, keyed by account id.
 * At most one entry per account exists, so only one request is ever in flight.
 * A load entry is erased when its response is consumed (or when the player logs
 * out), never on a timeout: the char server cannot be told to forget the request,
 * so the entry has to outlive it to absorb a late response.
 * `expire` only decides when need_storage_shop_prepare() starts reporting FAILED.
 */
struct pending_load {
	uint8 stor_id;
	bool saving;      // true while waiting for the previously held storage to be saved
	t_tick expire;
};

std::unordered_map<uint32, pending_load> pending_loads;

constexpr t_tick PENDING_TIMEOUT = 10000;  // ms

bool valid_storage_id( int32 stor_id ) {
	return stor_id >= 0 && stor_id <= NEED_STORAGE_SHOP_STORAGE_MAX;
}

/**
 * The storage the shop should write into, or nullptr when it is not usable right
 * now. A premium storage must be the one currently held by sd->premiumStorage and
 * must already carry the data the char server sent.
 */
s_storage* resolve_storage( map_session_data* sd, int32 stor_id ) {
	if( sd == nullptr || !valid_storage_id( stor_id ) )
		return nullptr;

	if( stor_id == 0 ){
		// state.put is only set once the char server has sent the storage over
		return sd->storage.state.put ? &sd->storage : nullptr;
	}

	if( sd->premiumStorage.stor_id != static_cast<uint8>( stor_id ) )
		return nullptr;

	return sd->premiumStorage.state.put ? &sd->premiumStorage : nullptr;
}

/// Shared "the player must not touch a storage right now" test.
bool player_busy( map_session_data* sd ) {
	if( sd->state.trading || sd->state.vending || sd->state.buyingstore || sd->state.prevend || sd->state.autotrade )
		return true;

	if( sd->state.mail_writing || sd->state.banking )
		return true;

	// Any open storage window (own, guild or premium) blocks the purchase.
	if( sd->state.storage_flag != 0 )
		return true;

	return !pc_can_give_items( sd );
}

}  // namespace

/**
 * Is the given storage unlocked for this account?
 * Storage 1 always is; storage 2 and 3 reuse the account variables the existing
 * NEED storage NPC sets when the account unlocks them with cash.
 */
bool need_storage_shop_is_unlocked( map_session_data* sd, int32 stor_id ) {
	if( sd == nullptr || !valid_storage_id( stor_id ) )
		return false;

	if( stor_id == 0 )
		return true;

	// The storage table itself has to exist as well (conf/inter_server.yml).
	if( !storage_exists( static_cast<uint8>( stor_id ) ) )
		return false;

	const char* var = UNLOCK_VARS[stor_id];

	if( var == nullptr )
		return false;

	return pc_readaccountreg( sd, add_str( var ) ) != 0;
}

/**
 * The storage the shop currently delivers to.
 * A stored choice the account may no longer use falls back to storage 1, and the
 * stored value is rewritten so the fallback is not repeated.
 * @param out_reset : set to true when such a fallback happened (optional)
 */
int32 need_storage_shop_get_target( map_session_data* sd, bool* out_reset ) {
	if( out_reset != nullptr )
		*out_reset = false;

	if( sd == nullptr )
		return 0;

	int32 stor_id = static_cast<int32>( pc_readaccountreg( sd, add_str( NEED_STORAGE_SHOP_TARGET_VAR ) ) );

	if( valid_storage_id( stor_id ) && need_storage_shop_is_unlocked( sd, stor_id ) )
		return stor_id;

	if( stor_id != 0 ) {
		pc_setaccountreg( sd, add_str( NEED_STORAGE_SHOP_TARGET_VAR ), 0 );

		if( out_reset != nullptr )
			*out_reset = true;
	}

	return 0;
}

/// Remember the storage the shop delivers to. Fails for a locked storage.
bool need_storage_shop_set_target( map_session_data* sd, int32 stor_id ) {
	if( sd == nullptr || !need_storage_shop_is_unlocked( sd, stor_id ) )
		return false;

	pc_setaccountreg( sd, add_str( NEED_STORAGE_SHOP_TARGET_VAR ), stor_id );

	return true;
}

/**
 * Make sure the target storage is loaded and writable before any zeny is taken.
 * Storage 1 is always resident; storage 2 and 3 are pulled from the char server in
 * the background, so the caller polls this until it stops returning WAITING.
 *
 * At most one premium storage load per account is ever in flight, and such a load
 * is never abandoned: intif_storage_request() cannot be recalled, so the pending
 * entry lives until the matching response has been consumed silently. Switching
 * target or running into the timeout therefore does not drop the entry, which is
 * what keeps a late response from being mistaken for a normal storage request and
 * popping the storage window.
 * @see e_need_storage_shop_prepare
 */
e_need_storage_shop_prepare need_storage_shop_prepare( map_session_data* sd, int32 stor_id ) {
	if( sd == nullptr )
		return NEED_STORAGE_SHOP_PREPARE_FAILED;

	if( !valid_storage_id( stor_id ) || !need_storage_shop_is_unlocked( sd, stor_id ) )
		return NEED_STORAGE_SHOP_PREPARE_LOCKED;

	if( player_busy( sd ) )
		return NEED_STORAGE_SHOP_PREPARE_BUSY;

	// Storage 1 lives in sd->storage and is never touched by a premium load, so it
	// must not be held up by one either.
	if( stor_id == 0 ) {
		if( resolve_storage( sd, 0 ) != nullptr )
			return NEED_STORAGE_SHOP_PREPARE_READY;

		// Requested at login and never swapped out, so it only takes a moment.
		return NEED_STORAGE_SHOP_PREPARE_WAITING;
	}

	t_tick now = gettick();
	auto it = pending_loads.find( sd->status.account_id );

	if( it != pending_loads.end() ) {
		pending_load& pending = it->second;

		if( !pending.saving ) {
			// A load is in flight. It cannot be cancelled, so wait for it even when
			// the player picked a different storage meanwhile: the response is
			// consumed silently and the next call issues the new request. Never send
			// a second load, not even after the timeout, or the two responses would
			// race over sd->premiumStorage.
			if( DIFF_TICK( now, pending.expire ) > 0 )
				return NEED_STORAGE_SHOP_PREPARE_FAILED;

			return NEED_STORAGE_SHOP_PREPARE_WAITING;
		}

		if( sd->premiumStorage.dirty ) {
			// Still waiting for the save acknowledgement. Saving is idempotent and
			// never overwrites memory, so a lost acknowledgement may be retried.
			if( DIFF_TICK( now, pending.expire ) > 0 ) {
				pending.expire = now + PENDING_TIMEOUT;
				storage_premiumStorage_save( sd );
			}

			return NEED_STORAGE_SHOP_PREPARE_WAITING;
		}

		// The storage held before is safely on the char server now.
		pending_loads.erase( it );
	}

	if( resolve_storage( sd, stor_id ) != nullptr )
		return NEED_STORAGE_SHOP_PREPARE_READY;

	// Swapping premium storages overwrites sd->premiumStorage wholesale, so any
	// unsaved change of the storage currently held has to reach the char server
	// first or it would be lost.
	if( sd->premiumStorage.stor_id != static_cast<uint8>( stor_id ) && sd->premiumStorage.dirty ) {
		pending_load saving = {};

		saving.stor_id = static_cast<uint8>( stor_id );
		saving.saving = true;
		saving.expire = now + PENDING_TIMEOUT;
		pending_loads[sd->status.account_id] = saving;

		storage_premiumStorage_save( sd );

		return NEED_STORAGE_SHOP_PREPARE_WAITING;
	}

	pending_load loading = {};

	loading.stor_id = static_cast<uint8>( stor_id );
	loading.saving = false;
	loading.expire = now + PENDING_TIMEOUT;
	pending_loads[sd->status.account_id] = loading;

	if( !intif_storage_request( sd, TABLE_STORAGE, static_cast<uint8>( stor_id ), STOR_MODE_ALL ) ) {
		// Nothing left the map server, so no response can arrive for this entry.
		pending_loads.erase( sd->status.account_id );
		return NEED_STORAGE_SHOP_PREPARE_FAILED;
	}

	return NEED_STORAGE_SHOP_PREPARE_WAITING;
}

/**
 * Is a premium storage load the supply shop issued still on its way?
 * While that is the case nothing else may request or write a premium storage for
 * this account, or the two would race over sd->premiumStorage.
 */
bool need_storage_shop_load_in_flight( const map_session_data& sd ) {
	auto it = pending_loads.find( sd.status.account_id );

	return it != pending_loads.end() && !it->second.saving;
}

/**
 * Drop the pending load/save bookkeeping of a leaving player.
 * Called from map_quit(); the account id would otherwise still look busy to the
 * next session of the same account.
 */
void need_storage_shop_session_end( map_session_data& sd ) {
	pending_loads.erase( sd.status.account_id );
}

/**
 * Called from intif_parse_StorageReceived() for premium storages.
 * @return true when this storage was pulled in by the supply shop, in which case
 *         the storage window must NOT be opened.
 */
bool need_storage_shop_consume_silent_load( map_session_data& sd, uint8 stor_id ) {
	auto it = pending_loads.find( sd.status.account_id );

	if( it == pending_loads.end() )
		return false;

	if( it->second.saving || it->second.stor_id != stor_id )
		return false;

	pending_loads.erase( it );

	return true;
}

/// Number of categories of the shop menu.
int32 need_storage_shop_category_count() {
	check_config();

	return CATEGORY_COUNT;
}

/// Number of items that are currently on sale in a category.
int32 need_storage_shop_item_count( int32 category ) {
	check_config();

	if( category < 0 || category >= CATEGORY_COUNT )
		return 0;

	int32 count = 0;

	for( size_t i = 0; i < CATEGORIES[category].count; i++ ) {
		if( CATEGORIES[category].items[i].enabled )
			count++;
	}

	return count;
}

/// Item id at the given position of the on sale list of a category, 0 when out of range.
t_itemid need_storage_shop_item_at( int32 category, int32 index ) {
	check_config();

	if( category < 0 || category >= CATEGORY_COUNT || index < 0 )
		return 0;

	int32 count = 0;

	for( size_t i = 0; i < CATEGORIES[category].count; i++ ) {
		if( !CATEGORIES[category].items[i].enabled )
			continue;

		if( count == index )
			return CATEGORIES[category].items[i].nameid;

		count++;
	}

	return 0;
}

/**
 * Unit price charged by the storage supply shop.
 * This is the plain item_db Buy price (or the configured override), the
 * merchant discount is deliberately not applied here.
 * @return the price, 0 when the item is not on sale
 */
int32 need_storage_shop_price( t_itemid nameid ) {
	check_config();

	const shop_item* entry = find_item( nameid );

	if( entry == nullptr || !entry->enabled )
		return 0;

	return entry_price( *entry );
}

/// Maximum amount of a single purchase, 0 when the item is not on sale.
int32 need_storage_shop_max_quantity( t_itemid nameid ) {
	check_config();

	const shop_item* entry = find_item( nameid );

	if( entry == nullptr || !entry->enabled )
		return 0;

	return entry_max_quantity( *entry );
}

/**
 * Buy an item and put it straight into the requested personal storage.
 * The zeny is only taken once that storage is known to be loaded and to have room
 * for the whole amount, and it is given back when the delivery still ends up
 * incomplete. A locked or not yet loaded storage is refused outright: the purchase
 * never falls back to another storage.
 * @param sd : player
 * @param stor_id : target storage (0 = storage 1, 1 = storage 2, 2 = storage 3)
 * @param nameid : item to buy
 * @param amount : amount to buy
 * @param out_price : filled with the unit price that was charged (optional)
 * @param out_total : filled with the total price that was charged (optional)
 * @param out_amount : filled with the amount that was actually stored (optional)
 * @return @see e_need_storage_shop_result
 */
e_need_storage_shop_result need_storage_shop_buy( map_session_data* sd, int32 stor_id, t_itemid nameid, int32 amount, int32* out_price, int32* out_total, int32* out_amount ) {
	if( out_price != nullptr )
		*out_price = 0;

	if( out_total != nullptr )
		*out_total = 0;

	if( out_amount != nullptr )
		*out_amount = 0;

	if( sd == nullptr )
		return NEED_STORAGE_SHOP_FAIL;

	check_config();

	// 1. is the item on sale?
	const shop_item* entry = find_item( nameid );

	if( entry == nullptr || !entry->enabled )
		return NEED_STORAGE_SHOP_NOT_SOLD;

	std::shared_ptr<item_data> id = item_db.find( nameid );

	if( id == nullptr )
		return NEED_STORAGE_SHOP_NOT_SOLD;

	if( !itemdb_isstackable2( id.get() ) || id->flag.guid )
		return NEED_STORAGE_SHOP_NOT_SOLD;

	// 2. amount within the allowed range?
	if( amount < 1 || amount > entry_max_quantity( *entry ) )
		return NEED_STORAGE_SHOP_BAD_AMOUNT;

	// 3. total price, computed on 64 bits so it cannot wrap around
	int32 price = entry_price( *entry );

	if( price <= 0 )
		return NEED_STORAGE_SHOP_NOT_SOLD;

	int64 total = static_cast<int64>( price ) * static_cast<int64>( amount );

	if( total <= 0 || total > static_cast<int64>( MAX_ZENY ) )
		return NEED_STORAGE_SHOP_OVERFLOW;

	if( out_price != nullptr )
		*out_price = price;

	if( out_total != nullptr )
		*out_total = static_cast<int32>( total );

	// 4. enough zeny?
	if( sd->status.zeny < total )
		return NEED_STORAGE_SHOP_NO_ZENY;

	// 5. is the target storage unlocked, loaded and able to hold everything?
	//    Never silently deliver anywhere else than where the player asked for.
	if( !need_storage_shop_is_unlocked( sd, stor_id ) )
		return NEED_STORAGE_SHOP_STORAGE_LOCKED;

	// A storage window that is already open (own, guild or premium storage) would
	// end up showing stale data, so the purchase waits for it to close. Trading,
	// vending and the like are blocked for the same reason.
	if( player_busy( sd ) )
		return NEED_STORAGE_SHOP_BUSY;

	// A background load would overwrite sd->premiumStorage wholesale once it
	// arrives, so nothing may be written into a premium storage until it landed.
	// Storage 1 is a different buffer and stays usable throughout.
	if( stor_id != 0 && need_storage_shop_load_in_flight( *sd ) )
		return NEED_STORAGE_SHOP_NOT_READY;

	// state.put is only set once the char server has sent the storage over, and a
	// premium storage has to be the one currently held by sd->premiumStorage.
	s_storage* stor = resolve_storage( sd, stor_id );

	if( stor == nullptr )
		return NEED_STORAGE_SHOP_NOT_READY;

	struct item it = {};

	it.nameid = nameid;
	it.identify = 1;

	if( !itemdb_canstore( &it, pc_get_group_level( sd ) ) )
		return NEED_STORAGE_SHOP_NOT_STORABLE;

	if( storage_additem_bulk( sd, stor, &it, amount, true ) < amount )
		return NEED_STORAGE_SHOP_NO_ROOM;

	// 6. take the zeny
	if( pc_payzeny( sd, static_cast<int32>( total ), LOG_TYPE_NPC ) != 0 )
		return NEED_STORAGE_SHOP_NO_ZENY;

	// 7. deliver, and give back whatever could not be delivered after all
	int32 stored = storage_additem_bulk( sd, stor, &it, amount, false );

	if( stored < amount ) {
		int64 refund = static_cast<int64>( price ) * static_cast<int64>( amount - stored );

		pc_getzeny( sd, static_cast<int32>( refund ), LOG_TYPE_NPC );

		ShowError( "need_storage_shop: incomplete delivery for char %d, storage %d, item %u, %d/%d stored, %d zeny refunded.\n",
			sd->status.char_id, stor_id + 1, nameid, stored, amount, static_cast<int32>( refund ) );

		if( stored <= 0 )
			return NEED_STORAGE_SHOP_NO_ROOM;

		amount = stored;
		total -= refund;

		if( out_total != nullptr )
			*out_total = static_cast<int32>( total );
	}

	// The caller reports the amount that really made it into the storage,
	// which is the requested amount unless the refund path above kicked in.
	if( out_amount != nullptr )
		*out_amount = amount;

	// A premium storage only lives in sd->premiumStorage until something saves it,
	// and it is dropped as soon as another storage is loaded into that slot. Push
	// it to the char server right away so a purchase can never be lost.
	if( stor != &sd->storage )
		storage_premiumStorage_save( sd );

	// 8. logs - the zeny movement is already logged by pc_payzeny()
	it.amount = amount;
	log_pick_pc( sd, LOG_TYPE_NPC, amount, &it );

	char message[128];

	safesnprintf( message, sizeof( message ), "storage supply shop: storage=%d item=%u amount=%d unit=%d total=%d",
		stor_id + 1, nameid, amount, price, static_cast<int32>( total ) );
	log_npc( sd, message );

	return NEED_STORAGE_SHOP_OK;
}
