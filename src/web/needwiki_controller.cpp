// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "needwiki_controller.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <random>
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
#include <common/needwiki_crypto.hpp>
#include <common/needwiki_diagnostics.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>

#include "sqllock.hpp"
#include "web.hpp"

static constexpr uint16 NEEDWIKI_PORT = 6905;
static constexpr uint16 NEEDWIKI_CMD_TEST_ACTION = 0x7A01;
static constexpr uint16 NEEDWIKI_CMD_ACTION_RESULT = 0x7A02;
static constexpr uint16 NEEDWIKI_ACTION_STATUS = 0;
static constexpr uint16 NEEDWIKI_ACTION_DISPBOTTOM = 1;
static constexpr uint16 NEEDWIKI_ACTION_NAVI = 2;
static constexpr uint16 NEEDWIKI_ACTION_SHOW_ITEM = 3;
static constexpr uint16 NEEDWIKI_ACTION_SHOW_GROUP = 4;
static constexpr uint16 NEEDWIKI_TOKEN_HASH_LEN = 64;
static constexpr uint16 NEEDWIKI_PACKET_HEADER_LEN = 6 + NEEDWIKI_TOKEN_HASH_LEN;
static constexpr uint64 NEEDWIKI_CODE_TTL_SECONDS = 120;
static constexpr size_t NEEDWIKI_BOOTSTRAP_LIMIT_PER_MINUTE = 10;
static constexpr const char* NEEDWIKI_ITEM_GROUP_DB_TYPE = "NEED_WIKI_ITEM_GROUP_DB";
static constexpr uint16 NEEDWIKI_ITEM_GROUP_DB_VERSION = 1;
static constexpr const char* NEEDWIKI_AUTH_HEADER = "Authorization";

enum NeedWikiActionResult : uint16 {
	NEEDWIKI_RESULT_OK = 0,
	NEEDWIKI_RESULT_NOT_BOUND = 1,
	NEEDWIKI_RESULT_EXPIRED = 2,
	NEEDWIKI_RESULT_INVALID_SESSION = 3,
	NEEDWIKI_RESULT_BAD_REQUEST = 4,
	NEEDWIKI_RESULT_INTERNAL_ERROR = 5,
};

enum NeedWikiBindingStatus : uint8 {
	NEEDWIKI_BINDING_WAITING = 0,
	NEEDWIKI_BINDING_READY = 1,
	NEEDWIKI_BINDING_REVOKED = 2,
	NEEDWIKI_BINDING_EXPIRED = 3,
};

static std::mutex needwiki_bootstrap_mutex;
static std::unordered_map<std::string, std::vector<std::chrono::steady_clock::time_point>> needwiki_bootstrap_requests;

static std::string needwiki_remote_ip(const Request& req)
{
	static constexpr const char* IPV4_MAPPED_PREFIX = "::ffff:";
	std::string ip = req.remote_addr;

	if (ip.rfind(IPV4_MAPPED_PREFIX, 0) == 0)
		ip.erase(0, strlen(IPV4_MAPPED_PREFIX));

	return ip;
}

static bool needwiki_get_bearer_hash(const Request& req, std::string& token_hash)
{
	if (!req.has_header(NEEDWIKI_AUTH_HEADER))
		return false;
	const std::string header = req.get_header_value(NEEDWIKI_AUTH_HEADER);
	static constexpr const char* prefix = "Bearer ";
	if (header.rfind(prefix, 0) != 0)
		return false;
	const std::string token = header.substr(strlen(prefix));
	if (!needwiki_crypto::is_lower_hex_hash(token))
		return false;
	token_hash = needwiki_crypto::sha256_hex(token);
	return true;
}

static bool needwiki_bootstrap_rate_allowed(const Request& req)
{
	const auto now = std::chrono::steady_clock::now();
	const auto cutoff = now - std::chrono::minutes(1);
	const std::string ip = needwiki_remote_ip(req);
	std::lock_guard<std::mutex> lock(needwiki_bootstrap_mutex);
	for (auto it = needwiki_bootstrap_requests.begin(); it != needwiki_bootstrap_requests.end();) {
		auto& values = it->second;
		values.erase(std::remove_if(values.begin(), values.end(),
			[cutoff](const auto& value) { return value < cutoff; }), values.end());
		if (values.empty())
			it = needwiki_bootstrap_requests.erase(it);
		else
			++it;
	}
	auto& requests = needwiki_bootstrap_requests[ip];
	if (requests.size() >= NEEDWIKI_BOOTSTRAP_LIMIT_PER_MINUTE)
		return false;
	requests.push_back(now);
	return true;
}

