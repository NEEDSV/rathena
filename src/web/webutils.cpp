// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder


#include "webutils.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>

#include <common/cbasetypes.hpp>
#include <common/showmsg.hpp>

static std::string base64Encode(const std::string& in) {
	static const char lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::string out;
	out.reserve(((in.size() + 2) / 3) * 4);

	int val = 0;
	int valb = -6;
	for (unsigned char c : in) {
		val = (val << 8) + c;
		valb += 8;
		while (valb >= 0) {
			out.push_back(lookup[(val >> valb) & 0x3F]);
			valb -= 6;
		}
	}

	if (valb > -6) {
		out.push_back(lookup[((val << 8) >> (valb + 8)) & 0x3F]);
	}

	while (out.size() % 4) {
		out.push_back('=');
	}

	return out;
}

static std::string base64Decode(const std::string& in) {
	static const signed char lookup[256] = {
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
		52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
		-1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
		15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
		-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
		41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
	};

	std::string out;
	out.reserve((in.size() / 4) * 3);

	int val = 0;
	int valb = -8;
	for (unsigned char c : in) {
		if (c == '=') {
			break;
		}

		if (lookup[c] < 0) {
			throw std::invalid_argument("Invalid base64 data");
		}

		val = (val << 6) + lookup[c];
		valb += 6;
		if (valb >= 0) {
			out.push_back(static_cast<char>((val >> valb) & 0xFF));
			valb -= 8;
		}
	}

	return out;
}

static std::string hexDumpEdge(const std::string& data, size_t offset, size_t length) {
	static const char digits[] = "0123456789ABCDEF";

	std::string out;
	out.reserve(length * 3);

	for (size_t i = 0; i < length && offset + i < data.size(); ++i) {
		if (!out.empty()) {
			out.push_back(' ');
		}

		const auto byte = static_cast<unsigned char>(data[offset + i]);
		out.push_back(digits[byte >> 4]);
		out.push_back(digits[byte & 0x0F]);
	}

	return out;
}

static void logConfigDataSaveFailureImpl(const char* config_type, int account_id, int char_id, const std::string& world_name, const std::string& data) {
	const size_t dump_len = std::min<size_t>(64, data.size());
	const size_t tail_offset = data.size() > 64 ? data.size() - 64 : 0;
	const std::string head = hexDumpEdge(data, 0, dump_len);
	const std::string tail = hexDumpEdge(data, tail_offset, dump_len);

	if (char_id >= 0) {
		ShowError("[%s] save failed: account_id=%d char_id=%d world_name=\"%s\" data_length=%" PRIuPTR "\n",
			config_type, account_id, char_id, world_name.c_str(), static_cast<uintptr>(data.size()));
	} else {
		ShowError("[%s] save failed: account_id=%d world_name=\"%s\" data_length=%" PRIuPTR "\n",
			config_type, account_id, world_name.c_str(), static_cast<uintptr>(data.size()));
	}

	ShowError("[%s] data head 64 bytes hex: %s\n", config_type, head.c_str());
	ShowError("[%s] data tail 64 bytes hex: %s\n", config_type, tail.c_str());
}


/**
 * Merge patch into orig recursively
 * if merge_null is true, this operates like json::merge_patch
 * if merge_null is false, then if patch has null, it does not override orig
 * Returns true on success
 */
bool mergeData(nlohmann::json &orig, const nlohmann::json &patch, bool merge_null) {
	if (!patch.is_object()) {
		// then it's a value
		if ((patch.is_null() && merge_null) || (!patch.is_null())) {
			orig = patch;
		}
		return true;
	}

	if (!orig.is_object()) {
		orig = nlohmann::json::object();
	}

	for (auto it = patch.begin(); it != patch.end(); ++it) {
		if (it.value().is_null()) {
			if (merge_null) {
				orig.erase(it.key());
			}
		} else {
			mergeData(orig[it.key()], it.value(), merge_null);
		}
	}
	return true;
}

std::string encodeConfigDataForDb(const std::string& data) {
	nlohmann::json wrapper;

	wrapper["_rathena_config_data"] = {
		{ "encoding", "base64" },
		{ "data", base64Encode(data) }
	};

	return wrapper.dump();
}

bool decodeConfigDataFromDb(const std::string& db_data, std::string& data) {
	try {
		const auto wrapper = nlohmann::json::parse(db_data);

		if (wrapper.is_object() && wrapper.contains("_rathena_config_data")) {
			const auto& payload = wrapper["_rathena_config_data"];

			if (!payload.is_object() ||
				!payload.contains("encoding") ||
				!payload.contains("data") ||
				payload["encoding"] != "base64" ||
				!payload["data"].is_string()) {
				return false;
			}

			data = base64Decode(payload["data"].get<std::string>());
			return true;
		}
	} catch (const std::exception&) {
		return false;
	}

	data = db_data;
	return true;
}

void logConfigDataSaveFailure(const char* config_type, int account_id, const std::string& world_name, const std::string& data) {
	logConfigDataSaveFailureImpl(config_type, account_id, -1, world_name, data);
}

void logConfigDataSaveFailure(const char* config_type, int account_id, int char_id, const std::string& world_name, const std::string& data) {
	logConfigDataSaveFailureImpl(config_type, account_id, char_id, world_name, data);
}
