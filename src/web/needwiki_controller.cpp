// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "needwiki_controller.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <chrono>
#include <cinttypes>
#include <ctime>
#include <filesystem>
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
#include <common/core.hpp>
#include <common/needwiki_crypto.hpp>
#include <common/needwiki_diagnostics.hpp>
#include <common/needwiki_item_group.hpp>
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

/**
 * Builds the /api/wiki/itemgroups body from the raw item group DB bytes.
 * The file is CP949 (see common/needwiki_item_group.hpp); every Name is converted to UTF-8
 * here, at the HTTP boundary, before it reaches nlohmann::json. The response shape is the
 * historic {"<group id>": "<Korean name>"} object.
 */
static bool needwiki_build_itemgroups_body(const std::string& path, const std::string& bytes, std::string& body, std::string& error)
{
	try {
		std::vector<std::string> warnings;
		const std::vector<needwiki_item_group::Group> sources = needwiki_item_group::parse(path, bytes, warnings);

		for (const std::string& warning : warnings)
			ShowWarning("[NeedWiki] %s\n", warning.c_str());

		nlohmann::json groups = nlohmann::json::object();
		for (const needwiki_item_group::Group& source : sources) {
			std::string name_utf8;

			if (!needwiki_item_group::cp949_to_utf8(source.name, name_utf8))
				throw std::runtime_error("Name of item group '" + source.id + "' is not valid CP949");

			groups[source.id] = name_utf8;
		}

		body = groups.dump();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
}

struct NeedWikiItemGroupCache {
	bool valid = false;
	std::filesystem::file_time_type mtime{};
	uintmax_t size = 0;
	bool ok = false;
	std::string body;
};

static std::mutex needwiki_itemgroups_mutex;
static NeedWikiItemGroupCache needwiki_itemgroups_cache;

HANDLER_FUNC(needwiki_itemgroups)
{
	const std::string path = needwiki_item_group::db_file(db_path);
	std::error_code ec;
	const auto mtime = std::filesystem::last_write_time(path, ec);
	const uintmax_t size = ec ? 0 : std::filesystem::file_size(path, ec);

	if (ec) {
		res.status = HTTP_NOT_FOUND;
		res.set_content("Item group DB not found", "text/plain");
		return;
	}

	std::lock_guard<std::mutex> guard(needwiki_itemgroups_mutex);
	NeedWikiItemGroupCache& cache = needwiki_itemgroups_cache;

	// Reuse the previous result while the file is unchanged (same mtime and size). A failed
	// load is cached too, so a broken file is reported once per change instead of per request.
	if (!cache.valid || cache.mtime != mtime || cache.size != size) {
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			res.status = HTTP_NOT_FOUND;
			res.set_content("Item group DB not found", "text/plain");
			return;
		}

		std::ostringstream stream;
		stream << input.rdbuf();

		std::string body;
		std::string error;
		cache.ok = needwiki_build_itemgroups_body(path, stream.str(), body, error);
		cache.body = cache.ok ? body : std::string();
		cache.mtime = mtime;
		cache.size = size;
		cache.valid = true;

		if (!cache.ok)
			ShowError("[NeedWiki] Failed to serve item group metadata '%s': %s\n", path.c_str(), error.c_str());
	}

	if (!cache.ok) {
		res.status = 500;
		res.set_content("Failed to load item group DB", "text/plain");
		return;
	}

	res.set_content(cache.body, "application/json; charset=utf-8");
}

/*==========================================
 * NEED Wiki article reports (POST /api/wiki/report)
 *
 * Anonymous, limited per client IP (req.remote_addr only - X-Forwarded-For is not trusted).
 * Stored in `needwiki_reports` (sql-files/main.sql). The web-server's SQL connection uses
 * default_codepage (euckr on NEED), so the UTF-8 text is bound as binary and converted by
 * the query itself (CONVERT(? USING utf8mb4)) - no connection setting is changed.
 *------------------------------------------*/