static std::string needwiki_generate_code()
{
	std::random_device random;
	uint64 value = (static_cast<uint64>(random()) << 32) | static_cast<uint64>(random());
	value %= 100000000ULL;
	std::ostringstream stream;
	stream << std::setw(8) << std::setfill('0') << value;
	return stream.str();
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

static NeedWikiActionResult needwiki_send_packet(const std::string& token_hash, uint16 action, const std::string& payload)
{
	if (!needwiki_crypto::is_lower_hex_hash(token_hash) ||
		payload.size() > UINT16_MAX - NEEDWIKI_PACKET_HEADER_LEN)
		return NEEDWIKI_RESULT_BAD_REQUEST;

	const uint16 len = static_cast<uint16>(NEEDWIKI_PACKET_HEADER_LEN + payload.size());
	std::vector<uint8> packet(len);

	WBUFW(packet.data(), 0) = NEEDWIKI_CMD_TEST_ACTION;
	WBUFW(packet.data(), 2) = len;
	WBUFW(packet.data(), 4) = action;
	memcpy(WBUFP(packet.data(), 6), token_hash.data(), NEEDWIKI_TOKEN_HASH_LEN);
	memcpy(WBUFP(packet.data(), NEEDWIKI_PACKET_HEADER_LEN), payload.data(), payload.size());

	int sock = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));

#ifdef WIN32
	if (sock == INVALID_SOCKET)
		return NEEDWIKI_RESULT_INTERNAL_ERROR;
#else
	if (sock < 0)
		return NEEDWIKI_RESULT_INTERNAL_ERROR;
#endif

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(NEEDWIKI_PORT);
	addr.sin_addr.s_addr = htonl(MAKEIP(127, 0, 0, 1));

	if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
		needwiki_close_socket(sock, false);
		return NEEDWIKI_RESULT_INTERNAL_ERROR;
	}

#ifdef WIN32
	DWORD timeout = 2000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
	timeval timeout{ 2, 0 };
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

	size_t sent = 0;
	while (sent < packet.size()) {
		int ret = send(sock, reinterpret_cast<const char*>(packet.data() + sent), static_cast<int>(packet.size() - sent), 0);

		if (ret <= 0)
			break;

		sent += static_cast<size_t>(ret);
	}

	if (sent != packet.size()) {
		needwiki_close_socket(sock, false);
		return NEEDWIKI_RESULT_INTERNAL_ERROR;
	}

	uint8 response[4] = {};
	size_t received = 0;
	while (received < sizeof(response)) {
		const int ret = recv(sock, reinterpret_cast<char*>(response + received), static_cast<int>(sizeof(response) - received), 0);
		if (ret <= 0)
			break;
		received += static_cast<size_t>(ret);
	}
	needwiki_close_socket(sock, false);
	if (received != sizeof(response) || RBUFW(response, 0) != NEEDWIKI_CMD_ACTION_RESULT)
		return NEEDWIKI_RESULT_INTERNAL_ERROR;

	const uint16 result = RBUFW(response, 2);
	return result <= NEEDWIKI_RESULT_INTERNAL_ERROR
		? static_cast<NeedWikiActionResult>(result)
		: NEEDWIKI_RESULT_INTERNAL_ERROR;
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

static NeedWikiBindingStatus needwiki_get_db_status(const std::string& token_hash)
{
	SQLLock lock(MAP_SQL_LOCK);
	lock.lock();
	auto handle = lock.getHandle();
	SqlStmt stmt{ *handle };
	uint32 status = NEEDWIKI_BINDING_REVOKED;
	uint64 code_expires_at = 0;
	uint64 expires_at = 0;
	const bool ok = SQL_SUCCESS == stmt.Prepare(
			"SELECT status,code_expires_at,expires_at FROM needwiki_sessions WHERE token_hash=? LIMIT 1")
		&& SQL_SUCCESS == stmt.BindParam(0, SQLDT_STRING, const_cast<char*>(token_hash.c_str()), token_hash.size())
		&& SQL_SUCCESS == stmt.Execute();
	if (!ok || stmt.NumRows() == 0) {
		lock.unlock();
		return NEEDWIKI_BINDING_REVOKED;
	}

	if (SQL_SUCCESS != stmt.BindColumn(0, SQLDT_UINT32, &status, sizeof(status)) ||
		SQL_SUCCESS != stmt.BindColumn(1, SQLDT_UINT64, &code_expires_at, sizeof(code_expires_at)) ||
		SQL_SUCCESS != stmt.BindColumn(2, SQLDT_UINT64, &expires_at, sizeof(expires_at)) ||
		SQL_SUCCESS != stmt.NextRow()) {
		lock.unlock();
		return NEEDWIKI_BINDING_REVOKED;
	}

	const uint64 now = static_cast<uint64>(time(nullptr));
	if ((status == NEEDWIKI_BINDING_WAITING && code_expires_at < now) ||
		(status == NEEDWIKI_BINDING_READY && expires_at < now)) {
		Sql_Query(handle, "UPDATE needwiki_sessions SET status='%u' WHERE token_hash='%s'",
			static_cast<uint32>(NEEDWIKI_BINDING_EXPIRED), token_hash.c_str());
		status = NEEDWIKI_BINDING_EXPIRED;
	}
	lock.unlock();
	return static_cast<NeedWikiBindingStatus>(status);
}

