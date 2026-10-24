// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED marble board - pure rules (no server state, no SQL).
//
// Everything here is deterministic so it can be compiled into a stand-alone
// unit test (tests/need_marble/logic_test.cpp) without the map-server.
// The SQL side lives in need_marble.cpp.

#ifndef MAP_NEED_MARBLE_LOGIC_HPP
#define MAP_NEED_MARBLE_LOGIC_HPP

#include <array>
#include <cstdint>
#include <ctime>
#include <map>
#include <set>

namespace need_marble {

constexpr int32_t BOARD_TILES = 33;
constexpr int32_t RATE_SCALE = 10000;   // tile outcome rates are per 10000
constexpr int32_t RATE_SLOTS = 4;       // outcomes per tile kind
constexpr int32_t POLICY_MAX = 512;     // policy keys 0..511 (tile keys reach 100 + 12*10 + 3 / 200 + 12*10 + 3)

// Fixed logical-day offset: 04:00 KST = UTC+9 shifted back 4 hours.
// Korea has no daylight saving, so a fixed offset is exact and independent of the OS time zone.
constexpr int64_t KST_OFFSET_SEC = 9 * 3600;
constexpr int64_t RESET_HOUR_SEC = 4 * 3600;

enum e_tile_kind : int32_t {
	TILE_NONE = 0,
	TILE_CITY = 1,
	TILE_EVENT = 2,
	TILE_TREASURE = 3,
	TILE_MONSTER = 4,
	TILE_BOSS = 5,
	TILE_PORING = 6,
	TILE_BONUS = 7,
	TILE_TAX = 8,
	TILE_JAIL = 9,
	TILE_START = 10,
	TILE_WARP = 11,
	TILE_DUNGEON = 12,
	TILE_KIND_MAX = 13,
};

// Board index -> kind. Mirrors .tile$ of npc/NEED/need_marble_motion_v1_2.txt (line 26)
// and npc/NEED/marble/need_marble_board_v2.txt. tests/need_marble checks both stay in sync.
constexpr std::array<int32_t, BOARD_TILES> BOARD = {
	TILE_START,    // 0 START
	TILE_CITY,     // 1 Prontera
	TILE_EVENT,    // 2 Event
	TILE_PORING,   // 3 Poring
	TILE_CITY,     // 4 Payon
	TILE_TREASURE, // 5 Treasure
	TILE_CITY,     // 6 Geffen
	TILE_WARP,     // 7 Warp
	TILE_BONUS,    // 8 BONUS
	TILE_CITY,     // 9 Morroc
	TILE_MONSTER,  // 10 Monster
	TILE_CITY,     // 11 Izlude
	TILE_EVENT,    // 12 Event
	TILE_CITY,     // 13 Hugel
	TILE_TAX,      // 14 Tax
	TILE_CITY,     // 15 Amatsu
	TILE_BOSS,     // 16 BOSS
	TILE_DUNGEON,  // 17 Dungeon
	TILE_TREASURE, // 18 Treasure
	TILE_CITY,     // 19 Comodo
	TILE_EVENT,    // 20 Event
	TILE_CITY,     // 21 Niflheim
	TILE_WARP,     // 22 Warp
	TILE_MONSTER,  // 23 Monster
	TILE_CITY,     // 24 Glast Heim
	TILE_JAIL,     // 25 JAIL
	TILE_CITY,     // 26 Alberta
	TILE_EVENT,    // 27 Event
	TILE_CITY,     // 28 Juno
	TILE_TREASURE, // 29 Treasure
	TILE_CITY,     // 30 Rachel
	TILE_MONSTER,  // 31 Monster
	TILE_CITY,     // 32 Lutie
};

// Policy keys (need_marble_policy.policy_key). Values are int32.
enum e_policy_key : int32_t {
	P_ENABLED = 1,               // master switch
	P_HUNT_ENABLED = 2,
	P_INSTANCE_ENABLED = 3,
	P_BG_ENABLED = 4,
	P_CP_APPLY_ENABLED = 5,      // must stay 0: no code path applies CP in this build
	P_SEASON_ID = 6,

