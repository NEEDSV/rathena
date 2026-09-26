// NEED Wiki item group DB unit test for src/common/needwiki_item_group.hpp.
// Built and run by tools/needwiki/run_tests.py:
//   needwiki_item_group_test <wiki_item_group.yml> <expected.tsv>
// expected.tsv (written by the runner from Python's own cp949 codec), one group per line:
//   <id> TAB <Name as CP949 hex> TAB <Name as UTF-8 hex> TAB <NameEN as hex, empty when the group has none>

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <common/needwiki_item_group.hpp>

namespace nig = needwiki_item_group;

static int failures = 0;
static int checks = 0;

#define CHECK(cond, what) do { ++checks; if (!(cond)) { ++failures; std::printf("FAIL  %s\n", what); } else { std::printf("PASS  %s\n", what); } } while (0)

static void on_yaml_error(const char* msg, size_t, ryml::Location, void*)
{
	throw std::runtime_error(msg);
}

static std::string read_file(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	std::ostringstream s;
	s << in.rdbuf();
	return s.str();
}

static std::string unhex(const std::string& hex)
{
	std::string out;
	for (size_t i = 0; i + 1 < hex.size(); i += 2)
		out.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
	return out;
}

static bool throws_with(const std::string& bytes, const char* needle)
{
	std::vector<std::string> warnings;
	try {
		nig::parse("fixture.yml", bytes, warnings);
	} catch (const std::exception& e) {
		return std::string(e.what()).find(needle) != std::string::npos;
	}
	return false;
}

