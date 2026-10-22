// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED daily quest - account day ledger and the virtual delivery activity (phase 1).
//
// Spec: md/daily_quest_20261019/NEED_Daily_Quest_Delivery_Final_Technical_Spec_v0_7.md (revision 2).
// - Character progress, account points and once-only stages live in SQL
//   (sql-files/need_daily_quest.sql), never in character variables.
// - A score is committed only when the character's REAL level range at that moment
//   equals its accept snapshot and the account's locked track (spec 4.6).
// - Delivery materials are virtual: a qualifying kill rolls a per-character counter.

#ifndef MAP_NEED_DAILY_QUEST_HPP
#define MAP_NEED_DAILY_QUEST_HPP

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <common/cbasetypes.hpp>
#include <common/database.hpp>
#include <common/mmo.hpp>

class map_session_data;
struct mob_data;

/// Script and test visible result codes.
enum e_need_dq_result : int32 {
	NEED_DQ_OK = 1,                 ///< done (accept: account already locked to this track)
	NEED_DQ_OK_UNLOCKED = 2,        ///< accept done, the account track is not locked yet (show the U6 warning)
	NEED_DQ_NO_GAIN = 0,            ///< nothing new to score
	NEED_DQ_NOT_ACCEPTED = -1,
	NEED_DQ_LOCKED_OUT = -2,        ///< the account is locked to another track today
	NEED_DQ_LEVEL_CHANGED = -3,     ///< real level range differs from the accept snapshot
	NEED_DQ_NOT_ELIGIBLE = -4,      ///< level below every track
	NEED_DQ_ALREADY = -5,           ///< accept: already accepted this track today
	NEED_DQ_NOT_NEEDED = -6,        ///< re-accept: the range did not change
	NEED_DQ_ERROR = -7,             ///< database error, nothing changed
	NEED_DQ_DISABLED = -8,          ///< feature off, schema or configuration invalid
	// cash claim (v1.5 Phase 5-Lite)
	NEED_DQ_CLAIM_NOT_ENOUGH = -9,  ///< today's account points are below the tier
	NEED_DQ_CLAIM_IP_LIMIT = -10,   ///< another account already claimed from this IP today (not family)
	NEED_DQ_CLAIM_PENDING = -11,    ///< an earlier claim of this tier is not confirmed: operator check
	NEED_DQ_CLAIM_CASH_FULL = -12,  ///< the cash balance has no room
	NEED_DQ_CLAIM_NOT_READY = -13,  ///< account variables not loaded / no session IP
	NEED_DQ_CLAIM_BAD_TIER = -14,
};

/// Cash per tier and the family list of the daily quest (summer family tables, own event id: no other
/// event's approvals are inherited).
constexpr int32 NEED_DQ_CLAIM_CASH = 10000;
constexpr uint32 NEED_DQ_FAMILY_EVENT_ID = 202699;

struct s_need_dq_stage {
	uint16 amount;
	uint16 points;   ///< cumulative points once this stage is reached
};

/// Instance kinds of the daily quest (phase 4). The number is the stage key (i1..i3): never renumber.
enum e_need_dq_inst_kind : uint16 {
	NEED_DQ_INST_NONE = 0,
	NEED_DQ_INST_OGH_HARD = 1,       ///< Old Glast Heim (Hard), instance 50
	NEED_DQ_INST_OGH = 2,            ///< Old Glast Heim, instance 12
	NEED_DQ_INST_SKY_FORTRESS = 3,   ///< Sky Fortress Invasion, instance 33
	NEED_DQ_INST_KIND_MAX = 3,
};

struct s_need_dq_inst_kind {
	uint16 kind;     ///< e_need_dq_inst_kind
	uint16 points;   ///< points of this kind (the activity is capped by Cap)
};

struct s_need_dq_delivery_map {
	std::string name;
	int16 m;                                  ///< map index on this server
	uint16 rate;                              ///< per 10000, independent roll per candidate
	int32 quest_id;                           ///< quest window entry (99100-99199)
	std::unordered_set<int32> monsters;       ///< whitelisted mob ids
};

