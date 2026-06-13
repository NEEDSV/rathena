// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "needwiki.hpp"

#include <cstdlib>
#include <string>
#include <unordered_map>

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
static constexpr uint16 NEEDWIKI_PACKET_HEADER_LEN = 14;
static constexpr int32 NEEDWIKI_DUPLICATE_WINDOW_MS = 500;
static constexpr int32 NEEDWIKI_DUPLICATE_CLEANUP_MS = 3000;

static int32 needwiki_fd = -1;
static std::unordered_map<std::string, t_tick> needwiki_recent_requests;

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

static void needwiki_displaymessage(map_session_data* sd, const std::string& utf8_message)
{
	if (sd == nullptr)
		return;

	const std::string cp949_message = needwiki_utf8_to_cp949(utf8_message);
	clif_displaymessage(sd->fd, cp949_message.c_str());
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
				safesnprintf(output, sizeof(output), msg_txt(sd, 1609), payload.c_str());
				needwiki_displaymessage(sd, output);
			} else if (action == NEEDWIKI_ACTION_NAVI) {
				std::string mapname;
				uint16 x = 0;
				uint16 y = 0;
				std::string name;

				if (needwiki_parse_navi_payload(payload, mapname, x, y, name)) {
					char output[CHAT_SIZE_MAX];
					safesnprintf(output, sizeof(output), msg_txt(sd, 1604), name.c_str());
					needwiki_displaymessage(sd, output);

					safesnprintf(output, sizeof(output), msg_txt(sd, 1605), mapname.c_str(), x, y);
					needwiki_displaymessage(sd, output);
					clif_navigateTo(sd, mapname.c_str(), x, y, NAV_KAFRA_AND_AIRSHIP, true, 0);
				} else {
					needwiki_displaymessage(sd, msg_txt(sd, 1606));
				}
			} else if (action == NEEDWIKI_ACTION_SHOW_ITEM) {
				uint32 item_id = 0;

				if (!needwiki_parse_u32(payload, item_id)) {
					needwiki_displaymessage(sd, msg_txt(sd, 1607));
				} else {
					std::shared_ptr<item_data> data = item_db.find(static_cast<t_itemid>(item_id));

					if (data == nullptr) {
						needwiki_displaymessage(sd, msg_txt(sd, 1607));
					} else {
						struct item link_item = {};

						link_item.nameid = data->nameid;
						link_item.identify = 1;
						link_item.amount = 1;
						link_item.refine = 0;

						const std::string item_link = item_db.create_item_link(link_item);
						std::string message = needwiki_utf8_to_cp949(msg_txt(sd, 1608)) + item_link;
						clif_displaymessage(sd->fd, message.c_str());
					}
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
	if (needwiki_fd >= 0) {
		do_close(needwiki_fd);
		needwiki_fd = -1;
	}
}
