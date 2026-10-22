// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED daily quest - account day ledger and the virtual delivery activity (phase 1).
//
// Lock order for every transaction that touches points (spec 4.4):
//   1. need_dq_account_day   (INSERT IGNORE, then SELECT ... FOR UPDATE)
//   2. need_dq_char_progress (SELECT ... FOR UPDATE / UPDATE)
//   3. need_dq_score_stage   (INSERT IGNORE)
//   4. need_dq_account_day   (UPDATE with constants computed from the locked row)
// Activity points are always recomputed from need_dq_score_stage, never incremented.

#include "need_daily_quest.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <common/nullpo.hpp>
#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "battle.hpp"
#include "battleground.hpp"
#include "clif.hpp"
#include "map.hpp"
#include "mob.hpp"
#include "need_event_hunt.hpp"
#include "party.hpp"
#include "path.hpp"
#include "pc.hpp"
#include "quest.hpp"
#include "script.hpp"

NeedDailyQuestDatabase need_dq_db;

#ifdef NEED_DQ_TEST
int32 need_dq_test_rate = -1;
int64 need_dq_test_offset = 0;
int32 need_dq_test_fail_point = 0;
int32 need_dq_test_allow_gm = 0;
int32 need_dq_test_bg_ip_percent = -1;
#endif

namespace {

constexpr const char* DQ_DELIVERY = "delivery";
constexpr const char* DQ_HUNT = "hunt";
constexpr const char* DQ_BG = "bg";
constexpr const char* DQ_INST = "inst";
constexpr uint16 DQ_DAILY_CAP = 80;
constexpr time_t DQ_KST_OFFSET = 9 * 60 * 60;    // daily reset at 04:00 KST
constexpr int32 DQ_RATE_SCALE = 10000;
constexpr uint16 DQ_FLUSH_COUNT = 10;          // decision D12 / U3
constexpr t_tick DQ_FLUSH_DELAY = 60000;       // decision D12 / U3: real timer
constexpr time_t DQ_IDLE_SECONDS = 60;         // decision U5
constexpr int32 DQ_FILTER_LEVEL_DIFFERENCE = 1000;   // the whitelist decides, not the level gap

constexpr int32 MSG_DQ_COLLECTED = 1580;       // [Daily Delivery] +1 (%u/%u)
constexpr int32 MSG_DQ_GOAL_REACHED = 1581;
constexpr int32 MSG_DQ_LEVEL_CHANGED = 1582;   // %s now range, %s accepted range
constexpr int32 MSG_DQ_LOCKED_OUT = 1583;      // %s locked range
constexpr int32 MSG_DQ_DB_ERROR = 1584;
constexpr int32 MSG_DQ_GM_EXCLUDED = 1585;    // GM kills never count (spec 7.1)
constexpr int32 MSG_DQ_MATERIAL_BASE = 1586;  // 1586..1589: material of track 100 / 130 / 150 / 175
constexpr int32 MSG_DQ_HUNT_STAGE = 1590;     // %u kills, +%d pts, today %u/80
constexpr int32 MSG_DQ_HUNT_STAGE_KNOWN = 1591;  // %u kills, stage already earned on the account
constexpr int32 MSG_DQ_HUNT_LABEL = 1592;     // quest window objective of the hunt quests
constexpr int32 MSG_DQ_HUNT_DONE = 1593;
constexpr int32 MSG_DQ_BG_REASON_BASE = 1569; // + e_need_dq_bg_reason (1..6) -> 1570..1575
constexpr int32 MSG_DQ_BG_NOT_ACCEPTED = 1576;
constexpr int32 MSG_DQ_BG_LABEL = 1594;
constexpr int32 MSG_DQ_BG_STAGE = 1595;       // game %u, +%d pts, today %u/80
constexpr int32 MSG_DQ_BG_STAGE_KNOWN = 1596;
constexpr int32 MSG_DQ_BG_INVALID = 1597;     // (%s) reason
constexpr int32 MSG_DQ_BG_NO_TRACK = 1598;
constexpr int32 MSG_DQ_BG_AUTO_ACCEPT = 1556; // v1.5: accepted by entering a battle
constexpr int32 MSG_DQ_BG_SAME_MATCH = 1568;  // another character of the account already has this battle
constexpr int32 MSG_DQ_INST_LABEL = 1560;
constexpr int32 MSG_DQ_INST_STAGE = 1561;        // %s instance, +%d pts, today %u/80
constexpr int32 MSG_DQ_INST_STAGE_KNOWN = 1562;  // %s instance
constexpr int32 MSG_DQ_INST_NOT_ACCEPTED = 1563; // %s instance
constexpr int32 MSG_DQ_INST_NO_TRACK = 1564;
constexpr int32 MSG_DQ_INST_NAME_BASE = 1564;    // + e_need_dq_inst_kind (1..3) -> 1565..1567
constexpr uint32 DQ_BG_IP_PERCENT = 75;       // decision U9

const char* const WEEKDAY_KEYS[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

bool dq_schema_ok = false;

struct s_dq_session {
	uint32 char_id = 0;
	uint32 account_id = 0;
	char cache_day[11] = {};
	bool loaded = false;
	s_need_dq_char_row row;
	uint16 locked_track = 0;
	s_need_dq_pending pending;
	s_need_dq_char_row hrow;      // hunt row of cache_day
	s_need_dq_pending hpending;    // hunt kills not written yet
	uint32 bg_games = 0;           // valid battleground games of cache_day
	std::vector<uint16> inst_kinds; // instance kinds cleared on cache_day
	uint16 notice_level = 0;   // band already told about (level changed)
	bool notice_locked = false;
	bool notice_gm = false;
};

std::unordered_map<uint32, s_dq_session> dq_sessions;

TIMER_FUNC(need_dq_flush_timer);
s_need_dq_actor dq_actor( const map_session_data* sd );

bool dq_active() {
	return battle_config.need_daily_quest_enable != 0 && dq_schema_ok && need_dq_db.valid;
}

void dq_track_label( uint16 track, char (&out)[16] ) {
	std::shared_ptr<s_need_dq_track> t = need_dq_db.find( track );
	if( t == nullptr )
		safesnprintf( out, sizeof( out ), "%u", track );
	else if( t->min_level == t->max_level )
		safesnprintf( out, sizeof( out ), "%u", t->min_level );
	else
		safesnprintf( out, sizeof( out ), "%u~%u", t->min_level, t->max_level );
}

/*==========================================
 * SQL helpers
 *------------------------------------------*/

bool dq_query_str( const char* query ) {
	if( SQL_ERROR == Sql_QueryStr( mmysql_handle, query ) ) {
		Sql_ShowDebug( mmysql_handle );
		return false;
	}
	return true;
}

void dq_rollback() {
	if( SQL_ERROR == Sql_QueryStr( mmysql_handle, "ROLLBACK" ) )
		Sql_ShowDebug( mmysql_handle );
}

uint32 dq_col_u32( uint32 column ) {
	char* data = nullptr;
	if( SQL_SUCCESS != Sql_GetData( mmysql_handle, column, &data, nullptr ) || data == nullptr )
		return 0;
	return static_cast<uint32>( strtoul( data, nullptr, 10 ) );
}

std::string dq_col_str( uint32 column ) {
	char* data = nullptr;
	size_t len = 0;
	if( SQL_SUCCESS != Sql_GetData( mmysql_handle, column, &data, &len ) || data == nullptr )
		return std::string();
	return std::string( data, len );
}

bool dq_read_char_sql( const char* day, uint32 char_id, s_need_dq_char_row& row, bool for_update, const char* activity = DQ_DELIVERY ) {
	row = s_need_dq_char_row();
	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"SELECT `track`,`map_name`,`collected`,`submitted`,`reaccept_count`,`abandoned_collected`,`count` "
		"FROM `need_dq_char_progress` WHERE `day_key`='%s' AND `char_id`='%u' AND `activity`='%s' "
		"AND `accepted_at` IS NOT NULL%s",
		day, char_id, activity, for_update ? " FOR UPDATE" : "" ) ) {
		Sql_ShowDebug( mmysql_handle );
		return false;
	}
	if( Sql_NumRows( mmysql_handle ) > 0 && SQL_SUCCESS == Sql_NextRow( mmysql_handle ) ) {
		row.exists = true;
		row.track = static_cast<uint16>( dq_col_u32( 0 ) );
		row.map_name = dq_col_str( 1 );
		row.collected = static_cast<uint16>( dq_col_u32( 2 ) );
		row.submitted = static_cast<uint16>( dq_col_u32( 3 ) );
		row.reaccept_count = static_cast<uint16>( dq_col_u32( 4 ) );
		row.abandoned = static_cast<uint16>( dq_col_u32( 5 ) );
		row.count = dq_col_u32( 6 );
	}
	Sql_FreeResult( mmysql_handle );
	return true;
}

bool dq_read_account_sql( const char* day, uint32 account_id, s_need_dq_account_row& row, bool for_update ) {
	row = s_need_dq_account_row();
	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"SELECT `locked_track`,`locked_char_id`,`hunt_pts`,`delivery_pts`,`bg_pts`,`inst_pts`,`total_pts` "
		"FROM `need_dq_account_day` WHERE `day_key`='%s' AND `account_id`='%u'%s",
		day, account_id, for_update ? " FOR UPDATE" : "" ) ) {
		Sql_ShowDebug( mmysql_handle );
		return false;
	}
	if( Sql_NumRows( mmysql_handle ) > 0 && SQL_SUCCESS == Sql_NextRow( mmysql_handle ) ) {
		row.exists = true;
		row.locked_track = static_cast<uint16>( dq_col_u32( 0 ) );
		row.locked_char_id = dq_col_u32( 1 );
		row.hunt_pts = static_cast<uint16>( dq_col_u32( 2 ) );
		row.delivery_pts = static_cast<uint16>( dq_col_u32( 3 ) );
		row.bg_pts = static_cast<uint16>( dq_col_u32( 4 ) );
		row.inst_pts = static_cast<uint16>( dq_col_u32( 5 ) );
		row.total_pts = static_cast<uint16>( dq_col_u32( 6 ) );
	}
	Sql_FreeResult( mmysql_handle );
	return true;
}

/// Step 1 of the lock order. Must run inside a transaction.
bool dq_lock_account_day( const char* day, uint32 account_id, s_need_dq_account_row& b ) {
	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"INSERT IGNORE INTO `need_dq_account_day` (`day_key`,`account_id`) VALUES ('%s','%u')", day, account_id ) ) {
		Sql_ShowDebug( mmysql_handle );
		return false;
	}
	return dq_read_account_sql( day, account_id, b, true ) && b.exists;
}

/// Points of one activity from its once-only stage keys.
uint16 dq_activity_points( const char* activity, const s_need_dq_track& track, const std::vector<std::string>& keys ) {
	uint16 points = 0;
	if( strcmp( activity, DQ_DELIVERY ) == 0 ) {
		for( const std::string& key : keys ) {
			if( key.size() < 2 || key[0] != 'd' )
				continue;
			size_t index = static_cast<size_t>( strtoul( key.c_str() + 1, nullptr, 10 ) );
			if( index >= 1 && index <= track.delivery_stages.size() )
				points = std::max( points, track.delivery_stages[index - 1].points );
		}
	}
	else if( strcmp( activity, DQ_HUNT ) == 0 ) {
		for( const std::string& key : keys ) {
			if( key.size() < 2 || key[0] != 'h' )
				continue;
			size_t index = static_cast<size_t>( strtoul( key.c_str() + 1, nullptr, 10 ) );
			if( index >= 1 && index <= track.hunt_stages.size() )
				points = std::max( points, track.hunt_stages[index - 1].points );
		}
	}
	else if( strcmp( activity, DQ_BG ) == 0 ) {
		for( const std::string& key : keys ) {
			if( key.size() < 2 || key[0] != 'b' )
				continue;
			size_t index = static_cast<size_t>( strtoul( key.c_str() + 1, nullptr, 10 ) );
			if( index >= 1 && index <= track.bg_stages.size() )
				points = std::max( points, track.bg_stages[index - 1].points );
		}
	}
	else if( strcmp( activity, DQ_INST ) == 0 ) {
		std::vector<uint16> kinds;
		for( const std::string& key : keys ) {
			if( key.size() >= 2 && key[0] == 'i' )
				kinds.push_back( static_cast<uint16>( strtoul( key.c_str() + 1, nullptr, 10 ) ) );
		}
		points = need_dq_inst_points( track, kinds );
	}
	return points;
}

uint16& dq_activity_slot( s_need_dq_account_row& b, const char* activity ) {
	if( strcmp( activity, "hunt" ) == 0 )
		return b.hunt_pts;
	if( strcmp( activity, "bg" ) == 0 )
		return b.bg_pts;
	if( strcmp( activity, "inst" ) == 0 )
		return b.inst_pts;
	return b.delivery_pts;
}

/// Common score commit (spec 4.4 + 4.6). Runs inside the caller's transaction, after
/// dq_lock_account_day. Never commits. snap_track = 0 for activities without an accept snapshot.
e_need_dq_result dq_commit_stages( const char* day, const s_need_dq_actor& actor, const char* activity, uint16 snap_track,
	const std::vector<uint16>& stages, s_need_dq_account_row& b, int32& gained ) {
	gained = 0;
	std::shared_ptr<s_need_dq_track> track = need_dq_band( actor.base_level );
	if( track == nullptr )
		return NEED_DQ_NOT_ELIGIBLE;
	if( snap_track != 0 && snap_track != track->track )
		return NEED_DQ_LEVEL_CHANGED;
	if( b.locked_track != 0 && b.locked_track != track->track )
		return NEED_DQ_LOCKED_OUT;

	for( uint16 stage : stages ) {
		if( SQL_ERROR == Sql_Query( mmysql_handle,
			"INSERT IGNORE INTO `need_dq_score_stage` (`day_key`,`account_id`,`activity`,`stage_key`,`char_id`,`track`,`base_level`) "
			"VALUES ('%s','%u','%s','%c%u','%u','%u','%u')",
			day, actor.account_id, activity, activity[0], stage, actor.char_id, track->track, actor.base_level ) ) {
			Sql_ShowDebug( mmysql_handle );
			return NEED_DQ_ERROR;
		}
	}

	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"SELECT `stage_key` FROM `need_dq_score_stage` WHERE `day_key`='%s' AND `account_id`='%u' AND `activity`='%s'",
		day, actor.account_id, activity ) ) {
		Sql_ShowDebug( mmysql_handle );
		return NEED_DQ_ERROR;
	}
	std::vector<std::string> keys;
	while( SQL_SUCCESS == Sql_NextRow( mmysql_handle ) )
		keys.push_back( dq_col_str( 0 ) );
	Sql_FreeResult( mmysql_handle );

	uint16 new_act = dq_activity_points( activity, *track, keys );
	uint16 old_act = dq_activity_slot( b, activity );
	if( new_act <= old_act )
		return NEED_DQ_NO_GAIN;

#ifdef NEED_DQ_TEST
	if( need_dq_test_fail_point == 1 ) {
		ShowWarning( "need_daily_quest: test fail point 1 (before the account UPDATE)\n" );
		return NEED_DQ_ERROR;
	}