struct s_need_dq_hunt_map {
	std::string name;
	int16 m;                                  ///< map index on this server
	std::unordered_set<int32> monsters;       ///< whitelisted mob ids (regular spawns of the hunting ground)
};

struct s_need_dq_track {
	uint16 track;                             ///< lower bound of the level range (100/130/150/175)
	uint16 min_level;
	uint16 max_level;
	std::vector<s_need_dq_stage> delivery_stages;
	std::vector<std::shared_ptr<s_need_dq_delivery_map>> delivery_maps;
	std::shared_ptr<s_need_dq_delivery_map> rotation[7];   ///< logical weekday (Sunday = 0) -> map
	std::vector<s_need_dq_stage> hunt_stages;              ///< kills -> cumulative points (stage keys h1..hN)
	std::vector<std::shared_ptr<s_need_dq_hunt_map>> hunt_maps;
	int32 hunt_quest_id = 0;                               ///< quest window entry (99200-99299)
	std::vector<s_need_dq_stage> bg_stages;                ///< valid games -> cumulative points (b1..); empty = no battleground
	int32 bg_quest_id = 0;                                 ///< quest window entry (99300-99399), 0 = none
	std::vector<s_need_dq_inst_kind> inst_kinds;           ///< instance clears that score, each once per account; empty = none
	uint16 inst_cap = 0;                                   ///< cap of the instance activity (40)
	int32 inst_quest_id = 0;                               ///< quest window entry (99400-99499), 0 = none
};

class NeedDailyQuestDatabase : public TypesafeYamlDatabase<uint16, s_need_dq_track> {
public:
	NeedDailyQuestDatabase() : TypesafeYamlDatabase( "NEED_DAILY_QUEST_DB", 1 ){
	}

	const std::string getDefaultLocation() override;
	uint64 parseBodyNode( const ryml::NodeRef& node ) override;
	uint64 parseTrack( const ryml::NodeRef& node );
	void loadingFinished() override;

	bool valid = false;   ///< whole-file validation result (ranges, rotation, shared maps)
	uint32 parse_errors = 0;   ///< any rejected track invalidates the whole file
	std::string location_override;   ///< self test only: load another file
};

extern NeedDailyQuestDatabase need_dq_db;

/// 04:00 logical day and its weekday.
struct s_need_dq_day {
	char key[11] = {};
	int32 wday = 0;
};

/// Who is acting. Sessions fill it from map_session_data, the self test uses fixed values.
struct s_need_dq_actor {
	uint32 account_id = 0;
	uint32 char_id = 0;
	uint16 base_level = 0;
};

/// Collected but not yet written. Carries its own day and track (spec 5.2).
struct s_need_dq_pending {
	char day[11] = {};
	uint16 track = 0;
	uint16 count = 0;
	int32 timer = -1;     ///< INVALID_TIMER
	bool hunt = false;    ///< hunt kills (count column) instead of delivery materials
};

/// Snapshot of a need_dq_char_progress delivery row.
struct s_need_dq_char_row {
	bool exists = false;
	uint16 track = 0;
	std::string map_name;
	uint16 collected = 0;
	uint16 submitted = 0;
	uint16 reaccept_count = 0;
	uint16 abandoned = 0;
	uint32 count = 0;          ///< hunt kills (hunt row only)
};

/// Snapshot of a need_dq_account_day row.
struct s_need_dq_account_row {
	bool exists = false;
	uint16 locked_track = 0;   ///< 0 = not locked
	uint32 locked_char_id = 0;
	uint16 hunt_pts = 0;
	uint16 delivery_pts = 0;
	uint16 bg_pts = 0;
	uint16 inst_pts = 0;
	uint16 total_pts = 0;
};