static constexpr size_t NEEDWIKI_REPORT_MAX_BODY_BYTES = 16 * 1024;
static constexpr size_t NEEDWIKI_REPORT_ARTICLE_ID_MAX = 128;
static constexpr size_t NEEDWIKI_REPORT_TITLE_MAX_CHARS = 255;      // varchar(255)
static constexpr size_t NEEDWIKI_REPORT_MESSAGE_MIN_UNITS = 5;      // same as the DLL (UTF-16 units)
static constexpr size_t NEEDWIKI_REPORT_MESSAGE_MAX_UNITS = 1000;   // same as the DLL (EM_LIMITTEXT)
static constexpr size_t NEEDWIKI_REPORT_MESSAGE_MAX_BYTES = 4096;
static constexpr size_t NEEDWIKI_REPORT_TYPE_RAW_MAX_BYTES = 64;
static constexpr uint64 NEEDWIKI_REPORT_WINDOW_SECONDS = 10 * 60;
static constexpr uint64 NEEDWIKI_REPORT_LIMIT_PER_WINDOW = 5;
static constexpr const char* NEEDWIKI_REPORT_PATH = "/api/wiki/report";

static void needwiki_report_reply(Response& res, int http_status, const char* status, uint64 id = 0)
{
	nlohmann::json body;
	body["status"] = status;
	if (id != 0)
		body["id"] = id;
	res.status = http_status;
	res.set_content(body.dump(), "application/json; charset=utf-8");
}

/// Maps the stable codes and the legacy DLL / documentation strings to a stable code.
static const char* needwiki_report_type_code(const std::string& raw)
{
	static const std::vector<std::pair<std::string, const char*>> map = {
		{ "incorrect_info", "incorrect_info" },
		{ "missing_content", "missing_content" },
		{ "typo", "typo" },
		{ "other", "other" },
		// Legacy Korean values, written as UTF-8 bytes so the source encoding does not matter.
		{ "\xEC\xA0\x95\xEB\xB3\xB4 \xEC\x98\xA4\xEB\xA5\x98", "incorrect_info" },   // "jeongbo oryu" (info error)
		{ "\xEB\x82\xB4\xEC\x9A\xA9 \xEC\xB6\x94\xEA\xB0\x80", "missing_content" },  // "naeyong chuga" (add content)
		{ "\xEC\x98\xA4\xED\x83\x80\xEC\x9E\x90", "typo" },   // "otaja": what the DLL has sent since the feature was added
		{ "\xEC\x98\xA4\xED\x83\x88\xEC\x9E\x90", "typo" },   // "otalja": what the documentation listed
		{ "\xEA\xB8\xB0\xED\x83\x80", "other" },              // "gita" (other)
	};

	for (const auto& entry : map) {
		if (entry.first == raw)
			return entry.second;
	}

	return nullptr;
}

static bool needwiki_report_valid_article_id(const std::string& id)
{
	if (id.empty() || id.size() > NEEDWIKI_REPORT_ARTICLE_ID_MAX)
		return false;

	for (const char ch : id) {
		if ((ch < 'a' || ch > 'z') && (ch < 'A' || ch > 'Z') && (ch < '0' || ch > '9') && ch != '_')
			return false;
	}

	return true;
}

/// Decodes valid UTF-8 (nlohmann::json::parse has already rejected ill-formed input).
static std::vector<uint32> needwiki_utf8_code_points(const std::string& text)
{
	std::vector<uint32> points;
	for (size_t i = 0; i < text.size();) {
		const unsigned char c = static_cast<unsigned char>(text[i]);
		size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
		uint32 cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);

		for (size_t k = 1; k < len && i + k < text.size(); ++k)
			cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);

		points.push_back(cp);
		i += len;
	}
	return points;
}

static bool needwiki_report_is_space(uint32 cp)
{
	return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == '\v' || cp == '\f' || cp == 0x3000;
}