#endif

	s_need_dq_account_row next = b;
	dq_activity_slot( next, activity ) = new_act;
	uint32 sum = static_cast<uint32>( next.hunt_pts ) + next.delivery_pts + next.bg_pts + next.inst_pts;
	next.total_pts = static_cast<uint16>( std::min<uint32>( DQ_DAILY_CAP, sum ) );
	bool first_score = ( b.locked_track == 0 );
	if( first_score ) {
		next.locked_track = track->track;
		next.locked_char_id = actor.char_id;
	}

	// One UPDATE, every value a constant computed from the locked row (no "col = col + x").
	char lock_set[96] = {};
	if( first_score )
		safesnprintf( lock_set, sizeof( lock_set ), ",`locked_track`='%u',`locked_char_id`='%u',`locked_at`=NOW()", next.locked_track, next.locked_char_id );
	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"UPDATE `need_dq_account_day` SET `hunt_pts`='%u',`delivery_pts`='%u',`bg_pts`='%u',`inst_pts`='%u',`total_pts`='%u'%s "
		"WHERE `day_key`='%s' AND `account_id`='%u'",
		next.hunt_pts, next.delivery_pts, next.bg_pts, next.inst_pts, next.total_pts, lock_set, day, actor.account_id ) ) {
		Sql_ShowDebug( mmysql_handle );
		return NEED_DQ_ERROR;
	}

	gained = new_act - old_act;
	b = next;
	return NEED_DQ_OK;
}

/// Pending write (spec 5.2): only into the pending's own day and track.
bool dq_flush_sql( uint32 char_id, const s_need_dq_pending& pending ) {
	if( pending.count == 0 )
		return true;
	std::shared_ptr<s_need_dq_track> track = need_dq_db.find( pending.track );
	if( track == nullptr )
		return false;
	if( pending.hunt ) {
		if( SQL_ERROR == Sql_Query( mmysql_handle,
			"UPDATE `need_dq_char_progress` SET `count`=LEAST(`count`+%u, %u) "
			"WHERE `day_key`='%s' AND `char_id`='%u' AND `activity`='%s' AND `track`='%u' AND `accepted_at` IS NOT NULL",
			pending.count, need_dq_hunt_goal( *track ), pending.day, char_id, DQ_HUNT, pending.track ) ) {
			Sql_ShowDebug( mmysql_handle );
			return false;
		}
		return true;
	}
	uint16 goal = need_dq_goal( *track );
	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"UPDATE `need_dq_char_progress` SET `collected`=LEAST(`collected`+%u, GREATEST(0, CAST(%u AS SIGNED)-CAST(`submitted` AS SIGNED))) "
		"WHERE `day_key`='%s' AND `char_id`='%u' AND `activity`='%s' AND `track`='%u' AND `accepted_at` IS NOT NULL",
		pending.count, goal, pending.day, char_id, DQ_DELIVERY, pending.track ) ) {
		Sql_ShowDebug( mmysql_handle );
		return false;
	}
	return true;
}

/*==========================================
 * Session cache, timers and kill credit
 *------------------------------------------*/

void dq_sync_one_timer( s_dq_session& s, s_need_dq_pending& p, map_session_data* sd ) {
	if( p.count == 0 && p.timer != INVALID_TIMER ) {
		delete_timer( p.timer, need_dq_flush_timer );
		p.timer = INVALID_TIMER;
	} else if( p.count > 0 && p.timer == INVALID_TIMER && sd != nullptr ) {
		p.timer = add_timer( gettick() + DQ_FLUSH_DELAY, need_dq_flush_timer, sd->id, static_cast<intptr_t>( s.char_id ) );
	}
}

void dq_sync_timer( s_dq_session& s, map_session_data* sd ) {
	dq_sync_one_timer( s, s.pending, sd );
	dq_sync_one_timer( s, s.hpending, sd );
}

void dq_session_reload( s_dq_session& s, const char* day ) {
	s_need_dq_account_row b;
	s.loaded = dq_read_char_sql( day, s.char_id, s.row, false ) && dq_read_char_sql( day, s.char_id, s.hrow, false, DQ_HUNT ) &&
		dq_read_account_sql( day, s.account_id, b, false ) && need_dq_read_bg_games( day, s.char_id, s.bg_games ) &&
		need_dq_read_inst_kinds( day, s.char_id, s.inst_kinds );
	s.locked_track = b.locked_track;
	safestrncpy( s.cache_day, day, sizeof( s.cache_day ) );
}

s_dq_session& dq_session( map_session_data* sd, const s_need_dq_day& day ) {
	s_dq_session& s = dq_sessions[sd->status.char_id];
	if( s.char_id == 0 ) {
		s.char_id = sd->status.char_id;
		s.account_id = sd->status.account_id;
		s.pending.timer = INVALID_TIMER;
		s.hpending.timer = INVALID_TIMER;
		s.hpending.hunt = true;
	}
	if( !s.loaded || strcmp( s.cache_day, day.key ) != 0 ) {
		dq_session_reload( s, day.key );
		s.notice_level = 0;
		s.notice_locked = false;
	}
	return s;
}

/// Hunt kills are written by need_dq_core_hunt_commit, which also scores every stage they reach,
/// so no stage is ever skipped between a write and a score (spec 4.5). Without a session (sd gone)
/// the kills are written only; their stage is scored on the next commit of that day.
void dq_hunt_flush( s_dq_session& s, map_session_data* sd, bool notify ) {
	if( s.hpending.count == 0 )
		return;
	char pday[11];
	safestrncpy( pday, s.hpending.day, sizeof( pday ) );
	uint16 n = s.hpending.count;
	uint32 before = s.hrow.count;
	bool same_day = strcmp( pday, s.cache_day ) == 0;
	e_need_dq_result r = NEED_DQ_ERROR;
	int32 gained = 0;
	if( sd != nullptr ) {
		s_need_dq_day d;
		safestrncpy( d.key, pday, sizeof( d.key ) );
		r = need_dq_core_hunt_commit( d, dq_actor( sd ), &s.hpending, gained );
		if( r == NEED_DQ_ERROR || r == NEED_DQ_DISABLED ) {
			ShowError( "need_daily_quest: hunt commit failed (char %u, day %s, %u kills dropped)\n", s.char_id, pday, n );
			s.hpending.count = 0;
		}
	} else if( !need_dq_flush( s.char_id, s.hpending ) ) {
		ShowError( "need_daily_quest: hunt flush failed (char %u, day %s, %u kills dropped)\n", s.char_id, pday, n );
	}
	if( battle_config.need_daily_quest_debug )
		ShowDebug( "need_daily_quest: hunt flush char %u day %s +%u -> result %d gained %d\n", s.char_id, pday, n, r, gained );
	if( !same_day )
		return;
	dq_session_reload( s, s.cache_day );
	if( !notify || sd == nullptr )
		return;
	std::shared_ptr<s_need_dq_track> t = s.hrow.exists ? need_dq_db.find( s.hrow.track ) : nullptr;
	if( t == nullptr )
		return;
	uint16 reached = 0;
	for( const auto& st : t->hunt_stages ) {
		if( st.amount > before && st.amount <= s.hrow.count )
			reached = st.amount;
	}
	char msg[CHAT_SIZE_MAX];
	if( reached != 0 && r == NEED_DQ_OK && gained > 0 ) {
		s_need_dq_account_row b;
		need_dq_read_account( s.cache_day, s.account_id, b );
		safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_HUNT_STAGE ), reached, gained, b.total_pts );
		clif_displaymessage( sd->fd, msg );
	} else if( reached != 0 && r == NEED_DQ_NO_GAIN ) {
		safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_HUNT_STAGE_KNOWN ), reached );
		clif_displaymessage( sd->fd, msg );
	}
	if( before < need_dq_hunt_goal( *t ) && s.hrow.count >= need_dq_hunt_goal( *t ) )
		clif_displaymessage( sd->fd, msg_txt( sd, MSG_DQ_HUNT_DONE ) );
}

/// Writes the pending count, then refreshes the cached row of that day.
void dq_session_flush( s_dq_session& s, map_session_data* sd ) {
	if( s.pending.count > 0 ) {
		if( !dq_flush_sql( s.char_id, s.pending ) )
			ShowError( "need_daily_quest: flush failed (char %u, day %s, %u pending dropped)\n", s.char_id, s.pending.day, s.pending.count );
		else if( battle_config.need_daily_quest_debug )
			ShowDebug( "need_daily_quest: flush char %u day %s track %u +%u\n", s.char_id, s.pending.day, s.pending.track, s.pending.count );
		bool same_day = strcmp( s.pending.day, s.cache_day ) == 0;
		s.pending.count = 0;
		if( same_day )
			dq_session_reload( s, s.cache_day );
	}
	dq_hunt_flush( s, sd, true );
	dq_sync_timer( s, sd );
}

TIMER_FUNC(need_dq_flush_timer) {
	auto it = dq_sessions.find( static_cast<uint32>( data ) );
	if( it == dq_sessions.end() )
		return 0;
	// fired: never delete_timer this id again
	if( it->second.pending.timer == tid )
		it->second.pending.timer = INVALID_TIMER;
	else if( it->second.hpending.timer == tid )
		it->second.hpending.timer = INVALID_TIMER;
	else
		return 0;
	dq_session_flush( it->second, map_id2sd( id ) );
	return 0;
}

uint16 dq_pending_for( const s_dq_session& s, const char* day, uint16 track, bool hunt = false ) {
	const s_need_dq_pending& p = hunt ? s.hpending : s.pending;
	return ( p.count > 0 && p.track == track && strcmp( p.day, day ) == 0 ) ? p.count : 0;
}

bool dq_busy( const map_session_data* sd ) {
	return sd->chatID != 0 || sd->state.vending || sd->state.buyingstore || sd->state.autotrade;
}

/// Delivery quest window entry of a track + map, or nullptr.
std::shared_ptr<s_need_dq_delivery_map> dq_delivery_map( uint16 track, const std::string& map_name ) {
	std::shared_ptr<s_need_dq_track> t = need_dq_db.find( track );
	if( t == nullptr )
		return nullptr;
	for( const auto& dmap : t->delivery_maps ) {
		if( dmap->name == map_name )
			return dmap;
	}
	return nullptr;
}

int32 dq_quest_index( const map_session_data* sd, int32 quest_id ) {
	for( int32 i = 0; i < sd->num_quests; i++ ) {
		if( sd->quest_log[i].quest_id == quest_id )
			return i;
	}
	return -1;
}

/// One quest window entry of a range: remove the others, (re)add the wanted one, write its count.
void dq_quest_sync_one( map_session_data* sd, int32 want, uint16 count, int32 qmin, int32 qmax ) {
	for( bool removed = true; removed; ) {
		removed = false;
		for( int32 i = 0; i < sd->num_quests; i++ ) {
			int32 qid = sd->quest_log[i].quest_id;
			if( qid < qmin || qid > qmax )
				continue;
			bool expired = sd->quest_log[i].time != 0 && sd->quest_log[i].time <= static_cast<uint32>( time( nullptr ) );
			if( qid != want || expired || sd->quest_log[i].state == Q_COMPLETE ) {
				quest_delete( sd, qid );
				removed = true;
				break;
			}
		}
	}
	if( want == 0 )
		return;

	int32 idx = dq_quest_index( sd, want );
	if( idx < 0 ) {
		if( quest_add( sd, want ) != 0 )
			return;
		idx = dq_quest_index( sd, want );
		if( idx < 0 )
			return;
	}
	struct quest& q = sd->quest_log[idx];
	if( q.count[0] != count ) {
		q.count[0] = count;
		sd->save_quest = true;
		clif_quest_update_objective( sd, &q );
	}
}

/// The quest window shows the ledger (display only, never read back):
/// - today's accepted delivery = its track + map quest (99100-99199), count = collected + submitted + pending
/// - today's accepted hunt = its track quest (99200-99299), count = kills + pending
/// - Lv150+: today's battleground = its track quest (99300-99399), count = valid games
/// - any other daily quest entry (another day, track or map) is removed
/// - an entry whose 04:00 limit has passed is removed and given again (same quest on another day)
/// Q_COMPLETE is never used: a completed quest could not be given again on a later day.
void dq_quest_sync( map_session_data* sd, s_dq_session& s, const s_need_dq_day& day ) {
	bool today = s.loaded && strcmp( s.cache_day, day.key ) == 0;
	int32 want = 0;
	uint16 count = 0;
	if( today && s.row.exists ) {
		std::shared_ptr<s_need_dq_delivery_map> dmap = dq_delivery_map( s.row.track, s.row.map_name );
		std::shared_ptr<s_need_dq_track> t = need_dq_db.find( s.row.track );
		if( dmap != nullptr && t != nullptr ) {
			want = dmap->quest_id;
			uint32 have = static_cast<uint32>( s.row.collected ) + s.row.submitted + dq_pending_for( s, day.key, s.row.track );
			count = static_cast<uint16>( std::min<uint32>( need_dq_goal( *t ), have ) );
		}
	}
	dq_quest_sync_one( sd, want, count, NEED_DQ_DELIVERY_QUEST_MIN, NEED_DQ_DELIVERY_QUEST_MAX );

	int32 hwant = 0;
	uint16 hcount = 0;
	if( today && s.hrow.exists ) {
		std::shared_ptr<s_need_dq_track> t = need_dq_db.find( s.hrow.track );
		if( t != nullptr ) {
			hwant = t->hunt_quest_id;
			uint32 have = s.hrow.count + dq_pending_for( s, day.key, s.hrow.track, true );
			hcount = static_cast<uint16>( std::min<uint32>( need_dq_hunt_goal( *t ), have ) );
		}
	}
	dq_quest_sync_one( sd, hwant, hcount, NEED_DQ_HUNT_QUEST_MIN, NEED_DQ_HUNT_QUEST_MAX );

	int32 bwant = 0;
	uint16 bcount = 0;
	if( today && s.row.exists ) {
		std::shared_ptr<s_need_dq_track> t = need_dq_db.find( s.row.track );
		if( t != nullptr && t->bg_quest_id != 0 ) {
			bwant = t->bg_quest_id;
			bcount = static_cast<uint16>( std::min<uint32>( need_dq_bg_goal( *t ), s.bg_games ) );
		}
	}
	dq_quest_sync_one( sd, bwant, bcount, NEED_DQ_BG_QUEST_MIN, NEED_DQ_BG_QUEST_MAX );

	int32 iwant = 0;
	uint16 icount = 0;
	if( today && s.row.exists ) {
		std::shared_ptr<s_need_dq_track> t = need_dq_db.find( s.row.track );
		if( t != nullptr && t->inst_quest_id != 0 ) {
			iwant = t->inst_quest_id;
			icount = need_dq_inst_points( *t, s.inst_kinds );
		}
	}
	dq_quest_sync_one( sd, iwant, icount, NEED_DQ_INST_QUEST_MIN, NEED_DQ_INST_QUEST_MAX );
}

bool dq_is_delivery_target( const s_dq_session& s, int16 m, int32 mob_id, std::shared_ptr<s_need_dq_delivery_map>& dmap ) {
	if( !s.row.exists )
		return false;
	dmap = dq_delivery_map( s.row.track, s.row.map_name );
	return dmap != nullptr && dmap->m == m && dmap->monsters.count( mob_id ) > 0;
}

bool dq_is_hunt_target( const s_dq_session& s, int16 m, int32 mob_id ) {
	if( !s.hrow.exists )
		return false;
	std::shared_ptr<s_need_dq_track> t = need_dq_db.find( s.hrow.track );
	if( t == nullptr )
		return false;
	for( const auto& hmap : t->hunt_maps ) {
		if( hmap->m == m && hmap->monsters.count( mob_id ) > 0 )
			return true;
	}
	return false;
}

