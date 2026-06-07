// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef COSTUME_COLLECTION_HPP
#define COSTUME_COLLECTION_HPP

#include <string>
#include <unordered_map>

#include <common/cbasetypes.hpp>
#include <common/mmo.hpp>

struct s_costume_collection
{
	uint32 collection_id;
	t_itemid item_id;
	std::string name;
	bool enabled;
};

class CostumeCollectionDatabase
{
public:
	bool load();
	bool reload();
	void clear();

	const s_costume_collection* find_by_itemid(t_itemid item_id) const;
	const s_costume_collection* find_by_collectionid(uint32 collection_id) const;

	uint32 get_last_collection_id() const;
	uint32 get_active_count() const;

private:
	std::unordered_map<t_itemid, s_costume_collection> m_item_map;
	std::unordered_map<uint32, s_costume_collection*> m_collection_map;

	uint32 m_last_collection_id = 0;
	uint32 m_active_count = 0;
};

extern CostumeCollectionDatabase costume_collection_db;

const s_costume_collection* costume_collection_search_itemid(t_itemid item_id);
const s_costume_collection* costume_collection_search_collectionid(uint32 collection_id);
uint32 costume_collection_get_last_collection_id();
uint32 costume_collection_get_active_count();
bool costume_collection_reload();

void do_init_costume_collection(void);
void do_final_costume_collection(void);

#endif /* COSTUME_COLLECTION_HPP */