static std::vector<nig::Group> parse_ok(const std::string& bytes, std::vector<std::string>& warnings)
{
	return nig::parse("fixture.yml", bytes, warnings);
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::printf("usage: %s <wiki_item_group.yml> <expected.tsv>\n", argv[0]);
		return 2;
	}
	ryml::set_callbacks(ryml::Callbacks(nullptr, nullptr, nullptr, on_yaml_error));

	// ---- the real CP949 source ---------------------------------------------------------
	const std::string source = read_file(argv[1]);
	struct Expected { std::string cp949, utf8, name_en; };
	std::map<std::string, Expected> expected;
	{
		std::istringstream lines(read_file(argv[2]));
		std::string line;
		while (std::getline(lines, line)) {
			std::istringstream cols(line);
			std::string id, cp949_hex, utf8_hex, en_hex;
			std::getline(cols, id, '\t');
			std::getline(cols, cp949_hex, '\t');
			std::getline(cols, utf8_hex, '\t');
			std::getline(cols, en_hex, '\t');
			if (!id.empty())
				expected[id] = { unhex(cp949_hex), unhex(utf8_hex), unhex(en_hex) };
		}
	}

	std::vector<std::string> warnings;
	std::vector<nig::Group> groups;
	try {
		groups = nig::parse(argv[1], source, warnings);
	} catch (const std::exception& e) {
		std::printf("FAIL  real file did not parse: %s\n", e.what());
		return 1;
	}

	CHECK(nig::source_policy_error(source).empty(), "real file passes the CP949 source policy");
	CHECK(groups.size() == 21 && expected.size() == 21, "real file: 21 groups");
	CHECK(warnings.empty(), "real file: no warnings");

	size_t web_ok = 0, chat_ok = 0, en_ok = 0, with_en = 0, items_ok = 0;
	nlohmann::json body = nlohmann::json::object();
	for (const nig::Group& g : groups) {
		const auto it = expected.find(g.id);
		if (it == expected.end())
			continue;

		std::string utf8;
		if (nig::cp949_to_utf8(g.name, utf8) && utf8 == it->second.utf8)
			++web_ok;
		body[g.id] = utf8;

		// KR session: the chat gets the stored CP949 bytes unchanged (NameEN never leaks into KR).
		if (nig::display_name(g, false) == it->second.cp949)
			++chat_ok;
		// EN session: NameEN when the group has one, otherwise the same Korean bytes.
		const std::string& want_en = it->second.name_en.empty() ? it->second.cp949 : it->second.name_en;
		if (g.name_en == it->second.name_en && nig::display_name(g, true) == want_en)
			++en_ok;
		if (!g.name_en.empty())
			++with_en;
		if (!g.items.empty())
			++items_ok;
	}
	CHECK(web_ok == 21, "CP949 Name -> UTF-8 equals Python's cp949 decode (21/21)");
	CHECK(chat_ok == 21, "KR chat name bytes == CP949 source bytes (21/21)");
	CHECK(en_ok == 21, "EN session name == NameEN, or the KR Name when a group has none (21/21)");
	std::printf("INFO  groups with NameEN: %zu/21\n", with_en);
	CHECK(items_ok == 21, "every real group has items");

	bool dump_ok = false;
	try {
		const std::string dumped = body.dump();
		dump_ok = nlohmann::json::parse(dumped) == body && nig::is_valid_utf8(dumped);
	} catch (const std::exception&) {
		dump_ok = false;
	}
	CHECK(dump_ok, "Web JSON dump of the converted names is valid UTF-8 (no type_error.316)");

	// The pre-W4-B path: dumping the raw CP949 name must still fail - proves the conversion
	// is what fixes the 500, not a changed file.
	bool raw_dump_throws = false;
	try {
		nlohmann::json raw = nlohmann::json::object();
		raw["x"] = groups.front().name;
		(void)raw.dump();
	} catch (const nlohmann::json::type_error&) {
		raw_dump_throws = true;
	}
	CHECK(raw_dump_throws, "control: raw CP949 name in JSON still throws type_error");

	// ---- NameEN ------------------------------------------------------------------------
	const std::string kr = "\xBB\xFD\xB4\xF8 \xC1\xF8\xBA\xB8 \xC4\xAB\xB5\xE5";  // CP949 "saengdeon jinbo kadeu"
	const std::string header = "Header:\n  Type: NEED_WIKI_ITEM_GROUP_DB\n  Version: 1\n\nGroups:\n";
	{
		std::vector<std::string> w;
		const auto g = parse_ok(header + "  - Id: A\n    Name: " + kr + "\n    NameEN: Test Boss Cards\n    Items:\n      - Apple\n", w);
		CHECK(g.size() == 1 && g[0].name_en == "Test Boss Cards", "NameEN ASCII is parsed");
		CHECK(g.size() == 1 && nig::display_name(g[0], true) == "Test Boss Cards", "EN session -> NameEN");
		CHECK(g.size() == 1 && nig::display_name(g[0], false) == kr, "KR session with NameEN -> KR Name");
		CHECK(w.empty(), "NameEN ASCII: no warning");
	}
	{
		std::vector<std::string> w;
		const auto g = parse_ok(header + "  - Id: A\n    Name: " + kr + "\n    NameEN:\n    Items:\n      - Apple\n"
			"  - Id: B\n    Name: " + kr + "\n    NameEN: ~\n    Items:\n      - Apple\n", w);
		CHECK(g.size() == 2 && nig::display_name(g[0], true) == kr && nig::display_name(g[1], true) == kr,
			"NameEN empty / null -> KR fallback");
	}
	{
		std::vector<std::string> w;
		const auto g = parse_ok(header + "  - Id: A\n    Name: " + kr + "\n    NameEN: " + kr + "\n    Items:\n      - Apple\n", w);
		CHECK(g.size() == 1 && g[0].name_en.empty() && nig::display_name(g[0], true) == kr && w.size() == 1,
			"NameEN non-ASCII is ignored with a warning -> KR fallback");
	}

	// ---- source policy -----------------------------------------------------------------
	CHECK(throws_with(std::string("\xEF\xBB\xBF") + source, "BOM"), "BOM file is rejected");
	{
		// the real file re-saved as UTF-8 (converted here with the helper under test)
		std::string utf8_file;
		std::string line;
		std::istringstream in(source);
		bool converted = true;
		while (std::getline(in, line)) {
			std::string u;
			converted = converted && nig::cp949_to_utf8(line, u);
			utf8_file += u + "\n";
		}
		CHECK(converted && throws_with(utf8_file, "UTF-8"), "UTF-8 re-saved file is rejected");
	}
	{
		std::vector<std::string> w;
		const auto g = parse_ok(header + "  - Id: Ascii_only\n    Name: ASCII name\n    Items:\n      - Apple\n", w);
		CHECK(g.size() == 1, "ASCII-only file is accepted");
	}
	CHECK(throws_with("Header:\n  Type: OTHER\n  Version: 1\nGroups:\n", "Unsupported"), "wrong header type is rejected");
	CHECK(throws_with("Groups: [", ""), "broken YAML throws (ryml callback)");
	{
		std::vector<std::string> w;
		const auto g = parse_ok(header + "  - Id: bad id\n    Name: x\n  - Name: y\n  - Id: ok\n    Name: z\n", w);
		CHECK(g.size() == 1 && g[0].id == "ok" && w.size() == 2, "invalid groups are skipped with warnings");
	}

	// ---- conversion helpers ------------------------------------------------------------
	{
		std::string out;
		CHECK(nig::cp949_to_utf8("plain ASCII", out) && out == "plain ASCII", "ASCII passes through unchanged");
		CHECK(!nig::cp949_to_utf8(std::string("\xB0"), out), "truncated CP949 lead byte is rejected");
		CHECK(!nig::cp949_to_utf8(std::string("a\xFF" "b"), out), "byte 0xFF is rejected");
		CHECK(nig::is_valid_utf8("\xEA\xB0\x80") && !nig::is_valid_utf8("\xC0\x80") && !nig::is_valid_utf8("\xED\xA0\x80"),
			"UTF-8 validator: accepts U+AC00, rejects overlong and surrogate");
		CHECK(nig::valid_group_id("Time_boots_") && nig::valid_group_id("a-b") && !nig::valid_group_id("a b") &&
			!nig::valid_group_id(std::string(65, 'a')), "group id rule");
	}

	std::printf("\n%d/%d passed\n", checks - failures, checks);
	return failures == 0 ? 0 : 1;
}