/// One candidate (spec 7.1 step 4): an independent delivery roll and/or one hunt kill.
void dq_credit_one( map_session_data* sd, const s_need_dq_day& day, int16 m, int32 mob_id ) {
	s_dq_session& s = dq_session( sd, day );
	if( !s.loaded )
		return;
	std::shared_ptr<s_need_dq_delivery_map> dmap;
	bool delivery = dq_is_delivery_target( s, m, mob_id, dmap );
	bool hunt = dq_is_hunt_target( s, m, mob_id );
	if( !delivery && !hunt )
		return;
	uint16 accepted = delivery ? s.row.track : s.hrow.track;

	std::shared_ptr<s_need_dq_track> band = need_dq_band( sd->status.base_level );
	if( band == nullptr || band->track != accepted ) {
		// Spec 4.6: the real level range left the accepted one. Collection and hunting stop.
		if( s.notice_level != ( band != nullptr ? band->track : 1 ) ) {
			s.notice_level = band != nullptr ? band->track : 1;
			char now_label[16], acc_label[16];
			dq_track_label( band != nullptr ? band->track : 0, now_label );
			dq_track_label( accepted, acc_label );
			char msg[CHAT_SIZE_MAX];
			safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_LEVEL_CHANGED ), now_label, acc_label );
			clif_displaymessage( sd->fd, msg );
		}
		return;
	}
	if( s.locked_track != 0 && s.locked_track != accepted ) {
		if( !s.notice_locked ) {
			s.notice_locked = true;
			char label[16];
			dq_track_label( s.locked_track, label );
			char msg[CHAT_SIZE_MAX];
			safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_LOCKED_OUT ), label );
			clif_displaymessage( sd->fd, msg );
		}
		return;
	}

	if( delivery ) {
		const s_need_dq_track& track = *need_dq_db.find( s.row.track );
		uint16 goal = need_dq_goal( track );
		uint32 have = static_cast<uint32>( s.row.collected ) + s.row.submitted + dq_pending_for( s, day.key, track.track );
		int32 rate = dmap->rate;
#ifdef NEED_DQ_TEST
		if( need_dq_test_rate >= 0 )
			rate = need_dq_test_rate;
#endif
		if( have < goal && rnd_chance<int32>( rate, DQ_RATE_SCALE ) ) {
			// A pending count of another day or track is written to its own row first (spec 5.2).
			if( s.pending.count > 0 && ( s.pending.track != track.track || strcmp( s.pending.day, day.key ) != 0 ) )
				dq_session_flush( s, sd );
			if( s.pending.count == 0 ) {
				safestrncpy( s.pending.day, day.key, sizeof( s.pending.day ) );
				s.pending.track = track.track;
			}
			s.pending.count++;
			have++;

			char msg[CHAT_SIZE_MAX];
			safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_COLLECTED ), have, goal );
			clif_displaymessage( sd->fd, msg );
			if( have >= goal )
				clif_displaymessage( sd->fd, msg_txt( sd, MSG_DQ_GOAL_REACHED ) );

			if( s.pending.count >= DQ_FLUSH_COUNT )
				dq_session_flush( s, sd );
			else
				dq_sync_timer( s, sd );
		}
	}

	if( hunt ) {
		const s_need_dq_track& track = *need_dq_db.find( s.hrow.track );
		uint16 goal = need_dq_hunt_goal( track );
		uint32 have = s.hrow.count + dq_pending_for( s, day.key, track.track, true );
		if( have < goal ) {
			if( s.hpending.count > 0 && ( s.hpending.track != track.track || strcmp( s.hpending.day, day.key ) != 0 ) )
				dq_hunt_flush( s, sd, true );
			if( s.hpending.count == 0 ) {
				safestrncpy( s.hpending.day, day.key, sizeof( s.hpending.day ) );
				s.hpending.track = track.track;
			}
			s.hpending.count++;
			have++;
			bool stage = false;
			for( const auto& st : track.hunt_stages ) {
				if( st.amount == have )
					stage = true;
			}
			// a stage scores the moment it is reached (spec 4.5); otherwise every 10 kills / 60 s
			if( stage || s.hpending.count >= DQ_FLUSH_COUNT )
				dq_hunt_flush( s, sd, true );
			dq_sync_timer( s, sd );
		}
	}
	dq_quest_sync( sd, s, day );
}

/// Candidates of one qualifying kill: the top raw-damage player, plus party members who are
/// on the same map, inside AREA_SIZE, alive and active within DQ_IDLE_SECONDS (spec 7.1, 7.2).
void dq_credit_kill( map_session_data* killer, int16 m, int32 mob_id, const block_list* center, const s_need_dq_day& day, bool allow_gm ) {
#ifdef NEED_DQ_TEST
	allow_gm = allow_gm || need_dq_test_allow_gm != 0;
#endif
	std::vector<map_session_data*> candidates;
	bool killer_gm = pc_get_group_level( killer ) != 0;
	if( !dq_busy( killer ) && ( allow_gm || !killer_gm ) )
		candidates.push_back( killer );
	else if( killer_gm && !allow_gm ) {
		// A GM who accepted today is told once why nothing is collected.
		s_dq_session& s = dq_session( killer, day );
		if( ( s.row.exists || s.hrow.exists ) && !s.notice_gm ) {
			s.notice_gm = true;
			clif_displaymessage( killer->fd, msg_txt( killer, MSG_DQ_GM_EXCLUDED ) );
		}
	}

	struct party_data* p = killer->status.party_id != 0 ? party_search( killer->status.party_id ) : nullptr;
	if( p != nullptr ) {
		for( int32 i = 0; i < MAX_PARTY; i++ ) {
			map_session_data* member = p->data[i].sd;
			if( member == nullptr || member == killer || member->m != m || pc_isdead( member ) || dq_busy( member ) )
				continue;
			if( !allow_gm && pc_get_group_level( member ) != 0 )
				continue;
			if( !check_distance_bl( center, member, AREA_SIZE ) )
				continue;
			if( DIFF_TICK( last_tick, member->idletime ) > DQ_IDLE_SECONDS )
				continue;
			candidates.push_back( member );
		}
	}

	if( battle_config.need_daily_quest_debug )
		ShowDebug( "need_daily_quest: target kill map %s mob %d killer char %u gm %d busy %d -> %zu candidate(s)\n",
			map_getmapdata( m )->name, mob_id, killer->status.char_id, killer_gm ? 1 : 0, dq_busy( killer ) ? 1 : 0, candidates.size() );
	for( map_session_data* c : candidates )
		dq_credit_one( c, day, m, mob_id );
}

/// Fast filter of every kill: today's delivery map + whitelist, or any track's hunting ground + whitelist.
bool dq_any_target( int16 m, int32 mob_id, const s_need_dq_day& day ) {
	for( const auto& entry : need_dq_db ) {
		const std::shared_ptr<s_need_dq_delivery_map>& today = entry.second->rotation[day.wday];
		if( today != nullptr && today->m == m && today->monsters.count( mob_id ) > 0 )
			return true;
		for( const auto& hmap : entry.second->hunt_maps ) {
			if( hmap->m == m && hmap->monsters.count( mob_id ) > 0 )
				return true;
		}
	}
	return false;
}

void dq_set_reg( map_session_data* sd, const char* name, int64 value ) {
	pc_setreg( sd, add_str( name ), value );
}

void dq_set_regstr( map_session_data* sd, const char* name, const char* value ) {
	pc_setregstr( sd, add_str( name ), value );
}

s_need_dq_actor dq_actor( const map_session_data* sd ) {
	s_need_dq_actor a;
	a.account_id = sd->status.account_id;
	a.char_id = sd->status.char_id;
	a.base_level = static_cast<uint16>( sd->status.base_level );
	return a;
}

}  // namespace

/*==========================================
 * Configuration (db/need/daily_quest.yml)
 *------------------------------------------*/

const std::string NeedDailyQuestDatabase::getDefaultLocation() {
	if( !this->location_override.empty() )
		return this->location_override;
	return std::string( db_path ) + "/need/daily_quest.yml";
}

uint64 NeedDailyQuestDatabase::parseBodyNode( const ryml::NodeRef& node ) {
	uint64 n = this->parseTrack( node );
	if( n == 0 )
		this->parse_errors++;
	return n;
}

uint64 NeedDailyQuestDatabase::parseTrack( const ryml::NodeRef& node ) {
	std::shared_ptr<s_need_dq_track> t = std::make_shared<s_need_dq_track>();

	if( !this->asUInt16( node, "Track", t->track ) || !this->asUInt16( node, "MinLevel", t->min_level ) ||
		!this->asUInt16( node, "MaxLevel", t->max_level ) )
		return 0;
	if( t->min_level == 0 || t->min_level > t->max_level || t->track != t->min_level ) {
		this->invalidWarning( node["Track"], "Track %hu: Track must equal MinLevel and MinLevel <= MaxLevel.\n", t->track );
		return 0;
	}
	if( !this->nodeExists( node, "Delivery" ) ) {
		this->invalidWarning( node, "Track %hu has no Delivery.\n", t->track );
		return 0;
	}
	const ryml::NodeRef& delivery = node["Delivery"];

	if( !this->nodeExists( delivery, "Stages" ) ) {
		this->invalidWarning( delivery, "Track %hu: Delivery has no Stages.\n", t->track );
		return 0;
	}
	for( const ryml::NodeRef& stageNode : delivery["Stages"] ) {
		s_need_dq_stage stage = {};
		if( !this->asUInt16( stageNode, "Amount", stage.amount ) || !this->asUInt16( stageNode, "Points", stage.points ) )
			return 0;
		if( !t->delivery_stages.empty() && ( stage.amount <= t->delivery_stages.back().amount || stage.points <= t->delivery_stages.back().points ) ) {
			this->invalidWarning( stageNode, "Track %hu: Stages must increase in Amount and Points.\n", t->track );
			return 0;
		}
		if( stage.amount == 0 || stage.points == 0 ) {
			this->invalidWarning( stageNode, "Track %hu: Amount and Points must be positive.\n", t->track );
			return 0;
		}
		t->delivery_stages.push_back( stage );
	}
	if( t->delivery_stages.empty() || t->delivery_stages.size() > 9 ) {
		this->invalidWarning( delivery, "Track %hu: 1..9 Stages required.\n", t->track );
		return 0;
	}

	if( !this->nodeExists( delivery, "Maps" ) ) {
		this->invalidWarning( delivery, "Track %hu: Delivery has no Maps.\n", t->track );
		return 0;
	}
	for( const ryml::NodeRef& mapNode : delivery["Maps"] ) {
		std::shared_ptr<s_need_dq_delivery_map> dmap = std::make_shared<s_need_dq_delivery_map>();
		if( !this->asString( mapNode, "Map", dmap->name ) || !this->asUInt16Rate( mapNode, "Rate", dmap->rate ) ||
			!this->asInt32( mapNode, "QuestId", dmap->quest_id ) )
			return 0;
		if( dmap->quest_id < NEED_DQ_DELIVERY_QUEST_MIN || dmap->quest_id > NEED_DQ_DELIVERY_QUEST_MAX ) {
			this->invalidWarning( mapNode["QuestId"], "Track %hu: QuestId %d is outside %d-%d.\n", t->track, dmap->quest_id, NEED_DQ_DELIVERY_QUEST_MIN, NEED_DQ_DELIVERY_QUEST_MAX );
			return 0;
		}
		if( dmap->name.empty() || dmap->name.size() > 15 ||
			dmap->name.find_first_not_of( "abcdefghijklmnopqrstuvwxyz0123456789_" ) != std::string::npos ) {
			this->invalidWarning( mapNode["Map"], "Track %hu: invalid map name \"%s\".\n", t->track, dmap->name.c_str() );
			return 0;
		}
		dmap->m = map_mapname2mapid( dmap->name.c_str() );
		if( dmap->m < 0 ) {
			this->invalidWarning( mapNode["Map"], "Track %hu: map \"%s\" is not loaded on this map-server.\n", t->track, dmap->name.c_str() );
			return 0;
		}
		if( dmap->rate == 0 ) {
			this->invalidWarning( mapNode["Rate"], "Track %hu: map \"%s\" Rate must be positive.\n", t->track, dmap->name.c_str() );
			return 0;
		}
		if( !this->nodeExists( mapNode, "Monsters" ) ) {
			this->invalidWarning( mapNode, "Track %hu: map \"%s\" has no Monsters.\n", t->track, dmap->name.c_str() );
			return 0;
		}
		for( const ryml::NodeRef& mobNode : mapNode["Monsters"] ) {
			std::string aegis;
			c4::from_chars( mobNode.val(), &aegis );
			std::shared_ptr<s_mob_db> mob = mobdb_search_aegisname( aegis.c_str() );
			if( mob == nullptr ) {
				this->invalidWarning( mobNode, "Track %hu: unknown monster \"%s\".\n", t->track, aegis.c_str() );
				return 0;
			}
			dmap->monsters.insert( static_cast<int32>( mob->id ) );
		}
		if( dmap->monsters.empty() ) {
			this->invalidWarning( mapNode, "Track %hu: map \"%s\" has an empty Monsters list.\n", t->track, dmap->name.c_str() );
			return 0;
		}
		for( const auto& other : t->delivery_maps ) {
			if( other->name == dmap->name ) {
				this->invalidWarning( mapNode["Map"], "Track %hu: map \"%s\" listed twice.\n", t->track, dmap->name.c_str() );
				return 0;
			}
		}
		t->delivery_maps.push_back( dmap );
	}

	if( !this->nodeExists( delivery, "Rotation" ) ) {
		this->invalidWarning( delivery, "Track %hu: Delivery has no Rotation.\n", t->track );
		return 0;
	}
	const ryml::NodeRef& rotation = delivery["Rotation"];
	for( int32 d = 0; d < 7; d++ ) {
		std::string name;
		if( !this->asString( rotation, WEEKDAY_KEYS[d], name ) )
			return 0;
		for( const auto& dmap : t->delivery_maps ) {
			if( dmap->name == name )
				t->rotation[d] = dmap;
		}
		if( t->rotation[d] == nullptr ) {
			this->invalidWarning( rotation[c4::to_csubstr( WEEKDAY_KEYS[d] )], "Track %hu: %s map \"%s\" is not in Maps.\n", t->track, WEEKDAY_KEYS[d], name.c_str() );
			return 0;
		}
	}

	// Hunt (phase 2): fixed hunting ground per track, accepted together with the delivery.
	if( !this->nodeExists( node, "Hunt" ) ) {
		this->invalidWarning( node, "Track %hu has no Hunt.\n", t->track );
		return 0;
	}
	const ryml::NodeRef& hunt = node["Hunt"];
	if( !this->asInt32( hunt, "QuestId", t->hunt_quest_id ) )
		return 0;
	if( t->hunt_quest_id < NEED_DQ_HUNT_QUEST_MIN || t->hunt_quest_id > NEED_DQ_HUNT_QUEST_MAX ) {
		this->invalidWarning( hunt["QuestId"], "Track %hu: Hunt QuestId %d is outside %d-%d.\n", t->track, t->hunt_quest_id, NEED_DQ_HUNT_QUEST_MIN, NEED_DQ_HUNT_QUEST_MAX );
		return 0;
	}
	if( !this->nodeExists( hunt, "Stages" ) || !this->nodeExists( hunt, "Maps" ) ) {
		this->invalidWarning( hunt, "Track %hu: Hunt needs Stages and Maps.\n", t->track );
		return 0;
	}
	for( const ryml::NodeRef& stageNode : hunt["Stages"] ) {
		s_need_dq_stage stage = {};
		if( !this->asUInt16( stageNode, "Amount", stage.amount ) || !this->asUInt16( stageNode, "Points", stage.points ) )
			return 0;
		if( stage.amount == 0 || stage.points == 0 || ( !t->hunt_stages.empty() &&
			( stage.amount <= t->hunt_stages.back().amount || stage.points <= t->hunt_stages.back().points ) ) ) {
			this->invalidWarning( stageNode, "Track %hu: Hunt Stages must be positive and increase.\n", t->track );
			return 0;
		}
		t->hunt_stages.push_back( stage );
	}
	if( t->hunt_stages.empty() || t->hunt_stages.size() > 9 ) {
		this->invalidWarning( hunt, "Track %hu: 1..9 Hunt Stages required.\n", t->track );
		return 0;
	}
	for( const ryml::NodeRef& mapNode : hunt["Maps"] ) {
		std::shared_ptr<s_need_dq_hunt_map> hmap = std::make_shared<s_need_dq_hunt_map>();
		if( !this->asString( mapNode, "Map", hmap->name ) )
			return 0;
		hmap->m = map_mapname2mapid( hmap->name.c_str() );
		if( hmap->m < 0 ) {
			this->invalidWarning( mapNode["Map"], "Track %hu: hunt map \"%s\" is not loaded on this map-server.\n", t->track, hmap->name.c_str() );
			return 0;
		}
		if( !this->nodeExists( mapNode, "Monsters" ) ) {
			this->invalidWarning( mapNode, "Track %hu: hunt map \"%s\" has no Monsters.\n", t->track, hmap->name.c_str() );
			return 0;
		}
		for( const ryml::NodeRef& mobNode : mapNode["Monsters"] ) {
			std::string aegis;
			c4::from_chars( mobNode.val(), &aegis );
			std::shared_ptr<s_mob_db> mob = mobdb_search_aegisname( aegis.c_str() );
			if( mob == nullptr ) {
				this->invalidWarning( mobNode, "Track %hu: unknown monster \"%s\".\n", t->track, aegis.c_str() );
				return 0;
			}
			hmap->monsters.insert( static_cast<int32>( mob->id ) );
		}
		if( hmap->monsters.empty() ) {
			this->invalidWarning( mapNode, "Track %hu: hunt map \"%s\" has an empty Monsters list.\n", t->track, hmap->name.c_str() );
			return 0;
		}
		t->hunt_maps.push_back( hmap );
	}
	if( t->hunt_maps.empty() ) {
		this->invalidWarning( hunt, "Track %hu: Hunt has no Maps.\n", t->track );
		return 0;
	}

	// Battleground (phase 3): optional, only the tracks that list it score games.
	if( this->nodeExists( node, "Battleground" ) ) {
		const ryml::NodeRef& bg = node["Battleground"];
		if( !this->asInt32( bg, "QuestId", t->bg_quest_id ) )
			return 0;
		if( t->bg_quest_id < NEED_DQ_BG_QUEST_MIN || t->bg_quest_id > NEED_DQ_BG_QUEST_MAX ) {
			this->invalidWarning( bg["QuestId"], "Track %hu: Battleground QuestId %d is outside %d-%d.\n", t->track, t->bg_quest_id, NEED_DQ_BG_QUEST_MIN, NEED_DQ_BG_QUEST_MAX );
			return 0;
		}
		if( !this->nodeExists( bg, "Stages" ) ) {
			this->invalidWarning( bg, "Track %hu: Battleground has no Stages.\n", t->track );
			return 0;
		}
		for( const ryml::NodeRef& stageNode : bg["Stages"] ) {
			s_need_dq_stage stage = {};
			if( !this->asUInt16( stageNode, "Amount", stage.amount ) || !this->asUInt16( stageNode, "Points", stage.points ) )
				return 0;
			if( stage.amount == 0 || stage.points == 0 || ( !t->bg_stages.empty() &&
				( stage.amount <= t->bg_stages.back().amount || stage.points <= t->bg_stages.back().points ) ) ) {
				this->invalidWarning( stageNode, "Track %hu: Battleground Stages must be positive and increase.\n", t->track );
				return 0;
			}
			t->bg_stages.push_back( stage );
		}
		if( t->bg_stages.empty() || t->bg_stages.size() > 9 ) {
			this->invalidWarning( bg, "Track %hu: 1..9 Battleground Stages required.\n", t->track );
			return 0;
		}
	}

	// Instances (phase 4): optional, only the tracks that list it score clears (decision 2026-10-21: Lv175).
	if( this->nodeExists( node, "Instance" ) ) {
		const ryml::NodeRef& inst = node["Instance"];
		if( !this->asInt32( inst, "QuestId", t->inst_quest_id ) || !this->asUInt16( inst, "Cap", t->inst_cap ) )
			return 0;
		if( t->inst_quest_id < NEED_DQ_INST_QUEST_MIN || t->inst_quest_id > NEED_DQ_INST_QUEST_MAX ) {
			this->invalidWarning( inst["QuestId"], "Track %hu: Instance QuestId %d is outside %d-%d.\n", t->track, t->inst_quest_id, NEED_DQ_INST_QUEST_MIN, NEED_DQ_INST_QUEST_MAX );
			return 0;
		}
		if( t->inst_cap == 0 ) {
			this->invalidWarning( inst["Cap"], "Track %hu: Instance Cap must be positive.\n", t->track );
			return 0;
		}
		if( !this->nodeExists( inst, "Kinds" ) ) {
			this->invalidWarning( inst, "Track %hu: Instance has no Kinds.\n", t->track );
			return 0;
		}
		for( const ryml::NodeRef& kindNode : inst["Kinds"] ) {
			std::string name;
			s_need_dq_inst_kind k = {};
			if( !this->asString( kindNode, "Name", name ) || !this->asUInt16( kindNode, "Points", k.points ) )
				return 0;
			k.kind = need_dq_inst_kind_id( name.c_str() );
			if( k.kind == NEED_DQ_INST_NONE ) {
				this->invalidWarning( kindNode["Name"], "Track %hu: unknown Instance kind %s (OGH_HARD, OGH, SKY_FORTRESS).\n", t->track, name.c_str() );
				return 0;
			}
			if( k.points == 0 ) {
				this->invalidWarning( kindNode, "Track %hu: Instance %s Points must be positive.\n", t->track, name.c_str() );
				return 0;
			}
			for( const auto& o : t->inst_kinds ) {
				if( o.kind == k.kind ) {
					this->invalidWarning( kindNode, "Track %hu: Instance %s is listed twice.\n", t->track, name.c_str() );
					return 0;
				}
			}
			t->inst_kinds.push_back( k );
		}
		if( t->inst_kinds.empty() ) {
			this->invalidWarning( inst, "Track %hu: Instance has no Kinds.\n", t->track );
			return 0;
		}
	}

	this->put( t->track, t );
	return 1;
}

