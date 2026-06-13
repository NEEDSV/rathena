// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "needwiki_controller.hpp"

#include <cstdlib>
#include <cstring>
#include <chrono>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

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
static constexpr uint16 NEEDWIKI_PACKET_HEADER_LEN = 14;
static constexpr int64 NEEDWIKI_DUPLICATE_WINDOW_MS = 500;

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

static bool needwiki_find_online_session(const Request& req, uint32& account_id, uint32& char_id, std::string& token, bool& token_enabled)
{
	const std::string remote_ip = needwiki_remote_ip(req);

	SQLLock loginlock(LOGIN_SQL_LOCK);
	loginlock.lock();
	auto login_handle = loginlock.getHandle();
	SqlStmt login_stmt{ *login_handle };

	if (SQL_SUCCESS != login_stmt.Prepare(
			"SELECT `account_id`, COALESCE(`web_auth_token`, ''), `web_auth_token_enabled` FROM `%s` WHERE `last_ip` = ?",
			login_table)
		|| SQL_SUCCESS != login_stmt.BindParam(0, SQLDT_STRING, const_cast<char*>(remote_ip.c_str()), remote_ip.size())) {
		SqlStmt_ShowDebug(login_stmt);
		loginlock.unlock();
		return false;
	}

	uint32 candidate_account_id = 0;
	char candidate_token[64] = {};
	uint8 candidate_enabled = 0;

	if (SQL_SUCCESS != login_stmt.Execute()
		|| SQL_SUCCESS != login_stmt.BindColumn(0, SQLDT_UINT32, &candidate_account_id, sizeof(candidate_account_id))
		|| SQL_SUCCESS != login_stmt.BindColumn(1, SQLDT_STRING, candidate_token, sizeof(candidate_token))
		|| SQL_SUCCESS != login_stmt.BindColumn(2, SQLDT_UINT8, &candidate_enabled, sizeof(candidate_enabled))) {
		SqlStmt_ShowDebug(login_stmt);
		loginlock.unlock();
		return false;
	}

	std::vector<std::tuple<uint32, std::string, bool>> login_candidates;
	while (SQL_SUCCESS == login_stmt.NextRow())
		login_candidates.emplace_back(candidate_account_id, candidate_token, candidate_enabled != 0);

	loginlock.unlock();

	std::vector<std::tuple<uint32, uint32, std::string, bool>> sessions;
	SQLLock charlock(CHAR_SQL_LOCK);
	charlock.lock();
	auto char_handle = charlock.getHandle();

	for (const auto& candidate : login_candidates) {
		uint32 aid = std::get<0>(candidate);
		uint32 cid = 0;
		SqlStmt char_stmt{ *char_handle };

		if (SQL_SUCCESS != char_stmt.Prepare(
				"SELECT `char_id` FROM `%s` WHERE `account_id` = ? AND `online` = '1'",
				char_db_table)
			|| SQL_SUCCESS != char_stmt.BindParam(0, SQLDT_UINT32, &aid, sizeof(aid))
			|| SQL_SUCCESS != char_stmt.Execute()
			|| SQL_SUCCESS != char_stmt.BindColumn(0, SQLDT_UINT32, &cid, sizeof(cid))) {
			SqlStmt_ShowDebug(char_stmt);
			charlock.unlock();
			return false;
		}

		while (SQL_SUCCESS == char_stmt.NextRow())
			sessions.emplace_back(aid, cid, std::get<1>(candidate), std::get<2>(candidate));
	}

	charlock.unlock();

	if (sessions.size() != 1)
		return false;

	account_id = std::get<0>(sessions.front());
	char_id = std::get<1>(sessions.front());
	token = std::get<2>(sessions.front());
	token_enabled = std::get<3>(sessions.front());
	return true;
}

static bool needwiki_refresh_token(uint32 account_id, std::string& token)
{
	SQLLock loginlock(LOGIN_SQL_LOCK);
	loginlock.lock();
	auto handle = loginlock.getHandle();

	for (int attempt = 0; attempt < 3; ++attempt) {
		SqlStmt update_stmt{ *handle };
		if (SQL_SUCCESS != update_stmt.Prepare(
				"UPDATE `%s` SET `web_auth_token` = LEFT(SHA2(CONCAT(UUID(), RAND()), 256), 16), `web_auth_token_enabled` = '1' WHERE `account_id` = ?",
				login_table)
			|| SQL_SUCCESS != update_stmt.BindParam(0, SQLDT_UINT32, &account_id, sizeof(account_id))
			|| SQL_SUCCESS != update_stmt.Execute()) {
			continue;
		}

		SqlStmt select_stmt{ *handle };
		char token_buffer[64] = {};
		if (SQL_SUCCESS == select_stmt.Prepare(
				"SELECT `web_auth_token` FROM `%s` WHERE `account_id` = ? AND `web_auth_token_enabled` = '1'",
				login_table)
			&& SQL_SUCCESS == select_stmt.BindParam(0, SQLDT_UINT32, &account_id, sizeof(account_id))
			&& SQL_SUCCESS == select_stmt.Execute()
			&& SQL_SUCCESS == select_stmt.BindColumn(0, SQLDT_STRING, token_buffer, sizeof(token_buffer))
			&& SQL_SUCCESS == select_stmt.NextRow()) {
			token = token_buffer;
			loginlock.unlock();
			return !token.empty();
		}
	}

	loginlock.unlock();
	return false;
}

static bool needwiki_is_authorized(const Request& req, uint32 account_id, uint32 char_id, const std::string& token)
{
	if (token.empty())
		return false;

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

static bool needwiki_require_auth(const Request& req, Response& res, uint32 account_id, uint32 char_id)
{
	std::string token;
	if (!needwiki_get_param(req, "token", token) || !needwiki_is_authorized(req, account_id, char_id, token)) {
		res.status = 401;
		res.set_content("AUTH_EXPIRED", "text/plain");
		return false;
	}

	return true;
}

HANDLER_FUNC(needwiki_auth)
{
	uint32 account_id = 0;
	uint32 char_id = 0;
	std::string token;
	bool token_enabled = false;

	if (!needwiki_find_online_session(req, account_id, char_id, token, token_enabled)) {
		ShowInfo("[NEED Wiki Auth] account_id=0 result=failed\n");
		res.status = 401;
		res.set_content("AUTH_NOT_FOUND", "text/plain");
		return;
	}

	std::string refresh_value;
	const bool force_refresh = needwiki_get_param(req, "refresh", refresh_value) && refresh_value == "1";
	if (force_refresh || !token_enabled || token.empty()) {
		if (!needwiki_refresh_token(account_id, token)) {
			ShowInfo("[NEED Wiki Auth] account_id=%u result=failed\n", account_id);
			res.status = 500;
			res.set_content("AUTH_ISSUE_FAILED", "text/plain");
			return;
		}
	}

	ShowInfo("[NEED Wiki Auth] account_id=%u result=success\n", account_id);
	const std::string body = "{\"account_id\":" + std::to_string(account_id)
		+ ",\"char_id\":" + std::to_string(char_id)
		+ ",\"token\":\"" + token + "\"}";
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