/// Quest window range of the daily quest. These quests only display the ledger:
/// quest_update_objective never counts kills for them (quest.cpp).
constexpr int32 NEED_DQ_QUEST_MIN = 99000;
constexpr int32 NEED_DQ_QUEST_MAX = 99999;
constexpr int32 NEED_DQ_DELIVERY_QUEST_MIN = 99100;
constexpr int32 NEED_DQ_DELIVERY_QUEST_MAX = 99199;
constexpr int32 NEED_DQ_HUNT_QUEST_MIN = 99200;
constexpr int32 NEED_DQ_HUNT_QUEST_MAX = 99299;
constexpr int32 NEED_DQ_BG_QUEST_MIN = 99300;
constexpr int32 NEED_DQ_BG_QUEST_MAX = 99399;
constexpr int32 NEED_DQ_INST_QUEST_MIN = 99400;
constexpr int32 NEED_DQ_INST_QUEST_MAX = 99499;
inline bool need_dq_managed_quest( int32 quest_id ) {
	return quest_id >= NEED_DQ_QUEST_MIN && quest_id <= NEED_DQ_QUEST_MAX;
}

/// A battle as seen at its start (phase 3): who fought, from which IPs, since when.
struct s_need_dq_bg_participant {
	uint32 char_id = 0;
	uint32 account_id = 0;
	uint32 ip = 0;
};

struct s_need_dq_bg_match {
	std::string type;          ///< Tierra / Flavius / KVM
	std::string key;           ///< type-unixstart: stable across restarts, one row per account
	time_t started = 0;
	int16 m = -1;              ///< battle map
	std::vector<s_need_dq_bg_participant> players;
	uint16 distinct_ips = 0;
};

/// Why a game does or does not count (stored in need_dq_bg_match.reason).
enum e_need_dq_bg_reason : int32 {
	NEED_DQ_BG_VALID = 0,
	NEED_DQ_BG_NOT_PARTICIPANT = 1,  ///< not in the battle at its start
	NEED_DQ_BG_NOT_ON_MAP = 2,       ///< not on the battle map at the reward
	NEED_DQ_BG_TOO_SHORT = 3,        ///< Tierra / Flavius < 6 min, KVM < 3 min
	NEED_DQ_BG_NO_ACTION = 4,        ///< no action since the battle start
	NEED_DQ_BG_SHARED_IP = 5,        ///< fewer than 75% distinct IPs (U9)
	NEED_DQ_BG_NO_MATCH = 6,         ///< no battle start was recorded
};

// Pure helpers
bool need_dq_logical_day( s_need_dq_day& day );
std::shared_ptr<s_need_dq_track> need_dq_band( uint16 base_level );
uint16 need_dq_goal( const s_need_dq_track& track );
uint16 need_dq_hunt_goal( const s_need_dq_track& track );
uint16 need_dq_bg_goal( const s_need_dq_track& track );
uint32 need_dq_bg_min_seconds( const std::string& type );
e_need_dq_bg_reason need_dq_bg_validate( const s_need_dq_bg_match& match, uint32 char_id, int16 m, time_t now, time_t idletime );
/// OGH_HARD / OGH / SKY_FORTRESS -> e_need_dq_inst_kind, NEED_DQ_INST_NONE when unknown.
uint16 need_dq_inst_kind_id( const char* name );
/// Points of the cleared kinds that this track scores, capped by its Cap.
uint16 need_dq_inst_points( const s_need_dq_track& track, const std::vector<uint16>& kinds );

// Core (SQL). Callers outside a session pass pending = nullptr.
e_need_dq_result need_dq_core_accept( const s_need_dq_day& day, const s_need_dq_actor& actor );
e_need_dq_result need_dq_core_reaccept( const s_need_dq_day& day, const s_need_dq_actor& actor, s_need_dq_pending* pending, s_need_dq_pending* hpending = nullptr );
/// Writes the hunt pending count and scores every stage it reached, in one transaction (spec 4.5).
e_need_dq_result need_dq_core_hunt_commit( const s_need_dq_day& day, const s_need_dq_actor& actor, s_need_dq_pending* hpending, int32& gained );
e_need_dq_result need_dq_core_submit( const s_need_dq_day& day, const s_need_dq_actor& actor, s_need_dq_pending* pending, int32& gained );
bool need_dq_flush( uint32 char_id, s_need_dq_pending& pending );
bool need_dq_read_char( const char* day, uint32 char_id, s_need_dq_char_row& row );
bool need_dq_read_hunt( const char* day, uint32 char_id, s_need_dq_char_row& row );
/// Eligible valid games of one character (quest window: "this character").
bool need_dq_read_bg_games( const char* day, uint32 char_id, uint32& games );
/// Eligible valid games of the account in one range, distinct battles (D1: the account count scores).
bool need_dq_read_bg_account_games( const char* day, uint32 account_id, uint16 track, uint32& games );
/// Records one game of a battle for the account (once per match) and scores the account's eligible games (D1).
e_need_dq_result need_dq_core_bg_commit( const s_need_dq_day& day, const s_need_dq_actor& actor, const s_need_dq_bg_match& match,
	e_need_dq_bg_reason reason, time_t now, int32& gained, uint32& games );