void NeedDailyQuestDatabase::loadingFinished() {
	// Whole-file checks. Any failure disables the feature (T7-1).
	this->valid = this->size() > 0 && this->parse_errors == 0;
	if( this->parse_errors > 0 )
		ShowError( "need_daily_quest: %u track(s) rejected.\n", this->parse_errors );
	std::vector<std::shared_ptr<s_need_dq_track>> tracks;
	for( const auto& entry : *this )
		tracks.push_back( entry.second );
	for( size_t i = 0; i < tracks.size(); i++ ) {
		for( size_t j = i + 1; j < tracks.size(); j++ ) {
			if( tracks[i]->min_level <= tracks[j]->max_level && tracks[j]->min_level <= tracks[i]->max_level ) {
				ShowError( "need_daily_quest: tracks %u and %u overlap in level range.\n", tracks[i]->track, tracks[j]->track );
				this->valid = false;
			}
			for( int32 d = 0; d < 7; d++ ) {
				if( tracks[i]->rotation[d]->name == tracks[j]->rotation[d]->name ) {
					// D10: one map never serves two tracks on the same weekday (ver_eju).
					ShowError( "need_daily_quest: %s map \"%s\" is assigned to tracks %u and %u.\n",
						WEEKDAY_KEYS[d], tracks[i]->rotation[d]->name.c_str(), tracks[i]->track, tracks[j]->track );
					this->valid = false;
				}
			}
		}
	}
	std::unordered_set<int32> quest_ids;
	for( const auto& t : tracks ) {
		for( const auto& dmap : t->delivery_maps ) {
			if( !quest_ids.insert( dmap->quest_id ).second ) {
				ShowError( "need_daily_quest: QuestId %d is used twice.\n", dmap->quest_id );
				this->valid = false;
			}
			std::shared_ptr<s_quest_db> qi = quest_search( dmap->quest_id );
			if( qi == nullptr || qi->objectives.size() != 1 || qi->objectives[0]->count != need_dq_goal( *t ) ) {
				ShowError( "need_daily_quest: quest %d (track %u, %s) is missing in quest_db or its target count is not %u (db/need/daily_quest_quests.yml).\n",
					dmap->quest_id, t->track, dmap->name.c_str(), need_dq_goal( *t ) );
				this->valid = false;
			}
		}
	}
	for( const auto& t : tracks ) {
		if( !quest_ids.insert( t->hunt_quest_id ).second ) {
			ShowError( "need_daily_quest: QuestId %d is used twice.\n", t->hunt_quest_id );
			this->valid = false;
		}
		std::shared_ptr<s_quest_db> qi = quest_search( t->hunt_quest_id );
		if( qi == nullptr || qi->objectives.size() != 1 || qi->objectives[0]->count != need_dq_hunt_goal( *t ) ) {
			ShowError( "need_daily_quest: hunt quest %d (track %u) is missing in quest_db or its target count is not %u (db/need/daily_quest_quests.yml).\n",
				t->hunt_quest_id, t->track, need_dq_hunt_goal( *t ) );
			this->valid = false;
		}
		if( t->bg_quest_id != 0 ) {
			if( !quest_ids.insert( t->bg_quest_id ).second ) {
				ShowError( "need_daily_quest: QuestId %d is used twice.\n", t->bg_quest_id );
				this->valid = false;
			}
			std::shared_ptr<s_quest_db> bq = quest_search( t->bg_quest_id );
			if( bq == nullptr || bq->objectives.size() != 1 || bq->objectives[0]->count != need_dq_bg_goal( *t ) ) {
				ShowError( "need_daily_quest: battleground quest %d (track %u) is missing in quest_db or its target count is not %u (db/need/daily_quest_quests.yml).\n",
					t->bg_quest_id, t->track, need_dq_bg_goal( *t ) );
				this->valid = false;
			}
		}
		if( t->inst_quest_id != 0 ) {
			if( !quest_ids.insert( t->inst_quest_id ).second ) {
				ShowError( "need_daily_quest: QuestId %d is used twice.\n", t->inst_quest_id );
				this->valid = false;
			}
			std::shared_ptr<s_quest_db> iq = quest_search( t->inst_quest_id );
			if( iq == nullptr || iq->objectives.size() != 1 || iq->objectives[0]->count != t->inst_cap ) {
				ShowError( "need_daily_quest: instance quest %d (track %u) is missing in quest_db or its target count is not %u (db/need/daily_quest_quests.yml).\n",
					t->inst_quest_id, t->track, t->inst_cap );
				this->valid = false;
			}
		}
		// A hunting ground is never a delivery map (v0.4 review: separate activities).
		for( const auto& hmap : t->hunt_maps ) {
			for( const auto& o : tracks ) {
				for( const auto& dmap : o->delivery_maps ) {
					if( dmap->m == hmap->m ) {
						ShowError( "need_daily_quest: hunt map \"%s\" (track %u) is also a delivery map of track %u.\n", hmap->name.c_str(), t->track, o->track );
						this->valid = false;
					}
				}
			}
		}
	}
	if( !this->valid )
		ShowError( "need_daily_quest: configuration '%s' is invalid, the daily quest is disabled.\n", this->getDefaultLocation().c_str() );
	TypesafeYamlDatabase::loadingFinished();
}

/*==========================================
 * Public helpers
 *------------------------------------------*/

bool need_dq_logical_day( s_need_dq_day& day ) {
	time_t now = time( nullptr );
#ifdef NEED_DQ_TEST
	now += static_cast<time_t>( need_dq_test_offset );
#endif
	// Operator decision 2026-10-22: the daily quest day is KST 04:00 whatever the host time zone is
	// (KST = UTC+9, no daylight saving). Other NEED events still follow the host's local time.
	time_t shifted = now + DQ_KST_OFFSET - ( 4 * 60 * 60 );
	struct tm* kst = gmtime( &shifted );
	if( kst == nullptr || strftime( day.key, sizeof( day.key ), "%Y-%m-%d", kst ) != 10 )
		return false;
	day.wday = kst->tm_wday;
	return true;
}

std::shared_ptr<s_need_dq_track> need_dq_band( uint16 base_level ) {
	for( const auto& entry : need_dq_db ) {
		if( base_level >= entry.second->min_level && base_level <= entry.second->max_level )
			return entry.second;
	}
	return nullptr;
}

uint16 need_dq_goal( const s_need_dq_track& track ) {
	return track.delivery_stages.empty() ? 0 : track.delivery_stages.back().amount;
}

uint16 need_dq_hunt_goal( const s_need_dq_track& track ) {
	return track.hunt_stages.empty() ? 0 : track.hunt_stages.back().amount;
}

uint16 need_dq_bg_goal( const s_need_dq_track& track ) {
	return track.bg_stages.empty() ? 0 : track.bg_stages.back().amount;
}

uint16 need_dq_inst_kind_id( const char* name ) {
	static const char* names[NEED_DQ_INST_KIND_MAX + 1] = { "", "OGH_HARD", "OGH", "SKY_FORTRESS" };
	for( uint16 k = 1; name != nullptr && k <= NEED_DQ_INST_KIND_MAX; k++ ) {
		if( strcmp( name, names[k] ) == 0 )
			return k;
	}
	return NEED_DQ_INST_NONE;
}

uint16 need_dq_inst_points( const s_need_dq_track& track, const std::vector<uint16>& kinds ) {
	uint32 sum = 0;
	for( const auto& k : track.inst_kinds ) {
		if( std::find( kinds.begin(), kinds.end(), k.kind ) != kinds.end() )
			sum += k.points;
	}
	return static_cast<uint16>( std::min<uint32>( track.inst_cap, sum ) );
}

uint32 need_dq_bg_min_seconds( const std::string& type ) {
	return type == "KVM" ? 180 : 360;   // U9 / v0.4: Tierra, Flavius 6 min, KVM 3 min
}

e_need_dq_bg_reason need_dq_bg_validate( const s_need_dq_bg_match& match, uint32 char_id, int16 m, time_t now, time_t idletime ) {
	if( match.started == 0 )
		return NEED_DQ_BG_NO_MATCH;
	bool joined = false;
	for( const auto& p : match.players ) {
		if( p.char_id == char_id )
			joined = true;
	}
	if( !joined )
		return NEED_DQ_BG_NOT_PARTICIPANT;
	if( m != match.m )
		return NEED_DQ_BG_NOT_ON_MAP;
	if( now - match.started < static_cast<time_t>( need_dq_bg_min_seconds( match.type ) ) )
		return NEED_DQ_BG_TOO_SHORT;
	if( idletime < match.started )
		return NEED_DQ_BG_NO_ACTION;   // the last action is older than the battle
	uint32 ip_percent = DQ_BG_IP_PERCENT;
#ifdef NEED_DQ_TEST
	if( need_dq_test_bg_ip_percent >= 0 )
		ip_percent = static_cast<uint32>( need_dq_test_bg_ip_percent );
#endif
	if( static_cast<uint32>( match.distinct_ips ) * 100 < static_cast<uint32>( match.players.size() ) * ip_percent )
		return NEED_DQ_BG_SHARED_IP;
	return NEED_DQ_BG_VALID;
}

