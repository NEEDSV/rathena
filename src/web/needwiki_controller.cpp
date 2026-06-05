// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "needwiki_controller.hpp"

#include <cstdlib>
#include <cstring>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef WIN32
	#include <common/winapi.hpp>
#else
	#include <arpa/inet.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

#include <common/cbasetypes.hpp>
#include <common/socket.hpp>

#include "web.hpp"

static constexpr uint16 NEEDWIKI_PORT = 6905;
static constexpr uint16 NEEDWIKI_CMD_TEST_ACTION = 0x7A01;
static constexpr uint16 NEEDWIKI_ACTION_DISPBOTTOM = 1;
static constexpr uint16 NEEDWIKI_ACTION_NAVI = 2;
static constexpr uint16 NEEDWIKI_ACTION_SHOW_ITEM = 3;
static constexpr uint16 NEEDWIKI_PACKET_HEADER_LEN = 14;
static constexpr int64 NEEDWIKI_DUPLICATE_WINDOW_MS = 500;

static std::mutex needwiki_duplicate_mutex;
static std::unordered_map<std::string, std::chrono::steady_clock::time_point> needwiki_duplicate_requests;

static std::string needwiki_duplicate_key(uint32 char_id, uint16 action, const std::string& payload)
{
	return std::to_string(char_id) + "|" + std::to_string(action) + "|" + payload;
}

static bool needwiki_is_duplicate_request(uint32 char_id, uint16 action, const std::string& payload)
{
	const auto now = std::chrono::steady_clock::now();
	const auto window = std::chrono::milliseconds(NEEDWIKI_DUPLICATE_WINDOW_MS);
	const std::string key = needwiki_duplicate_key(char_id, action, payload);

	std::lock_guard<std::mutex> lock(needwiki_duplicate_mutex);

	for (auto it = needwiki_duplicate_requests.begin(); it != needwiki_duplicate_requests.end();) {
		if (now - it->second > window)
			it = needwiki_duplicate_requests.erase(it);
		else
			++it;
	}

	auto it = needwiki_duplicate_requests.find(key);

	if (it != needwiki_duplicate_requests.end() && now - it->second <= window)
		return true;

	needwiki_duplicate_requests[key] = now;
	return false;
}

static void needwiki_close_socket(int sock, bool shutdown_first)
{
#ifdef WIN32
	if (sock == INVALID_SOCKET)
		return;

	if (shutdown_first)
		shutdown(sock, SD_SEND);

	closesocket(sock);
#else
	if (sock < 0)
		return;

	if (shutdown_first)
		shutdown(sock, SHUT_WR);

	close(sock);
#endif
}

static bool needwiki_send_packet(uint32 account_id, uint32 char_id, uint16 action, const std::string& payload)
{
	if (payload.size() > UINT16_MAX - NEEDWIKI_PACKET_HEADER_LEN)
		return false;

	const uint16 len = static_cast<uint16>(NEEDWIKI_PACKET_HEADER_LEN + payload.size());
	std::vector<uint8> packet(len);

	WBUFW(packet.data(), 0) = NEEDWIKI_CMD_TEST_ACTION;
	WBUFW(packet.data(), 2) = len;
	WBUFL(packet.data(), 4) = account_id;
	WBUFL(packet.data(), 8) = char_id;
	WBUFW(packet.data(), 12) = action;
	memcpy(WBUFP(packet.data(), NEEDWIKI_PACKET_HEADER_LEN), payload.data(), payload.size());

	int sock = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));

#ifdef WIN32
	if (sock == INVALID_SOCKET)
		return false;
#else
	if (sock < 0)
		return false;
#endif

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(NEEDWIKI_PORT);
	addr.sin_addr.s_addr = htonl(MAKEIP(127, 0, 0, 1));

	bool success = false;

	if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
		needwiki_close_socket(sock, false);
		return false;
	}

	size_t sent = 0;
	while (sent < packet.size()) {
		int ret = send(sock, reinterpret_cast<const char*>(packet.data() + sent), static_cast<int>(packet.size() - sent), 0);

		if (ret <= 0)
			break;

		sent += static_cast<size_t>(ret);
	}

	success = sent == packet.size();
	needwiki_close_socket(sock, true);

	return success;
}

static bool needwiki_parse_u32(const std::string& value, uint32& out)
{
	if (value.empty())
		return false;

	char* end = nullptr;
	const unsigned long parsed = strtoul(value.c_str(), &end, 10);

	if (end == value.c_str() || *end != '\0' || parsed > UINT32_MAX)
		return false;

	out = static_cast<uint32>(parsed);
	return true;
}

static bool needwiki_parse_u16(const std::string& value, uint16& out)
{
	if (value.empty())
		return false;

	char* end = nullptr;
	const unsigned long parsed = strtoul(value.c_str(), &end, 10);

	if (end == value.c_str() || *end != '\0' || parsed > UINT16_MAX)
		return false;

	out = static_cast<uint16>(parsed);
	return true;
}