	P_EXCHANGE_COST = 10,
	P_DAILY_TOTAL_CAP = 11,
	P_DAILY_HUNT_CAP = 12,
	P_DAILY_INSTANCE_CAP = 13,
	P_DAILY_BG_CAP = 14,
	P_DICE_HOLD_MAX = 15,
	P_EXCHANGE_DAILY_MAX = 16,   // dice per logical day, 0 = no limit
	P_EXCHANGE_TTL_SEC = 17,
	P_EXCHANGE_MAX_BUNDLE = 18,  // dice per single request

	P_HUNT_TIER_KILLS = 20,
	P_HUNT_TIER_POINTS = 21,
	P_HUNT_MAX_TIERS = 22,
	P_HUNT_LEVEL_DIFF = 23,
	P_HUNT_ACTIVE_SEC = 24,
	P_HUNT_CAPTCHA_EXCLUDE = 25, // operator decision pending (see OPERATOR_BALANCE_CONFIG)
	P_HUNT_FLUSH_KILLS = 26,
	P_HUNT_FLUSH_SEC = 27,
	P_HUNT_PARTY_MODE = 28,      // 0 top damage only, 1 P2-active

	P_INSTANCE_POINTS = 30,
	P_INSTANCE_REQUIRE_ACTIVE = 31,
	P_INSTANCE_ACTIVE_SEC = 32,
	P_INSTANCE_CAPPED_CONSUMES = 33, // N1: a 0-point clear still uses today's dungeon slot

	P_BG_POINTS = 40,
	P_BG_ACTIVE_SEC = 41,
	P_BG_SAMPLE_SEC = 42,
	P_BG_MIN_SAMPLES = 43,
	P_BG_MIN_ACTIVE_PERMIL = 44,

	P_WARP_DEST_7 = 50,
	P_WARP_DEST_22 = 51,
	P_DUNGEON_AUTO_SAFE = 52,
	P_DUNGEON_AUTO_SEC = 53,
	P_LAP_STEPS = 54,
	P_LAP_CP = 55,
	P_DUNGEON_SAFE_CP = 56,
	P_DUNGEON_CHALLENGE_RATE = 57,
	P_DUNGEON_CHALLENGE_CP = 58,