bool need_dq_flush( uint32 char_id, s_need_dq_pending& pending ) {
	bool ok = dq_flush_sql( char_id, pending );
	pending.count = 0;
	return ok;
}

bool need_dq_read_char( const char* day, uint32 char_id, s_need_dq_char_row& row ) {
	return dq_read_char_sql( day, char_id, row, false );
}

bool need_dq_read_hunt( const char* day, uint32 char_id, s_need_dq_char_row& row ) {
	return dq_read_char_sql( day, char_id, row, false, DQ_HUNT );
}

bool need_dq_read_bg_games( const char* day, uint32 char_id, uint32& games ) {
	games = 0;
	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"SELECT COUNT(*) FROM `need_dq_bg_match` WHERE `day_key`='%s' AND `char_id`='%u' AND `valid`='1' AND `eligible`='1'", day, char_id ) ) {
		Sql_ShowDebug( mmysql_handle );
		return false;
	}
	if( SQL_SUCCESS == Sql_NextRow( mmysql_handle ) )
		games = dq_col_u32( 0 );
	Sql_FreeResult( mmysql_handle );
	return true;
}

bool need_dq_read_inst_kinds( const char* day, uint32 char_id, std::vector<uint16>& kinds ) {
	kinds.clear();
	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"SELECT `kind` FROM `need_dq_inst_clear` WHERE `day_key`='%s' AND `char_id`='%u'", day, char_id ) ) {
		Sql_ShowDebug( mmysql_handle );
		return false;
	}
	while( SQL_SUCCESS == Sql_NextRow( mmysql_handle ) )
		kinds.push_back( static_cast<uint16>( dq_col_u32( 0 ) ) );
	Sql_FreeResult( mmysql_handle );
	return true;
}

bool need_dq_read_bg_account_games( const char* day, uint32 account_id, uint16 track, uint32& games ) {
	games = 0;
	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"SELECT COUNT(*) FROM `need_dq_bg_match` WHERE `day_key`='%s' AND `account_id`='%u' AND `track`='%u' AND `valid`='1' AND `eligible`='1'",
		day, account_id, track ) ) {
		Sql_ShowDebug( mmysql_handle );
		return false;
	}
	if( SQL_SUCCESS == Sql_NextRow( mmysql_handle ) )
		games = dq_col_u32( 0 );
	Sql_FreeResult( mmysql_handle );
	return true;
}

bool need_dq_read_account( const char* day, uint32 account_id, s_need_dq_account_row& row ) {
	return dq_read_account_sql( day, account_id, row, false );
}

/*==========================================
 * Core transactions (one script command = one transaction)
 *------------------------------------------*/