/// Trims surrounding white space (like the DLL's iswspace trim). Returns the trimmed text,
/// its length in UTF-16 code units (what the DLL counts) and whether it holds a control
/// character other than TAB/CR/LF.
static std::string needwiki_report_trim(const std::string& text, size_t& utf16_units, bool& has_control, bool allow_newlines)
{
	const std::vector<uint32> points = needwiki_utf8_code_points(text);
	size_t first = 0;
	size_t last = points.size();

	while (first < last && needwiki_report_is_space(points[first]))
		++first;
	while (last > first && needwiki_report_is_space(points[last - 1]))
		--last;

	utf16_units = 0;
	has_control = false;

	size_t byte_begin = 0;
	size_t byte_end = 0;
	size_t offset = 0;
	for (size_t i = 0; i < points.size(); ++i) {
		const uint32 cp = points[i];
		const size_t len = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;

		if (i == first)
			byte_begin = offset;
		offset += len;
		if (i + 1 == last)
			byte_end = offset;

		if (i < first || i >= last)
			continue;

		utf16_units += cp >= 0x10000 ? 2 : 1;

		const bool newline = cp == '\r' || cp == '\n' || cp == '\t';
		if ((cp < 0x20 && !(allow_newlines && newline)) || cp == 0x7F || (cp >= 0x80 && cp < 0xA0))
			has_control = true;
	}

	return first < last ? text.substr(byte_begin, byte_end - byte_begin) : std::string();
}

/**
 * Pre-routing guard: runs BEFORE cpp-httplib reads the request body, so an oversized report
 * is refused without buffering it. Other routes are untouched (no global payload cap).
 */
bool needwiki_report_pre_routing(const Request& req, Response& res)
{
	if (req.path != NEEDWIKI_REPORT_PATH || req.method != "POST")
		return false;

	if (!req.has_header("Content-Length")) {
		// Chunked or unsized bodies could be of any size.
		needwiki_report_reply(res, 411, "LENGTH_REQUIRED");
		return true;
	}

	const std::string value = req.get_header_value("Content-Length");
	char* end = nullptr;
	const unsigned long long length = strtoull(value.c_str(), &end, 10);

	if (value.empty() || end == value.c_str() || *end != '\0' || length > NEEDWIKI_REPORT_MAX_BODY_BYTES) {
		needwiki_report_reply(res, 413, "PAYLOAD_TOO_LARGE");
		return true;
	}

	return false;
}