bool need_dq_read_inst_kinds( const char* day, uint32 char_id, std::vector<uint16>& kinds );
/// Records one instance clear of the character (once per kind and day) and scores the kinds it cleared.
e_need_dq_result need_dq_core_inst_commit( const s_need_dq_day& day, const s_need_dq_actor& actor, uint16 kind, int32 instance_id,
	int32& gained, uint16& char_pts );
bool need_dq_read_account( const char* day, uint32 account_id, s_need_dq_account_row& row );
/// Cash claim, step 1 (one transaction): points, tier not claimed yet, IP slot -> reserved row (status 0).
e_need_dq_result need_dq_core_claim( const s_need_dq_day& day, const s_need_dq_actor& actor, uint16 tier, const char* ip, uint32 family_group );
/// Cash claim, step 2: mark the reserved row granted (1) or not granted (2).
bool need_dq_claim_finish( const char* day, uint32 account_id, uint16 tier, bool granted );
/// Operator approved daily-quest family group of the account, 0 = none (errors count as none).
uint32 need_dq_family_group( uint32 account_id );

// Session side
void need_daily_quest_on_kill( map_session_data* killer, mob_data* md, int32 type );
void need_daily_quest_session_end( map_session_data* sd );
/// Objective name for the quest window in the session language. False = not a daily quest objective.
bool need_daily_quest_objective_label( const map_session_data* sd, int32 quest_id, int32 objective, char (&out)[NAME_LENGTH] );
int32 need_daily_quest_script_accept( map_session_data* sd );
int32 need_daily_quest_script_reaccept( map_session_data* sd );
int32 need_daily_quest_script_submit( map_session_data* sd );
int32 need_daily_quest_script_status( map_session_data* sd );
/// Battle start: remembers the participants of both teams. Returns how many.
int32 need_daily_quest_script_bg_start( const char* type, int32 bg_id1, int32 bg_id2, const char* map_name );
/// Official end of a battle (win, loss, draw, time-out; D2): judges every player on the battle map once,
/// independent of the reward NPCs. Returns how many were judged, -1 when no battle of this type is recorded.
int32 need_daily_quest_script_bg_finish( const char* type );
/// Final boss of an instance died with this player on the boss map.
int32 need_daily_quest_script_inst_clear( map_session_data* sd, const char* kind, int32 instance_id );
/// NPC "claim reward": the tier's 10,000 cash into #CASHPOINTS, once per account, day and tier.
int32 need_daily_quest_script_claim( map_session_data* sd, int32 tier );

void need_daily_quest_init();
void need_daily_quest_final();

#ifdef NEED_DQ_TEST
// Test build only (decision U11): never present in the live executable.
extern int32 need_dq_test_rate;          ///< -1 = configured rate, otherwise 0..10000
extern int64 need_dq_test_offset;        ///< seconds added to the clock for the logical day
extern int32 need_dq_test_fail_point;    ///< 1 = before the account UPDATE, 2 = before COMMIT of a submit
extern int32 need_dq_test_allow_gm;      ///< 1 = GM characters count in real kills (test only)
extern int32 need_dq_test_bg_ip_percent; ///< -1 = DQ_BG_IP_PERCENT (75), otherwise 0..100 (in-house testers share one IP)
void need_dq_test_credit( map_session_data* killer, int32 mob_id, int32 kills );
void need_dq_test_dump( map_session_data* sd );
void need_dq_test_flush( map_session_data* sd );
bool need_dq_selftest( const char* out_path );
#endif

#endif  // MAP_NEED_DAILY_QUEST_HPP