static void needwiki_set_json_status(Response& res, const char* status, int http_status = 200)
{
	res.status = http_status;
	res.set_content(std::string("{\"status\":\"") + status + "\"}", "application/json; charset=utf-8");
}

static void needwiki_set_action_result(Response& res, NeedWikiActionResult result)
{
	switch (result) {
		case NEEDWIKI_RESULT_OK:
			res.set_content("OK", "text/plain");
			break;
		case NEEDWIKI_RESULT_EXPIRED:
			res.status = 401;
			res.set_content("EXPIRED", "text/plain");
			break;
		case NEEDWIKI_RESULT_NOT_BOUND:
		case NEEDWIKI_RESULT_INVALID_SESSION:
			res.status = 401;
			res.set_content("NOT_BOUND", "text/plain");
			break;
		case NEEDWIKI_RESULT_BAD_REQUEST:
			res.status = HTTP_BAD_REQUEST;
			res.set_content("BAD_REQUEST", "text/plain");
			break;
		default:
			res.status = 503;
			res.set_content("SERVER_UNAVAILABLE", "text/plain");
			break;
	}
}

HANDLER_FUNC(needwiki_bootstrap_start)
{
	std::string token_hash;
	if (!needwiki_get_bearer_hash(req, token_hash)) {
		NEEDWIKI_DIAG("[NeedWiki] bootstrap start: INVALID_TOKEN\n");
		res.status = 401;
		res.set_content("INVALID_TOKEN", "text/plain");
		return;
	}
	if (!needwiki_bootstrap_rate_allowed(req)) {
		NEEDWIKI_DIAG("[NeedWiki] bootstrap start: RATE_LIMITED\n");
		res.status = 429;
		res.set_content("RATE_LIMITED", "text/plain");
		return;
	}

	const uint64 now = static_cast<uint64>(time(nullptr));
	std::lock_guard<std::mutex> guard(needwiki_bootstrap_mutex);
	SQLLock lock(MAP_SQL_LOCK);
	lock.lock();
	auto handle = lock.getHandle();
	Sql_Query(handle,
		"DELETE FROM needwiki_sessions WHERE "
		"(status IN (2,3) AND expires_at<'%" PRIu64 "') OR "
		"(status=0 AND code_expires_at<'%" PRIu64 "')",
		now - 300, now - 300);
	if (SQL_ERROR == Sql_Query(handle, "DELETE FROM needwiki_sessions WHERE token_hash='%s'", token_hash.c_str())) {
		lock.unlock();
		NEEDWIKI_DIAG("[NeedWiki] bootstrap start: DB_ERROR\n");
		res.status = 503;
		res.set_content("DB_ERROR", "text/plain");
		return;
	}

	std::string code;
	bool inserted = false;
	for (int attempt = 0; attempt < 20 && !inserted; ++attempt) {
		code = needwiki_generate_code();
		const std::string code_hash = needwiki_crypto::sha256_hex(code);
		inserted = SQL_SUCCESS == Sql_Query(handle,
			"INSERT INTO needwiki_sessions "
			"(token_hash,code_hash,status,created_at,code_expires_at,expires_at) "
			"VALUES ('%s','%s','%u','%" PRIu64 "','%" PRIu64 "','0')",
			token_hash.c_str(), code_hash.c_str(), static_cast<uint32>(NEEDWIKI_BINDING_WAITING),
			now, now + NEEDWIKI_CODE_TTL_SECONDS);
	}
	lock.unlock();
	if (!inserted) {
		NEEDWIKI_DIAG("[NeedWiki] bootstrap start: ISSUE_FAILED\n");
		res.status = 503;
		res.set_content("ISSUE_FAILED", "text/plain");
		return;
	}

	nlohmann::json body;
	body["status"] = "WAITING";
	body["code"] = code;
	body["expires_in"] = NEEDWIKI_CODE_TTL_SECONDS;
	NEEDWIKI_DIAG("[NeedWiki] bootstrap start: WAITING\n");
	res.set_content(body.dump(), "application/json; charset=utf-8");
}