e_need_dq_result need_dq_core_accept( const s_need_dq_day& day, const s_need_dq_actor& actor ) {
	if( !dq_active() )
		return NEED_DQ_DISABLED;
	std::shared_ptr<s_need_dq_track> band = need_dq_band( actor.base_level );
	if( band == nullptr )
		return NEED_DQ_NOT_ELIGIBLE;
	if( !dq_query_str( "START TRANSACTION" ) )
		return NEED_DQ_ERROR;

	s_need_dq_account_row b;
	s_need_dq_char_row a, h;
	if( !dq_lock_account_day( day.key, actor.account_id, b ) || !dq_read_char_sql( day.key, actor.char_id, a, true ) ||
		!dq_read_char_sql( day.key, actor.char_id, h, true, DQ_HUNT ) ) {
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	if( ( a.exists && a.track != band->track ) || ( h.exists && h.track != band->track ) ) {
		dq_rollback();
		return NEED_DQ_LEVEL_CHANGED;
	}
	if( a.exists && h.exists ) {
		dq_rollback();
		return NEED_DQ_ALREADY;
	}
	if( b.locked_track != 0 && b.locked_track != band->track ) {
		dq_rollback();
		return NEED_DQ_LOCKED_OUT;
	}

	// One accept starts both activities (decision 2026-10-21); a row accepted before phase 2 gets its partner.
	char esc_map[MAP_NAME_LENGTH_EXT * 2 + 1] = {};
	const std::string& map_name = band->rotation[day.wday]->name;
	Sql_EscapeStringLen( mmysql_handle, esc_map, map_name.c_str(), map_name.size() );
	if( !a.exists && SQL_ERROR == Sql_Query( mmysql_handle,
		"INSERT INTO `need_dq_char_progress` (`day_key`,`char_id`,`activity`,`account_id`,`track`,`map_name`,`accepted_at`) "
		"VALUES ('%s','%u','%s','%u','%u','%s',NOW())",
		day.key, actor.char_id, DQ_DELIVERY, actor.account_id, band->track, esc_map ) ) {
		Sql_ShowDebug( mmysql_handle );
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	if( !h.exists && SQL_ERROR == Sql_Query( mmysql_handle,
		"INSERT INTO `need_dq_char_progress` (`day_key`,`char_id`,`activity`,`account_id`,`track`,`map_name`,`accepted_at`) "
		"VALUES ('%s','%u','%s','%u','%u','',NOW())",
		day.key, actor.char_id, DQ_HUNT, actor.account_id, band->track ) ) {
		Sql_ShowDebug( mmysql_handle );
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	if( !dq_query_str( "COMMIT" ) ) {
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	return b.locked_track != 0 ? NEED_DQ_OK : NEED_DQ_OK_UNLOCKED;
}

e_need_dq_result need_dq_core_reaccept( const s_need_dq_day& day, const s_need_dq_actor& actor, s_need_dq_pending* pending, s_need_dq_pending* hpending ) {
	if( !dq_active() )
		return NEED_DQ_DISABLED;
	std::shared_ptr<s_need_dq_track> band = need_dq_band( actor.base_level );
	if( band == nullptr )
		return NEED_DQ_NOT_ELIGIBLE;

	// Pending of another day goes to its own row before the transaction (spec 5.2).
	for( s_need_dq_pending* p : { pending, hpending } ) {
		if( p != nullptr && p->count > 0 && strcmp( p->day, day.key ) != 0 )
			need_dq_flush( actor.char_id, *p );
	}
	if( !dq_query_str( "START TRANSACTION" ) )
		return NEED_DQ_ERROR;

	uint16 saved = pending != nullptr ? pending->count : 0;
	uint16 hsaved = hpending != nullptr ? hpending->count : 0;
	auto fail = [&]( e_need_dq_result r ) {
		dq_rollback();
		if( pending != nullptr )
			pending->count = saved;
		if( hpending != nullptr )
			hpending->count = hsaved;
		return r;
	};

	s_need_dq_account_row b;
	s_need_dq_char_row a, h;
	if( !dq_lock_account_day( day.key, actor.account_id, b ) )
		return fail( NEED_DQ_ERROR );
	// The old track's pending counts land in the old rows first, then become abandoned_collected.
	for( s_need_dq_pending* p : { pending, hpending } ) {
		if( p != nullptr && p->count > 0 && !need_dq_flush( actor.char_id, *p ) )
			return fail( NEED_DQ_ERROR );
	}
	if( !dq_read_char_sql( day.key, actor.char_id, a, true ) || !dq_read_char_sql( day.key, actor.char_id, h, true, DQ_HUNT ) )
		return fail( NEED_DQ_ERROR );
	bool move_a = a.exists && a.track != band->track;
	bool move_h = h.exists && h.track != band->track;
	if( !move_a && !move_h )
		return fail( NEED_DQ_NOT_NEEDED );
	if( b.locked_track != 0 && b.locked_track != band->track )
		return fail( NEED_DQ_LOCKED_OUT );

	if( move_a ) {
		char esc_map[MAP_NAME_LENGTH_EXT * 2 + 1] = {};
		const std::string& map_name = band->rotation[day.wday]->name;
		Sql_EscapeStringLen( mmysql_handle, esc_map, map_name.c_str(), map_name.size() );
		uint32 abandoned = std::min<uint32>( 65535, static_cast<uint32>( a.abandoned ) + a.collected );
		if( SQL_ERROR == Sql_Query( mmysql_handle,
			"UPDATE `need_dq_char_progress` SET `track`='%u',`map_name`='%s',`accepted_at`=NOW(),`abandoned_collected`='%u',"
			"`collected`=0,`submitted`=0,`reaccept_count`='%u' "
			"WHERE `day_key`='%s' AND `char_id`='%u' AND `activity`='%s'",
			band->track, esc_map, abandoned, std::min<uint32>( 255, a.reaccept_count + 1u ), day.key, actor.char_id, DQ_DELIVERY ) ) {
			Sql_ShowDebug( mmysql_handle );
			return fail( NEED_DQ_ERROR );
		}
	}
	if( move_h ) {
		// Spec 4.6 L4: the hunt restarts from 0 in the new range; the old kills are kept for audit.
		uint32 abandoned = std::min<uint32>( 65535, static_cast<uint32>( h.abandoned ) + h.count );
		if( SQL_ERROR == Sql_Query( mmysql_handle,
			"UPDATE `need_dq_char_progress` SET `track`='%u',`accepted_at`=NOW(),`abandoned_collected`='%u',"
			"`count`=0,`reaccept_count`='%u' "
			"WHERE `day_key`='%s' AND `char_id`='%u' AND `activity`='%s'",
			band->track, abandoned, std::min<uint32>( 255, h.reaccept_count + 1u ), day.key, actor.char_id, DQ_HUNT ) ) {
			Sql_ShowDebug( mmysql_handle );
			return fail( NEED_DQ_ERROR );
		}
	}
	if( !dq_query_str( "COMMIT" ) )
		return fail( NEED_DQ_ERROR );
	return NEED_DQ_OK;
}

e_need_dq_result need_dq_core_submit( const s_need_dq_day& day, const s_need_dq_actor& actor, s_need_dq_pending* pending, int32& gained ) {
	gained = 0;
	if( !dq_active() )
		return NEED_DQ_DISABLED;

	if( pending != nullptr && pending->count > 0 && strcmp( pending->day, day.key ) != 0 )
		need_dq_flush( actor.char_id, *pending );   // previous day: its own row, never today's score
	if( !dq_query_str( "START TRANSACTION" ) )
		return NEED_DQ_ERROR;

	uint16 saved = pending != nullptr ? pending->count : 0;
	auto fail = [&]( e_need_dq_result r ) {
		dq_rollback();
		if( pending != nullptr )
			pending->count = saved;   // nothing was written: keep the count for its own row
		return r;
	};

	s_need_dq_account_row b;
	s_need_dq_char_row a;
	if( !dq_lock_account_day( day.key, actor.account_id, b ) )
		return fail( NEED_DQ_ERROR );
	if( pending != nullptr && pending->count > 0 && !need_dq_flush( actor.char_id, *pending ) )
		return fail( NEED_DQ_ERROR );
	if( !dq_read_char_sql( day.key, actor.char_id, a, true ) )
		return fail( NEED_DQ_ERROR );
	if( !a.exists )
		return fail( NEED_DQ_NOT_ACCEPTED );

	std::shared_ptr<s_need_dq_track> snap = need_dq_db.find( a.track );
	if( snap == nullptr )
		return fail( NEED_DQ_ERROR );
	uint16 goal = need_dq_goal( *snap );
	uint16 new_sub = static_cast<uint16>( std::min<uint32>( goal, static_cast<uint32>( a.submitted ) + a.collected ) );
	std::vector<uint16> stages;
	for( size_t i = 0; i < snap->delivery_stages.size(); i++ ) {
		if( snap->delivery_stages[i].amount <= new_sub )
			stages.push_back( static_cast<uint16>( i + 1 ) );
	}

	e_need_dq_result r = dq_commit_stages( day.key, actor, DQ_DELIVERY, a.track, stages, b, gained );
	if( r == NEED_DQ_LEVEL_CHANGED || r == NEED_DQ_NOT_ELIGIBLE || r == NEED_DQ_ERROR ) {
		gained = 0;
		return fail( r );   // spec 4.6: no write at all, the collection stays where it is
	}
	// OK / NO_GAIN / LOCKED_OUT (spec 4.2: the delivery is recorded, it scores 0)
	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"UPDATE `need_dq_char_progress` SET `submitted`='%u',`collected`=0 "
		"WHERE `day_key`='%s' AND `char_id`='%u' AND `activity`='%s'",
		new_sub, day.key, actor.char_id, DQ_DELIVERY ) ) {
		Sql_ShowDebug( mmysql_handle );
		gained = 0;
		return fail( NEED_DQ_ERROR );
	}
#ifdef NEED_DQ_TEST
	if( need_dq_test_fail_point == 2 ) {
		ShowWarning( "need_daily_quest: test fail point 2 (before COMMIT)\n" );
		gained = 0;
		return fail( NEED_DQ_ERROR );
	}
#endif
	if( !dq_query_str( "COMMIT" ) ) {
		gained = 0;
		return fail( NEED_DQ_ERROR );
	}
	return r;
}

e_need_dq_result need_dq_core_hunt_commit( const s_need_dq_day& day, const s_need_dq_actor& actor, s_need_dq_pending* hpending, int32& gained ) {
	gained = 0;
	if( !dq_active() )
		return NEED_DQ_DISABLED;
	// Kills of another day are written to that day's row; their stages are that day's business.
	if( hpending != nullptr && hpending->count > 0 && strcmp( hpending->day, day.key ) != 0 )
		need_dq_flush( actor.char_id, *hpending );
	if( !dq_query_str( "START TRANSACTION" ) )
		return NEED_DQ_ERROR;

	uint16 saved = hpending != nullptr ? hpending->count : 0;
	auto fail = [&]( e_need_dq_result r ) {
		dq_rollback();
		if( hpending != nullptr )
			hpending->count = saved;
		return r;
	};

	s_need_dq_account_row b;
	s_need_dq_char_row h;
	if( !dq_lock_account_day( day.key, actor.account_id, b ) || !dq_read_char_sql( day.key, actor.char_id, h, true, DQ_HUNT ) )
		return fail( NEED_DQ_ERROR );
	if( !h.exists ) {
		if( hpending != nullptr )
			hpending->count = 0;   // no row: the kills have nowhere to go (not accepted)
		dq_rollback();
		return NEED_DQ_NOT_ACCEPTED;
	}
	// the row is locked: write the pending kills, then read the new count
	if( hpending != nullptr && hpending->count > 0 ) {
		if( !need_dq_flush( actor.char_id, *hpending ) || !dq_read_char_sql( day.key, actor.char_id, h, true, DQ_HUNT ) )
			return fail( NEED_DQ_ERROR );
	}
	std::shared_ptr<s_need_dq_track> snap = need_dq_db.find( h.track );
	if( snap == nullptr )
		return fail( NEED_DQ_ERROR );
	std::vector<uint16> stages;
	for( size_t i = 0; i < snap->hunt_stages.size(); i++ ) {
		if( snap->hunt_stages[i].amount <= h.count )
			stages.push_back( static_cast<uint16>( i + 1 ) );
	}
	e_need_dq_result r = stages.empty() ? NEED_DQ_NO_GAIN : dq_commit_stages( day.key, actor, DQ_HUNT, h.track, stages, b, gained );
	if( r == NEED_DQ_ERROR ) {
		gained = 0;
		return fail( r );
	}
	// OK / NO_GAIN / LOCKED_OUT / LEVEL_CHANGED / NOT_ELIGIBLE: the kills are kept, only the score
	// follows the rules of dq_commit_stages (spec 4.6: no points outside the accepted range).
	if( r != NEED_DQ_OK )
		gained = 0;
	if( !dq_query_str( "COMMIT" ) ) {
		gained = 0;
		return fail( NEED_DQ_ERROR );
	}
	return r;
}

e_need_dq_result need_dq_core_bg_commit( const s_need_dq_day& day, const s_need_dq_actor& actor, const s_need_dq_bg_match& match,
	e_need_dq_bg_reason reason, time_t now, int32& gained, uint32& games ) {
	gained = 0;
	games = 0;
	if( !dq_active() )
		return NEED_DQ_DISABLED;
	if( match.key.empty() || match.key.find_first_not_of( "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-" ) != std::string::npos ||
		match.type.find_first_not_of( "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz" ) != std::string::npos || match.key.size() > 40 || match.type.size() > 8 )
		return NEED_DQ_ERROR;
	if( !dq_query_str( "START TRANSACTION" ) )
		return NEED_DQ_ERROR;

	s_need_dq_account_row b;
	s_need_dq_char_row a;
	if( !dq_lock_account_day( day.key, actor.account_id, b ) || !dq_read_char_sql( day.key, actor.char_id, a, true ) ) {
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	if( !a.exists ) {   // decision 2026-10-21: the daily quest must be accepted for games to count
		dq_rollback();
		return NEED_DQ_NOT_ACCEPTED;
	}
	std::shared_ptr<s_need_dq_track> snap = need_dq_db.find( a.track );
	if( snap == nullptr ) {
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	if( snap->bg_stages.empty() ) {   // Lv100-149: no battleground activity
		dq_rollback();
		return NEED_DQ_NOT_ELIGIBLE;
	}

	// D1 (2026-10-21): games are summed per account, but only games the account can score in this range:
	// valid, played at the accepted range's level, not against the account's lock (the dq_commit_stages rules).
	std::shared_ptr<s_need_dq_track> band = need_dq_band( actor.base_level );
	bool eligible = reason == NEED_DQ_BG_VALID && band != nullptr && band->track == a.track &&
		( b.locked_track == 0 || b.locked_track == a.track );
	uint32 duration = now > match.started ? static_cast<uint32>( now - match.started ) : 0;
	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"INSERT IGNORE INTO `need_dq_bg_match` (`day_key`,`account_id`,`match_key`,`char_id`,`bg_type`,`track`,`started_at`,`duration_s`,"
		"`participants`,`distinct_ips`,`valid`,`eligible`,`reason`) VALUES ('%s','%u','%s','%u','%s','%u',FROM_UNIXTIME(%" PRId64 "),'%u','%u','%u','%d','%d','%d')",
		day.key, actor.account_id, match.key.c_str(), actor.char_id, match.type.c_str(), a.track, static_cast<int64>( match.started ), duration,
		static_cast<uint32>( match.players.size() ), match.distinct_ips, reason == NEED_DQ_BG_VALID ? 1 : 0, eligible ? 1 : 0, static_cast<int32>( reason ) ) ) {
		Sql_ShowDebug( mmysql_handle );
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	if( Sql_NumRowsAffected( mmysql_handle ) == 0 ) {   // this account already had this match
		dq_rollback();
		return NEED_DQ_ALREADY;
	}
	if( reason != NEED_DQ_BG_VALID ) {   // refused game: kept for audit, scores nothing
		if( !dq_query_str( "COMMIT" ) ) {
			dq_rollback();
			return NEED_DQ_ERROR;
		}
		return NEED_DQ_NO_GAIN;
	}

	if( !need_dq_read_bg_account_games( day.key, actor.account_id, a.track, games ) ) {
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	std::vector<uint16> stages;
	for( size_t i = 0; i < snap->bg_stages.size(); i++ ) {
		if( snap->bg_stages[i].amount <= games )
			stages.push_back( static_cast<uint16>( i + 1 ) );
	}
	// an ineligible game always goes through dq_commit_stages: it returns the reason (LOCKED_OUT / LEVEL_CHANGED) and writes nothing
	e_need_dq_result r = ( stages.empty() && eligible ) ? NEED_DQ_NO_GAIN : dq_commit_stages( day.key, actor, DQ_BG, a.track, stages, b, gained );
	if( r == NEED_DQ_ERROR ) {
		gained = 0;
		dq_rollback();
		return r;
	}
	if( r != NEED_DQ_OK )
		gained = 0;   // the game is kept; LOCKED_OUT / LEVEL_CHANGED score nothing (spec 4.6)
	if( !dq_query_str( "COMMIT" ) ) {
		gained = 0;
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	return r;
}

e_need_dq_result need_dq_core_inst_commit( const s_need_dq_day& day, const s_need_dq_actor& actor, uint16 kind, int32 instance_id,
	int32& gained, uint16& char_pts ) {
	gained = 0;
	char_pts = 0;
	if( !dq_active() )
		return NEED_DQ_DISABLED;
	if( kind == NEED_DQ_INST_NONE || kind > NEED_DQ_INST_KIND_MAX )
		return NEED_DQ_ERROR;
	if( !dq_query_str( "START TRANSACTION" ) )
		return NEED_DQ_ERROR;

	s_need_dq_account_row b;
	s_need_dq_char_row a;
	if( !dq_lock_account_day( day.key, actor.account_id, b ) || !dq_read_char_sql( day.key, actor.char_id, a, true ) ) {
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	if( !a.exists ) {   // decision 2026-10-21: clears count only after today's accept
		dq_rollback();
		return NEED_DQ_NOT_ACCEPTED;
	}
	std::shared_ptr<s_need_dq_track> snap = need_dq_db.find( a.track );
	if( snap == nullptr ) {
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	bool scored = false;
	for( const auto& k : snap->inst_kinds )
		scored = scored || k.kind == kind;
	if( !scored ) {   // decision 2026-10-21: Lv175 only
		dq_rollback();
		return NEED_DQ_NOT_ELIGIBLE;
	}

	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"INSERT IGNORE INTO `need_dq_inst_clear` (`day_key`,`char_id`,`kind`,`account_id`,`track`,`instance_id`) VALUES ('%s','%u','%u','%u','%u','%d')",
		day.key, actor.char_id, kind, actor.account_id, a.track, instance_id ) ) {
		Sql_ShowDebug( mmysql_handle );
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	if( Sql_NumRowsAffected( mmysql_handle ) == 0 ) {   // this character already cleared this kind today
		dq_rollback();
		return NEED_DQ_ALREADY;
	}

	std::vector<uint16> kinds;
	if( !need_dq_read_inst_kinds( day.key, actor.char_id, kinds ) ) {
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	char_pts = need_dq_inst_points( *snap, kinds );
	std::vector<uint16> stages;
	for( const auto& k : snap->inst_kinds ) {
		if( std::find( kinds.begin(), kinds.end(), k.kind ) != kinds.end() )
			stages.push_back( k.kind );
	}
	e_need_dq_result r = dq_commit_stages( day.key, actor, DQ_INST, a.track, stages, b, gained );
	if( r == NEED_DQ_ERROR ) {
		gained = 0;
		dq_rollback();
		return r;
	}
	if( r != NEED_DQ_OK )
		gained = 0;   // the clear is kept; LOCKED_OUT / LEVEL_CHANGED score nothing (spec 4.6)
	if( !dq_query_str( "COMMIT" ) ) {
		gained = 0;
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	return r;
}

/*==========================================
 * Cash claim (v1.5 Phase 5-Lite). The IP rule is the one of the honey songpyun
 * (need_chuseok_hunt.cpp: one first account per IP and day, approved family members exempt),
 * the family list is the summer family tables under the daily quest's own event id, and the cash
 * goes into #CASHPOINTS like the other NEED events (saved when the NPC script ends).
 *------------------------------------------*/
uint32 need_dq_family_group( uint32 account_id ) {
	if( mmysql_handle == nullptr )
		return 0;
	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"SELECT `m`.`family_group_id` FROM `need_summer_attendance_family_member` `m` "
		"JOIN `need_summer_attendance_family_group` `g` "
		"ON `g`.`event_id`=`m`.`event_id` AND `g`.`family_group_id`=`m`.`family_group_id` "
		"WHERE `m`.`event_id`='%u' AND `m`.`account_id`='%u' AND `m`.`active`='1' AND `g`.`active`='1' LIMIT 1",
		NEED_DQ_FAMILY_EVENT_ID, account_id ) ) {
		Sql_ShowDebug( mmysql_handle );
		return 0;   // like the honey songpyun: a broken family list means "no family", the IP rule applies
	}
	uint32 group_id = 0;
	if( SQL_SUCCESS == Sql_NextRow( mmysql_handle ) )
		group_id = dq_col_u32( 0 );
	Sql_FreeResult( mmysql_handle );
	return group_id;
}

e_need_dq_result need_dq_core_claim( const s_need_dq_day& day, const s_need_dq_actor& actor, uint16 tier, const char* ip, uint32 family_group ) {
	if( !dq_active() )
		return NEED_DQ_DISABLED;
	if( tier != 20 && tier != 40 && tier != 60 && tier != 80 )
		return NEED_DQ_CLAIM_BAD_TIER;
	if( ip == nullptr || ip[0] == '\0' || strlen( ip ) > 15 || strspn( ip, "0123456789." ) != strlen( ip ) )
		return NEED_DQ_CLAIM_NOT_READY;
	if( !dq_query_str( "START TRANSACTION" ) )
		return NEED_DQ_ERROR;

	s_need_dq_account_row b;
	if( !dq_lock_account_day( day.key, actor.account_id, b ) ) {
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	if( b.total_pts < tier ) {
		dq_rollback();
		return NEED_DQ_CLAIM_NOT_ENOUGH;
	}

	// this tier of this account today
	if( SQL_ERROR == Sql_Query( mmysql_handle, "SELECT `status` FROM `need_dq_claim` WHERE `day_key`='%s' AND `account_id`='%u' AND `tier`='%u' FOR UPDATE",
		day.key, actor.account_id, tier ) ) {
		Sql_ShowDebug( mmysql_handle );
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	bool exists = false;
	uint32 status = 0;
	if( SQL_SUCCESS == Sql_NextRow( mmysql_handle ) ) {
		exists = true;
		status = dq_col_u32( 0 );
	}
	Sql_FreeResult( mmysql_handle );
	if( exists && status == 1 ) {
		dq_rollback();
		return NEED_DQ_ALREADY;
	}
	if( exists && status == 0 ) {
		dq_rollback();
		return NEED_DQ_CLAIM_PENDING;   // not confirmed: never paid again automatically
	}

	// IP of the day (honey songpyun rule)
	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"INSERT IGNORE INTO `need_dq_ip_daily` (`day_key`,`ip`,`first_account_id`,`first_char_id`,`family_group_id`) VALUES ('%s',INET6_ATON('%s'),'%u','%u','%u')",
		day.key, ip, actor.account_id, actor.char_id, family_group ) ) {
		Sql_ShowDebug( mmysql_handle );
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	if( Sql_NumRowsAffected( mmysql_handle ) == 0 ) {
		if( SQL_ERROR == Sql_Query( mmysql_handle, "SELECT `first_account_id` FROM `need_dq_ip_daily` WHERE `day_key`='%s' AND `ip`=INET6_ATON('%s') FOR UPDATE",
			day.key, ip ) ) {
			Sql_ShowDebug( mmysql_handle );
			dq_rollback();
			return NEED_DQ_ERROR;
		}
		uint32 first = 0;
		if( SQL_SUCCESS == Sql_NextRow( mmysql_handle ) )
			first = dq_col_u32( 0 );
		Sql_FreeResult( mmysql_handle );
		if( first != actor.account_id && family_group == 0 ) {
			dq_rollback();
			return NEED_DQ_CLAIM_IP_LIMIT;
		}
	}

	bool ok;
	if( exists )   // status 2: the earlier attempt certainly paid nothing
		ok = SQL_SUCCESS == Sql_Query( mmysql_handle,
			"UPDATE `need_dq_claim` SET `status`='0',`char_id`='%u',`ip`=INET6_ATON('%s'),`family_group_id`='%u' WHERE `day_key`='%s' AND `account_id`='%u' AND `tier`='%u' AND `status`='2'",
			actor.char_id, ip, family_group, day.key, actor.account_id, tier ) && Sql_NumRowsAffected( mmysql_handle ) == 1;
	else
		ok = SQL_SUCCESS == Sql_Query( mmysql_handle,
			"INSERT INTO `need_dq_claim` (`day_key`,`account_id`,`tier`,`char_id`,`ip`,`family_group_id`,`amount`,`status`) VALUES ('%s','%u','%u','%u',INET6_ATON('%s'),'%u','%d','0')",
			day.key, actor.account_id, tier, actor.char_id, ip, family_group, NEED_DQ_CLAIM_CASH );
	if( !ok ) {
		Sql_ShowDebug( mmysql_handle );
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	if( !dq_query_str( "COMMIT" ) ) {
		dq_rollback();
		return NEED_DQ_ERROR;
	}
	return NEED_DQ_OK;
}

bool need_dq_claim_finish( const char* day, uint32 account_id, uint16 tier, bool granted ) {
	if( SQL_ERROR == Sql_Query( mmysql_handle, "UPDATE `need_dq_claim` SET `status`='%d' WHERE `day_key`='%s' AND `account_id`='%u' AND `tier`='%u' AND `status`='0'",
		granted ? 1 : 2, day, account_id, tier ) ) {
		Sql_ShowDebug( mmysql_handle );
		return false;
	}
	return Sql_NumRowsAffected( mmysql_handle ) == 1;
}

/*==========================================
 * Session side
 *------------------------------------------*/

void need_daily_quest_on_kill( map_session_data* killer, mob_data* md, int32 type ) {
	if( killer == nullptr || md == nullptr || !dq_active() )
		return;
	s_need_dq_day day;
	if( !need_dq_logical_day( day ) )
		return;
	// Map + monster whitelist first: every other kill returns here.
	if( !dq_any_target( md->m, md->mob_id, day ) )
		return;
	if( !need_event_hunt_normal_field_target( killer, md, type, DQ_FILTER_LEVEL_DIFFERENCE ) )
		return;
	dq_credit_kill( killer, md->m, md->mob_id, md, day, false );
}

bool need_daily_quest_objective_label( const map_session_data* sd, int32 quest_id, int32 objective, char (&out)[NAME_LENGTH] ) {
	if( !need_dq_managed_quest( quest_id ) || objective != 0 )
		return false;
	static const uint16 tracks[4] = { 100, 130, 150, 175 };
	for( int32 i = 0; i < 4; i++ ) {
		std::shared_ptr<s_need_dq_track> t = need_dq_db.find( tracks[i] );
		if( t == nullptr )
			continue;
		if( t->hunt_quest_id == quest_id ) {
			safestrncpy( out, msg_txt( const_cast<map_session_data*>( sd ), MSG_DQ_HUNT_LABEL ), NAME_LENGTH );
			return true;
		}
		if( t->bg_quest_id != 0 && t->bg_quest_id == quest_id ) {
			safestrncpy( out, msg_txt( const_cast<map_session_data*>( sd ), MSG_DQ_BG_LABEL ), NAME_LENGTH );
			return true;
		}
		if( t->inst_quest_id != 0 && t->inst_quest_id == quest_id ) {
			safestrncpy( out, msg_txt( const_cast<map_session_data*>( sd ), MSG_DQ_INST_LABEL ), NAME_LENGTH );
			return true;
		}
		for( const auto& dmap : t->delivery_maps ) {
			if( dmap->quest_id == quest_id ) {
				safestrncpy( out, msg_txt( const_cast<map_session_data*>( sd ), MSG_DQ_MATERIAL_BASE + i ), NAME_LENGTH );
				return true;
			}
		}
	}
	return false;
}

void need_daily_quest_session_end( map_session_data* sd ) {
	if( sd == nullptr )
		return;
	auto it = dq_sessions.find( sd->status.char_id );
	if( it == dq_sessions.end() )
		return;
	s_dq_session& s = it->second;
	for( s_need_dq_pending* p : { &s.pending, &s.hpending } ) {
		if( p->timer != INVALID_TIMER ) {
			delete_timer( p->timer, need_dq_flush_timer );
			p->timer = INVALID_TIMER;
		}
	}
	if( s.pending.count > 0 && dq_schema_ok && !dq_flush_sql( s.char_id, s.pending ) )
		ShowError( "need_daily_quest: logout flush failed (char %u, %u pending dropped)\n", s.char_id, s.pending.count );
	if( s.hpending.count > 0 && dq_active() )
		dq_hunt_flush( s, sd, false );   // also scores a stage reached by these kills
	dq_sessions.erase( it );
}

namespace {

/// Runs a core call with the session's pending count, then refreshes the cache and the timer.
template <typename F> int32 dq_script_call( map_session_data* sd, F call ) {
	if( !dq_active() )
		return NEED_DQ_DISABLED;
	s_need_dq_day day;
	if( !need_dq_logical_day( day ) )
		return NEED_DQ_ERROR;
	s_dq_session& s = dq_session( sd, day );
	e_need_dq_result r = call( day, dq_actor( sd ), s );
	dq_session_reload( s, day.key );
	s.notice_level = 0;
	s.notice_locked = false;
	dq_sync_timer( s, sd );
	dq_quest_sync( sd, s, day );
	if( r == NEED_DQ_ERROR )
		clif_displaymessage( sd->fd, msg_txt( sd, MSG_DQ_DB_ERROR ) );
	return r;
}

}  // namespace

int32 need_daily_quest_script_accept( map_session_data* sd ) {
	return dq_script_call( sd, []( const s_need_dq_day& day, const s_need_dq_actor& actor, auto& ) {
		return need_dq_core_accept( day, actor );
	} );
}

int32 need_daily_quest_script_reaccept( map_session_data* sd ) {
	return dq_script_call( sd, []( const s_need_dq_day& day, const s_need_dq_actor& actor, auto& s ) {
		return need_dq_core_reaccept( day, actor, &s.pending, &s.hpending );
	} );
}

int32 need_daily_quest_script_submit( map_session_data* sd ) {
	int32 gained = 0;
	int32 r = dq_script_call( sd, [&gained]( const s_need_dq_day& day, const s_need_dq_actor& actor, auto& s ) {
		return need_dq_core_submit( day, actor, &s.pending, gained );
	} );
	dq_set_reg( sd, "@dq_gained", gained );
	return r;
}

/// Fills the @dq_* display variables. Returns the state:
/// 0 not accepted, 1 accepted, 2 capped, 3 locked out, 4 level changed, 5 not eligible, -8 disabled.
int32 need_daily_quest_script_status( map_session_data* sd ) {
	if( !dq_active() )
		return NEED_DQ_DISABLED;
	s_need_dq_day day;
	if( !need_dq_logical_day( day ) )
		return NEED_DQ_ERROR;
	s_dq_session& s = dq_session( sd, day );
	dq_session_reload( s, day.key );
	dq_quest_sync( sd, s, day );
	s_need_dq_account_row b;
	need_dq_read_account( day.key, sd->status.account_id, b );

	std::shared_ptr<s_need_dq_track> band = need_dq_band( static_cast<uint16>( sd->status.base_level ) );
	std::shared_ptr<s_need_dq_track> row_track = s.row.exists ? need_dq_db.find( s.row.track ) : nullptr;
	std::shared_ptr<s_need_dq_track> shown = row_track != nullptr ? row_track : band;

	time_t now = time( nullptr );
#ifdef NEED_DQ_TEST
	now += static_cast<time_t>( need_dq_test_offset );
#endif
	time_t kst_now = now + DQ_KST_OFFSET;
	struct tm* local = gmtime( &kst_now );   // KST wall clock for the 03:50-04:00 warning

	dq_set_regstr( sd, "@dq_day$", day.key );
	dq_set_reg( sd, "@dq_wday", day.wday );
	dq_set_reg( sd, "@dq_reset_soon", ( local != nullptr && local->tm_hour == 3 && local->tm_min >= 50 ) ? 1 : 0 );
	dq_set_reg( sd, "@dq_band", band != nullptr ? band->track : 0 );
	dq_set_reg( sd, "@dq_band_min", band != nullptr ? band->min_level : 0 );
	dq_set_reg( sd, "@dq_band_max", band != nullptr ? band->max_level : 0 );
	dq_set_regstr( sd, "@dq_today_map$", band != nullptr ? band->rotation[day.wday]->name.c_str() : "" );
	dq_set_reg( sd, "@dq_track", s.row.exists ? s.row.track : 0 );
	dq_set_reg( sd, "@dq_track_min", row_track != nullptr ? row_track->min_level : 0 );
	dq_set_reg( sd, "@dq_track_max", row_track != nullptr ? row_track->max_level : 0 );
	dq_set_regstr( sd, "@dq_map$", s.row.exists ? s.row.map_name.c_str() : "" );
	uint16 pending = s.row.exists ? dq_pending_for( s, day.key, s.row.track ) : 0;
	uint16 goal = shown != nullptr ? need_dq_goal( *shown ) : 0;
	uint32 collected = s.row.exists ? std::min<uint32>( static_cast<uint32>( s.row.collected ) + pending, goal > s.row.submitted ? goal - s.row.submitted : 0 ) : 0;
	dq_set_reg( sd, "@dq_collected", collected );
	dq_set_reg( sd, "@dq_submitted", s.row.exists ? s.row.submitted : 0 );
	dq_set_reg( sd, "@dq_goal", goal );
	dq_set_reg( sd, "@dq_reaccept_count", s.row.reaccept_count );
	dq_set_reg( sd, "@dq_abandoned", s.row.abandoned );
	int32 rate = 0;
	if( row_track != nullptr ) {
		for( const auto& dmap : row_track->delivery_maps ) {
			if( dmap->name == s.row.map_name )
				rate = dmap->rate;
		}
	}
	dq_set_reg( sd, "@dq_rate", rate );
	dq_set_reg( sd, "@dq_stage_count", shown != nullptr ? static_cast<int64>( shown->delivery_stages.size() ) : 0 );
	for( size_t i = 0; shown != nullptr && i < shown->delivery_stages.size(); i++ ) {
		pc_setreg( sd, reference_uid( add_str( "@dq_stage_amount" ), static_cast<uint32>( i ) ), shown->delivery_stages[i].amount );
		pc_setreg( sd, reference_uid( add_str( "@dq_stage_points" ), static_cast<uint32>( i ) ), shown->delivery_stages[i].points );
	}
	std::shared_ptr<s_need_dq_track> locked = b.locked_track != 0 ? need_dq_db.find( b.locked_track ) : nullptr;
	dq_set_reg( sd, "@dq_locked_track", b.locked_track );
	dq_set_reg( sd, "@dq_locked_min", locked != nullptr ? locked->min_level : 0 );
	dq_set_reg( sd, "@dq_locked_max", locked != nullptr ? locked->max_level : 0 );
	dq_set_reg( sd, "@dq_acc_hunt", b.hunt_pts );
	dq_set_reg( sd, "@dq_acc_delivery", b.delivery_pts );
	dq_set_reg( sd, "@dq_acc_bg", b.bg_pts );
	dq_set_reg( sd, "@dq_acc_inst", b.inst_pts );
	dq_set_reg( sd, "@dq_acc_total", b.total_pts );

	std::shared_ptr<s_need_dq_track> htrack = s.hrow.exists ? need_dq_db.find( s.hrow.track ) : nullptr;
	std::shared_ptr<s_need_dq_track> hshown = htrack != nullptr ? htrack : shown;
	uint16 hgoal = hshown != nullptr ? need_dq_hunt_goal( *hshown ) : 0;
	uint32 hcount = s.hrow.exists ? std::min<uint32>( hgoal, s.hrow.count + dq_pending_for( s, day.key, s.hrow.track, true ) ) : 0;
	dq_set_reg( sd, "@dq_hunt_track", s.hrow.exists ? s.hrow.track : 0 );
	dq_set_reg( sd, "@dq_hunt_count", hcount );
	dq_set_reg( sd, "@dq_hunt_goal", hgoal );
	dq_set_reg( sd, "@dq_hunt_stage_count", hshown != nullptr ? static_cast<int64>( hshown->hunt_stages.size() ) : 0 );
	for( size_t i = 0; hshown != nullptr && i < hshown->hunt_stages.size(); i++ ) {
		pc_setreg( sd, reference_uid( add_str( "@dq_hunt_stage_amount" ), static_cast<uint32>( i ) ), hshown->hunt_stages[i].amount );
		pc_setreg( sd, reference_uid( add_str( "@dq_hunt_stage_points" ), static_cast<uint32>( i ) ), hshown->hunt_stages[i].points );
	}

	uint16 bgoal = shown != nullptr ? need_dq_bg_goal( *shown ) : 0;
	dq_set_reg( sd, "@dq_bg_games", s.row.exists ? std::min<uint32>( bgoal, s.bg_games ) : 0 );
	dq_set_reg( sd, "@dq_bg_goal", bgoal );
	uint32 bacc = 0;
	if( s.row.exists && bgoal > 0 )
		need_dq_read_bg_account_games( day.key, sd->status.account_id, s.row.track, bacc );
	dq_set_reg( sd, "@dq_bg_acc_games", std::min<uint32>( bgoal, bacc ) );
	dq_set_reg( sd, "@dq_bg_stage_count", shown != nullptr ? static_cast<int64>( shown->bg_stages.size() ) : 0 );
	for( size_t i = 0; shown != nullptr && i < shown->bg_stages.size(); i++ ) {
		pc_setreg( sd, reference_uid( add_str( "@dq_bg_stage_amount" ), static_cast<uint32>( i ) ), shown->bg_stages[i].amount );
		pc_setreg( sd, reference_uid( add_str( "@dq_bg_stage_points" ), static_cast<uint32>( i ) ), shown->bg_stages[i].points );
	}

	for( int32 i = 0; i < 4; i++ )
		pc_setreg( sd, reference_uid( add_str( "@dq_claim_st" ), static_cast<uint32>( i ) ), 0 );
	if( SQL_SUCCESS == Sql_Query( mmysql_handle, "SELECT `tier`,`status` FROM `need_dq_claim` WHERE `day_key`='%s' AND `account_id`='%u'",
		day.key, sd->status.account_id ) ) {
		while( SQL_SUCCESS == Sql_NextRow( mmysql_handle ) ) {
			uint32 tier = dq_col_u32( 0 ), st = dq_col_u32( 1 );
			if( tier >= 20 && tier <= 80 && tier % 20 == 0 )
				pc_setreg( sd, reference_uid( add_str( "@dq_claim_st" ), tier / 20 - 1 ), st == 1 ? 1 : st == 0 ? 2 : 3 );
		}
		Sql_FreeResult( mmysql_handle );
	} else
		Sql_ShowDebug( mmysql_handle );
	dq_set_reg( sd, "@dq_inst_cap", shown != nullptr ? shown->inst_cap : 0 );
	dq_set_reg( sd, "@dq_inst_pts", s.row.exists && shown != nullptr ? need_dq_inst_points( *shown, s.inst_kinds ) : 0 );
	dq_set_reg( sd, "@dq_inst_kind_count", shown != nullptr ? static_cast<int64>( shown->inst_kinds.size() ) : 0 );
	for( size_t i = 0; shown != nullptr && i < shown->inst_kinds.size(); i++ ) {
		const s_need_dq_inst_kind& k = shown->inst_kinds[i];
		bool done = s.row.exists && std::find( s.inst_kinds.begin(), s.inst_kinds.end(), k.kind ) != s.inst_kinds.end();
		pc_setreg( sd, reference_uid( add_str( "@dq_inst_kind_id" ), static_cast<uint32>( i ) ), k.kind );
		pc_setreg( sd, reference_uid( add_str( "@dq_inst_kind_points" ), static_cast<uint32>( i ) ), k.points );
		pc_setreg( sd, reference_uid( add_str( "@dq_inst_kind_done" ), static_cast<uint32>( i ) ), done ? 1 : 0 );
	}

	if( band == nullptr )
		return 5;
	if( ( s.row.exists && s.row.track != band->track ) || ( s.hrow.exists && s.hrow.track != band->track ) )
		return 4;
	if( !s.row.exists || !s.hrow.exists )
		return 0;   // the accept also adds the missing activity (a delivery accepted before phase 2)
	if( b.locked_track != 0 && b.locked_track != s.row.track )
		return 3;
	if( static_cast<uint32>( s.row.submitted ) + collected >= goal && hcount >= hgoal )
		return 2;
	return 1;
}

namespace {

/// Battles running now, by type (one battle of each type at a time: bat_a01 / bat_b01 / bat_c01).
std::unordered_map<std::string, s_need_dq_bg_match> dq_bg_matches;

/// v1.5: entering a battle registers the daily quest (= the NPC accept) for Lv150+ participants
/// who have not accepted today; the scoring rules are unchanged (accept range, account lock, 80 cap).
void dq_bg_autoaccept( map_session_data* sd ) {
	if( !dq_active() )
		return;
	std::shared_ptr<s_need_dq_track> band = need_dq_band( static_cast<uint16>( sd->status.base_level ) );
	if( band == nullptr || band->bg_stages.empty() )
		return;
	int32 r = need_daily_quest_script_accept( sd );
	if( r == NEED_DQ_OK || r == NEED_DQ_OK_UNLOCKED )
		clif_displaymessage( sd->fd, msg_txt( sd, MSG_DQ_BG_AUTO_ACCEPT ) );
}

bool dq_bg_type_ok( const char* type ) {
	return type != nullptr && ( strcmp( type, "Tierra" ) == 0 || strcmp( type, "Flavius" ) == 0 || strcmp( type, "KVM" ) == 0 );
}

}  // namespace

int32 need_daily_quest_script_bg_start( const char* type, int32 bg_id1, int32 bg_id2, const char* map_name ) {
	if( !dq_bg_type_ok( type ) )
		return 0;   // recorded even while the quest is off, so a later end never meets an older battle
	s_need_dq_bg_match match;
	match.type = type;
	match.started = time( nullptr );
	match.key = match.type + "-" + std::to_string( static_cast<int64>( match.started ) );
	match.m = map_name != nullptr ? map_mapname2mapid( map_name ) : -1;
	std::unordered_set<uint32> ips;
	for( int32 id : { bg_id1, bg_id2 } ) {
		auto it = bg_team_db.find( id );
		if( it == bg_team_db.end() || it->second == nullptr )
			continue;
		for( const auto& member : it->second->members ) {
			map_session_data* msd = member.sd;
			if( msd == nullptr )
				continue;
			s_need_dq_bg_participant p;
			p.char_id = msd->status.char_id;
			p.account_id = msd->status.account_id;
			dq_bg_autoaccept( msd );
			p.ip = ( msd->fd > 0 && session_isActive( msd->fd ) ) ? session[msd->fd]->client_addr : 0;
			ips.insert( p.ip );
			match.players.push_back( p );
		}
	}
	match.distinct_ips = static_cast<uint16>( ips.size() );
	if( battle_config.need_daily_quest_debug )
		ShowDebug( "need_daily_quest: battle start %s map %d players %zu distinct ips %u\n",
			match.key.c_str(), match.m, match.players.size(), match.distinct_ips );
	size_t n = match.players.size();
	dq_bg_matches[match.type] = match;
	return static_cast<int32>( n );
}

namespace {

int32 dq_bg_collect_pc( block_list* bl, va_list ap ) {
	std::vector<map_session_data*>* out = va_arg( ap, std::vector<map_session_data*>* );
	out->push_back( reinterpret_cast<map_session_data*>( bl ) );
	return 1;
}

/// One player at the official end of a battle (D2): judge, record once per account, tell the player.
/// No reward dialog runs before this, so sd->idletime still holds the last action of the battle.
int32 dq_bg_judge( map_session_data* sd, const s_need_dq_bg_match& match, const s_need_dq_day& day, time_t now ) {
	e_need_dq_bg_reason reason = need_dq_bg_validate( match, sd->status.char_id, sd->m, now, sd->idletime );
	char msg[CHAT_SIZE_MAX];

	int32 gained = 0;
	uint32 games = 0;
	e_need_dq_result r = need_dq_core_bg_commit( day, dq_actor( sd ), match, reason, now, gained, games );
	if( battle_config.need_daily_quest_debug )
		ShowDebug( "need_daily_quest: battle end %s char %u reason %d -> result %d games %u gained %d\n",
			match.key.c_str(), sd->status.char_id, reason, r, games, gained );

	s_dq_session& s = dq_session( sd, day );
	dq_session_reload( s, day.key );
	switch( r ) {
	case NEED_DQ_NOT_ACCEPTED:
		clif_displaymessage( sd->fd, msg_txt( sd, MSG_DQ_BG_NOT_ACCEPTED ) );
		break;
	case NEED_DQ_NOT_ELIGIBLE:
		clif_displaymessage( sd->fd, msg_txt( sd, MSG_DQ_BG_NO_TRACK ) );
		break;
	case NEED_DQ_ALREADY:
		clif_displaymessage( sd->fd, msg_txt( sd, MSG_DQ_BG_SAME_MATCH ) );
		break;
	case NEED_DQ_OK: {
		s_need_dq_account_row b;
		need_dq_read_account( day.key, sd->status.account_id, b );
		safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_BG_STAGE ), games, gained, b.total_pts );
		clif_displaymessage( sd->fd, msg );
		break;
	}
	case NEED_DQ_NO_GAIN:
		if( reason != NEED_DQ_BG_VALID ) {
			safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_BG_INVALID ), msg_txt( sd, MSG_DQ_BG_REASON_BASE + reason ) );
			clif_displaymessage( sd->fd, msg );
		} else if( games >= 1 ) {
			std::shared_ptr<s_need_dq_track> t = s.row.exists ? need_dq_db.find( s.row.track ) : nullptr;
			if( t != nullptr && games <= need_dq_bg_goal( *t ) ) {
				safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_BG_STAGE_KNOWN ), games );
				clif_displaymessage( sd->fd, msg );
			}
		}
		break;
	case NEED_DQ_LOCKED_OUT: {
		char label[16];
		dq_track_label( s.locked_track, label );
		safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_LOCKED_OUT ), label );
		clif_displaymessage( sd->fd, msg );
		break;
	}
	case NEED_DQ_LEVEL_CHANGED: {
		std::shared_ptr<s_need_dq_track> band = need_dq_band( static_cast<uint16>( sd->status.base_level ) );
		char now_label[16], acc_label[16];
		dq_track_label( band != nullptr ? band->track : 0, now_label );
		dq_track_label( s.row.track, acc_label );
		safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_LEVEL_CHANGED ), now_label, acc_label );
		clif_displaymessage( sd->fd, msg );
		break;
	}
	case NEED_DQ_ERROR:
		clif_displaymessage( sd->fd, msg_txt( sd, MSG_DQ_DB_ERROR ) );
		break;
	default:
		break;
	}
	dq_quest_sync( sd, s, day );
	return r;
}

}  // namespace

