// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "costume_collection.hpp"

#include <algorithm>
#include <cstdio>
#include <inttypes.h>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <ryml.hpp>
#include <ryml_std.hpp>

#include <common/malloc.hpp>
#include <common/showmsg.hpp>

#include "map.hpp"

static const char* NEED_COSTUME_COLLECTION_TYPE = "NEED_COSTUME_COLLECTION_DB";
static const uint16 NEED_COSTUME_COLLECTION_VERSION = 1;

CostumeCollectionDatabase costume_collection_db;

static bool costume_collection_file_exists(const std::string& path)
{
	FILE* fp = fopen(path.c_str(), "r");

	if (fp == nullptr)
		return false;

	fclose(fp);
	return true;
}

static std::string costume_collection_default_path()
{
	const std::string import_path = "db/import/costume_collection_master.yml";

	if (costume_collection_file_exists(import_path))
		return import_path;

	return std::string(db_path) + "/costume_collection_master.yml";
}

static bool costume_collection_node_exists(const ryml::NodeRef& node, const char* name)
{
	return node.num_children() > 0 && node.has_child(c4::to_csubstr(name));
}

template <typename T>
static bool costume_collection_read_node(const ryml::NodeRef& node, const char* name, T& out, const std::string& path)
{
	if (!costume_collection_node_exists(node, name)) {
		ShowError("Missing Costume Collection node \"%s\" in '%s'.\n", name, path.c_str());
		return false;
	}

	const ryml::NodeRef& value_node = node[c4::to_csubstr(name)];

	if (value_node.val_is_null()) {
		ShowError("Costume Collection node \"%s\" is missing a value in '%s'.\n", name, path.c_str());
		return false;
	}

	try {
		value_node >> out;
	} catch (const std::runtime_error&) {
		ShowError("Costume Collection node \"%s\" has an invalid value in '%s'.\n", name, path.c_str());
		return false;
	}

	return true;
}