HANDLER_FUNC(needwiki_auth)
{
	std::string token_hash;
	if (!needwiki_get_bearer_hash(req, token_hash)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("INVALID_TOKEN", "text/plain");
		return;
	}

	const NeedWikiBindingStatus status = needwiki_get_db_status(token_hash);
	if (status == NEEDWIKI_BINDING_WAITING) {
		needwiki_set_json_status(res, "WAITING");
	} else if (status == NEEDWIKI_BINDING_EXPIRED) {
		needwiki_set_json_status(res, "EXPIRED", 401);
	} else if (status != NEEDWIKI_BINDING_READY) {
		needwiki_set_json_status(res, "NOT_BOUND", 401);
	} else {
		const NeedWikiActionResult result = needwiki_send_packet(token_hash, NEEDWIKI_ACTION_STATUS, "");
		if (result == NEEDWIKI_RESULT_OK)
			needwiki_set_json_status(res, "READY");
		else if (result == NEEDWIKI_RESULT_EXPIRED)
			needwiki_set_json_status(res, "EXPIRED", 401);
		else if (result == NEEDWIKI_RESULT_INTERNAL_ERROR)
			needwiki_set_json_status(res, "UNAVAILABLE", 503);
		else
			needwiki_set_json_status(res, "NOT_BOUND", 401);
	}
}

HANDLER_FUNC(needwiki_test)
{
	std::string msg;

	if (!needwiki_get_param(req, "msg", msg)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Missing msg", "text/plain");
		return;
	}

	std::string token_hash;
	if (!needwiki_get_bearer_hash(req, token_hash)) {
		res.status = 401;
		res.set_content("INVALID_TOKEN", "text/plain");
		return;
	}

	needwiki_set_action_result(res, needwiki_send_packet(token_hash, NEEDWIKI_ACTION_DISPBOTTOM, msg));
}

HANDLER_FUNC(needwiki_navi)
{
	std::string map;
	std::string x_str;
	std::string y_str;
	std::string name;

	if (!needwiki_get_param(req, "map", map) ||
		!needwiki_get_param(req, "x", x_str) ||
		!needwiki_get_param(req, "y", y_str) ||
		!needwiki_get_param(req, "name", name)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Missing map, x, y, or name", "text/plain");
		return;
	}

	uint16 x = 0;
	uint16 y = 0;

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

	std::string token_hash;
	if (!needwiki_get_bearer_hash(req, token_hash)) {
		res.status = 401;
		res.set_content("INVALID_TOKEN", "text/plain");
		return;
	}
	needwiki_set_action_result(res, needwiki_send_packet(token_hash, NEEDWIKI_ACTION_NAVI, payload));
}

HANDLER_FUNC(needwiki_showitem)
{
	std::string item_id_str;

	if (!needwiki_get_param(req, "item_id", item_id_str)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Missing item_id", "text/plain");
		return;
	}

	uint32 item_id = 0;

	if (!needwiki_parse_u32(item_id_str, item_id) || item_id == 0) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Invalid item_id", "text/plain");
		return;
	}

	const std::string payload = std::to_string(item_id);
	std::string token_hash;
	if (!needwiki_get_bearer_hash(req, token_hash)) {
		res.status = 401;
		res.set_content("INVALID_TOKEN", "text/plain");
		return;
	}
	needwiki_set_action_result(res, needwiki_send_packet(token_hash, NEEDWIKI_ACTION_SHOW_ITEM, payload));
}

HANDLER_FUNC(needwiki_showgroup)
{
	std::string group_id;

	if (!needwiki_get_param(req, "group_id", group_id)) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Missing group_id", "text/plain");
		return;
	}

	if (group_id.empty() || group_id.size() > 64 ||
		group_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) {
		res.status = HTTP_BAD_REQUEST;
		res.set_content("Invalid group_id", "text/plain");
		return;
	}

	std::string token_hash;
	if (!needwiki_get_bearer_hash(req, token_hash)) {
		res.status = 401;
		res.set_content("INVALID_TOKEN", "text/plain");
		return;
	}
	needwiki_set_action_result(res, needwiki_send_packet(token_hash, NEEDWIKI_ACTION_SHOW_GROUP, group_id));
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
