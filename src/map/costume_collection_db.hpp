// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef COSTUME_COLLECTION_DB_HPP
#define COSTUME_COLLECTION_DB_HPP

#include <common/cbasetypes.hpp>
#include <common/mmo.hpp>

class map_session_data;

bool costume_collection_db_load(map_session_data* sd);
void costume_collection_db_clear(map_session_data* sd);

bool costume_collection_is_registered(map_session_data* sd, uint32 collection_id);
bool costume_collection_is_registered_item(map_session_data* sd, t_itemid item_id);
int32 costume_collection_register(map_session_data* sd, t_itemid item_id);
uint32 costume_collection_get_register_count(map_session_data* sd);

#endif /* COSTUME_COLLECTION_DB_HPP */
