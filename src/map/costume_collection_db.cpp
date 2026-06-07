// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "costume_collection_db.hpp"

#include <cstdlib>

#include <common/showmsg.hpp>
#include <common/sql.hpp>

#include "costume_collection.hpp"
#include "map.hpp"
#include "pc.hpp"

static const char* NEED_COSTUME_COLLECTION_TABLE = "need_costume_collection";

bool costume_collection_db_load(map_session_data* sd)
{
	if (sd == nullptr)
		return false;

	costume_collection_db_clear(sd);

	if (SQL_ERROR == Sql_Query(
		mmysql_handle,
		"SELECT `collection_id`, `item_id` FROM `%s` WHERE `account_id` = '%u'",
		NEED_COSTUME_COLLECTION_TABLE,
		sd->status.account_id
	)) {
		Sql_ShowDebug(mmysql_handle);
		return false;
	}

	char* data = nullptr;

	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		uint32 collection_id = 0;
		t_itemid item_id = 0;

		Sql_GetData(mmysql_handle, 0, &data, nullptr);
		if (data == nullptr)
			continue;
		collection_id = static_cast<uint32>(strtoul(data, nullptr, 10));

		Sql_GetData(mmysql_handle, 1, &data, nullptr);
		if (data == nullptr)
			continue;
		item_id = static_cast<t_itemid>(strtoul(data, nullptr, 10));

		if (collection_id == 0 || item_id == 0)
			continue;

		sd->costume_collection.registered_collections.insert(collection_id);
		sd->costume_collection.registered_items.insert(item_id);
	}

	Sql_FreeResult(mmysql_handle);
	return true;
}

void costume_collection_db_clear(map_session_data* sd)
{
	if (sd == nullptr)
		return;

	sd->costume_collection.registered_collections.clear();
	sd->costume_collection.registered_items.clear();
}

bool costume_collection_is_registered(map_session_data* sd, uint32 collection_id)
{
	if (sd == nullptr || collection_id == 0)
		return false;

	const s_costume_collection* costume = costume_collection_search_collectionid(collection_id);

	if (costume == nullptr || !costume->enabled)
		return false;

	return sd->costume_collection.registered_collections.find(collection_id) != sd->costume_collection.registered_collections.end();
}

bool costume_collection_is_registered_item(map_session_data* sd, t_itemid item_id)
{
	if (sd == nullptr || item_id == 0)
		return false;

	const s_costume_collection* costume = costume_collection_search_itemid(item_id);

	if (costume == nullptr || !costume->enabled)
		return false;

	return sd->costume_collection.registered_items.find(item_id) != sd->costume_collection.registered_items.end();
}

int32 costume_collection_register(map_session_data* sd, t_itemid item_id)
{
	if (sd == nullptr || item_id == 0)
		return 0;

	const s_costume_collection* costume = costume_collection_search_itemid(item_id);

	if (costume == nullptr || !costume->enabled || costume->collection_id == 0)
		return 0;

	if (costume_collection_is_registered_item(sd, item_id) ||
		costume_collection_is_registered(sd, costume->collection_id)) {
		return 2;
	}

	if (SQL_ERROR == Sql_Query(
		mmysql_handle,
		"INSERT INTO `%s` (`account_id`, `collection_id`, `item_id`) VALUES ('%u', '%u', '%u')",
		NEED_COSTUME_COLLECTION_TABLE,
		sd->status.account_id,
		costume->collection_id,
		static_cast<uint32>(item_id)
	)) {
		Sql_ShowDebug(mmysql_handle);
		return -1;
	}

	sd->costume_collection.registered_collections.insert(costume->collection_id);
	sd->costume_collection.registered_items.insert(item_id);

	return 1;
}

uint32 costume_collection_get_register_count(map_session_data* sd)
{
	if (sd == nullptr)
		return 0;

	return static_cast<uint32>(sd->costume_collection.registered_collections.size());
}
