// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "needwiki_controller.hpp"

#include <cstdlib>
#include <cstring>
#include <cctype>
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <ryml.hpp>
#include <ryml_std.hpp>

#ifdef WIN32
	#include <common/winapi.hpp>
#else
	#include <arpa/inet.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

#include <common/cbasetypes.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>

#include "sqllock.hpp"
#include "web.hpp"

static constexpr uint16 NEEDWIKI_PORT = 6905;
static constexpr uint16 NEEDWIKI_CMD_TEST_ACTION = 0x7A01;
static constexpr uint16 NEEDWIKI_ACTION_DISPBOTTOM = 1;
static constexpr uint16 NEEDWIKI_ACTION_NAVI = 2;
static constexpr uint16 NEEDWIKI_ACTION_SHOW_ITEM = 3;
static constexpr uint16 NEEDWIKI_ACTION_SHOW_GROUP = 4;
static constexpr uint16 NEEDWIKI_PACKET_HEADER_LEN = 14;
static constexpr int64 NEEDWIKI_DUPLICATE_WINDOW_MS = 500;
static constexpr const char* NEEDWIKI_ITEM_GROUP_DB_TYPE = "NEED_WIKI_ITEM_GROUP_DB";
static constexpr uint16 NEEDWIKI_ITEM_GROUP_DB_VERSION = 1;
static constexpr const char* NEEDWIKI_AUTH_HEADER = "X-NeedWiki-Token";
static constexpr size_t NEEDWIKI_AUTH_TOKEN_LENGTH = 16;

static std::mutex needwiki_duplicate_mutex;
static std::unordered_map<std::string, std::chrono::steady_clock::time_point> needwiki_duplicate_requests;

static std::string needwiki_remote_ip(const Request& req)
{
	static constexpr const char* IPV4_MAPPED_PREFIX = "::ffff:";
	std::string ip = req.remote_addr;

	if (ip.rfind(IPV4_MAPPED_PREFIX, 0) == 0)
		ip.erase(0, strlen(IPV4_MAPPED_PREFIX));

	return ip;
}

static bool needwiki_is_authorized(const Request& req, uint32 account_id, uint32 char_id, const std::string& token)
{
	if (token.size() != NEEDWIKI_AUTH_TOKEN_LENGTH)
		return false;
	for (const unsigned char ch : token) {
		if (!std::isxdigit(ch))
			return false;
	}

	const std::string remote_ip = needwiki_remote_ip(req);
	SQLLock loginlock(LOGIN_SQL_LOCK);
	loginlock.lock();
	auto login_handle = loginlock.getHandle();
	SqlStmt login_stmt{ *login_handle };

	const bool login_ok = SQL_SUCCESS == login_stmt.Prepare(
			"SELECT `account_id` FROM `%s` WHERE `account_id` = ? AND `web_auth_token` = ? AND `web_auth_token_enabled` = '1' AND `last_ip` = ?",
			login_table)
		&& SQL_SUCCESS == login_stmt.BindParam(0, SQLDT_UINT32, &account_id, sizeof(account_id))
		&& SQL_SUCCESS == login_stmt.BindParam(1, SQLDT_STRING, const_cast<char*>(token.c_str()), token.size())
		&& SQL_SUCCESS == login_stmt.BindParam(2, SQLDT_STRING, const_cast<char*>(remote_ip.c_str()), remote_ip.size())
		&& SQL_SUCCESS == login_stmt.Execute()
		&& login_stmt.NumRows() == 1;

	loginlock.unlock();
	if (!login_ok)
		return false;

	SQLLock charlock(CHAR_SQL_LOCK);
	charlock.lock();
	auto char_handle = charlock.getHandle();
	SqlStmt char_stmt{ *char_handle };
	const bool char_ok = SQL_SUCCESS == char_stmt.Prepare(
			"SELECT `char_id` FROM `%s` WHERE `account_id` = ? AND `char_id` = ? AND `online` = '1'",
			char_db_table)
		&& SQL_SUCCESS == char_stmt.BindParam(0, SQLDT_UINT32, &account_id, sizeof(account_id))
		&& SQL_SUCCESS == char_stmt.BindParam(1, SQLDT_UINT32, &char_id, sizeof(char_id))
		&& SQL_SUCCESS == char_stmt.Execute()
		&& char_stmt.NumRows() == 1;

	charlock.unlock();
	return char_ok;
}

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

static bool needwiki_get_auth_token(const Request& req, std::string& token)
{
	if (req.has_header(NEEDWIKI_AUTH_HEADER)) {
		token = req.get_header_value(NEEDWIKI_AUTH_HEADER);
		return !token.empty();
	}

	// Keep query-token compatibility for already deployed DLLs. New DLLs use the
	// header so credentials are not written to access logs as part of the URL.
	return needwiki_get_param(req, "token", token) && !token.empty();
}