static bool needwiki_get_param(const Request& req, const char* name, std::string& value)
{
	if (req.has_param(name)) {
		value = req.get_param_value(name);
		return true;
	}

	if (req.has_file(name)) {
		value = req.get_file_value(name).content;
		return true;
	}

	return false;
}

HANDLER_FUNC(needwiki_test)
{
	std::string account_id_str;
	std::string char_id_str;
	std::string msg;

	if (!needwiki_get_param(req, "account_id", account_id_str) ||
		!needwiki_get_param(req, "char_id", char_id_str) ||
		!needwiki_get_param(req, "msg", msg)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Missing account_id, char_id, or msg", "text/plain");
		return;
	}

	uint32 account_id = 0;

	if (!needwiki_parse_u32(account_id_str, account_id)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Invalid account_id", "text/plain");
		return;
	}

	uint32 char_id = 0;

	if (!needwiki_parse_u32(char_id_str, char_id)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Invalid char_id", "text/plain");
		return;
	}

	if (needwiki_is_duplicate_request(char_id, NEEDWIKI_ACTION_DISPBOTTOM, msg)) {
		res.set_content("OK_DUPLICATE", "text/plain");
		return;
	}

	if (!needwiki_send_packet(account_id, char_id, NEEDWIKI_ACTION_DISPBOTTOM, msg)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Failed to send NEED Wiki test packet", "text/plain");
		return;
	}

	res.set_content("OK", "text/plain");
}

HANDLER_FUNC(needwiki_navi)
{
	std::string account_id_str;
	std::string char_id_str;
	std::string map;
	std::string x_str;
	std::string y_str;
	std::string name;

	if (!needwiki_get_param(req, "account_id", account_id_str) ||
		!needwiki_get_param(req, "char_id", char_id_str) ||
		!needwiki_get_param(req, "map", map) ||
		!needwiki_get_param(req, "x", x_str) ||
		!needwiki_get_param(req, "y", y_str) ||
		!needwiki_get_param(req, "name", name)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Missing account_id, char_id, map, x, y, or name", "text/plain");
		return;
	}

	uint32 account_id = 0;
	uint32 char_id = 0;
	uint16 x = 0;
	uint16 y = 0;

	if (!needwiki_parse_u32(account_id_str, account_id)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Invalid account_id", "text/plain");
		return;
	}

	if (!needwiki_parse_u32(char_id_str, char_id)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Invalid char_id", "text/plain");
		return;
	}

	if (!needwiki_parse_u16(x_str, x)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Invalid x", "text/plain");
		return;
	}

	if (!needwiki_parse_u16(y_str, y)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Invalid y", "text/plain");
		return;
	}

	if (map.empty() || name.empty() || map.find('|') != std::string::npos || name.find('|') != std::string::npos) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Invalid map or name", "text/plain");
		return;
	}

	const std::string payload = map + "|" + std::to_string(x) + "|" + std::to_string(y) + "|" + name;

	if (needwiki_is_duplicate_request(char_id, NEEDWIKI_ACTION_NAVI, payload)) {
		res.set_content("OK_DUPLICATE", "text/plain");
		return;
	}

	if (!needwiki_send_packet(account_id, char_id, NEEDWIKI_ACTION_NAVI, payload)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Failed to send NEED Wiki navi packet", "text/plain");
		return;
	}

	res.set_content("OK", "text/plain");
}

HANDLER_FUNC(needwiki_showitem)
{
	std::string account_id_str;
	std::string char_id_str;
	std::string item_id_str;

	if (!needwiki_get_param(req, "account_id", account_id_str) ||
		!needwiki_get_param(req, "char_id", char_id_str) ||
		!needwiki_get_param(req, "item_id", item_id_str)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Missing account_id, char_id, or item_id", "text/plain");
		return;
	}

	uint32 account_id = 0;
	uint32 char_id = 0;
	uint32 item_id = 0;

	if (!needwiki_parse_u32(account_id_str, account_id)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Invalid account_id", "text/plain");
		return;
	}

	if (!needwiki_parse_u32(char_id_str, char_id)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Invalid char_id", "text/plain");
		return;
	}

	if (!needwiki_parse_u32(item_id_str, item_id) || item_id == 0) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Invalid item_id", "text/plain");
		return;
	}

	const std::string payload = std::to_string(item_id);

	if (needwiki_is_duplicate_request(char_id, NEEDWIKI_ACTION_SHOW_ITEM, payload)) {
		res.set_content("OK_DUPLICATE", "text/plain");
		return;
	}

	if (!needwiki_send_packet(account_id, char_id, NEEDWIKI_ACTION_SHOW_ITEM, payload)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Failed to send NEED Wiki showitem packet", "text/plain");
		return;
	}

	res.set_content("OK", "text/plain");
}
