// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "needwiki.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ryml.hpp>
#include <ryml_std.hpp>

#include <common/cbasetypes.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>
#ifdef WIN32
	#include <common/winapi.hpp>

	#ifndef CP_UTF8
		#define CP_UTF8 65001
	#endif

	extern "C" {
		__declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int CodePage, unsigned long dwFlags, const char* lpMultiByteStr, int cbMultiByte, wchar_t* lpWideCharStr, int cchWideChar);
		__declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int CodePage, unsigned long dwFlags, const wchar_t* lpWideCharStr, int cchWideChar, char* lpMultiByteStr, int cbMultiByte, const char* lpDefaultChar, int* lpUsedDefaultChar);
	}
#endif

#include "clif.hpp"
#include "itemdb.hpp"
#include "map.hpp"
#include "pc.hpp"
#include "script.hpp"

static constexpr uint16 NEEDWIKI_PORT = 6905;
static constexpr uint16 NEEDWIKI_CMD_TEST_ACTION = 0x7A01;
static constexpr uint16 NEEDWIKI_ACTION_DISPBOTTOM = 1;
static constexpr uint16 NEEDWIKI_ACTION_NAVI = 2;
static constexpr uint16 NEEDWIKI_ACTION_SHOW_ITEM = 3;
static constexpr uint16 NEEDWIKI_ACTION_SHOW_GROUP = 4;
static constexpr uint16 NEEDWIKI_PACKET_HEADER_LEN = 14;
static constexpr int32 NEEDWIKI_DUPLICATE_WINDOW_MS = 500;
static constexpr int32 NEEDWIKI_DUPLICATE_CLEANUP_MS = 3000;
static constexpr const char* NEEDWIKI_ITEM_GROUP_DB_TYPE = "NEED_WIKI_ITEM_GROUP_DB";
static constexpr uint16 NEEDWIKI_ITEM_GROUP_DB_VERSION = 1;

static int32 needwiki_fd = -1;
static std::unordered_map<std::string, t_tick> needwiki_recent_requests;

struct NeedWikiItemGroup {
	std::string name;
	std::vector<t_itemid> items;
};

static std::unordered_map<std::string, NeedWikiItemGroup> needwiki_item_groups;

static bool needwiki_valid_group_id(const std::string& id)
{
	if (id.empty() || id.size() > 64)
		return false;

	for (const char ch : id) {
		if ((ch < 'a' || ch > 'z') && (ch < 'A' || ch > 'Z') &&
			(ch < '0' || ch > '9') && ch != '_' && ch != '-')
			return false;
	}

	return true;
}

bool needwiki_reload_item_groups(void)
{
	const std::string path = "db/import/wiki_item_group.yml";
	std::ifstream input(path, std::ios::binary);

	if (!input) {
		ShowError("[NeedWiki] Failed to open item group DB '%s'.\n", path.c_str());
		return false;
	}

	std::ostringstream stream;
	stream << input.rdbuf();
	const std::string yaml = stream.str();
	ryml::Parser parser;
	ryml::Tree tree;

	try {
		tree = parser.parse_in_arena(c4::to_csubstr(path), c4::to_csubstr(yaml));
	} catch (const std::runtime_error& error) {
		ShowError("[NeedWiki] Failed to parse item group DB '%s': %s\n", path.c_str(), error.what());
		return false;
	}

	const ryml::NodeRef root = tree.rootref();
	if (!root.has_child("Header")) {
		ShowError("[NeedWiki] Item group DB has no Header node: '%s'.\n", path.c_str());
		return false;
	}

	const ryml::NodeRef header = root["Header"];
	if (!header.has_child("Type") || !header.has_child("Version")) {
		ShowError("[NeedWiki] Item group DB Header requires Type and Version: '%s'.\n", path.c_str());
		return false;
	}

	std::string type;
	uint16 version = 0;
	header["Type"] >> type;
	header["Version"] >> version;

	if (type != NEEDWIKI_ITEM_GROUP_DB_TYPE || version != NEEDWIKI_ITEM_GROUP_DB_VERSION) {
		ShowError("[NeedWiki] Unsupported item group DB header type='%s' version=%hu.\n", type.c_str(), version);
		return false;
	}

	if (!root.has_child("Groups")) {
		ShowError("[NeedWiki] Item group DB has no Groups node: '%s'.\n", path.c_str());
		return false;
	}

	std::unordered_map<std::string, NeedWikiItemGroup> loaded;
	for (const ryml::NodeRef& node : root["Groups"]) {
		if (!node.has_child("Id") || !node.has_child("Name") || !node.has_child("Items")) {
			ShowWarning("[NeedWiki] Skipping item group with missing Id, Name, or Items.\n");
			continue;
		}

		std::string id;
		NeedWikiItemGroup group;
		node["Id"] >> id;
		node["Name"] >> group.name;

		if (!needwiki_valid_group_id(id) || group.name.empty()) {
			ShowWarning("[NeedWiki] Skipping invalid item group id='%s'.\n", id.c_str());
			continue;
		}

		for (const ryml::NodeRef& item_node : node["Items"]) {
			std::string aegis_name;
			item_node >> aegis_name;
			std::shared_ptr<item_data> data = item_db.search_aegisname(aegis_name.c_str());

			if (data == nullptr) {
				ShowWarning("[NeedWiki] Unknown item '%s' in group '%s'.\n", aegis_name.c_str(), id.c_str());
				continue;
			}

			group.items.push_back(data->nameid);
		}

		loaded[id] = std::move(group);
	}

	needwiki_item_groups.swap(loaded);
	ShowInfo("[NeedWiki] Load item group count=%zu\n", needwiki_item_groups.size());
	return true;
}

