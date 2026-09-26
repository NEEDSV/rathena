// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef NEEDWIKI_ITEM_GROUP_HPP
#define NEEDWIKI_ITEM_GROUP_HPP

// NEED Wiki item group DB (db/import/wiki_item_group.yml), shared by the web-server
// (/api/wiki/itemgroups) and the map-server (ITEMGROUP click chat).
//
// Source encoding policy: the file is CP949 without a BOM, like the rest of the Korean
// server data. Nothing here depends on the OS the server runs on:
//   - the map-server sends Name / NameEN to the Ragnarok client exactly as stored (CP949),
//   - the web-server converts Name to UTF-8 with cp949_to_utf8() before building JSON.
// A file that was re-saved as UTF-8, or with a BOM, is rejected by parse() instead of
// being shown as mojibake.

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <ryml.hpp>
#include <ryml_std.hpp>

#ifdef _WIN32
	// common/winapi.hpp defines NONLS, which hides the NLS conversion API.
	extern "C" {
		__declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int CodePage, unsigned long dwFlags, const char* lpMultiByteStr, int cbMultiByte, wchar_t* lpWideCharStr, int cchWideChar);
		__declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int CodePage, unsigned long dwFlags, const wchar_t* lpWideCharStr, int cchWideChar, char* lpMultiByteStr, int cbMultiByte, const char* lpDefaultChar, int* lpUsedDefaultChar);
	}
#else
	#include <cerrno>
	#include <iconv.h>
#endif

namespace needwiki_item_group {

static constexpr const char* DB_TYPE = "NEED_WIKI_ITEM_GROUP_DB";
static constexpr unsigned int DB_VERSION = 1;
static constexpr size_t GROUP_ID_MAX = 64;

struct Group {
	std::string id;
	std::string name;     // Korean display name, CP949 bytes as stored in the file
	std::string name_en;  // optional English display name, printable ASCII only
	std::vector<std::string> items;  // AegisNames, resolved by the map-server
};

/// The file location, relative to the server root like every other rAthena DB file.
inline std::string db_file(const char* db_path)
{
	return std::string(db_path) + "/import/wiki_item_group.yml";
}

inline bool valid_group_id(const std::string& id)
{
	if (id.empty() || id.size() > GROUP_ID_MAX)
		return false;

	for (const char ch : id) {
		if ((ch < 'a' || ch > 'z') && (ch < 'A' || ch > 'Z') &&
			(ch < '0' || ch > '9') && ch != '_' && ch != '-')
			return false;
	}

	return true;
}

inline bool is_printable_ascii(const std::string& text)
{
	for (const unsigned char ch : text) {
		if (ch < 0x20 || ch > 0x7E)
			return false;
	}

	return true;
}

inline bool has_non_ascii(const std::string& text)
{
	for (const unsigned char ch : text) {
		if (ch >= 0x80)
			return true;
	}

	return false;
}

/// Strict UTF-8 check (no overlongs, no surrogates, nothing above U+10FFFF).
inline bool is_valid_utf8(const std::string& text)
{
	const unsigned char* p = reinterpret_cast<const unsigned char*>(text.data());
	const unsigned char* end = p + text.size();

	while (p < end) {
		const unsigned char c = *p;
		size_t len = 0;
		unsigned int cp = 0;

		if (c < 0x80) { ++p; continue; }
		else if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1F; }
		else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0F; }
		else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07; }
		else return false;

		if (static_cast<size_t>(end - p) < len)
			return false;

		for (size_t i = 1; i < len; ++i) {
			if ((p[i] & 0xC0) != 0x80)
				return false;
			cp = (cp << 6) | (p[i] & 0x3F);
		}

		if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
			(cp >= 0xD800 && cp <= 0xDFFF))
			return false;

		p += len;
	}

	return true;
}

/// Source policy check on the raw file bytes. Returns an empty string when the file is
/// acceptable, otherwise the reason it is not.
inline std::string source_policy_error(const std::string& bytes)
{
	if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
		static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF)
		return "the file starts with a UTF-8 BOM; it must be CP949 without a BOM";

	// Korean CP949 text is never valid UTF-8 (its lead bytes 0xB0-0xC8 are UTF-8
	// continuation bytes), so non-ASCII content that IS valid UTF-8 means the file was
	// re-saved in the wrong encoding.
	if (has_non_ascii(bytes) && is_valid_utf8(bytes))
		return "the file is UTF-8; it must be CP949 without a BOM";

	return std::string();
}