static bool needwiki_require_auth(const Request& req, Response& res, uint32 account_id, uint32 char_id)
{
	std::string token;
	if (!needwiki_get_auth_token(req, token) || !needwiki_is_authorized(req, account_id, char_id, token)) {
		res.status = 401;
		res.set_content("AUTH_EXPIRED", "text/plain");
		return false;
	}

	return true;
}

HANDLER_FUNC(needwiki_auth)
{
	std::string account_id_str;
	std::string char_id_str;
	std::string token;

	if (!needwiki_get_param(req, "account_id", account_id_str) ||
		!needwiki_get_param(req, "char_id", char_id_str) ||
		!needwiki_get_auth_token(req, token)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("AUTH_CLIENT_IDENTITY", "text/plain");
		return;
	}

	uint32 account_id = 0;
	uint32 char_id = 0;
	if (!needwiki_parse_u32(account_id_str, account_id) || account_id == 0 ||
		!needwiki_parse_u32(char_id_str, char_id) || char_id == 0) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("AUTH_CLIENT_IDENTITY", "text/plain");
		return;
	}

	if (!needwiki_is_authorized(req, account_id, char_id, token)) {
		ShowInfo("[NEED Wiki Auth] account_id=%u char_id=%u result=failed\n", account_id, char_id);
		res.status = 401;
		res.set_content("AUTH_INVALID", "text/plain");
		return;
	}

	ShowInfo("[NEED Wiki Auth] account_id=%u char_id=%u result=success\n", account_id, char_id);
	const std::string body = "{\"account_id\":" + std::to_string(account_id)
		+ ",\"char_id\":" + std::to_string(char_id)
		+ "}";
	res.set_content(body, "application/json; charset=utf-8");
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

	if (!needwiki_require_auth(req, res, account_id, char_id))
		return;

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

	if (!needwiki_require_auth(req, res, account_id, char_id))
		return;

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

	if (!needwiki_require_auth(req, res, account_id, char_id))
		return;

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

HANDLER_FUNC(needwiki_showgroup)
{
	std::string account_id_str;
	std::string char_id_str;
	std::string group_id;

	if (!needwiki_get_param(req, "account_id", account_id_str) ||
		!needwiki_get_param(req, "char_id", char_id_str) ||
		!needwiki_get_param(req, "group_id", group_id)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Missing account_id, char_id, or group_id", "text/plain");
		return;
	}

	uint32 account_id = 0;
	uint32 char_id = 0;
	if (!needwiki_parse_u32(account_id_str, account_id) || !needwiki_parse_u32(char_id_str, char_id)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Invalid account_id or char_id", "text/plain");
		return;
	}

	if (group_id.empty() || group_id.size() > 64 ||
		group_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Invalid group_id", "text/plain");
		return;
	}

	if (!needwiki_require_auth(req, res, account_id, char_id))
		return;

	if (needwiki_is_duplicate_request(char_id, NEEDWIKI_ACTION_SHOW_GROUP, group_id)) {
		res.set_content("OK_DUPLICATE", "text/plain");
		return;
	}

	if (!needwiki_send_packet(account_id, char_id, NEEDWIKI_ACTION_SHOW_GROUP, group_id)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Failed to send NEED Wiki showgroup packet", "text/plain");
		return;
	}

	res.set_content("OK", "text/plain");
}

HANDLER_FUNC(needwiki_itemgroups)
{
	const std::string path = "db/import/wiki_item_group.yml";
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		res.status = HTTP_NOT_FOUND;
		res.set_content("Item group DB not found", "text/plain");
		return;
	}

	std::ostringstream stream;
	stream << input.rdbuf();
	const std::string yaml = stream.str();

	try {
		ryml::Parser parser;
		ryml::Tree tree = parser.parse_in_arena(c4::to_csubstr(path), c4::to_csubstr(yaml));
		const ryml::NodeRef root = tree.rootref();
		if (!root.has_child("Header"))
			throw std::runtime_error("Header node not found");

		const ryml::NodeRef header = root["Header"];
		if (!header.has_child("Type") || !header.has_child("Version"))
			throw std::runtime_error("Header requires Type and Version");

		std::string type;
		uint16 version = 0;
		header["Type"] >> type;
		header["Version"] >> version;
		if (type != NEEDWIKI_ITEM_GROUP_DB_TYPE || version != NEEDWIKI_ITEM_GROUP_DB_VERSION)
			throw std::runtime_error("Unsupported item group DB header");

		if (!root.has_child("Groups"))
			throw std::runtime_error("Groups node not found");

		nlohmann::json groups = nlohmann::json::object();
		for (const ryml::NodeRef& node : root["Groups"]) {
			if (!node.has_child("Id") || !node.has_child("Name"))
				continue;

			std::string id;
			std::string name;
			node["Id"] >> id;
			node["Name"] >> name;
			if (!id.empty() && !name.empty())
				groups[id] = name;
		}

		res.set_content(groups.dump(), "application/json; charset=utf-8");
	} catch (const std::exception& error) {
		ShowError("[NeedWiki] Failed to serve item group metadata: %s\n", error.what());
		res.status = 500;
		res.set_content("Failed to load item group DB", "text/plain");
	}
}