static std::string needwiki_utf8_to_cp949(const std::string& utf8)
{
#ifdef WIN32
	if (utf8.empty())
		return {};

	const int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);

	if (wide_len <= 0)
		return utf8;

	std::wstring wide(wide_len, L'\0');

	if (MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), wide_len) <= 0)
		return utf8;

	const int cp949_len = WideCharToMultiByte(949, 0, wide.c_str(), wide_len, nullptr, 0, nullptr, nullptr);

	if (cp949_len <= 0)
		return utf8;

	std::string cp949(cp949_len, '\0');

	if (WideCharToMultiByte(949, 0, wide.c_str(), wide_len, cp949.data(), cp949_len, nullptr, nullptr) <= 0)
		return utf8;

	return cp949;
#else
	return utf8;
#endif
}

static std::string needwiki_duplicate_key(uint32 char_id, uint16 action, const std::string& payload)
{
	return std::to_string(char_id) + "|" + std::to_string(action) + "|" + payload;
}

static bool needwiki_is_duplicate_request(uint32 char_id, uint16 action, const std::string& payload)
{
	const t_tick now = gettick();

	for (auto it = needwiki_recent_requests.begin(); it != needwiki_recent_requests.end();) {
		if (DIFF_TICK(now, it->second) > NEEDWIKI_DUPLICATE_CLEANUP_MS)
			it = needwiki_recent_requests.erase(it);
		else
			++it;
	}

	const std::string key = needwiki_duplicate_key(char_id, action, payload);
	auto it = needwiki_recent_requests.find(key);

	if (it != needwiki_recent_requests.end() && DIFF_TICK(now, it->second) <= NEEDWIKI_DUPLICATE_WINDOW_MS)
		return true;

	needwiki_recent_requests[key] = now;
	return false;
}

static bool needwiki_parse_u16(const std::string& value, uint16& out)
{
	if (value.empty())
		return false;

	char* end = nullptr;
	const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);

	if (end == value.c_str() || *end != '\0' || parsed > UINT16_MAX)
		return false;

	out = static_cast<uint16>(parsed);
	return true;
}

static bool needwiki_parse_u32(const std::string& value, uint32& out)
{
	if (value.empty())
		return false;

	char* end = nullptr;
	const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);

	if (end == value.c_str() || *end != '\0' || parsed > UINT32_MAX)
		return false;

	out = static_cast<uint32>(parsed);
	return true;
}

static bool needwiki_parse_navi_payload(const std::string& payload, std::string& mapname, uint16& x, uint16& y, std::string& name)
{
	const size_t first = payload.find('|');
	const size_t second = first == std::string::npos ? std::string::npos : payload.find('|', first + 1);
	const size_t third = second == std::string::npos ? std::string::npos : payload.find('|', second + 1);

	if (first == std::string::npos || second == std::string::npos || third == std::string::npos || payload.find('|', third + 1) != std::string::npos)
		return false;

	mapname = payload.substr(0, first);
	const std::string x_str = payload.substr(first + 1, second - first - 1);
	const std::string y_str = payload.substr(second + 1, third - second - 1);
	name = payload.substr(third + 1);

	if (mapname.empty() || name.empty())
		return false;

	return needwiki_parse_u16(x_str, x) && needwiki_parse_u16(y_str, y);
}