/// CP949 -> UTF-8. Returns false when the input is not valid CP949.
inline bool cp949_to_utf8(const std::string& cp949, std::string& utf8)
{
	utf8.clear();

	if (!has_non_ascii(cp949)) {
		utf8 = cp949;
		return true;
	}

#ifdef _WIN32
	static constexpr unsigned int CODEPAGE_CP949 = 949;
	static constexpr unsigned int CODEPAGE_UTF8 = 65001;
	static constexpr unsigned long FLAG_ERR_INVALID_CHARS = 0x00000008;  // MB_ERR_INVALID_CHARS

	const int wide_len = MultiByteToWideChar(CODEPAGE_CP949, FLAG_ERR_INVALID_CHARS, cp949.data(), static_cast<int>(cp949.size()), nullptr, 0);
	if (wide_len <= 0)
		return false;

	std::wstring wide(static_cast<size_t>(wide_len), L'\0');
	if (MultiByteToWideChar(CODEPAGE_CP949, FLAG_ERR_INVALID_CHARS, cp949.data(), static_cast<int>(cp949.size()), &wide[0], wide_len) != wide_len)
		return false;

	const int utf8_len = WideCharToMultiByte(CODEPAGE_UTF8, 0, wide.data(), wide_len, nullptr, 0, nullptr, nullptr);
	if (utf8_len <= 0)
		return false;

	utf8.assign(static_cast<size_t>(utf8_len), '\0');
	if (WideCharToMultiByte(CODEPAGE_UTF8, 0, wide.data(), wide_len, &utf8[0], utf8_len, nullptr, nullptr) != utf8_len) {
		utf8.clear();
		return false;
	}

	return true;
#else
	iconv_t cd = iconv_open("UTF-8", "CP949");
	if (cd == reinterpret_cast<iconv_t>(-1))
		return false;

	std::string input = cp949;
	char* in = &input[0];
	size_t in_left = input.size();
	std::string output(input.size() * 3 + 4, '\0');
	char* out = &output[0];
	size_t out_left = output.size();
	const size_t result = iconv(cd, &in, &in_left, &out, &out_left);
	iconv_close(cd);

	if (result == static_cast<size_t>(-1) || in_left != 0)
		return false;

	output.resize(output.size() - out_left);
	utf8.swap(output);
	return true;
#endif
}

/// The name a player sees in chat: NameEN for an English session when it exists,
/// otherwise the Korean Name.
inline const std::string& display_name(const Group& group, bool english)
{
	return english && !group.name_en.empty() ? group.name_en : group.name;
}

/// Parses the raw file bytes. Throws std::runtime_error when the file cannot be used at
/// all (encoding policy, YAML syntax, header). Groups that are individually invalid are
/// skipped and reported through `warnings`.
inline std::vector<Group> parse(const std::string& path, const std::string& bytes, std::vector<std::string>& warnings)
{
	const std::string policy = source_policy_error(bytes);
	if (!policy.empty())
		throw std::runtime_error(policy);

	ryml::Parser parser;
	ryml::Tree tree = parser.parse_in_arena(c4::to_csubstr(path), c4::to_csubstr(bytes));
	const ryml::NodeRef root = tree.rootref();

	if (!root.is_map() || !root.has_child("Header"))
		throw std::runtime_error("Header node not found");

	const ryml::NodeRef header = root["Header"];
	if (!header.has_child("Type") || !header.has_child("Version"))
		throw std::runtime_error("Header requires Type and Version");

	std::string type;
	unsigned int version = 0;
	header["Type"] >> type;
	header["Version"] >> version;
	if (type != DB_TYPE || version != DB_VERSION)
		throw std::runtime_error("Unsupported item group DB header type='" + type + "' version=" + std::to_string(version));

	if (!root.has_child("Groups"))
		throw std::runtime_error("Groups node not found");

	std::vector<Group> groups;
	for (const ryml::NodeRef& node : root["Groups"]) {
		if (!node.is_map() || !node.has_child("Id") || !node.has_child("Name")) {
			warnings.push_back("Skipping item group with missing Id or Name.");
			continue;
		}

		Group group;
		node["Id"] >> group.id;
		if (node["Name"].has_val() && !node["Name"].val_is_null())
			node["Name"] >> group.name;

		if (!valid_group_id(group.id) || group.name.empty()) {
			warnings.push_back("Skipping invalid item group id='" + group.id + "'.");
			continue;
		}

		if (node.has_child("NameEN") && node["NameEN"].has_val() && !node["NameEN"].val_is_null()) {
			node["NameEN"] >> group.name_en;

			// The English name goes to English clients as-is, so it has to be plain ASCII.
			if (!is_printable_ascii(group.name_en)) {
				warnings.push_back("Ignoring NameEN of item group '" + group.id + "': it must be printable ASCII.");
				group.name_en.clear();
			}
		}

		if (node.has_child("Items")) {
			for (const ryml::NodeRef& item_node : node["Items"]) {
				std::string aegis_name;
				item_node >> aegis_name;
				if (!aegis_name.empty())
					group.items.push_back(aegis_name);
			}
		}

		groups.push_back(std::move(group));
	}

	return groups;
}

} // namespace needwiki_item_group

#endif /* NEEDWIKI_ITEM_GROUP_HPP */
