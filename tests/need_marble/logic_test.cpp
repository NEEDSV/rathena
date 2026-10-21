// NEED marble board - offline unit test of src/map/need_marble_logic.hpp (no server, no SQL).
// Build (MSVC):  cl /nologo /std:c++17 /EHsc /W4 /I src tests\need_marble\logic_test.cpp /Fe:<scratch>\marble_logic_test.exe
// Exit code 0 = every check passed.

#include <cstdio>
#include <cstdlib>

#include "map/need_marble_logic.hpp"

using namespace need_marble;

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond) \
	do { \
		if (cond) { \
			g_pass++; \
		} else { \
			g_fail++; \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

static_assert(tile_rate_key(TILE_KIND_MAX - 1, RATE_SLOTS - 1) < POLICY_MAX, "tile rate keys must fit the policy array");
static_assert(tile_cp_key(TILE_KIND_MAX - 1, RATE_SLOTS - 1) < POLICY_MAX, "tile CP keys must fit the policy array");

static void test_policy_defaults() {
	const policy_values v = policy_defaults();
	int bad = -1;
	CHECK(validate_policy(v, &bad) == PE_OK);
	// INITIAL_BALANCE_V1_0 sums
	CHECK(v[P_DAILY_HUNT_CAP] == 100);
	CHECK(v[P_DAILY_INSTANCE_CAP] == 100);
	CHECK(v[P_DAILY_BG_CAP] == 100);
	CHECK(v[P_DAILY_TOTAL_CAP] == 200);
	CHECK(v[P_HUNT_TIER_POINTS] * v[P_HUNT_MAX_TIERS] == v[P_DAILY_HUNT_CAP]);
	CHECK(v[P_HUNT_TIER_KILLS] * v[P_HUNT_MAX_TIERS] == 400);
	CHECK(v[P_INSTANCE_POINTS] * 2 == v[P_DAILY_INSTANCE_CAP]);
	CHECK(v[P_BG_POINTS] * 2 == v[P_DAILY_BG_CAP]);
	CHECK(v[P_DAILY_TOTAL_CAP] / v[P_EXCHANGE_COST] == 4);   // theoretical 4 dice per day
	CHECK(v[P_EXCHANGE_COST] == 50);
	CHECK(v[P_DICE_HOLD_MAX] == 30);
	CHECK(v[P_EXCHANGE_DAILY_MAX] == 0);
	CHECK(v[P_HUNT_LEVEL_DIFF] == 30);
	CHECK(v[P_LAP_STEPS] == 33);
	CHECK(v[P_LAP_CP] == 10000);
	// switches default OFF
	CHECK(v[P_ENABLED] == 0);
	CHECK(v[P_CP_APPLY_ENABLED] == 0);
	CHECK(v[P_BG_ENABLED] == 0);
	CHECK(v[P_DUNGEON_AUTO_SAFE] == 0);
}

static void test_policy_rejects() {
	policy_values v = policy_defaults();
	v[P_CP_APPLY_ENABLED] = 1;
	CHECK(validate_policy(v) == PE_CP_SWITCH);

	v = policy_defaults();
	v[tile_rate_key(TILE_CITY, 0)] = 4999;   // sum 9999
	CHECK(validate_policy(v) == PE_RATE_SUM);

	v = policy_defaults();
	v[P_WARP_DEST_7] = 22;                   // a warp tile
	CHECK(validate_policy(v) == PE_WARP_DEST);
	v[P_WARP_DEST_7] = 0;                    // START
	CHECK(validate_policy(v) == PE_WARP_DEST);
	v[P_WARP_DEST_7] = 33;                   // off board
	CHECK(validate_policy(v) == PE_WARP_DEST);

	v = policy_defaults();
	v[P_EXCHANGE_COST] = 0;
	CHECK(validate_policy(v) == PE_EXCHANGE);

	v = policy_defaults();
	v[P_HUNT_TIER_KILLS] = 0;
	CHECK(validate_policy(v) == PE_HUNT);

	v = policy_defaults();
	v[P_DUNGEON_CHALLENGE_RATE] = 10001;
	CHECK(validate_policy(v) == PE_DUNGEON);
}

static void test_board() {
	int count[TILE_KIND_MAX] = {};
	for (int32_t k : BOARD)
		count[k]++;
	CHECK(BOARD.size() == 33);
	CHECK(count[TILE_CITY] == 14);
	CHECK(count[TILE_EVENT] == 4);
	CHECK(count[TILE_TREASURE] == 3);
	CHECK(count[TILE_MONSTER] == 3);
	CHECK(count[TILE_WARP] == 2);
	CHECK(count[TILE_START] == 1);
	CHECK(count[TILE_PORING] == 1);
	CHECK(count[TILE_BONUS] == 1);
	CHECK(count[TILE_TAX] == 1);
	CHECK(count[TILE_BOSS] == 1);
	CHECK(count[TILE_DUNGEON] == 1);
	CHECK(count[TILE_JAIL] == 1);
	CHECK(BOARD[0] == TILE_START && BOARD[7] == TILE_WARP && BOARD[22] == TILE_WARP && BOARD[17] == TILE_DUNGEON);

	const policy_values v = policy_defaults();
	CHECK(warp_destination(v, 7) == 13 && BOARD[13] == TILE_CITY);
	CHECK(warp_destination(v, 22) == 28 && BOARD[28] == TILE_CITY);
	CHECK(warp_destination(v, 5) == -1);
}

static void test_rates() {
	const policy_values v = policy_defaults();
	for (int32_t kind = TILE_CITY; kind < TILE_KIND_MAX; kind++) {
		if (!tile_uses_table(kind))
			continue;
		int32_t sum = 0;
		for (int32_t s = 0; s < RATE_SLOTS; s++)
			sum += v[tile_rate_key(kind, s)];
		CHECK(sum == RATE_SCALE);
	}
	// City 50% 500 / 40% 1000 / 10% 2000: boundaries
	CHECK(pick_outcome(v, TILE_CITY, 1).cp == 500);
	CHECK(pick_outcome(v, TILE_CITY, 5000).cp == 500);
	CHECK(pick_outcome(v, TILE_CITY, 5001).cp == 1000);
	CHECK(pick_outcome(v, TILE_CITY, 9000).cp == 1000);
	CHECK(pick_outcome(v, TILE_CITY, 9001).cp == 2000);
	CHECK(pick_outcome(v, TILE_CITY, 10000).cp == 2000);
	CHECK(pick_outcome(v, TILE_CITY, 0).slot == -1);
	CHECK(pick_outcome(v, TILE_CITY, 10001).slot == -1);
	CHECK(pick_outcome(v, TILE_WARP, 1).slot == -1);      // warp is not a table tile
	CHECK(pick_outcome(v, TILE_DUNGEON, 1).slot == -1);   // dungeon uses the choice rule
	CHECK(pick_outcome(v, TILE_BOSS, 2500).cp == 8000);
	CHECK(pick_outcome(v, TILE_BOSS, 2501).cp == 0);
	CHECK(pick_outcome(v, TILE_MONSTER, 7500).cp == 1500);
	CHECK(pick_outcome(v, TILE_MONSTER, 7501).cp == 0);
	CHECK(pick_outcome(v, TILE_TREASURE, 9501).cp == 10000);
	CHECK(pick_outcome(v, TILE_EVENT, 4500).cp == 0);
	CHECK(pick_outcome(v, TILE_EVENT, 4501).cp == 1500);
	CHECK(pick_outcome(v, TILE_PORING, 7777).cp == 1500);
	CHECK(pick_outcome(v, TILE_BONUS, 1).cp == 3000);
	CHECK(pick_outcome(v, TILE_TAX, 1).cp == 0 && pick_outcome(v, TILE_TAX, 1).slot == 0);
	CHECK(pick_outcome(v, TILE_JAIL, 10000).cp == 0);
	CHECK(pick_outcome(v, TILE_START, 5000).cp == 0);

	CHECK(dungeon_cp(v, 1, 0) == 1000);
	CHECK(dungeon_cp(v, 2, 4000) == 4000);
	CHECK(dungeon_cp(v, 2, 4001) == 0);
	CHECK(dungeon_cp(v, 3, 1) == 0);

	// Expected tile CP per arrival with a uniform landing (v1.0 sec.3: about 1,086 with "challenge").
	double total_safe = 0, total_chal = 0;
	for (int32_t kind : BOARD) {
		double e = 0;
		if (tile_uses_table(kind)) {
			for (int32_t s = 0; s < RATE_SLOTS; s++)
				e += v[tile_rate_key(kind, s)] / 10000.0 * v[tile_cp_key(kind, s)];
			total_safe += e;
			total_chal += e;
		} else if (kind == TILE_DUNGEON) {
			total_safe += v[P_DUNGEON_SAFE_CP];
			total_chal += v[P_DUNGEON_CHALLENGE_RATE] / 10000.0 * v[P_DUNGEON_CHALLENGE_CP];
		}
	}
	std::printf("uniform tile EV: safe %.1f / challenge %.1f CP per roll\n", total_safe / 33, total_chal / 33);
	CHECK(static_cast<int>(total_chal / 33 * 10 + 0.5) == 10856);
	CHECK(static_cast<int>(total_safe / 33 * 10 + 0.5) == 10674);
}

static void test_logical_day() {
	// 04:00 KST = 19:00 UTC of the previous calendar day; the OS time zone plays no role.
	CHECK(logical_day(1792436399) == 20261019u);   // 2026-10-20 03:59:59 KST
	CHECK(logical_day(1792436400) == 20261020u);   // 2026-10-20 04:00:00 KST
	CHECK(logical_day(1798743599) == 20261231u);   // 2027-01-01 03:59:59 KST
	CHECK(logical_day(1798743600) == 20270101u);   // 2027-01-01 04:00:00 KST
	CHECK(logical_day(1835377200) == 20280229u);   // leap day
	CHECK(logical_day(1835463600) == 20280301u);
	CHECK(logical_day(0) == 19700101u);            // 1970-01-01 09:00 KST
	CHECK(yyyymmdd_from_days(0) == 19700101u);
	CHECK(yyyymmdd_from_days(-1) == 19691231u);
}

static void test_credit() {
	// requested, earned_act, cap_act, earned_total, cap_total
	CHECK(credit_amount(25, 0, 100, 0, 200) == 25);
	CHECK(credit_amount(25, 90, 100, 0, 200) == 10);
	CHECK(credit_amount(25, 100, 100, 0, 200) == 0);
	CHECK(credit_amount(50, 0, 100, 180, 200) == 20);
	CHECK(credit_amount(50, 0, 100, 200, 200) == 0);
	CHECK(credit_amount(50, 120, 100, 0, 200) == 0);   // over cap stays 0, never negative
	// Exchanges do not reopen the cap: the cap only reads EARN totals (earned), never the score.
	int32_t earned = 0;
	for (int i = 0; i < 10; i++)
		earned += credit_amount(50, earned, 100, earned, 200);
	CHECK(earned == 100);
}

static void test_hunt_tiers() {
	CHECK(hunt_next_tier(0, 1, 100, 4) == 1);
	CHECK(hunt_next_tier(99, 1, 100, 4) == 1);
	CHECK(hunt_next_tier(100, 1, 100, 4) == 2);
	CHECK(hunt_next_tier(399, 1, 100, 4) == 4);
	CHECK(hunt_next_tier(400, 1, 100, 4) == 5);
	CHECK(hunt_next_tier(4000000000u, 1, 100, 4) == 5);   // no runaway tier count (v0.4 TINYINT overflow)
	CHECK(hunt_next_tier(500, 5, 100, 4) == 5);
	CHECK(hunt_next_tier(250, 2, 100, 4) == 3);
	// F2: a day is capped once next_tier passed max_tiers
	CHECK(!hunt_day_maxed(1, 4));
	CHECK(!hunt_day_maxed(4, 4));
	CHECK(hunt_day_maxed(5, 4));
	CHECK(hunt_day_maxed(hunt_next_tier(400, 1, 100, 4), 4));
	CHECK(!hunt_day_maxed(hunt_next_tier(399, 1, 100, 4), 4));
	CHECK(hunt_day_maxed(1, 0));   // max_tiers 0 = hunting gives nothing that day
}

// V22 fixtures: one login feeding kills through the flush / checkpoint order of need_marble.cpp
// (layer 1 lookup at the first kill, a batch every flush_kills, layer 2 refusal on a capped day).
struct hunt_day_sim {
	uint32_t kills;
	int32_t next_tier;
	int32_t earned_hunt;
	int32_t earned_total;
	uint32_t batches = 0;
	int32_t credited = 0;
	int32_t tier_rows = 0;
	uint32_t pending = 0;
	int32_t done = -1;

	hunt_day_sim(uint32_t k, int32_t nt, int32_t eh, int32_t et) : kills(k), next_tier(nt), earned_hunt(eh), earned_total(et) {}

	void checkpoint(const policy_values& v) {
		if (hunt_day_maxed(next_tier, v[P_HUNT_MAX_TIERS])) {
			pending = 0;
			done = 1;
			return;
		}
		kills += pending;
		pending = 0;
		batches++;
		const int32_t nn = hunt_next_tier(kills, next_tier, v[P_HUNT_TIER_KILLS], v[P_HUNT_MAX_TIERS]);
		for (int32_t t = next_tier; t < nn; t++) {
			const int32_t c = credit_amount(v[P_HUNT_TIER_POINTS], earned_hunt, v[P_DAILY_HUNT_CAP], earned_total, v[P_DAILY_TOTAL_CAP]);
			earned_hunt += c;
			earned_total += c;
			credited += c;
			tier_rows++;
		}
		next_tier = nn;
		if (hunt_day_maxed(next_tier, v[P_HUNT_MAX_TIERS]))
			done = 1;
	}

	void feed(const policy_values& v, uint32_t n) {
		for (uint32_t i = 0; i < n; i++) {
			if (done < 0)
				done = hunt_day_maxed(next_tier, v[P_HUNT_MAX_TIERS]) ? 1 : 0;
			if (done == 1)
				continue;
			if (++pending >= static_cast<uint32_t>(v[P_HUNT_FLUSH_KILLS]))
				checkpoint(v);
		}
	}
};

static void test_hunt_v22_fixtures() {
	const policy_values v = policy_defaults();
	// 1 clean account and day: 30,000 -> 20 batches, kills 400, 4 tier rows, 100 points, then nothing
	hunt_day_sim a(0, 1, 0, 0);
	a.feed(v, 30000);
	CHECK(a.kills == 400 && a.next_tier == 5 && a.batches == 20 && a.tier_rows == 4 && a.credited == 100);
	a.feed(v, 5000);
	a.checkpoint(v);   // logout flush
	CHECK(a.kills == 400 && a.batches == 20 && a.tier_rows == 4 && a.credited == 100);
	// 2 one kill and one batch first: 20 new batches, kills 401, the same 100 points
	hunt_day_sim b(1, 1, 0, 0);
	b.feed(v, 30000);
	CHECK(b.kills == 401 && b.next_tier == 5 && b.batches == 20 && b.tier_rows == 4 && b.credited == 100);
	b.feed(v, 5000);
	CHECK(b.kills == 401 && b.batches == 20);
	// 3 capped day after a relog: layer 1 stops every kill, layer 2 alone would still write nothing
	hunt_day_sim c(401, 5, 100, 100);
	c.feed(v, 5010);
	c.checkpoint(v);
	CHECK(c.kills == 401 && c.batches == 0 && c.tier_rows == 0 && c.credited == 0);
	hunt_day_sim d(401, 5, 100, 100);
	d.done = 0;   // layer 1 lookup failed
	d.feed(v, 25);
	CHECK(d.kills == 401 && d.batches == 0 && d.tier_rows == 0);
	// total cap already 150: tiers 3 and 4 still get rows but credit 0
	hunt_day_sim e(0, 1, 0, 150);
	e.feed(v, 30000);
	CHECK(e.tier_rows == 4 && e.credited == 50 && e.earned_total == 200 && e.next_tier == 5);
}

static void test_laps() {
	CHECK(lap_reached(32, 33) == 0);
	CHECK(lap_reached(33, 33) == 1);
	CHECK(lap_reached(34, 33) == 0);
	CHECK(lap_reached(66, 33) == 2);
	CHECK(lap_reached(0, 33) == 0);
	CHECK(lap_reached(33, 0) == 0);
}

int main() {
	test_policy_defaults();
	test_policy_rejects();
	test_board();
	test_rates();
	test_logical_day();
	test_credit();
	test_hunt_tiers();
	test_hunt_v22_fixtures();
	test_laps();
	std::printf("need_marble logic: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