static int32 needwiki_parse(int32 fd)
{
	while (RFIFOREST(fd) >= 4) {
		const uint16 cmd = RFIFOW(fd, 0);
		const uint16 len = RFIFOW(fd, 2);

		if (cmd != NEEDWIKI_CMD_TEST_ACTION || len < NEEDWIKI_PACKET_HEADER_LEN) {
			ShowWarning("NEED Wiki: invalid packet from %d.%d.%d.%d (fd %d).\n", CONVIP(session[fd]->client_addr), fd);
			do_close(fd);
			return 0;
		}

		if (RFIFOREST(fd) < len)
			return 0;

		const uint32 account_id = RFIFOL(fd, 4);
		const uint32 char_id = RFIFOL(fd, 8);
		const uint16 action = RFIFOW(fd, 12);
		const size_t payload_len = len - NEEDWIKI_PACKET_HEADER_LEN;
		std::string payload(RFIFOCP(fd, NEEDWIKI_PACKET_HEADER_LEN), payload_len);

		if (needwiki_is_duplicate_request(char_id, action, payload)) {
			RFIFOSKIP(fd, len);
			do_close(fd);
			return 0;
		}

		map_session_data* sd = map_charid2sd(char_id);

		if (sd != nullptr) {
			if (action == NEEDWIKI_ACTION_DISPBOTTOM) {
				char output[CHAT_SIZE_MAX];
				const std::string cp949_payload = needwiki_utf8_to_cp949(payload);
				safesnprintf(output, sizeof(output), msg_txt(sd, 1609), cp949_payload.c_str());
				clif_displaymessage(sd->fd, output);
			} else if (action == NEEDWIKI_ACTION_NAVI) {
				std::string mapname;
				uint16 x = 0;
				uint16 y = 0;
				std::string name;

				if (needwiki_parse_navi_payload(payload, mapname, x, y, name)) {
					char output[CHAT_SIZE_MAX];
					const std::string cp949_name = needwiki_utf8_to_cp949(name);
					safesnprintf(output, sizeof(output), msg_txt(sd, 1604), cp949_name.c_str());
					clif_displaymessage(sd->fd, output);

					safesnprintf(output, sizeof(output), msg_txt(sd, 1605), mapname.c_str(), x, y);
					clif_displaymessage(sd->fd, output);
					clif_navigateTo(sd, mapname.c_str(), x, y, NAV_KAFRA_AND_AIRSHIP, true, 0);
				} else {
					clif_displaymessage(sd->fd, msg_txt(sd, 1606));
				}
			} else if (action == NEEDWIKI_ACTION_SHOW_ITEM) {
				uint32 item_id = 0;

				if (!needwiki_parse_u32(payload, item_id)) {
					clif_displaymessage(sd->fd, msg_txt(sd, 1607));
				} else {
					std::shared_ptr<item_data> data = item_db.find(static_cast<t_itemid>(item_id));

					if (data == nullptr) {
						clif_displaymessage(sd->fd, msg_txt(sd, 1607));
					} else {
						struct item link_item = {};

						link_item.nameid = data->nameid;
						link_item.identify = 1;
						link_item.amount = 1;
						link_item.refine = 0;

						const std::string item_link = item_db.create_item_link(link_item);
						std::string message = std::string(msg_txt(sd, 1608)) + item_link;
						clif_displaymessage(sd->fd, message.c_str());
					}
				}
			} else if (action == NEEDWIKI_ACTION_SHOW_GROUP) {
				ShowInfo("[NeedWiki] ITEMGROUP click id=%s\n", payload.c_str());
				auto group_it = needwiki_item_groups.find(payload);

				if (!needwiki_valid_group_id(payload) || group_it == needwiki_item_groups.end()) {
					clif_displaymessage(sd->fd, msg_txt(sd, 1611));
				} else {
					const NeedWikiItemGroup& group = group_it->second;
					char output[CHAT_SIZE_MAX];
					const std::string cp949_name = needwiki_utf8_to_cp949(group.name);
					safesnprintf(output, sizeof(output), msg_txt(sd, 1610), cp949_name.c_str());
					clif_displaymessage(sd->fd, output);

					for (const t_itemid item_id : group.items) {
						std::shared_ptr<item_data> data = item_db.find(item_id);
						if (data == nullptr)
							continue;

						struct item link_item = {};
						link_item.nameid = data->nameid;
						link_item.identify = 1;
						link_item.amount = 1;
						clif_displaymessage(sd->fd, item_db.create_item_link(link_item).c_str());
					}

					ShowInfo("[NeedWiki] Show item group id=%s item_count=%zu\n", payload.c_str(), group.items.size());
				}
			} else {
				ShowWarning("NEED Wiki: unsupported action %u from fd %d.\n", action, fd);
			}
		} else {
			ShowInfo("NEED Wiki: character %" PRIu32 " is not connected (account %" PRIu32 ").\n", char_id, account_id);
		}

		RFIFOSKIP(fd, len);
		do_close(fd);
		return 0;
	}

	return 0;
}

static int32 needwiki_accept(int32 listen_fd)
{
	int32 fd = connect_client(listen_fd);

	if (fd < 0)
		return fd;

	if (session[fd]->client_addr != MAKEIP(127, 0, 0, 1)) {
		ShowWarning("NEED Wiki: rejected non-local connection from %d.%d.%d.%d.\n", CONVIP(session[fd]->client_addr));
		do_close(fd);
		return -1;
	}

	session[fd]->func_parse = needwiki_parse;
	session[fd]->rdata_tick = 0;
	session[fd]->wdata_tick = 0;

	return fd;
}

void do_init_needwiki(void)
{
	needwiki_reload_item_groups();
	needwiki_fd = make_listen_bind(MAKEIP(127, 0, 0, 1), NEEDWIKI_PORT);

	if (needwiki_fd < 0) {
		ShowError("NEED Wiki: failed to listen on 127.0.0.1:%u.\n", NEEDWIKI_PORT);
		return;
	}

	session[needwiki_fd]->func_recv = needwiki_accept;
	ShowStatus("NEED Wiki internal listener ready on 127.0.0.1:%u.\n", NEEDWIKI_PORT);
}

void do_final_needwiki(void)
{
	needwiki_item_groups.clear();
	if (needwiki_fd >= 0) {
		do_close(needwiki_fd);
		needwiki_fd = -1;
	}
}