int32 need_daily_quest_script_bg_finish( const char* type ) {
	if( !dq_bg_type_ok( type ) )
		return -1;
	auto it = dq_bg_matches.find( type );
	if( it == dq_bg_matches.end() )
		return -1;   // nothing recorded (restart) or already finished: a second end call does nothing
	s_need_dq_bg_match match = it->second;
	dq_bg_matches.erase( it );
	if( !dq_active() || match.m < 0 )
		return 0;
	s_need_dq_day day;
	if( !need_dq_logical_day( day ) )   // D5: the logical day of the official end
		return 0;
	time_t now = time( nullptr );
	std::vector<map_session_data*> players;
	map_foreachinmap( dq_bg_collect_pc, match.m, BL_PC, &players );
	for( map_session_data* sd : players )
		dq_bg_judge( sd, match, day, now );
	if( battle_config.need_daily_quest_debug )
		ShowDebug( "need_daily_quest: battle finish %s map %d judged %zu after %" PRId64 " s\n",
			match.key.c_str(), match.m, players.size(), static_cast<int64>( now - match.started ) );
	return static_cast<int32>( players.size() );
}

int32 need_daily_quest_script_claim( map_session_data* sd, int32 tier ) {
	if( sd == nullptr || !dq_active() )
		return NEED_DQ_DISABLED;
	if( tier != 20 && tier != 40 && tier != 60 && tier != 80 )
		return NEED_DQ_CLAIM_BAD_TIER;
	if( !sd->vars_ok )   // #CASHPOINTS cannot be saved before the account variables are loaded
		return NEED_DQ_CLAIM_NOT_READY;
	if( sd->cashPoints > MAX_CASHPOINT - NEED_DQ_CLAIM_CASH )
		return NEED_DQ_CLAIM_CASH_FULL;
	char ip[16];
	if( !need_event_hunt_client_ip( sd, ip ) )
		return NEED_DQ_CLAIM_NOT_READY;
	s_need_dq_day day;
	if( !need_dq_logical_day( day ) )
		return NEED_DQ_ERROR;

	uint32 family = need_dq_family_group( sd->status.account_id );
	e_need_dq_result r = need_dq_core_claim( day, dq_actor( sd ), static_cast<uint16>( tier ), ip, family );
	if( r != NEED_DQ_OK ) {
		if( r == NEED_DQ_CLAIM_PENDING )
			ShowWarning( "need_daily_quest: claim %s account %u tier %d is reserved but not confirmed (operator check)\n", day.key, sd->status.account_id, tier );
		return r;
	}

	// same variable and path as the other NEED cash events (#CASHPOINTS, cashlog); saved when the NPC script ends
	int32 before = sd->cashPoints;
	bool granted = pc_setparam( sd, SP_CASHPOINTS, static_cast<int64>( before ) + NEED_DQ_CLAIM_CASH ) && sd->cashPoints == before + NEED_DQ_CLAIM_CASH;
	if( !need_dq_claim_finish( day.key, sd->status.account_id, static_cast<uint16>( tier ), granted ) )
		ShowError( "need_daily_quest: claim %s account %u char %u tier %d: cash %s but the ledger was not updated (status 0, operator check)\n",
			day.key, sd->status.account_id, sd->status.char_id, tier, granted ? "granted" : "NOT granted" );
	if( battle_config.need_daily_quest_debug )
		ShowDebug( "need_daily_quest: claim %s account %u char %u tier %d family %u cash %d -> %d\n",
			day.key, sd->status.account_id, sd->status.char_id, tier, family, before, sd->cashPoints );
	return granted ? NEED_DQ_OK : NEED_DQ_ERROR;
}