HANDLER_FUNC(needwiki_report)
{
	const std::string ip = needwiki_remote_ip(req);

	if (req.body.size() > NEEDWIKI_REPORT_MAX_BODY_BYTES) {
		needwiki_report_reply(res, 413, "PAYLOAD_TOO_LARGE");
		return;
	}

	nlohmann::json root;
	try {
		// Also rejects ill-formed UTF-8 inside strings.
		root = nlohmann::json::parse(req.body);
	} catch (const std::exception&) {
		needwiki_report_reply(res, HTTP_BAD_REQUEST, "INVALID_JSON");
		return;
	}

	if (!root.is_object()) {
		needwiki_report_reply(res, HTTP_BAD_REQUEST, "INVALID_JSON");
		return;
	}

	// article_id: required. Both the DLL and the web viewer send it.
	const auto article_it = root.find("article_id");
	if (article_it == root.end() || !article_it->is_string() ||
		!needwiki_report_valid_article_id(article_it->get<std::string>())) {
		needwiki_report_reply(res, HTTP_BAD_REQUEST, "INVALID_ARTICLE_ID");
		return;
	}
	const std::string article_id = article_it->get<std::string>();

	// article_title: optional; the web viewer sends "title".
	std::string article_title;
	{
		auto title_it = root.find("article_title");
		if (title_it == root.end() || title_it->is_null())
			title_it = root.find("title");

		if (title_it != root.end() && !title_it->is_null()) {
			size_t units = 0;
			bool has_control = false;

			if (!title_it->is_string()) {
				needwiki_report_reply(res, HTTP_BAD_REQUEST, "INVALID_ARTICLE_TITLE");
				return;
			}

			article_title = needwiki_report_trim(title_it->get<std::string>(), units, has_control, false);
			if (has_control || needwiki_utf8_code_points(article_title).size() > NEEDWIKI_REPORT_TITLE_MAX_CHARS) {
				needwiki_report_reply(res, HTTP_BAD_REQUEST, "INVALID_ARTICLE_TITLE");
				return;
			}
		}
	}

	// report_type: absent -> other (web viewer); unknown -> 400.
	std::string report_type_raw;
	const char* report_type = "other";
	{
		const auto type_it = root.find("report_type");
		if (type_it != root.end() && !type_it->is_null()) {
			if (!type_it->is_string()) {
				needwiki_report_reply(res, HTTP_BAD_REQUEST, "INVALID_REPORT_TYPE");
				return;
			}

			report_type_raw = type_it->get<std::string>();
			report_type = needwiki_report_type_code(report_type_raw);
			if (report_type == nullptr || report_type_raw.size() > NEEDWIKI_REPORT_TYPE_RAW_MAX_BYTES) {
				needwiki_report_reply(res, HTTP_BAD_REQUEST, "INVALID_REPORT_TYPE");
				return;
			}
		}
	}

	// message: required, 5..1000 UTF-16 units after trimming (the DLL's own rule), <= 4 KiB.
	std::string message;
	{
		const auto message_it = root.find("message");
		if (message_it == root.end() || !message_it->is_string()) {
			needwiki_report_reply(res, HTTP_BAD_REQUEST, "MESSAGE_REQUIRED");
			return;
		}

		size_t units = 0;
		bool has_control = false;
		message = needwiki_report_trim(message_it->get<std::string>(), units, has_control, true);

		if (message.empty()) {
			needwiki_report_reply(res, HTTP_BAD_REQUEST, "MESSAGE_REQUIRED");
			return;
		}
		if (has_control) {
			needwiki_report_reply(res, HTTP_BAD_REQUEST, "INVALID_MESSAGE");
			return;
		}
		if (units < NEEDWIKI_REPORT_MESSAGE_MIN_UNITS) {
			needwiki_report_reply(res, HTTP_BAD_REQUEST, "MESSAGE_TOO_SHORT");
			return;
		}
		if (units > NEEDWIKI_REPORT_MESSAGE_MAX_UNITS || message.size() > NEEDWIKI_REPORT_MESSAGE_MAX_BYTES) {
			needwiki_report_reply(res, HTTP_BAD_REQUEST, "MESSAGE_TOO_LONG");
			return;
		}
	}

	const std::string message_hash = needwiki_crypto::sha256_hex(message);
	const uint64 now = static_cast<uint64>(time(nullptr));
	const uint64 window_start = now - NEEDWIKI_REPORT_WINDOW_SECONDS;
	std::string type_code = report_type;

	// One lock for the duplicate check, the rate limit and the insert: every web-server SQL
	// access shares this mutex, so two requests cannot both pass the checks.
	SQLLock lock(MAP_SQL_LOCK);
	lock.lock();
	auto handle = lock.getHandle();

	uint64 duplicate_id = 0;
	uint64 recent = 0;
	{
		SqlStmt stmt{ *handle };
		if (SQL_SUCCESS != stmt.PrepareStr(
				"SELECT `id` FROM `needwiki_reports` WHERE `remote_ip`=? AND `created_at`>=? "
				"AND `article_id`=? AND `report_type`=? AND `message_hash`=? ORDER BY `id` DESC LIMIT 1")
			|| SQL_SUCCESS != stmt.BindParam(0, SQLDT_STRING, const_cast<char*>(ip.c_str()), ip.size())
			|| SQL_SUCCESS != stmt.BindParam(1, SQLDT_UINT64, const_cast<uint64*>(&window_start), sizeof(window_start))
			|| SQL_SUCCESS != stmt.BindParam(2, SQLDT_STRING, const_cast<char*>(article_id.c_str()), article_id.size())
			|| SQL_SUCCESS != stmt.BindParam(3, SQLDT_STRING, const_cast<char*>(type_code.c_str()), type_code.size())
			|| SQL_SUCCESS != stmt.BindParam(4, SQLDT_STRING, const_cast<char*>(message_hash.c_str()), message_hash.size())
			|| SQL_SUCCESS != stmt.Execute()
			|| SQL_SUCCESS != stmt.BindColumn(0, SQLDT_UINT64, &duplicate_id, sizeof(duplicate_id))) {
			lock.unlock();
			ShowError("[NeedWiki] report DB_ERROR (duplicate check) ip=%s\n", ip.c_str());
			needwiki_report_reply(res, 500, "DB_ERROR");
			return;
		}
		if (stmt.NumRows() == 0 || SQL_SUCCESS != stmt.NextRow())
			duplicate_id = 0;
	}

	if (duplicate_id != 0) {
		lock.unlock();
		ShowNotice("[NeedWiki] report duplicate id=%" PRIu64 " ip=%s article=%s type=%s\n", duplicate_id, ip.c_str(), article_id.c_str(), type_code.c_str());
		needwiki_report_reply(res, 200, "DUPLICATE", duplicate_id);
		return;
	}

	{
		SqlStmt stmt{ *handle };
		if (SQL_SUCCESS != stmt.PrepareStr(
				"SELECT COUNT(*) FROM `needwiki_reports` WHERE `remote_ip`=? AND `created_at`>=?")
			|| SQL_SUCCESS != stmt.BindParam(0, SQLDT_STRING, const_cast<char*>(ip.c_str()), ip.size())
			|| SQL_SUCCESS != stmt.BindParam(1, SQLDT_UINT64, const_cast<uint64*>(&window_start), sizeof(window_start))
			|| SQL_SUCCESS != stmt.Execute()
			|| SQL_SUCCESS != stmt.BindColumn(0, SQLDT_UINT64, &recent, sizeof(recent))
			|| SQL_SUCCESS != stmt.NextRow()) {
			lock.unlock();
			ShowError("[NeedWiki] report DB_ERROR (rate check) ip=%s\n", ip.c_str());
			needwiki_report_reply(res, 500, "DB_ERROR");
			return;
		}
	}

	if (recent >= NEEDWIKI_REPORT_LIMIT_PER_WINDOW) {
		lock.unlock();
		ShowWarning("[NeedWiki] report RATE_LIMITED ip=%s recent=%" PRIu64 "\n", ip.c_str(), recent);
		needwiki_report_reply(res, 429, "RATE_LIMITED");
		return;
	}

	uint64 id = 0;
	{
		SqlStmt stmt{ *handle };
		// Text columns are bound as binary and reinterpreted as UTF-8 by the server, so the
		// connection's default_codepage (euckr) never re-encodes them.
		if (SQL_SUCCESS != stmt.PrepareStr(
				"INSERT INTO `needwiki_reports` (`created_at`,`remote_ip`,`article_id`,`article_title`,"
				"`report_type`,`report_type_raw`,`message`,`message_hash`) VALUES "
				"(?,?,?,CONVERT(? USING utf8mb4),?,CONVERT(? USING utf8mb4),CONVERT(? USING utf8mb4),?)")
			|| SQL_SUCCESS != stmt.BindParam(0, SQLDT_UINT64, const_cast<uint64*>(&now), sizeof(now))
			|| SQL_SUCCESS != stmt.BindParam(1, SQLDT_STRING, const_cast<char*>(ip.c_str()), ip.size())
			|| SQL_SUCCESS != stmt.BindParam(2, SQLDT_STRING, const_cast<char*>(article_id.c_str()), article_id.size())
			|| SQL_SUCCESS != stmt.BindParam(3, SQLDT_BLOB, const_cast<char*>(article_title.c_str()), article_title.size())
			|| SQL_SUCCESS != stmt.BindParam(4, SQLDT_STRING, const_cast<char*>(type_code.c_str()), type_code.size())
			|| SQL_SUCCESS != stmt.BindParam(5, SQLDT_BLOB, const_cast<char*>(report_type_raw.c_str()), report_type_raw.size())
			|| SQL_SUCCESS != stmt.BindParam(6, SQLDT_BLOB, const_cast<char*>(message.c_str()), message.size())
			|| SQL_SUCCESS != stmt.BindParam(7, SQLDT_STRING, const_cast<char*>(message_hash.c_str()), message_hash.size())
			|| SQL_SUCCESS != stmt.Execute()) {
			lock.unlock();
			ShowError("[NeedWiki] report DB_ERROR (insert) ip=%s article=%s\n", ip.c_str(), article_id.c_str());
			needwiki_report_reply(res, 500, "DB_ERROR");
			return;
		}
		id = stmt.LastInsertId();
	}
	lock.unlock();

	// Never the title or the message (log injection, personal data) - they are in the DB.
	ShowNotice("[NeedWiki] report stored id=%" PRIu64 " ip=%s article=%s type=%s\n", id, ip.c_str(), article_id.c_str(), type_code.c_str());
	needwiki_report_reply(res, 200, "OK", id);
}