	// 100 + kind*10 + slot = rate (per 10000), 200 + kind*10 + slot = CP of that outcome.
	P_TILE_RATE_BASE = 100,
	P_TILE_CP_BASE = 200,
};

constexpr int32_t tile_rate_key(int32_t kind, int32_t slot) { return P_TILE_RATE_BASE + kind * 10 + slot; }
constexpr int32_t tile_cp_key(int32_t kind, int32_t slot) { return P_TILE_CP_BASE + kind * 10 + slot; }
static_assert(tile_cp_key(TILE_KIND_MAX - 1, RATE_SLOTS - 1) < POLICY_MAX, "policy keys must fit policy_values");

// Tile kinds whose arrival result is drawn from the rate table.
constexpr bool tile_uses_table(int32_t kind) {
	return kind == TILE_CITY || kind == TILE_EVENT || kind == TILE_TREASURE || kind == TILE_MONSTER ||
		kind == TILE_BOSS || kind == TILE_PORING || kind == TILE_BONUS || kind == TILE_TAX ||
		kind == TILE_JAIL || kind == TILE_START;
}

using policy_values = std::array<int32_t, POLICY_MAX>;

// Initial values from NEED_MARBLE_INITIAL_BALANCE_V1_0 (operator delegated, 2026-10-20).
// The master switch and the CP switch default to OFF.
inline policy_values policy_defaults() {
	policy_values v{};
	v[P_ENABLED] = 0;
	v[P_HUNT_ENABLED] = 1;
	v[P_INSTANCE_ENABLED] = 1;
	v[P_BG_ENABLED] = 1;            // G2-C v1.1: 50 points per normally finished match (the master switch stays off)
	v[P_CP_APPLY_ENABLED] = 0;
	v[P_SEASON_ID] = 1;

	v[P_EXCHANGE_COST] = 50;
	v[P_DAILY_TOTAL_CAP] = 200;
	v[P_DAILY_HUNT_CAP] = 100;
	v[P_DAILY_INSTANCE_CAP] = 100;
	v[P_DAILY_BG_CAP] = 100;
	v[P_DICE_HOLD_MAX] = 30;
	v[P_EXCHANGE_DAILY_MAX] = 0;
	v[P_EXCHANGE_TTL_SEC] = 300;
	v[P_EXCHANGE_MAX_BUNDLE] = 10;

	v[P_HUNT_TIER_KILLS] = 100;
	v[P_HUNT_TIER_POINTS] = 25;
	v[P_HUNT_MAX_TIERS] = 4;
	v[P_HUNT_LEVEL_DIFF] = 30;
	v[P_HUNT_ACTIVE_SEC] = 60;
	v[P_HUNT_CAPTCHA_EXCLUDE] = 1;
	v[P_HUNT_FLUSH_KILLS] = 20;
	v[P_HUNT_FLUSH_SEC] = 60;
	v[P_HUNT_PARTY_MODE] = 1;

	v[P_INSTANCE_POINTS] = 50;
	v[P_INSTANCE_REQUIRE_ACTIVE] = 1;
	v[P_INSTANCE_ACTIVE_SEC] = 60;
	v[P_INSTANCE_CAPPED_CONSUMES] = 1;

	v[P_BG_POINTS] = 50;
	v[P_BG_ACTIVE_SEC] = 60;
	v[P_BG_SAMPLE_SEC] = 30;
	v[P_BG_MIN_SAMPLES] = 10;          // unused since G2-C v1.1 (no sample count / ratio rule), kept for old policy rows
	v[P_BG_MIN_ACTIVE_PERMIL] = 500;   // unused since G2-C v1.1

	v[P_WARP_DEST_7] = 13;          // draft, operator confirmation pending (N10)
	v[P_WARP_DEST_22] = 28;         // draft, operator confirmation pending (N10)
	v[P_DUNGEON_AUTO_SAFE] = 0;     // 60 s auto "safe" is implemented but off (N12)
	v[P_DUNGEON_AUTO_SEC] = 60;
	v[P_LAP_STEPS] = 33;
	v[P_LAP_CP] = 10000;
	v[P_DUNGEON_SAFE_CP] = 1000;
	v[P_DUNGEON_CHALLENGE_RATE] = 4000;
	v[P_DUNGEON_CHALLENGE_CP] = 4000;

	auto set = [&v](int32_t kind, std::array<int32_t, RATE_SLOTS> rates, std::array<int32_t, RATE_SLOTS> cps) {
		for (int32_t s = 0; s < RATE_SLOTS; s++) {
			v[tile_rate_key(kind, s)] = rates[s];
			v[tile_cp_key(kind, s)] = cps[s];
		}
	};
	set(TILE_CITY,     {5000, 4000, 1000, 0}, {500, 1000, 2000, 0});
	set(TILE_EVENT,    {4500, 4000, 1500, 0}, {0, 1500, 4000, 0});
	set(TILE_TREASURE, {7000, 2500, 500, 0},  {1500, 4000, 10000, 0});
	set(TILE_MONSTER,  {7500, 2500, 0, 0},    {1500, 0, 0, 0});
	set(TILE_BOSS,     {2500, 7500, 0, 0},    {8000, 0, 0, 0});
	set(TILE_PORING,   {10000, 0, 0, 0},      {1500, 0, 0, 0});
	set(TILE_BONUS,    {10000, 0, 0, 0},      {3000, 0, 0, 0});
	set(TILE_TAX,      {10000, 0, 0, 0},      {0, 0, 0, 0});
	set(TILE_JAIL,     {10000, 0, 0, 0},      {0, 0, 0, 0});
	set(TILE_START,    {10000, 0, 0, 0},      {0, 0, 0, 0});
	return v;
}

// Error codes of validate_policy (0 = valid).
enum e_policy_error : int32_t {
	PE_OK = 0,
	PE_RATE_SUM = 1,
	PE_NEGATIVE = 2,
	PE_WARP_DEST = 3,
	PE_CAPS = 4,
	PE_HUNT = 5,
	PE_DUNGEON = 6,
	PE_EXCHANGE = 7,
	PE_CP_SWITCH = 8,
	PE_LAP = 9,
};

constexpr int32_t CP_SANITY_MAX = 1000000;

inline int32_t validate_policy(const policy_values& v, int32_t* bad_key = nullptr) {
	auto fail = [bad_key](int32_t code, int32_t key) {
		if (bad_key != nullptr)
			*bad_key = key;
		return code;
	};
	for (int32_t kind = TILE_CITY; kind < TILE_KIND_MAX; kind++) {
		if (!tile_uses_table(kind))
			continue;
		int32_t sum = 0;
		for (int32_t s = 0; s < RATE_SLOTS; s++) {
			int32_t r = v[tile_rate_key(kind, s)], c = v[tile_cp_key(kind, s)];
			if (r < 0 || c < 0 || c > CP_SANITY_MAX)
				return fail(PE_NEGATIVE, tile_rate_key(kind, s));
			sum += r;
		}
		if (sum != RATE_SCALE)
			return fail(PE_RATE_SUM, tile_rate_key(kind, 0));
	}
	for (int32_t key : {P_WARP_DEST_7, P_WARP_DEST_22}) {
		int32_t d = v[key];
		if (d < 0 || d >= BOARD_TILES || BOARD[d] == TILE_WARP || BOARD[d] == TILE_START)
			return fail(PE_WARP_DEST, key);
	}
	if (v[P_DAILY_TOTAL_CAP] < 0 || v[P_DAILY_HUNT_CAP] < 0 || v[P_DAILY_INSTANCE_CAP] < 0 || v[P_DAILY_BG_CAP] < 0)
		return fail(PE_CAPS, P_DAILY_TOTAL_CAP);
	if (v[P_HUNT_TIER_KILLS] <= 0 || v[P_HUNT_TIER_POINTS] < 0 || v[P_HUNT_MAX_TIERS] < 0 || v[P_HUNT_MAX_TIERS] > 1000 ||
		v[P_HUNT_FLUSH_KILLS] <= 0 || v[P_HUNT_FLUSH_SEC] <= 0 || v[P_HUNT_LEVEL_DIFF] < 0 || v[P_HUNT_ACTIVE_SEC] <= 0)
		return fail(PE_HUNT, P_HUNT_TIER_KILLS);
	if (v[P_DUNGEON_SAFE_CP] < 0 || v[P_DUNGEON_CHALLENGE_CP] < 0 || v[P_DUNGEON_CHALLENGE_RATE] < 0 ||
		v[P_DUNGEON_CHALLENGE_RATE] > RATE_SCALE || v[P_DUNGEON_AUTO_SEC] <= 0)
		return fail(PE_DUNGEON, P_DUNGEON_CHALLENGE_RATE);
	if (v[P_EXCHANGE_COST] <= 0 || v[P_DICE_HOLD_MAX] <= 0 || v[P_EXCHANGE_DAILY_MAX] < 0 ||
		v[P_EXCHANGE_TTL_SEC] <= 0 || v[P_EXCHANGE_MAX_BUNDLE] <= 0)
		return fail(PE_EXCHANGE, P_EXCHANGE_COST);
	if (v[P_CP_APPLY_ENABLED] != 0)
		return fail(PE_CP_SWITCH, P_CP_APPLY_ENABLED);
	if (v[P_LAP_STEPS] <= 0 || v[P_LAP_CP] < 0 || v[P_LAP_CP] > CP_SANITY_MAX)
		return fail(PE_LAP, P_LAP_STEPS);
	return PE_OK;
}

// Days since 1970-01-01 -> civil date (Howard Hinnant's algorithm), no time zone database.
inline uint32_t yyyymmdd_from_days(int64_t z) {
	z += 719468;
	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
	const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64_t y = static_cast<int64_t>(yoe) + era * 400;
	const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const uint32_t mp = (5 * doy + 2) / 153;
	const uint32_t d = doy - (153 * mp + 2) / 5 + 1;
	const uint32_t m = mp < 10 ? mp + 3 : mp - 9;
	return static_cast<uint32_t>((y + (m <= 2 ? 1 : 0)) * 10000 + m * 100 + d);
}

// Logical day (YYYYMMDD) for a unix time: the day starts at 04:00 KST.
inline uint32_t logical_day(int64_t unix_time) {
	int64_t t = unix_time + KST_OFFSET_SEC - RESET_HOUR_SEC;
	int64_t days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
	return yyyymmdd_from_days(days);
}

// Points actually credited for one achievement, given what was earned that day.
inline int32_t credit_amount(int32_t requested, int32_t earned_activity, int32_t cap_activity,
	int32_t earned_total, int32_t cap_total) {
	int32_t room_a = cap_activity - earned_activity;
	int32_t room_t = cap_total - earned_total;
	int32_t c = requested;
	if (room_a < c)
		c = room_a;
	if (room_t < c)
		c = room_t;
	return c > 0 ? c : 0;
}

struct tile_outcome {
	int32_t slot = -1;   // 0-based outcome slot, -1 when the table is broken
	int32_t cp = 0;
};

// roll is 1..RATE_SCALE.
inline tile_outcome pick_outcome(const policy_values& v, int32_t kind, int32_t roll) {
	tile_outcome out;
	if (!tile_uses_table(kind) || roll < 1 || roll > RATE_SCALE)
		return out;
	int32_t acc = 0;
	for (int32_t s = 0; s < RATE_SLOTS; s++) {
		acc += v[tile_rate_key(kind, s)];
		if (roll <= acc) {
			out.slot = s;
			out.cp = v[tile_cp_key(kind, s)];
			return out;
		}
	}
	return out;
}

// Dungeon tile: choice 1 = safe, 2 = challenge (roll 1..RATE_SCALE, ignored for safe).
inline int32_t dungeon_cp(const policy_values& v, int32_t choice, int32_t roll) {
	if (choice == 1)
		return v[P_DUNGEON_SAFE_CP];
	if (choice == 2)
		return roll >= 1 && roll <= v[P_DUNGEON_CHALLENGE_RATE] ? v[P_DUNGEON_CHALLENGE_CP] : 0;
	return 0;
}

inline int32_t warp_destination(const policy_values& v, int32_t pos) {
	if (pos == 7)
		return v[P_WARP_DEST_7];
	if (pos == 22)
		return v[P_WARP_DEST_22];
	return -1;
}

// L1: a bonus every lap_steps walked steps. Returns the lap number reached by this step, 0 if none.
inline uint64_t lap_reached(uint64_t step_seq_after, int32_t lap_steps) {
	if (lap_steps <= 0 || step_seq_after == 0)
		return 0;
	return (step_seq_after % static_cast<uint64_t>(lap_steps)) == 0 ? step_seq_after / static_cast<uint64_t>(lap_steps) : 0;
}

// True when every hunt tier of the day is already credited (next_tier is the first tier not credited yet).
inline bool hunt_day_maxed(int32_t next_tier, int32_t max_tiers) {
	return next_tier > max_tiers;
}

// G2-C v1.1: a battleground participant is left out only when obviously AFK for the whole match: every
// activity sample of the match (taken every P_BG_SAMPLE_SEC on the team) was idle AND the player is idle
// now. No sample at all (short match, a relog, a restart) never excludes anyone; support players who
// heal, buff, guard or wait between deaths count as long as they acted once in a sample window.
inline bool bg_afk_excluded(uint32_t samples, uint32_t active_samples, bool idle_now) {
	return samples > 0 && active_samples == 0 && idle_now;
}

// V21: what a hunt batch (collect_session, batch_no) is, given the rows already stored under that key.
// No stored row: a fresh batch. Otherwise ALREADY only when every stored row equals the batch and every batch
// day without a stored row is a day capped now (the checkpoint skips capped days, F2 layer 2); anything else
// is a CONFLICT (same key, other content).
enum e_hunt_resend : int32_t { HUNT_RESEND_FRESH = 0, HUNT_RESEND_ALREADY = 1, HUNT_RESEND_CONFLICT = 2 };

inline int32_t hunt_resend_check(const std::map<uint32_t, uint32_t>& stored, const std::map<uint32_t, uint32_t>& batch,
	const std::set<uint32_t>& capped_days) {
	if (stored.empty())
		return HUNT_RESEND_FRESH;
	for (const auto& s : stored) {
		auto it = batch.find(s.first);
		if (it == batch.end() || it->second != s.second)
			return HUNT_RESEND_CONFLICT;
	}
	for (const auto& b : batch)
		if (stored.find(b.first) == stored.end() && capped_days.find(b.first) == capped_days.end())
			return HUNT_RESEND_CONFLICT;
	return HUNT_RESEND_ALREADY;
}

// Hunt tiers newly reached when the period count goes from kills to kills + add.
// next_tier is the first tier not yet credited (1-based); the result is the new next_tier.
inline int32_t hunt_next_tier(uint32_t kills_after, int32_t next_tier, int32_t tier_kills, int32_t max_tiers) {
	while (next_tier <= max_tiers && static_cast<uint64_t>(kills_after) >= static_cast<uint64_t>(next_tier) * static_cast<uint64_t>(tier_kills))
		next_tier++;
	return next_tier;
}

}  // namespace need_marble

#endif  // MAP_NEED_MARBLE_LOGIC_HPP