bool CostumeCollectionDatabase::load()
{
	const std::string path = costume_collection_default_path();

	ShowStatus("Loading '" CL_WHITE "%s" CL_RESET "'..." CL_CLL "\r", path.c_str());

	FILE* fp = fopen(path.c_str(), "r");

	if (fp == nullptr) {
		ShowError("Failed to open Costume Collection database file from '" CL_WHITE "%s" CL_RESET "'.\n", path.c_str());
		return false;
	}

	fseek(fp, 0, SEEK_END);
	const size_t size = ftell(fp);
	char* buffer = static_cast<char*>(aMalloc(size + 1));
	rewind(fp);

	const size_t read_size = fread(buffer, sizeof(char), size, fp);
	buffer[read_size] = '\0';
	fclose(fp);

	ryml::Parser parser;
	ryml::Tree tree;

	try {
		tree = parser.parse_in_arena(c4::to_csubstr(path), c4::to_csubstr(buffer));
	} catch (const std::runtime_error& e) {
		ShowError("Failed to load Costume Collection database file from '" CL_WHITE "%s" CL_RESET "'.\n", path.c_str());
		ShowError("There is likely a syntax error in the file.\n");
		ShowError("Error message: %s\n", e.what());
		aFree(buffer);
		return false;
	}

	if (!costume_collection_node_exists(tree.rootref(), "Header")) {
		ShowError("No Costume Collection database \"Header\" was found.\n");
		aFree(buffer);
		return false;
	}

	const ryml::NodeRef& header = tree["Header"];
	std::string type;
	uint16 version = 0;
	uint32 last_collection_id = 0;

	if (!costume_collection_read_node(header, "Type", type, path) ||
		!costume_collection_read_node(header, "Version", version, path) ||
		!costume_collection_read_node(header, "LastCollectionID", last_collection_id, path)) {
		aFree(buffer);
		return false;
	}

	if (type != NEED_COSTUME_COLLECTION_TYPE) {
		ShowError("Database type mismatch: %s != %s.\n", NEED_COSTUME_COLLECTION_TYPE, type.c_str());
		aFree(buffer);
		return false;
	}

	if (version != NEED_COSTUME_COLLECTION_VERSION) {
		ShowError("Database version %hu is not supported. Supported version is: %hu\n", version, NEED_COSTUME_COLLECTION_VERSION);
		aFree(buffer);
		return false;
	}

	if (!costume_collection_node_exists(tree.rootref(), "Body")) {
		ShowError("No Costume Collection database \"Body\" was found.\n");
		aFree(buffer);
		return false;
	}

	std::unordered_map<t_itemid, s_costume_collection> item_map;
	std::unordered_set<uint32> collection_ids;
	uint32 max_collection_id = 0;
	uint32 active_count = 0;
	uint64 count = 0;
	bool failed = false;
	const ryml::NodeRef& body = tree["Body"];

	item_map.reserve(body.num_children());
	collection_ids.reserve(body.num_children());

	for (const ryml::NodeRef& node : body) {
		s_costume_collection entry = {};

		if (!costume_collection_read_node(node, "CollectionID", entry.collection_id, path) ||
			!costume_collection_read_node(node, "ItemID", entry.item_id, path) ||
			!costume_collection_read_node(node, "Name", entry.name, path) ||
			!costume_collection_read_node(node, "Enabled", entry.enabled, path)) {
			failed = true;
			break;
		}

		if (entry.collection_id < 1) {
			ShowError("Invalid Costume CollectionID %u\n", entry.collection_id);
			failed = true;
			break;
		}

		if (entry.item_id == 0) {
			ShowError("Invalid Costume ItemID %u\n", static_cast<uint32>(entry.item_id));
			failed = true;
			break;
		}

		if (collection_ids.find(entry.collection_id) != collection_ids.end()) {
			ShowError("Duplicate Costume CollectionID %u\n", entry.collection_id);
			failed = true;
			break;
		}

		if (item_map.find(entry.item_id) != item_map.end()) {
			ShowError("Duplicate Costume ItemID %u\n", static_cast<uint32>(entry.item_id));
			failed = true;
			break;
		}

		collection_ids.insert(entry.collection_id);
		max_collection_id = std::max(max_collection_id, entry.collection_id);
		if (entry.enabled)
			++active_count;
		item_map.emplace(entry.item_id, std::move(entry));
		++count;
	}

	if (!failed && last_collection_id < max_collection_id) {
		ShowError("Costume LastCollectionID %u is lower than maximum CollectionID %u\n", last_collection_id, max_collection_id);
		failed = true;
	}

	if (failed) {
		aFree(buffer);
		return false;
	}

	this->m_item_map = std::move(item_map);
	this->m_collection_map.clear();
	this->m_collection_map.reserve(this->m_item_map.size());

	for (auto& pair : this->m_item_map)
		this->m_collection_map.emplace(pair.second.collection_id, &pair.second);

	this->m_last_collection_id = last_collection_id;
	this->m_active_count = active_count;

	ShowStatus(
		"Done reading '" CL_WHITE "%" PRIu64 CL_RESET "' costume collections. LastCollectionID=%u\n",
		count,
		this->m_last_collection_id
	);

	aFree(buffer);
	return true;
}

bool CostumeCollectionDatabase::reload()
{
	return this->load();
}

void CostumeCollectionDatabase::clear()
{
	this->m_collection_map.clear();
	this->m_item_map.clear();
	this->m_last_collection_id = 0;
	this->m_active_count = 0;
}

const s_costume_collection* CostumeCollectionDatabase::find_by_itemid(t_itemid item_id) const
{
	const auto it = this->m_item_map.find(item_id);

	if (it == this->m_item_map.end())
		return nullptr;

	return &it->second;
}

const s_costume_collection* CostumeCollectionDatabase::find_by_collectionid(uint32 collection_id) const
{
	const auto it = this->m_collection_map.find(collection_id);

	if (it == this->m_collection_map.end())
		return nullptr;

	return it->second;
}

uint32 CostumeCollectionDatabase::get_last_collection_id() const
{
	return this->m_last_collection_id;
}

uint32 CostumeCollectionDatabase::get_active_count() const
{
	return this->m_active_count;
}

const s_costume_collection* costume_collection_search_itemid(t_itemid item_id)
{
	return costume_collection_db.find_by_itemid(item_id);
}

const s_costume_collection* costume_collection_search_collectionid(uint32 collection_id)
{
	return costume_collection_db.find_by_collectionid(collection_id);
}

uint32 costume_collection_get_last_collection_id()
{
	return costume_collection_db.get_last_collection_id();
}

uint32 costume_collection_get_active_count()
{
	return costume_collection_db.get_active_count();
}

bool costume_collection_reload()
{
	return costume_collection_db.reload();
}

void do_init_costume_collection(void)
{
	costume_collection_db.load();
}

void do_final_costume_collection(void)
{
	costume_collection_db.clear();
}