int32 need_daily_quest_script_inst_clear( map_session_data* sd, const char* kind_name, int32 instance_id ) {
	if( sd == nullptr || !dq_active() )
		return NEED_DQ_DISABLED;
	uint16 kind = need_dq_inst_kind_id( kind_name );
	if( kind == NEED_DQ_INST_NONE ) {
		ShowWarning( "need_daily_quest: needdq_inst_clear: unknown instance kind '%s'.\n", kind_name );
		return NEED_DQ_ERROR;
	}
	s_need_dq_day day;
	if( !need_dq_logical_day( day ) )
		return NEED_DQ_ERROR;

	int32 gained = 0;
	uint16 char_pts = 0;
	e_need_dq_result r = need_dq_core_inst_commit( day, dq_actor( sd ), kind, instance_id, gained, char_pts );
	if( battle_config.need_daily_quest_debug )
		ShowDebug( "need_daily_quest: instance clear %s (instance %d) char %u -> result %d gained %d char points %u\n",
			kind_name, instance_id, sd->status.char_id, r, gained, char_pts );

	s_dq_session& s = dq_session( sd, day );
	dq_session_reload( s, day.key );
	const char* name = msg_txt( sd, MSG_DQ_INST_NAME_BASE + kind );
	char msg[CHAT_SIZE_MAX];
	switch( r ) {
	case NEED_DQ_NOT_ACCEPTED: {   // told only to characters whose range scores instances
		std::shared_ptr<s_need_dq_track> band = need_dq_band( static_cast<uint16>( sd->status.base_level ) );
		if( band != nullptr && !band->inst_kinds.empty() ) {
			safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_INST_NOT_ACCEPTED ), name );
			clif_displaymessage( sd->fd, msg );
		}
		break;
	}
	case NEED_DQ_NOT_ELIGIBLE:
		clif_displaymessage( sd->fd, msg_txt( sd, MSG_DQ_INST_NO_TRACK ) );
		break;
	case NEED_DQ_OK: {
		s_need_dq_account_row b;
		need_dq_read_account( day.key, sd->status.account_id, b );
		safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_INST_STAGE ), name, gained, b.total_pts );
		clif_displaymessage( sd->fd, msg );
		break;
	}
	case NEED_DQ_NO_GAIN:
	case NEED_DQ_ALREADY:
		safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_INST_STAGE_KNOWN ), name );
		clif_displaymessage( sd->fd, msg );
		break;
	case NEED_DQ_LOCKED_OUT: {
		char label[16];
		dq_track_label( s.locked_track, label );
		safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_LOCKED_OUT ), label );
		clif_displaymessage( sd->fd, msg );
		break;
	}
	case NEED_DQ_LEVEL_CHANGED: {
		std::shared_ptr<s_need_dq_track> band = need_dq_band( static_cast<uint16>( sd->status.base_level ) );
		char now_label[16], acc_label[16];
		dq_track_label( band != nullptr ? band->track : 0, now_label );
		dq_track_label( s.row.track, acc_label );
		safesnprintf( msg, sizeof( msg ), msg_txt( sd, MSG_DQ_LEVEL_CHANGED ), now_label, acc_label );
		clif_displaymessage( sd->fd, msg );
		break;
	}
	case NEED_DQ_ERROR:
		clif_displaymessage( sd->fd, msg_txt( sd, MSG_DQ_DB_ERROR ) );
		break;
	default:
		break;
	}
	dq_quest_sync( sd, s, day );
	return r;
}

/*==========================================
 * Init / final
 *------------------------------------------*/

namespace {

bool dq_schema_check() {
	if( mmysql_handle == nullptr )
		return false;
	static const char* checks[] = {
		"SELECT `day_key`,`char_id`,`activity`,`account_id`,`track`,`map_name`,`accepted_at`,`collected`,`submitted`,`count`,`reaccept_count`,`abandoned_collected` FROM `need_dq_char_progress` LIMIT 0",
		"SELECT `day_key`,`account_id`,`locked_track`,`locked_char_id`,`locked_at`,`hunt_pts`,`delivery_pts`,`bg_pts`,`inst_pts`,`total_pts` FROM `need_dq_account_day` LIMIT 0",
		"SELECT `day_key`,`account_id`,`activity`,`stage_key`,`char_id`,`track`,`base_level` FROM `need_dq_score_stage` LIMIT 0",
		"SELECT `day_key`,`account_id`,`match_key`,`char_id`,`bg_type`,`track`,`started_at`,`duration_s`,`participants`,`distinct_ips`,`valid`,`eligible`,`reason` FROM `need_dq_bg_match` LIMIT 0",
		"SELECT `day_key`,`char_id`,`kind`,`account_id`,`track`,`instance_id` FROM `need_dq_inst_clear` LIMIT 0",
		"SELECT `day_key`,`account_id`,`tier`,`char_id`,`ip`,`family_group_id`,`amount`,`status` FROM `need_dq_claim` LIMIT 0",
		"SELECT `day_key`,`ip`,`first_account_id`,`first_char_id`,`family_group_id` FROM `need_dq_ip_daily` LIMIT 0",
	};
	for( const char* query : checks ) {
		if( SQL_ERROR == Sql_QueryStr( mmysql_handle, query ) ) {
			Sql_ShowDebug( mmysql_handle );
			return false;
		}
		Sql_FreeResult( mmysql_handle );
	}
	if( SQL_ERROR == Sql_QueryStr( mmysql_handle,
		"SELECT COUNT(*) FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA`=DATABASE() "
		"AND `TABLE_NAME` IN ('need_dq_char_progress','need_dq_account_day','need_dq_score_stage','need_dq_bg_match','need_dq_inst_clear','need_dq_claim','need_dq_ip_daily') AND `ENGINE`='InnoDB'" ) ||
		SQL_SUCCESS != Sql_NextRow( mmysql_handle ) ) {
		Sql_ShowDebug( mmysql_handle );
		Sql_FreeResult( mmysql_handle );
		return false;
	}
	bool ok = dq_col_u32( 0 ) == 7;
	Sql_FreeResult( mmysql_handle );
	return ok;
}

}  // namespace

void need_daily_quest_init() {
	add_timer_func_list( need_dq_flush_timer, "need_dq_flush_timer" );
	dq_sessions.clear();
	dq_schema_ok = false;
	need_dq_db.clear();
	need_dq_db.valid = false;
	need_dq_db.parse_errors = 0;
	if( battle_config.need_daily_quest_enable == 0 )
		return;

	need_dq_db.load();
	dq_schema_ok = dq_schema_check();
	if( !dq_schema_ok )
		ShowError( "need_daily_quest: need_dq_* tables (4) are missing or not InnoDB (sql-files/need_daily_quest.sql), the daily quest is disabled.\n" );
#ifdef NEED_DQ_TEST
	ShowWarning( "need_daily_quest: TEST BUILD (NEED_DQ_TEST) - test commands and overrides are compiled in.\n" );
#endif
	if( dq_active() )
		ShowNotice( "need_daily_quest: enabled, %zu tracks.\n", need_dq_db.size() );
}

void need_daily_quest_final() {
	for( auto& entry : dq_sessions ) {
		if( entry.second.pending.timer != INVALID_TIMER )
			delete_timer( entry.second.pending.timer, need_dq_flush_timer );
	}
	dq_sessions.clear();
	dq_bg_matches.clear();
	need_dq_db.clear();
	need_dq_db.valid = false;
	dq_schema_ok = false;
}

#ifdef NEED_DQ_TEST
#include "need_daily_quest_test.inc"
#endif
