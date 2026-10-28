// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED daily quest - TEST BUILD ONLY (NEED_DQ_TEST, decision U11).
// Included at the end of need_daily_quest.cpp; never compiled into the live executable.
//
// need_dq_selftest() runs the ledger scenarios of the integrity test plan directly against
// the database with fixed test ids (accounts 3999xxx) and days in 2099, then writes one
// line per check. Kill credit (party, idle, distance) needs real sessions and is tested in game.

#include <cstdarg>

namespace {

struct s_dq_selftest {
	FILE* out = nullptr;
	int32 pass = 0;
	int32 fail = 0;

	void check( const char* id, bool ok, const char* fmt, ... ) {
		char detail[512];
		va_list ap;
		va_start( ap, fmt );
		vsnprintf( detail, sizeof( detail ), fmt, ap );
		va_end( ap );
		if( ok )
			pass++;
		else
			fail++;
		if( out != nullptr )
			fprintf( out, "%-8s %s  %s\n", ok ? "PASS" : "FAIL", id, detail );
		if( ok )
			ShowInfo( "[dq selftest] PASS %s  %s\n", id, detail );
		else
			ShowError( "[dq selftest] FAIL %s  %s\n", id, detail );
	}
};

s_need_dq_day dq_test_day( int32 year, int32 month, int32 mday ) {
	struct tm t = {};
	t.tm_year = year - 1900;
	t.tm_mon = month - 1;
	t.tm_mday = mday;
	t.tm_hour = 12;
	t.tm_isdst = -1;
	time_t v = mktime( &t );
	struct tm* local = localtime( &v );
	s_need_dq_day day;
	strftime( day.key, sizeof( day.key ), "%Y-%m-%d", local );
	day.wday = local->tm_wday;
	return day;
}

/// First day of January 2099 (+ week offset) with the given weekday.
s_need_dq_day dq_test_weekday( int32 wday, int32 week = 0 ) {
	for( int32 d = 1; d <= 7; d++ ) {
		s_need_dq_day day = dq_test_day( 2099, 1, d + week * 7 );
		if( day.wday == wday )
			return day;
	}
	return dq_test_day( 2099, 1, 1 );
}

s_need_dq_actor dq_test_actor( uint32 account_id, uint32 char_id, uint16 level ) {
	s_need_dq_actor a;
	a.account_id = account_id;
	a.char_id = char_id;
	a.base_level = level;
	return a;
}

/// Virtual collection without a session: the same flush SQL the kill path uses.
bool dq_test_collect( const s_need_dq_day& day, const s_need_dq_actor& a, uint16 amount ) {
	s_need_dq_char_row row;
	if( !need_dq_read_char( day.key, a.char_id, row ) )
		return false;
	s_need_dq_pending p;
	safestrncpy( p.day, day.key, sizeof( p.day ) );
	p.track = row.exists ? row.track : 0;
	p.count = amount;
	return need_dq_flush( a.char_id, p );
}

/// Hunt kills without a session: the same commit transaction the kill path uses.
e_need_dq_result dq_test_hunt( const s_need_dq_day& day, const s_need_dq_actor& a, uint16 kills, int32& gained ) {
	s_need_dq_char_row h;
	need_dq_read_hunt( day.key, a.char_id, h );
	s_need_dq_pending p;
	safestrncpy( p.day, day.key, sizeof( p.day ) );
	p.track = h.exists ? h.track : 0;
	p.count = kills;
	p.hunt = true;
	return need_dq_core_hunt_commit( day, a, &p, gained );
}

uint32 dq_test_count( const char* query ) {
	if( SQL_ERROR == Sql_QueryStr( mmysql_handle, query ) ) {
		Sql_ShowDebug( mmysql_handle );
		return 0xFFFFFFFF;
	}
	uint32 v = 0xFFFFFFFF;
	if( SQL_SUCCESS == Sql_NextRow( mmysql_handle ) )
		v = dq_col_u32( 0 );
	Sql_FreeResult( mmysql_handle );
	return v;
}

uint32 dq_test_stage_rows( const s_need_dq_day& day, uint32 account_id ) {
	char q[256];
	safesnprintf( q, sizeof( q ), "SELECT COUNT(*) FROM `need_dq_score_stage` WHERE `day_key`='%s' AND `account_id`='%u'", day.key, account_id );
	return dq_test_count( q );
}

/// Digest of every 2099 row, to prove that a refused call wrote nothing.
std::string dq_test_digest() {
	std::string digest;
	static const char* queries[] = {
		"SELECT CONCAT_WS(',',`day_key`,`char_id`,`activity`,`track`,`map_name`,`collected`,`submitted`,`count`,`reaccept_count`,`abandoned_collected`) FROM `need_dq_char_progress` WHERE `day_key`>='2099-01-01' ORDER BY `day_key`,`char_id`,`activity`",
		"SELECT CONCAT_WS(',',`day_key`,`account_id`,IFNULL(`locked_track`,0),IFNULL(`locked_char_id`,0),`hunt_pts`,`delivery_pts`,`bg_pts`,`inst_pts`,`total_pts`) FROM `need_dq_account_day` WHERE `day_key`>='2099-01-01' ORDER BY `day_key`,`account_id`",
		"SELECT CONCAT_WS(',',`day_key`,`account_id`,`activity`,`stage_key`,`char_id`,`track`,`base_level`) FROM `need_dq_score_stage` WHERE `day_key`>='2099-01-01' ORDER BY `day_key`,`account_id`,`stage_key`",
		"SELECT CONCAT_WS(',',`day_key`,`account_id`,`match_key`,`char_id`,`valid`,`reason`) FROM `need_dq_bg_match` WHERE `day_key`>='2099-01-01' ORDER BY `day_key`,`account_id`,`match_key`",
		"SELECT CONCAT_WS(',',`day_key`,`char_id`,`kind`,`account_id`,`track`,`instance_id`) FROM `need_dq_inst_clear` WHERE `day_key`>='2099-01-01' ORDER BY `day_key`,`char_id`,`kind`",
		"SELECT CONCAT_WS(',',`day_key`,`account_id`,`tier`,`char_id`,INET6_NTOA(`ip`),`family_group_id`,`amount`,`status`) FROM `need_dq_claim` WHERE `day_key`>='2099-01-01' ORDER BY `day_key`,`account_id`,`tier`",
		"SELECT CONCAT_WS(',',`day_key`,INET6_NTOA(`ip`),`first_account_id`,`family_group_id`) FROM `need_dq_ip_daily` WHERE `day_key`>='2099-01-01' ORDER BY `day_key`,`ip`",
	};
	for( const char* q : queries ) {
		if( SQL_ERROR == Sql_QueryStr( mmysql_handle, q ) ) {
			Sql_ShowDebug( mmysql_handle );
			return "ERROR";
		}
		while( SQL_SUCCESS == Sql_NextRow( mmysql_handle ) )
			digest += dq_col_str( 0 ) + ";";
		Sql_FreeResult( mmysql_handle );
		digest += "|";
	}
	return digest;
}

const char* dq_test_rname( int32 r ) {
	switch( r ) {
		case NEED_DQ_OK: return "OK";
		case NEED_DQ_OK_UNLOCKED: return "OK_UNLOCKED";
		case NEED_DQ_NO_GAIN: return "NO_GAIN";
		case NEED_DQ_NOT_ACCEPTED: return "NOT_ACCEPTED";
		case NEED_DQ_LOCKED_OUT: return "LOCKED_OUT";
		case NEED_DQ_LEVEL_CHANGED: return "LEVEL_CHANGED";
		case NEED_DQ_NOT_ELIGIBLE: return "NOT_ELIGIBLE";
		case NEED_DQ_ALREADY: return "ALREADY";
		case NEED_DQ_NOT_NEEDED: return "NOT_NEEDED";
		case NEED_DQ_ERROR: return "ERROR";
		case NEED_DQ_DISABLED: return "DISABLED";
		case NEED_DQ_CLAIM_NOT_ENOUGH: return "NOT_ENOUGH";
		case NEED_DQ_CLAIM_IP_LIMIT: return "IP_LIMIT";
		case NEED_DQ_CLAIM_PENDING: return "PENDING";
		case NEED_DQ_CLAIM_CASH_FULL: return "CASH_FULL";
		case NEED_DQ_CLAIM_NOT_READY: return "NOT_READY";
		case NEED_DQ_CLAIM_BAD_TIER: return "BAD_TIER";
		default: return "?";
	}
}

}  // namespace

void need_dq_test_credit( map_session_data* killer, int32 mob_id, int32 kills ) {
	nullpo_retv( killer );
	s_need_dq_day day;
	if( !dq_active() || !need_dq_logical_day( day ) ) {
		clif_displaymessage( killer->fd, "dqtest: daily quest inactive" );
		return;
	}
	if( !dq_any_target( killer->m, mob_id, day ) ) {
		clif_displaymessage( killer->fd, "dqtest: this map + monster is neither today's delivery nor a hunting target" );
		return;
	}
	for( int32 i = 0; i < kills; i++ )
		dq_credit_kill( killer, killer->m, mob_id, killer, day, true );
}

void need_dq_test_flush( map_session_data* sd ) {
	nullpo_retv( sd );
	auto it = dq_sessions.find( sd->status.char_id );
	if( it != dq_sessions.end() )
		dq_session_flush( it->second, sd );
}

void need_dq_test_dump( map_session_data* sd ) {
	nullpo_retv( sd );
	char msg[CHAT_SIZE_MAX];
	s_need_dq_day day;
	if( !need_dq_logical_day( day ) )
		return;
	std::shared_ptr<s_need_dq_track> band = need_dq_band( static_cast<uint16>( sd->status.base_level ) );
	safesnprintf( msg, sizeof( msg ), "dqtest: day %s wday %d band %u rate_override %d offset %" PRId64 " fail_point %d allow_gm %d bgip %d active %d",
		day.key, day.wday, band != nullptr ? band->track : 0, need_dq_test_rate, need_dq_test_offset, need_dq_test_fail_point, need_dq_test_allow_gm,
		need_dq_test_bg_ip_percent, dq_active() ? 1 : 0 );
	clif_displaymessage( sd->fd, msg );
	auto it = dq_sessions.find( sd->status.char_id );
	if( it != dq_sessions.end() ) {
		const s_dq_session& s = it->second;
		safesnprintf( msg, sizeof( msg ), "dqtest: cache day %s row %d track %u map %s collected %u submitted %u locked %u | pending day %s track %u count %u timer %d",
			s.cache_day, s.row.exists ? 1 : 0, s.row.track, s.row.map_name.c_str(), s.row.collected, s.row.submitted, s.locked_track,
			s.pending.day, s.pending.track, s.pending.count, s.pending.timer );
		clif_displaymessage( sd->fd, msg );
	}
	s_need_dq_char_row a;
	s_need_dq_account_row b;
	need_dq_read_char( day.key, sd->status.char_id, a );
	need_dq_read_account( day.key, sd->status.account_id, b );
	safesnprintf( msg, sizeof( msg ), "dqtest: db A exists %d track %u map %s collected %u submitted %u reaccept %u abandoned %u",
		a.exists ? 1 : 0, a.track, a.map_name.c_str(), a.collected, a.submitted, a.reaccept_count, a.abandoned );
	clif_displaymessage( sd->fd, msg );
	safesnprintf( msg, sizeof( msg ), "dqtest: db B exists %d locked %u by %u hunt %u delivery %u total %u",
		b.exists ? 1 : 0, b.locked_track, b.locked_char_id, b.hunt_pts, b.delivery_pts, b.total_pts );
	clif_displaymessage( sd->fd, msg );
	s_need_dq_char_row h;
	need_dq_read_hunt( day.key, sd->status.char_id, h );
	safesnprintf( msg, sizeof( msg ), "dqtest: db H exists %d track %u count %u reaccept %u abandoned %u | pending %u",
		h.exists ? 1 : 0, h.track, h.count, h.reaccept_count, h.abandoned, it != dq_sessions.end() ? it->second.hpending.count : 0 );
	clif_displaymessage( sd->fd, msg );
}

bool need_dq_selftest( const char* out_path ) {
	s_dq_selftest t;
	t.out = out_path != nullptr ? fopen( out_path, "w" ) : nullptr;
	if( out_path != nullptr && t.out == nullptr ) {
		ShowError( "need_dq_selftest: cannot write %s\n", out_path );
		return false;
	}
	need_dq_test_fail_point = 0;
	int32 r = 0;
	int32 gained = 0;
	s_need_dq_char_row a;
	s_need_dq_account_row b;

	t.check( "SETUP", dq_active(), "feature active (enable, schema, configuration) = %d", dq_active() ? 1 : 0 );
	if( !dq_active() ) {
		if( t.out != nullptr )
			fclose( t.out );
		return false;
	}
	dq_query_str( "DELETE FROM `need_dq_char_progress` WHERE `day_key`>='2099-01-01'" );
	dq_query_str( "DELETE FROM `need_dq_account_day` WHERE `day_key`>='2099-01-01'" );
	dq_query_str( "DELETE FROM `need_dq_score_stage` WHERE `day_key`>='2099-01-01'" );
	dq_query_str( "DELETE FROM `need_dq_bg_match` WHERE `day_key`>='2099-01-01'" );
	dq_query_str( "DELETE FROM `need_dq_inst_clear` WHERE `day_key`>='2099-01-01'" );
	dq_query_str( "DELETE FROM `need_dq_claim` WHERE `day_key`>='2099-01-01'" );
	dq_query_str( "DELETE FROM `need_dq_ip_daily` WHERE `day_key`>='2099-01-01'" );

	// ---- T7: rotation table (oracle = whitelist document 3) and configuration checks
	static const char* expected[4][7] = {
		{ "juperos_02", "juperos_02", "ice_dun03", "ra_fild05", "juperos_02", "ice_dun03", "ra_fild05" },
		{ "dic_dun02", "dic_dun02", "dic_dun01", "bif_fild01", "dic_dun02", "dic_dun01", "bif_fild01" },
		{ "prt_prison", "prt_prison", "gl_cas02_", "ver_eju", "prt_prison", "gl_cas02_", "ver_eju" },
		{ "ver_eju", "ver_eju", "verus02", "verus03", "ver_eju", "verus02", "verus03" },
	};
	static const uint16 tracks[4] = { 100, 130, 150, 175 };
	static const uint16 levels[4] = { 110, 140, 160, 175 };
	for( int32 ti = 0; ti < 4; ti++ ) {
		for( int32 wd = 0; wd < 7; wd++ ) {
			s_need_dq_day day = dq_test_weekday( wd, 3 );
			s_need_dq_actor x = dq_test_actor( 3999900 + ti * 10 + wd, 3999900 + ti * 10 + wd, levels[ti] );
			r = need_dq_core_accept( day, x );
			need_dq_read_char( day.key, x.char_id, a );
			char id[16];
			safesnprintf( id, sizeof( id ), "T7-5.%u.%d", tracks[ti], wd );
			t.check( id, r == NEED_DQ_OK_UNLOCKED && a.exists && a.track == tracks[ti] && a.map_name == expected[ti][wd],
				"%s wday %d track %u -> %s (expected %s) r=%s", day.key, day.wday, a.track, a.map_name.c_str(), expected[ti][wd], dq_test_rname( r ) );
		}
	}

	// ---- T1: each track, stage by stage
	for( int32 ti = 0; ti < 4; ti++ ) {
		std::shared_ptr<s_need_dq_track> tr = need_dq_db.find( tracks[ti] );
		const s_need_dq_day day = dq_test_weekday( 1 );
		s_need_dq_actor x = dq_test_actor( 3999001 + ti, 3999101 + ti, levels[ti] );
		uint16 s1 = tr->delivery_stages[0].amount;
		uint16 goal = need_dq_goal( *tr );
		char id[16];

		r = need_dq_core_accept( day, x );
		safesnprintf( id, sizeof( id ), "T1-1.%u", tracks[ti] );
		int32 r1 = need_dq_core_submit( day, x, nullptr, gained );
		need_dq_read_char( day.key, x.char_id, a );
		t.check( id, r == NEED_DQ_OK_UNLOCKED && r1 == NEED_DQ_NO_GAIN && a.submitted == 0 && dq_test_stage_rows( day, x.account_id ) == 0,
			"accept %s, submit with 0 -> %s submitted %u", dq_test_rname( r ), dq_test_rname( r1 ), a.submitted );

		dq_test_collect( day, x, s1 - 1 );
		r = need_dq_core_submit( day, x, nullptr, gained );
		need_dq_read_char( day.key, x.char_id, a );
		safesnprintf( id, sizeof( id ), "T1-2.%u", tracks[ti] );
		t.check( id, r == NEED_DQ_NO_GAIN && a.submitted == s1 - 1 && dq_test_stage_rows( day, x.account_id ) == 0,
			"stage-1 (%u) -> %s submitted %u", s1 - 1, dq_test_rname( r ), a.submitted );

		dq_test_collect( day, x, 1 );
		r = need_dq_core_submit( day, x, nullptr, gained );
		need_dq_read_account( day.key, x.account_id, b );
		safesnprintf( id, sizeof( id ), "T1-3.%u", tracks[ti] );
		t.check( id, r == NEED_DQ_OK && gained == 10 && b.delivery_pts == 10 && b.locked_track == tracks[ti] && dq_test_stage_rows( day, x.account_id ) == 1,
			"+1 -> %s gained %d delivery %u locked %u", dq_test_rname( r ), gained, b.delivery_pts, b.locked_track );

		dq_test_collect( day, x, goal - s1 );
		r = need_dq_core_submit( day, x, nullptr, gained );
		need_dq_read_account( day.key, x.account_id, b );
		need_dq_read_char( day.key, x.char_id, a );
		safesnprintf( id, sizeof( id ), "T1-4.%u", tracks[ti] );
		t.check( id, r == NEED_DQ_OK && a.submitted == goal && b.delivery_pts == tr->delivery_stages.back().points &&
			dq_test_stage_rows( day, x.account_id ) == tr->delivery_stages.size(),
			"to goal %u -> %s delivery %u stages %u", goal, dq_test_rname( r ), b.delivery_pts, dq_test_stage_rows( day, x.account_id ) );

		dq_test_collect( day, x, 5 );
		need_dq_read_char( day.key, x.char_id, a );
		safesnprintf( id, sizeof( id ), "T1-5.%u", tracks[ti] );
		t.check( id, a.collected == 0, "capped: +5 after the goal -> collected %u", a.collected );

		// all at once on a fresh account
		s_need_dq_actor y = dq_test_actor( 3999011 + ti, 3999111 + ti, levels[ti] );
		need_dq_core_accept( day, y );
		dq_test_collect( day, y, goal + 7 );
		need_dq_read_char( day.key, y.char_id, a );
		r = need_dq_core_submit( day, y, nullptr, gained );
		need_dq_read_account( day.key, y.account_id, b );
		safesnprintf( id, sizeof( id ), "T1-4b.%u", tracks[ti] );
		t.check( id, a.collected == goal && r == NEED_DQ_OK && gained == tr->delivery_stages.back().points && b.delivery_pts == gained,
			"collect goal+7 capped at %u, one submit -> %s gained %d", a.collected, dq_test_rname( r ), gained );
	}

	{
		const s_need_dq_day day = dq_test_weekday( 1 );
		s_need_dq_actor x = dq_test_actor( 3999021, 3999121, 110 );
		dq_test_collect( day, x, 50 );
		need_dq_read_char( day.key, x.char_id, a );
		r = need_dq_core_submit( day, x, nullptr, gained );
		t.check( "T1-6", !a.exists && r == NEED_DQ_NOT_ACCEPTED, "no accept: collect writes nothing (row %d), submit %s", a.exists ? 1 : 0, dq_test_rname( r ) );
	}

	// ---- T2: duplicates, characters of one account, fault injection
	{
		const s_need_dq_day day = dq_test_weekday( 2 );
		s_need_dq_actor x = dq_test_actor( 3999031, 3999131, 110 );
		need_dq_core_accept( day, x );
		dq_test_collect( day, x, 15 );
		int32 total = 0;
		int32 oks = 0;
		for( int32 i = 0; i < 5; i++ ) {
			r = need_dq_core_submit( day, x, nullptr, gained );
			total += gained;
			oks += r == NEED_DQ_OK ? 1 : 0;
		}
		t.check( "T2-1", total == 10 && oks == 1 && dq_test_stage_rows( day, x.account_id ) == 1, "5 submits: gained %d, OK %d, stage rows %u", total, oks, dq_test_stage_rows( day, x.account_id ) );

		s_need_dq_actor x2 = dq_test_actor( 3999031, 3999132, 115 );
		need_dq_core_accept( day, x2 );
		dq_test_collect( day, x2, 30 );
		r = need_dq_core_submit( day, x2, nullptr, gained );
		need_dq_read_account( day.key, x.account_id, b );
		t.check( "T2-3", r == NEED_DQ_OK && gained == 10 && b.delivery_pts == 20 && dq_test_stage_rows( day, x.account_id ) == 2,
			"second char 30 -> %s gained %d delivery %u", dq_test_rname( r ), gained, b.delivery_pts );

		s_need_dq_actor x3 = dq_test_actor( 3999031, 3999133, 120 );
		need_dq_core_accept( day, x3 );
		dq_test_collect( day, x3, 30 );
		r = need_dq_core_submit( day, x3, nullptr, gained );
		need_dq_read_account( day.key, x.account_id, b );
		t.check( "T2-4", r == NEED_DQ_NO_GAIN && gained == 0 && b.delivery_pts == 20, "third char 30 -> %s delivery %u (U7: no summing)", dq_test_rname( r ), b.delivery_pts );

		for( int32 fp = 1; fp <= 2; fp++ ) {
			s_need_dq_actor f = dq_test_actor( 3999040 + fp, 3999140 + fp, 110 );
			need_dq_core_accept( day, f );
			dq_test_collect( day, f, 15 );
			std::string before = dq_test_digest();
			need_dq_test_fail_point = fp;
			r = need_dq_core_submit( day, f, nullptr, gained );
			need_dq_test_fail_point = 0;
			std::string after = dq_test_digest();
			char id[16];
			safesnprintf( id, sizeof( id ), "T12-3.fp%d", fp );
			t.check( id, r == NEED_DQ_ERROR && before == after, "fault at point %d -> %s, rows unchanged %d", fp, dq_test_rname( r ), before == after ? 1 : 0 );
			r = need_dq_core_submit( day, f, nullptr, gained );
			safesnprintf( id, sizeof( id ), "T12-4.fp%d", fp );
			t.check( id, r == NEED_DQ_OK && gained == 10, "retry -> %s gained %d", dq_test_rname( r ), gained );
		}
	}

	// ---- T3: track lock
	{
		const s_need_dq_day day = dq_test_weekday( 3 );
		s_need_dq_actor low = dq_test_actor( 3999051, 3999151, 110 );
		s_need_dq_actor top = dq_test_actor( 3999051, 3999152, 175 );
		s_need_dq_actor mid = dq_test_actor( 3999051, 3999153, 130 );
		r = need_dq_core_accept( day, low );
		dq_test_collect( day, low, 20 );
		need_dq_read_account( day.key, low.account_id, b );
		t.check( "T3-1", r == NEED_DQ_OK_UNLOCKED && b.locked_track == 0, "accept + collect does not lock (locked %u)", b.locked_track );
		r = need_dq_core_accept( day, top );
		t.check( "T3-2", r == NEED_DQ_OK_UNLOCKED, "other track while unlocked -> %s (warning)", dq_test_rname( r ) );
		dq_test_collect( day, top, 15 );
		r = need_dq_core_submit( day, top, nullptr, gained );
		need_dq_read_account( day.key, low.account_id, b );
		t.check( "T3-3", r == NEED_DQ_OK && b.locked_track == 175 && b.locked_char_id == top.char_id, "first score by Lv175 -> locked %u by %u", b.locked_track, b.locked_char_id );
		r = need_dq_core_submit( day, low, nullptr, gained );
		need_dq_read_char( day.key, low.char_id, a );
		need_dq_read_account( day.key, low.account_id, b );
		t.check( "T3-4", r == NEED_DQ_LOCKED_OUT && gained == 0 && a.submitted == 20 && a.collected == 0 && b.delivery_pts == 10,
			"Lv110 submit -> %s, submitted %u recorded, delivery %u", dq_test_rname( r ), a.submitted, b.delivery_pts );
		r = need_dq_core_accept( day, mid );
		t.check( "T3-5", r == NEED_DQ_LOCKED_OUT, "Lv130 accept after the lock -> %s", dq_test_rname( r ) );
		r = need_dq_core_accept( day, top );
		t.check( "T3-ALREADY", r == NEED_DQ_ALREADY, "second accept -> %s", dq_test_rname( r ) );
	}

	// ---- T12: points recomputed, never doubled; cap 80
	{
		const s_need_dq_day day = dq_test_weekday( 4 );
		s_need_dq_actor x = dq_test_actor( 3999061, 3999161, 110 );
		need_dq_core_accept( day, x );
		dq_test_collect( day, x, 15 );
		need_dq_core_submit( day, x, nullptr, gained );
		char q[256];
		safesnprintf( q, sizeof( q ), "UPDATE `need_dq_account_day` SET `hunt_pts`=20,`total_pts`=30 WHERE `day_key`='%s' AND `account_id`='%u'", day.key, x.account_id );
		dq_query_str( q );   // stands in for a hunt score of a later phase
		dq_test_collect( day, x, 15 );
		r = need_dq_core_submit( day, x, nullptr, gained );
		need_dq_read_account( day.key, x.account_id, b );
		t.check( "T12-1", r == NEED_DQ_OK && b.delivery_pts == 20 && b.total_pts == 40 && b.hunt_pts == 20,
			"delivery 10->20 with hunt 20 -> delivery %u total %u (50 = the old double count)", b.delivery_pts, b.total_pts );
		std::string before = dq_test_digest();
		r = need_dq_core_submit( day, x, nullptr, gained );
		t.check( "T12-2", r == NEED_DQ_NO_GAIN && before == dq_test_digest(), "same submit again -> %s, rows unchanged", dq_test_rname( r ) );

		s_need_dq_actor c = dq_test_actor( 3999062, 3999162, 110 );
		need_dq_core_accept( day, c );
		dq_test_collect( day, c, 15 );
		need_dq_core_submit( day, c, nullptr, gained );
		safesnprintf( q, sizeof( q ), "UPDATE `need_dq_account_day` SET `hunt_pts`=70,`total_pts`=80 WHERE `day_key`='%s' AND `account_id`='%u'", day.key, c.account_id );
		dq_query_str( q );
		dq_test_collect( day, c, 30 );
		r = need_dq_core_submit( day, c, nullptr, gained );
		need_dq_read_account( day.key, c.account_id, b );
		t.check( "T11-1", r == NEED_DQ_OK && b.delivery_pts == 30 && b.total_pts == 80, "hunt 70 + delivery 30 -> total %u (cap 80)", b.total_pts );
		t.check( "T11-3", b.hunt_pts == 70 && b.bg_pts == 0 && b.inst_pts == 0, "delivery leaves other columns alone (hunt %u bg %u inst %u)", b.hunt_pts, b.bg_pts, b.inst_pts );
	}

	// ---- T13: the 04:00 boundary, pending carries its own day
	{
		const s_need_dq_day d1 = dq_test_weekday( 1, 1 );
		const s_need_dq_day d2 = dq_test_weekday( 2, 1 );
		s_need_dq_actor x = dq_test_actor( 3999071, 3999171, 110 );
		need_dq_core_accept( d1, x );
		need_dq_core_accept( d2, x );
		s_need_dq_pending p;
		safesnprintf( p.day, sizeof( p.day ), "%s", d1.key );
		p.track = 100;
		p.count = 3;
		need_dq_flush( x.char_id, p );
		s_need_dq_char_row a1, a2;
		need_dq_read_char( d1.key, x.char_id, a1 );
		need_dq_read_char( d2.key, x.char_id, a2 );
		t.check( "T13-2", a1.collected == 3 && a2.collected == 0, "pending of %s -> that row %u, %s row %u", d1.key, a1.collected, d2.key, a2.collected );

		s_need_dq_actor y = dq_test_actor( 3999072, 3999172, 110 );
		need_dq_core_accept( d1, y );
		safesnprintf( p.day, sizeof( p.day ), "%s", d1.key );
		p.track = 100;
		p.count = 5;
		r = need_dq_core_submit( d2, y, &p, gained );
		need_dq_read_char( d1.key, y.char_id, a1 );
		need_dq_read_account( d2.key, y.account_id, b );
		t.check( "T13-4", r == NEED_DQ_NOT_ACCEPTED && a1.collected == 5 && p.count == 0 && !b.exists,
			"submit on %s without accept -> %s, old pending to %s (%u), new day row %d", d2.key, dq_test_rname( r ), d1.key, a1.collected, b.exists ? 1 : 0 );

		s_need_dq_actor z = dq_test_actor( 3999073, 3999173, 110 );
		need_dq_core_accept( d1, z );
		need_dq_core_accept( d2, z );
		safesnprintf( p.day, sizeof( p.day ), "%s", d1.key );
		p.track = 100;
		p.count = 5;
		r = need_dq_core_submit( d2, z, &p, gained );
		need_dq_read_char( d1.key, z.char_id, a1 );
		need_dq_read_char( d2.key, z.char_id, a2 );
		t.check( "T13-5", r == NEED_DQ_NO_GAIN && a1.collected == 5 && a2.collected == 0 && a2.submitted == 0,
			"accepted both days, submit on %s -> %s, old row %u, new row %u/%u", d2.key, dq_test_rname( r ), a1.collected, a2.collected, a2.submitted );

		s_need_dq_actor w = dq_test_actor( 3999074, 3999174, 175 );
		s_need_dq_actor w2 = dq_test_actor( 3999074, 3999175, 110 );
		need_dq_core_accept( d1, w );
		dq_test_collect( d1, w, 15 );
		need_dq_core_submit( d1, w, nullptr, gained );
		r = need_dq_core_accept( d2, w2 );
		t.check( "T13-6", r == NEED_DQ_OK_UNLOCKED, "locked 175 yesterday, Lv110 accept today -> %s", dq_test_rname( r ) );
	}

	// ---- T14: level range changes after accept (spec 4.6)
	{
		const s_need_dq_day day = dq_test_weekday( 1, 2 );
		s_need_dq_actor x = dq_test_actor( 3999081, 3999181, 129 );
		need_dq_core_accept( day, x );
		dq_test_collect( day, x, 14 );
		x.base_level = 130;
		std::string before = dq_test_digest();
		r = need_dq_core_submit( day, x, nullptr, gained );
		t.check( "T14-1", r == NEED_DQ_LEVEL_CHANGED && before == dq_test_digest(), "129->130 before any score: submit -> %s, nothing written", dq_test_rname( r ) );
		int32 same = 0;
		for( int32 i = 0; i < 5; i++ )
			same += need_dq_core_submit( day, x, nullptr, gained ) == NEED_DQ_LEVEL_CHANGED ? 1 : 0;
		t.check( "T14-16", same == 5 && before == dq_test_digest(), "5 more submits -> LEVEL_CHANGED x%d, nothing written", same );
		r = need_dq_core_reaccept( day, x, nullptr );
		need_dq_read_char( day.key, x.char_id, a );
		need_dq_read_account( day.key, x.account_id, b );
		std::shared_ptr<s_need_dq_track> t130 = need_dq_db.find( 130 );
		t.check( "T14-3", r == NEED_DQ_OK && a.track == 130 && a.map_name == t130->rotation[day.wday]->name && a.collected == 0 && a.submitted == 0 &&
			a.abandoned == 14 && a.reaccept_count == 1 && b.locked_track == 0,
			"re-accept -> %s track %u map %s abandoned %u reaccept %u locked %u", dq_test_rname( r ), a.track, a.map_name.c_str(), a.abandoned, a.reaccept_count, b.locked_track );
		dq_test_collect( day, x, 15 );
		r = need_dq_core_submit( day, x, nullptr, gained );
		need_dq_read_account( day.key, x.account_id, b );
		char q[256];
		safesnprintf( q, sizeof( q ), "SELECT COUNT(*) FROM `need_dq_score_stage` WHERE `day_key`='%s' AND `account_id`='%u' AND `track`=130 AND `base_level`=130", day.key, x.account_id );
		t.check( "T14-4", r == NEED_DQ_OK && gained == 10 && b.locked_track == 130 && dq_test_count( q ) == 1, "15 at Lv130 -> %s locked %u", dq_test_rname( r ), b.locked_track );

		s_need_dq_actor y = dq_test_actor( 3999082, 3999182, 129 );
		need_dq_core_accept( day, y );
		dq_test_collect( day, y, 15 );
		need_dq_core_submit( day, y, nullptr, gained );
		dq_test_collect( day, y, 10 );
		y.base_level = 130;
		before = dq_test_digest();
		r = need_dq_core_submit( day, y, nullptr, gained );
		need_dq_read_account( day.key, y.account_id, b );
		t.check( "T14-5", r == NEED_DQ_LEVEL_CHANGED && before == dq_test_digest() && b.delivery_pts == 10 && b.locked_track == 100,
			"scored at 129 (locked 100), then 130: submit -> %s, delivery %u kept", dq_test_rname( r ), b.delivery_pts );
		r = need_dq_core_reaccept( day, y, nullptr );
		t.check( "T14-6", r == NEED_DQ_LOCKED_OUT && before == dq_test_digest(), "re-accept while locked to 100 -> %s", dq_test_rname( r ) );
		s_need_dq_actor y2 = dq_test_actor( 3999082, 3999183, 120 );
		r = need_dq_core_accept( day, y2 );
		dq_test_collect( day, y2, 30 );
		int32 r2 = need_dq_core_submit( day, y2, nullptr, gained );
		need_dq_read_account( day.key, y.account_id, b );
		t.check( "T14-7", r == NEED_DQ_OK && r2 == NEED_DQ_OK && gained == 10 && b.delivery_pts == 20, "other Lv120 char -> accept %s, submit %s gained %d", dq_test_rname( r ), dq_test_rname( r2 ), gained );

		s_need_dq_actor xx = dq_test_actor( 3999083, 3999184, 129 );
		s_need_dq_actor yy = dq_test_actor( 3999083, 3999185, 140 );
		r = need_dq_core_accept( day, xx );
		need_dq_core_accept( day, yy );
		dq_test_collect( day, yy, 15 );
		need_dq_core_submit( day, yy, nullptr, gained );
		dq_test_collect( day, xx, 20 );
		xx.base_level = 130;
		int32 rr = need_dq_core_reaccept( day, xx, nullptr );
		dq_test_collect( day, xx, 15 );
		int32 rs1 = need_dq_core_submit( day, xx, nullptr, gained );
		dq_test_collect( day, xx, 15 );
		int32 rs2 = need_dq_core_submit( day, xx, nullptr, gained );
		need_dq_read_account( day.key, xx.account_id, b );
		t.check( "T14-8", r == NEED_DQ_OK_UNLOCKED && rr == NEED_DQ_OK && rs1 == NEED_DQ_NO_GAIN && rs2 == NEED_DQ_OK && b.delivery_pts == 20 &&
			dq_test_stage_rows( day, xx.account_id ) == 2,
			"locked 130 by another char; 129->130 re-accept %s, d1 again %s, d2 %s, delivery %u", dq_test_rname( rr ), dq_test_rname( rs1 ), dq_test_rname( rs2 ), b.delivery_pts );

		s_need_dq_actor pz = dq_test_actor( 3999084, 3999186, 129 );
		need_dq_core_accept( day, pz );
		dq_test_collect( day, pz, 6 );
		s_need_dq_pending p;
		safesnprintf( p.day, sizeof( p.day ), "%s", day.key );
		p.track = 100;
		p.count = 5;
		pz.base_level = 130;
		r = need_dq_core_reaccept( day, pz, &p );
		need_dq_read_char( day.key, pz.char_id, a );
		t.check( "T14-10", r == NEED_DQ_OK && p.count == 0 && a.abandoned == 11 && a.collected == 0 && a.track == 130,
			"re-accept with 5 pending -> %s abandoned %u (6 + 5) pending %u", dq_test_rname( r ), a.abandoned, p.count );

		const s_need_dq_day wed = dq_test_weekday( 3, 2 );
		s_need_dq_actor v = dq_test_actor( 3999085, 3999187, 174 );
		need_dq_core_accept( wed, v );
		need_dq_read_char( wed.key, v.char_id, a );
		bool ver = a.map_name == "ver_eju";
		dq_test_collect( wed, v, 9 );
		v.base_level = 175;
		r = need_dq_core_reaccept( wed, v, nullptr );
		need_dq_read_char( wed.key, v.char_id, a );
		t.check( "T14-11", ver && r == NEED_DQ_OK && a.track == 175 && a.map_name != "ver_eju" && a.collected == 0,
			"Wed Lv174 on ver_eju -> 175 re-accept %s map %s", dq_test_rname( r ), a.map_name.c_str() );

		s_need_dq_actor n = dq_test_actor( 3999086, 3999188, 125 );
		need_dq_core_accept( day, n );
		dq_test_collect( day, n, 15 );
		n.base_level = 99;
		before = dq_test_digest();
		int32 n1 = need_dq_core_submit( day, n, nullptr, gained );
		int32 n2 = need_dq_core_reaccept( day, n, nullptr );
		int32 n3 = need_dq_core_accept( day, n );
		t.check( "T14-13", n1 == NEED_DQ_NOT_ELIGIBLE && n2 == NEED_DQ_NOT_ELIGIBLE && n3 == NEED_DQ_NOT_ELIGIBLE && before == dq_test_digest(),
			"Lv99 -> submit %s, re-accept %s, accept %s, nothing written", dq_test_rname( n1 ), dq_test_rname( n2 ), dq_test_rname( n3 ) );

		s_need_dq_actor j = dq_test_actor( 3999087, 3999189, 125 );
		need_dq_core_accept( day, j );
		j.base_level = 140;
		r = need_dq_core_reaccept( day, j, nullptr );
		need_dq_read_char( day.key, j.char_id, a );
		t.check( "T14-15", r == NEED_DQ_OK && a.track == 130, "125 -> 140 in one jump: re-accept %s track %u", dq_test_rname( r ), a.track );
		r = need_dq_core_reaccept( day, j, nullptr );
		t.check( "T14-NN", r == NEED_DQ_NOT_NEEDED, "re-accept in the same range -> %s", dq_test_rname( r ) );

		const s_need_dq_day next = dq_test_weekday( 2, 2 );
		s_need_dq_char_row old_row;
		need_dq_read_char( day.key, x.char_id, old_row );
		r = need_dq_core_accept( next, x );
		need_dq_read_char( day.key, x.char_id, a );
		t.check( "T14-17", r == NEED_DQ_OK_UNLOCKED && a.collected == old_row.collected && a.submitted == old_row.submitted,
			"next day accept at Lv130 -> %s, previous row unchanged", dq_test_rname( r ) );
	}

	// ---- H: hunting (phase 2)
	{
		const s_need_dq_day day = dq_test_weekday( 2, 3 );
		int32 hits = 0;
		for( int32 ti = 0; ti < 4; ti++ ) {
			s_need_dq_actor x = dq_test_actor( 3999201 + ti, 3999301 + ti, levels[ti] );
			r = need_dq_core_accept( day, x );
			s_need_dq_char_row h;
			need_dq_read_char( day.key, x.char_id, a );
			need_dq_read_hunt( day.key, x.char_id, h );
			hits += ( r == NEED_DQ_OK_UNLOCKED && a.exists && h.exists && h.track == tracks[ti] && h.count == 0 ) ? 1 : 0;
		}
		t.check( "H1", hits == 4, "one accept starts delivery + hunt for every track (%d/4)", hits );

		s_need_dq_actor x = dq_test_actor( 3999201, 3999301, 110 );
		s_need_dq_char_row h;
		r = dq_test_hunt( day, x, 399, gained );
		need_dq_read_hunt( day.key, x.char_id, h );
		need_dq_read_account( day.key, x.account_id, b );
		t.check( "H2-1", r == NEED_DQ_NO_GAIN && h.count == 399 && b.hunt_pts == 0 && b.locked_track == 0, "399 kills -> %s count %u hunt %u", dq_test_rname( r ), h.count, b.hunt_pts );
		r = dq_test_hunt( day, x, 1, gained );
		need_dq_read_hunt( day.key, x.char_id, h );
		need_dq_read_account( day.key, x.account_id, b );
		t.check( "H2-2", r == NEED_DQ_OK && gained == 10 && h.count == 400 && b.hunt_pts == 10 && b.locked_track == 100, "400th kill -> %s gained %d locked %u", dq_test_rname( r ), gained, b.locked_track );
		r = dq_test_hunt( day, x, 1600, gained );
		need_dq_read_hunt( day.key, x.char_id, h );
		need_dq_read_account( day.key, x.account_id, b );
		char q[256];
		safesnprintf( q, sizeof( q ), "SELECT COUNT(*) FROM `need_dq_score_stage` WHERE `day_key`='%s' AND `account_id`='%u' AND `activity`='hunt'", day.key, x.account_id );
		t.check( "H2-3", r == NEED_DQ_OK && gained == 40 && h.count == 2000 && b.hunt_pts == 50 && dq_test_count( q ) == 5, "to 2000 -> gained %d hunt %u stages %u", gained, b.hunt_pts, dq_test_count( q ) );
		r = dq_test_hunt( day, x, 50, gained );
		need_dq_read_hunt( day.key, x.char_id, h );
		t.check( "H2-4", r == NEED_DQ_NO_GAIN && h.count == 2000, "50 more after the goal -> %s count %u (cap)", dq_test_rname( r ), h.count );
		dq_test_collect( day, x, 45 );
		need_dq_core_submit( day, x, nullptr, gained );
		need_dq_read_account( day.key, x.account_id, b );
		t.check( "H3", b.hunt_pts == 50 && b.delivery_pts == 30 && b.total_pts == 80, "hunt 50 + delivery 30 -> total %u", b.total_pts );

		s_need_dq_actor top = dq_test_actor( 3999204, 3999304, 175 );
		r = dq_test_hunt( day, top, 600, gained );
		need_dq_read_account( day.key, top.account_id, b );
		safesnprintf( q, sizeof( q ), "SELECT COUNT(*) FROM `need_dq_score_stage` WHERE `day_key`='%s' AND `account_id`='%u' AND `activity`='hunt'", day.key, top.account_id );
		t.check( "H4", r == NEED_DQ_OK && gained == 40 && b.hunt_pts == 40 && dq_test_count( q ) == 4, "Lv175 600 kills -> hunt %u in %u stages", b.hunt_pts, dq_test_count( q ) );
	}
	{
		const s_need_dq_day day = dq_test_weekday( 3, 3 );
		s_need_dq_actor x = dq_test_actor( 3999211, 3999311, 129 );
		need_dq_core_accept( day, x );
		dq_test_hunt( day, x, 399, gained );
		x.base_level = 130;
		r = dq_test_hunt( day, x, 1, gained );
		s_need_dq_char_row h;
		need_dq_read_hunt( day.key, x.char_id, h );
		need_dq_read_account( day.key, x.account_id, b );
		t.check( "H5", r == NEED_DQ_LEVEL_CHANGED && h.count == 400 && b.hunt_pts == 0 && dq_test_stage_rows( day, x.account_id ) == 0,
			"129->130 then the 400th kill -> %s, kills kept %u, no points", dq_test_rname( r ), h.count );
		r = need_dq_core_reaccept( day, x, nullptr );
		need_dq_read_hunt( day.key, x.char_id, h );
		need_dq_read_char( day.key, x.char_id, a );
		t.check( "H7", r == NEED_DQ_OK && h.track == 130 && h.count == 0 && h.abandoned == 400 && h.reaccept_count == 1 && a.track == 130,
			"re-accept moves the hunt too: track %u count %u abandoned %u", h.track, h.count, h.abandoned );

		s_need_dq_actor low = dq_test_actor( 3999212, 3999312, 110 );
		s_need_dq_actor high = dq_test_actor( 3999212, 3999313, 175 );
		need_dq_core_accept( day, low );
		need_dq_core_accept( day, high );
		dq_test_collect( day, high, 15 );
		need_dq_core_submit( day, high, nullptr, gained );
		r = dq_test_hunt( day, low, 400, gained );
		need_dq_read_hunt( day.key, low.char_id, h );
		need_dq_read_account( day.key, low.account_id, b );
		t.check( "H6", r == NEED_DQ_LOCKED_OUT && h.count == 400 && b.hunt_pts == 0 && b.locked_track == 175, "locked 175, Lv110 400 kills -> %s, kills kept, hunt %u", dq_test_rname( r ), b.hunt_pts );

		s_need_dq_actor c1 = dq_test_actor( 3999213, 3999314, 110 );
		s_need_dq_actor c2 = dq_test_actor( 3999213, 3999315, 120 );
		need_dq_core_accept( day, c1 );
		need_dq_core_accept( day, c2 );
		dq_test_hunt( day, c1, 800, gained );
		int32 r1 = dq_test_hunt( day, c2, 400, gained );
		int32 r2 = dq_test_hunt( day, c2, 800, gained );
		need_dq_read_account( day.key, c1.account_id, b );
		t.check( "H8", r1 == NEED_DQ_NO_GAIN && r2 == NEED_DQ_OK && gained == 10 && b.hunt_pts == 30, "second char: 400 -> %s, 1200 -> %s +%d, account hunt %u (U7)", dq_test_rname( r1 ), dq_test_rname( r2 ), gained, b.hunt_pts );

		s_need_dq_actor old = dq_test_actor( 3999214, 3999316, 110 );
		char q[320];
		safesnprintf( q, sizeof( q ), "INSERT INTO `need_dq_char_progress` (`day_key`,`char_id`,`activity`,`account_id`,`track`,`map_name`,`accepted_at`) VALUES ('%s','%u','delivery','%u','100','%s',NOW())",
			day.key, old.char_id, old.account_id, need_dq_db.find( 100 )->rotation[day.wday]->name.c_str() );
		dq_query_str( q );
		r = need_dq_core_accept( day, old );
		s_need_dq_char_row oh;
		need_dq_read_hunt( day.key, old.char_id, oh );
		int32 again = need_dq_core_accept( day, old );
		t.check( "H9", r == NEED_DQ_OK_UNLOCKED && oh.exists && oh.track == 100 && again == NEED_DQ_ALREADY, "delivery accepted before phase 2: accept adds the hunt (%s), then %s", dq_test_rname( r ), dq_test_rname( again ) );

		s_need_dq_actor none = dq_test_actor( 3999215, 3999317, 110 );
		std::string before = dq_test_digest();
		r = dq_test_hunt( day, none, 50, gained );
		t.check( "H10", r == NEED_DQ_NOT_ACCEPTED && before == dq_test_digest(), "kills without accept -> %s, nothing written", dq_test_rname( r ) );

		s_need_dq_actor f = dq_test_actor( 3999216, 3999318, 110 );
		need_dq_core_accept( day, f );
		dq_test_hunt( day, f, 399, gained );
		before = dq_test_digest();
		s_need_dq_pending p;
		safestrncpy( p.day, day.key, sizeof( p.day ) );
		p.track = 100;
		p.count = 1;
		p.hunt = true;
		need_dq_test_fail_point = 1;
		r = need_dq_core_hunt_commit( day, f, &p, gained );
		need_dq_test_fail_point = 0;
		bool same = before == dq_test_digest();
		bool kept = p.count == 1;
		int32 r2b = need_dq_core_hunt_commit( day, f, &p, gained );
		t.check( "H11", r == NEED_DQ_ERROR && same && kept && r2b == NEED_DQ_OK && gained == 10 && p.count == 0,
			"fault before the account UPDATE -> %s, rows unchanged %d, pending kept %d; retry %s +%d", dq_test_rname( r ), same ? 1 : 0, kept ? 1 : 0, dq_test_rname( r2b ), gained );
	}

	// ---- B: battleground games (phase 3)
	{
		s_need_dq_bg_match m;
		m.type = "Tierra";
		m.key = "Tierra-4070000000";
		m.started = 4070000000;
		m.m = 7;
		for( int32 i = 0; i < 8; i++ ) {
			s_need_dq_bg_participant p;
			p.char_id = 3999501 + i;
			p.account_id = 3999401 + i;
			p.ip = 0x0A000001 + ( i < 6 ? i : 5 );
			m.players.push_back( p );
		}
		m.distinct_ips = 6;
		time_t end = m.started + 360;
		t.check( "BV-1", need_dq_bg_validate( m, 3999501, 7, end, m.started ) == NEED_DQ_BG_VALID, "8 players, 6 IPs (75%%), 360 s, acted at the start -> valid" );
		t.check( "BV-2", need_dq_bg_validate( m, 3999599, 7, end, end ) == NEED_DQ_BG_NOT_PARTICIPANT, "not in the battle at its start" );
		t.check( "BV-3", need_dq_bg_validate( m, 3999501, 8, end, end ) == NEED_DQ_BG_NOT_ON_MAP, "not on the battle map" );
		t.check( "BV-4", need_dq_bg_validate( m, 3999501, 7, end - 1, end - 1 ) == NEED_DQ_BG_TOO_SHORT, "Tierra 359 s -> too short" );
		s_need_dq_bg_match k = m;
		k.type = "KVM";
		t.check( "BV-5", need_dq_bg_validate( k, 3999501, 7, k.started + 180, k.started + 1 ) == NEED_DQ_BG_VALID &&
			need_dq_bg_validate( k, 3999501, 7, k.started + 179, k.started + 1 ) == NEED_DQ_BG_TOO_SHORT, "KVM 180 s valid, 179 s too short" );
		t.check( "BV-6", need_dq_bg_validate( m, 3999501, 7, end, m.started - 1 ) == NEED_DQ_BG_NO_ACTION, "last action before the start -> no action" );
		s_need_dq_bg_match ip = m;
		ip.distinct_ips = 5;
		t.check( "BV-7", need_dq_bg_validate( ip, 3999501, 7, end, end ) == NEED_DQ_BG_SHARED_IP, "8 players, 5 IPs (62%%) -> shared IPs" );
		need_dq_test_bg_ip_percent = 50;
		bool lowered = need_dq_bg_validate( ip, 3999501, 7, end, end ) == NEED_DQ_BG_VALID;
		need_dq_test_bg_ip_percent = -1;
		t.check( "BV-9", lowered && need_dq_bg_validate( ip, 3999501, 7, end, end ) == NEED_DQ_BG_SHARED_IP,
			"@dqtest bgip 50 accepts 5/8 IPs, -1 restores 75%%" );
		s_need_dq_bg_match none;
		t.check( "BV-8", need_dq_bg_validate( none, 3999501, 7, end, end ) == NEED_DQ_BG_NO_MATCH, "no battle start recorded" );
	}
	{
		const s_need_dq_day day = dq_test_weekday( 4, 3 );
		time_t now = time( nullptr );
		auto mk = [&]( int32 n ) {
			s_need_dq_bg_match m;
			m.type = "Flavius";
			m.key = "Flavius-" + std::to_string( 4080000000LL + n );
			m.started = now - 400;
			m.m = 1;
			m.distinct_ips = 8;
			for( int32 i = 0; i < 8; i++ ) {
				s_need_dq_bg_participant p;
				p.char_id = 3999501 + i;
				m.players.push_back( p );
			}
			return m;
		};
		uint32 games = 0;
		char q[256];
		s_need_dq_actor x = dq_test_actor( 3999401, 3999501, 160 );
		s_need_dq_actor nx = dq_test_actor( 3999402, 3999502, 160 );
		r = need_dq_core_bg_commit( day, nx, mk( 1 ), NEED_DQ_BG_VALID, now, gained, games );
		safesnprintf( q, sizeof( q ), "SELECT COUNT(*) FROM `need_dq_bg_match` WHERE `day_key`='%s' AND `account_id`='%u'", day.key, nx.account_id );
		t.check( "BC-1", r == NEED_DQ_NOT_ACCEPTED && dq_test_count( q ) == 0, "not accepted -> %s, no game row", dq_test_rname( r ) );

		need_dq_core_accept( day, x );
		r = need_dq_core_bg_commit( day, x, mk( 1 ), NEED_DQ_BG_VALID, now, gained, games );
		need_dq_read_account( day.key, x.account_id, b );
		t.check( "BC-2", r == NEED_DQ_OK && gained == 30 && games == 1 && b.bg_pts == 30 && b.locked_track == 150, "first valid game -> %s +%d bg %u locked %u", dq_test_rname( r ), gained, b.bg_pts, b.locked_track );
		std::string before = dq_test_digest();
		r = need_dq_core_bg_commit( day, x, mk( 1 ), NEED_DQ_BG_VALID, now, gained, games );
		t.check( "BC-3", r == NEED_DQ_ALREADY && before == dq_test_digest(), "same match again -> %s, nothing written", dq_test_rname( r ) );
		r = need_dq_core_bg_commit( day, x, mk( 2 ), NEED_DQ_BG_VALID, now, gained, games );
		need_dq_read_account( day.key, x.account_id, b );
		t.check( "BC-4", r == NEED_DQ_OK && gained == 20 && games == 2 && b.bg_pts == 50, "second game -> +%d bg %u", gained, b.bg_pts );
		r = need_dq_core_bg_commit( day, x, mk( 3 ), NEED_DQ_BG_VALID, now, gained, games );
		need_dq_read_account( day.key, x.account_id, b );
		t.check( "BC-5", r == NEED_DQ_NO_GAIN && games == 3 && b.bg_pts == 50, "third game -> %s, bg stays %u (cap 50)", dq_test_rname( r ), b.bg_pts );
		r = need_dq_core_bg_commit( day, x, mk( 4 ), NEED_DQ_BG_TOO_SHORT, now, gained, games );
		uint32 g = 0;
		need_dq_read_bg_games( day.key, x.char_id, g );
		safesnprintf( q, sizeof( q ), "SELECT COUNT(*) FROM `need_dq_bg_match` WHERE `day_key`='%s' AND `account_id`='%u' AND `valid`=0 AND `reason`=3", day.key, x.account_id );
		t.check( "BC-6", r == NEED_DQ_NO_GAIN && g == 3 && dq_test_count( q ) == 1, "too short -> %s, kept as refused (reason 3), games still %u", dq_test_rname( r ), g );

		s_need_dq_actor low = dq_test_actor( 3999403, 3999503, 110 );
		need_dq_core_accept( day, low );
		r = need_dq_core_bg_commit( day, low, mk( 5 ), NEED_DQ_BG_VALID, now, gained, games );
		safesnprintf( q, sizeof( q ), "SELECT COUNT(*) FROM `need_dq_bg_match` WHERE `day_key`='%s' AND `account_id`='%u'", day.key, low.account_id );
		t.check( "BC-7", r == NEED_DQ_NOT_ELIGIBLE && dq_test_count( q ) == 0, "Lv110 -> %s, no game row (no battleground below Lv150)", dq_test_rname( r ) );

		s_need_dq_actor y = dq_test_actor( 3999404, 3999504, 160 );
		s_need_dq_actor y2 = dq_test_actor( 3999404, 3999505, 175 );
		need_dq_core_accept( day, y );
		need_dq_core_accept( day, y2 );
		dq_test_collect( day, y2, 15 );
		need_dq_core_submit( day, y2, nullptr, gained );
		r = need_dq_core_bg_commit( day, y, mk( 6 ), NEED_DQ_BG_VALID, now, gained, games );
		need_dq_read_account( day.key, y.account_id, b );
		t.check( "BC-8", r == NEED_DQ_LOCKED_OUT && b.bg_pts == 0 && games == 0, "locked 175 -> Lv160 game %s, kept but not eligible (account games %u), bg %u", dq_test_rname( r ), games, b.bg_pts );
		r = need_dq_core_bg_commit( day, y, mk( 10 ), NEED_DQ_BG_NO_ACTION, now, gained, games );
		t.check( "BC-11", r == NEED_DQ_NO_GAIN && gained == 0, "refused game of a locked-out char -> %s (the refusal reason is shown, not the lock)", dq_test_rname( r ) );

		s_need_dq_actor z = dq_test_actor( 3999405, 3999506, 160 );
		need_dq_core_accept( day, z );
		z.base_level = 175;
		r = need_dq_core_bg_commit( day, z, mk( 7 ), NEED_DQ_BG_VALID, now, gained, games );
		need_dq_read_account( day.key, z.account_id, b );
		t.check( "BC-9", r == NEED_DQ_LEVEL_CHANGED && b.bg_pts == 0, "accepted 150-174, now 175 -> %s, no points", dq_test_rname( r ) );

		s_need_dq_actor w = dq_test_actor( 3999406, 3999507, 175 );
		need_dq_core_accept( day, w );
		dq_test_hunt( day, w, 600, gained );
		dq_test_collect( day, w, 30 );
		need_dq_core_submit( day, w, nullptr, gained );
		need_dq_core_bg_commit( day, w, mk( 8 ), NEED_DQ_BG_VALID, now, gained, games );
		need_dq_core_bg_commit( day, w, mk( 9 ), NEED_DQ_BG_VALID, now, gained, games );
		need_dq_read_account( day.key, w.account_id, b );
		t.check( "BC-10", b.hunt_pts == 40 && b.delivery_pts == 20 && b.bg_pts == 50 && b.total_pts == 80, "Lv175 hunt 40 + delivery 20 + bg 50 -> total %u (cap 80)", b.total_pts );
	}

	// ---- BG-A / BG-E: phase 4.5 (D1 account games, D2 official end, D5 day of the end)
	{
		const s_need_dq_day day = dq_test_weekday( 6, 3 );
		time_t now = time( nullptr );
		auto mk = [&]( int32 n ) {
			s_need_dq_bg_match m;
			m.type = "KVM";
			m.key = "KVM-" + std::to_string( 4090000000LL + n );
			m.started = now - 400;
			m.m = 1;
			m.distinct_ips = 8;
			for( int32 i = 0; i < 8; i++ ) {
				s_need_dq_bg_participant p;
				p.char_id = 3999851 + i;
				m.players.push_back( p );
			}
			return m;
		};
		uint32 games = 0;
		int32 g1 = 0;
		s_need_dq_actor a1 = dq_test_actor( 3999751, 3999851, 160 );
		s_need_dq_actor a2 = dq_test_actor( 3999751, 3999852, 165 );
		need_dq_core_accept( day, a1 );
		need_dq_core_accept( day, a2 );
		e_need_dq_result r1 = need_dq_core_bg_commit( day, a1, mk( 1 ), NEED_DQ_BG_VALID, now, gained, games );
		g1 = gained;
		r = need_dq_core_bg_commit( day, a2, mk( 2 ), NEED_DQ_BG_VALID, now, gained, games );
		need_dq_read_account( day.key, a1.account_id, b );
		uint32 c1 = 0, c2 = 0;
		need_dq_read_bg_games( day.key, a1.char_id, c1 );
		need_dq_read_bg_games( day.key, a2.char_id, c2 );
		t.check( "BG-A1", r1 == NEED_DQ_OK && g1 == 30 && r == NEED_DQ_OK && gained == 20 && games == 2 && b.bg_pts == 50 && c1 == 1 && c2 == 1,
			"same account, A 1 game + B another game -> +%d +%d, account games %u, bg %u (each char 1)", g1, gained, games, b.bg_pts );

		r = need_dq_core_bg_commit( day, a1, mk( 3 ), NEED_DQ_BG_VALID, now, gained, games );
		e_need_dq_result again = need_dq_core_bg_commit( day, a2, mk( 3 ), NEED_DQ_BG_VALID, now, gained, games );
		need_dq_read_account( day.key, a1.account_id, b );
		t.check( "BG-A4", r == NEED_DQ_NO_GAIN && again == NEED_DQ_ALREADY && b.bg_pts == 50,
			"third game -> %s, same game by the other char -> %s, bg stays %u", dq_test_rname( r ), dq_test_rname( again ), b.bg_pts );

		s_need_dq_actor s1 = dq_test_actor( 3999752, 3999853, 160 );
		s_need_dq_actor s2 = dq_test_actor( 3999752, 3999854, 160 );
		need_dq_core_accept( day, s1 );
		need_dq_core_accept( day, s2 );
		r1 = need_dq_core_bg_commit( day, s1, mk( 4 ), NEED_DQ_BG_VALID, now, gained, games );
		r = need_dq_core_bg_commit( day, s2, mk( 4 ), NEED_DQ_BG_VALID, now, gained, games );
		uint32 acc = 0;
		need_dq_read_bg_account_games( day.key, s1.account_id, 150, acc );
		need_dq_read_account( day.key, s1.account_id, b );
		t.check( "BG-A2", r1 == NEED_DQ_OK && r == NEED_DQ_ALREADY && acc == 1 && b.bg_pts == 30,
			"A and B in the same battle -> %s / %s, account games %u, bg %u", dq_test_rname( r1 ), dq_test_rname( r ), acc, b.bg_pts );

		// other range / locked out / level changed never add to the account count
		s_need_dq_actor l150 = dq_test_actor( 3999753, 3999855, 160 );
		s_need_dq_actor l175 = dq_test_actor( 3999753, 3999856, 175 );
		need_dq_core_accept( day, l150 );
		need_dq_core_accept( day, l175 );
		dq_test_collect( day, l175, 15 );
		need_dq_core_submit( day, l175, nullptr, gained );   // account locked to 175
		r1 = need_dq_core_bg_commit( day, l150, mk( 5 ), NEED_DQ_BG_VALID, now, gained, games );
		r = need_dq_core_bg_commit( day, l175, mk( 6 ), NEED_DQ_BG_VALID, now, gained, games );
		need_dq_read_account( day.key, l175.account_id, b );
		bool locked_ok = r1 == NEED_DQ_LOCKED_OUT && r == NEED_DQ_OK && gained == 30 && games == 1 && b.bg_pts == 30;
		s_need_dq_actor v1 = dq_test_actor( 3999754, 3999857, 160 );
		s_need_dq_actor v2 = dq_test_actor( 3999754, 3999858, 160 );
		need_dq_core_accept( day, v1 );
		need_dq_core_accept( day, v2 );
		v1.base_level = 175;   // accepted 150-174, now 175: LEVEL_CHANGED
		r1 = need_dq_core_bg_commit( day, v1, mk( 7 ), NEED_DQ_BG_VALID, now, gained, games );
		r = need_dq_core_bg_commit( day, v2, mk( 8 ), NEED_DQ_BG_VALID, now, gained, games );
		need_dq_read_account( day.key, v2.account_id, b );
		bool level_ok = r1 == NEED_DQ_LEVEL_CHANGED && r == NEED_DQ_OK && gained == 30 && games == 1 && b.bg_pts == 30;
		t.check( "BG-A3", locked_ok && level_ok,
			"LOCKED_OUT (Lv160 vs locked 175) and LEVEL_CHANGED games are kept but not counted: the next valid game is the account's first (+30)" );

		// D2: judged at the official end, before any reward dialog can touch idletime
		s_need_dq_bg_match m = mk( 9 );
		m.type = "Tierra";
		m.m = 7;
		time_t end = m.started + 600;
		t.check( "BG-E1", need_dq_bg_validate( m, 3999851, 7, end, m.started - 30 ) == NEED_DQ_BG_NO_ACTION,
			"idle since before the start, judged at the end -> NO_ACTION (no reward close2 runs before the judge)" );
		t.check( "BG-E2", need_dq_bg_validate( m, 3999851, 7, end, m.started + 200 ) == NEED_DQ_BG_VALID,
			"acted during the battle, judged at the end -> VALID (no reward NPC needed)" );
		t.check( "BG-E4", need_dq_bg_validate( m, 3999851, 7, m.started + 300, m.started + 200 ) == NEED_DQ_BG_TOO_SHORT,
			"Tierra won at 5:00 -> TOO_SHORT at the official end, whenever the reward is taken" );

		// D2: one judgement per battle: the end removes the record, a second end does nothing
		dq_bg_matches["KVM"] = mk( 10 );
		int32 f1 = need_daily_quest_script_bg_finish( "KVM" );
		int32 f2 = need_daily_quest_script_bg_finish( "KVM" );
		t.check( "BG-E6", f1 >= 0 && f2 == -1 && dq_bg_matches.count( "KVM" ) == 0,
			"finish -> %d (players on the map), second finish -> %d, record removed", f1, f2 );

		// D5: a battle ending after 04:00 is judged on the new day; without a new accept nothing is written
		const s_need_dq_day next = dq_test_day( 2099, 1, 25 );
		char q[256];
		uint32 before_rows = 0;
		safesnprintf( q, sizeof( q ), "SELECT COUNT(*) FROM `need_dq_bg_match` WHERE `account_id`='%u'", a1.account_id );
		before_rows = dq_test_count( q );
		r = need_dq_core_bg_commit( next, a1, mk( 11 ), NEED_DQ_BG_VALID, now, gained, games );
		t.check( "BG-E8", r == NEED_DQ_NOT_ACCEPTED && dq_test_count( q ) == before_rows,
			"ended after 04:00, no accept on the new day -> %s, nothing written on either day", dq_test_rname( r ) );
	}

	// ---- I: instance clears (phase 4)
	{
		std::shared_ptr<s_need_dq_track> t175 = need_dq_db.find( 175 );
		std::shared_ptr<s_need_dq_track> t150 = need_dq_db.find( 150 );
		bool names = need_dq_inst_kind_id( "OGH_HARD" ) == NEED_DQ_INST_OGH_HARD && need_dq_inst_kind_id( "OGH" ) == NEED_DQ_INST_OGH &&
			need_dq_inst_kind_id( "SKY_FORTRESS" ) == NEED_DQ_INST_SKY_FORTRESS && need_dq_inst_kind_id( "ogh" ) == NEED_DQ_INST_NONE &&
			need_dq_inst_kind_id( "" ) == NEED_DQ_INST_NONE;
		t.check( "IV-1", names, "kind names OGH_HARD/OGH/SKY_FORTRESS -> 1/2/3, others unknown (case sensitive)" );
		bool pts = t175 != nullptr && t175->inst_cap == 40 &&
			need_dq_inst_points( *t175, {} ) == 0 &&
			need_dq_inst_points( *t175, { NEED_DQ_INST_OGH } ) == 15 &&
			need_dq_inst_points( *t175, { NEED_DQ_INST_OGH, NEED_DQ_INST_SKY_FORTRESS } ) == 30 &&
			need_dq_inst_points( *t175, { NEED_DQ_INST_OGH_HARD } ) == 40 &&
			need_dq_inst_points( *t175, { NEED_DQ_INST_OGH_HARD, NEED_DQ_INST_OGH, NEED_DQ_INST_SKY_FORTRESS } ) == 40;
		t.check( "IV-2", pts, "Lv175 points: none 0, OGH 15, OGH+Sky 30, hard 40, all three capped at 40" );
		t.check( "IV-3", t150 != nullptr && t150->inst_kinds.empty() && t150->inst_quest_id == 0 && need_dq_inst_points( *t150, { NEED_DQ_INST_OGH_HARD } ) == 0,
			"Lv150-174 has no instance activity (decision 2026-10-21: Lv175 only)" );
	}
	{
		const s_need_dq_day day = dq_test_weekday( 5, 3 );
		char q[256];
		uint16 cp = 0;
		auto rows = [&]( uint32 account_id ) {
			safesnprintf( q, sizeof( q ), "SELECT COUNT(*) FROM `need_dq_inst_clear` WHERE `day_key`='%s' AND `account_id`='%u'", day.key, account_id );
			return dq_test_count( q );
		};
		s_need_dq_actor nx = dq_test_actor( 3999701, 3999801, 175 );
		r = need_dq_core_inst_commit( day, nx, NEED_DQ_INST_OGH, 0, gained, cp );
		t.check( "IC-1", r == NEED_DQ_NOT_ACCEPTED && rows( nx.account_id ) == 0, "not accepted -> %s, no clear row", dq_test_rname( r ) );

		need_dq_core_accept( day, nx );
		r = need_dq_core_inst_commit( day, nx, NEED_DQ_INST_OGH, 9001, gained, cp );
		need_dq_read_account( day.key, nx.account_id, b );
		t.check( "IC-2", r == NEED_DQ_OK && gained == 15 && cp == 15 && b.inst_pts == 15 && b.locked_track == 175,
			"OGH clear -> %s +%d char %u inst %u locked %u", dq_test_rname( r ), gained, cp, b.inst_pts, b.locked_track );
		std::string before = dq_test_digest();
		r = need_dq_core_inst_commit( day, nx, NEED_DQ_INST_OGH, 9002, gained, cp );
		t.check( "IC-3", r == NEED_DQ_ALREADY && before == dq_test_digest(), "same character, same instance again -> %s, nothing written", dq_test_rname( r ) );
		r = need_dq_core_inst_commit( day, nx, NEED_DQ_INST_SKY_FORTRESS, 9003, gained, cp );
		need_dq_read_account( day.key, nx.account_id, b );
		t.check( "IC-4", r == NEED_DQ_OK && gained == 15 && cp == 30 && b.inst_pts == 30, "Sky Fortress -> +%d inst %u", gained, b.inst_pts );
		r = need_dq_core_inst_commit( day, nx, NEED_DQ_INST_OGH_HARD, 9004, gained, cp );
		need_dq_read_account( day.key, nx.account_id, b );
		t.check( "IC-5", r == NEED_DQ_OK && gained == 10 && cp == 40 && b.inst_pts == 40, "OGH hard on top -> +%d inst %u (cap 40)", gained, b.inst_pts );

		s_need_dq_actor nx2 = dq_test_actor( 3999701, 3999802, 175 );
		need_dq_core_accept( day, nx2 );
		r = need_dq_core_inst_commit( day, nx2, NEED_DQ_INST_OGH, 9005, gained, cp );
		need_dq_read_account( day.key, nx.account_id, b );
		t.check( "IC-6", r == NEED_DQ_NO_GAIN && cp == 15 && b.inst_pts == 40 && rows( nx.account_id ) == 4,
			"second character of the account clears OGH -> %s, its clear kept (4 rows), inst stays %u", dq_test_rname( r ), b.inst_pts );

		s_need_dq_actor low = dq_test_actor( 3999702, 3999803, 160 );
		need_dq_core_accept( day, low );
		r = need_dq_core_inst_commit( day, low, NEED_DQ_INST_OGH_HARD, 9006, gained, cp );
		t.check( "IC-7", r == NEED_DQ_NOT_ELIGIBLE && rows( low.account_id ) == 0, "Lv160 -> %s, no clear row", dq_test_rname( r ) );

		s_need_dq_actor lk = dq_test_actor( 3999703, 3999804, 160 );
		s_need_dq_actor lk2 = dq_test_actor( 3999703, 3999805, 175 );
		need_dq_core_accept( day, lk );
		need_dq_core_accept( day, lk2 );
		dq_test_collect( day, lk, 15 );
		need_dq_core_submit( day, lk, nullptr, gained );
		r = need_dq_core_inst_commit( day, lk2, NEED_DQ_INST_OGH_HARD, 9007, gained, cp );
		need_dq_read_account( day.key, lk2.account_id, b );
		t.check( "IC-8", r == NEED_DQ_LOCKED_OUT && b.inst_pts == 0 && b.locked_track == 150 && rows( lk2.account_id ) == 1,
			"account locked to 150 -> Lv175 clear %s, kept, inst %u", dq_test_rname( r ), b.inst_pts );

		s_need_dq_actor w = dq_test_actor( 3999704, 3999806, 175 );
		need_dq_core_accept( day, w );
		dq_test_hunt( day, w, 600, gained );
		dq_test_collect( day, w, 30 );
		need_dq_core_submit( day, w, nullptr, gained );
		need_dq_core_inst_commit( day, w, NEED_DQ_INST_OGH_HARD, 9008, gained, cp );
		need_dq_read_account( day.key, w.account_id, b );
		t.check( "IC-9", b.hunt_pts == 40 && b.delivery_pts == 20 && b.inst_pts == 40 && b.total_pts == 80,
			"Lv175 hunt 40 + delivery 20 + instance 40 -> total %u (cap 80)", b.total_pts );

		before = dq_test_digest();
		e_need_dq_result r0 = need_dq_core_inst_commit( day, w, NEED_DQ_INST_NONE, 0, gained, cp );
		e_need_dq_result r4 = need_dq_core_inst_commit( day, w, NEED_DQ_INST_KIND_MAX + 1, 0, gained, cp );
		t.check( "IC-10", r0 == NEED_DQ_ERROR && r4 == NEED_DQ_ERROR && before == dq_test_digest(), "kind 0 / 4 -> ERROR, nothing written" );

		s_need_dq_actor z = dq_test_actor( 3999705, 3999807, 174 );
		need_dq_core_accept( day, z );
		z.base_level = 175;
		r = need_dq_core_inst_commit( day, z, NEED_DQ_INST_OGH, 9009, gained, cp );
		t.check( "IC-11", r == NEED_DQ_NOT_ELIGIBLE && rows( z.account_id ) == 0,
			"accepted at Lv174 (150-174 has no instances), cleared at 175 -> %s; re-accept needed", dq_test_rname( r ) );
	}

	// ---- CL: cash claim (v1.5 Phase 5-Lite)
	{
		const s_need_dq_day day = dq_test_weekday( 5, 2 );
		const char* ip = "10.9.9.9";
		char q[320];
		auto status_of = [&]( uint32 account_id, uint16 tier ) -> int32 {
			safesnprintf( q, sizeof( q ), "SELECT COUNT(*) FROM `need_dq_claim` WHERE `day_key`='%s' AND `account_id`='%u' AND `tier`='%u'", day.key, account_id, tier );
			if( dq_test_count( q ) == 0 )
				return -1;
			safesnprintf( q, sizeof( q ), "SELECT `status` FROM `need_dq_claim` WHERE `day_key`='%s' AND `account_id`='%u' AND `tier`='%u'", day.key, account_id, tier );
			return static_cast<int32>( dq_test_count( q ) );
		};
		// family rows of the test accounts only (event 202699 = daily quest, 202609 = chuseok)
		dq_query_str( "DELETE FROM `need_summer_attendance_family_member` WHERE `account_id` BETWEEN 3999601 AND 3999609" );
		dq_query_str( "DELETE FROM `need_summer_attendance_family_group` WHERE `group_name`='dq_selftest'" );

		s_need_dq_actor a = dq_test_actor( 3999601, 3999611, 110 );
		need_dq_core_accept( day, a );
		r = need_dq_core_claim( day, a, 20, ip, 0 );
		t.check( "CL-1", r == NEED_DQ_CLAIM_NOT_ENOUGH && status_of( a.account_id, 20 ) == -1, "0 points, claim 20 -> %s, no row", dq_test_rname( r ) );

		dq_test_collect( day, a, 45 );
		need_dq_core_submit( day, a, nullptr, gained );   // delivery 30
		r = need_dq_core_claim( day, a, 20, ip, 0 );
		int32 st_reserved = status_of( a.account_id, 20 );
		bool fin = need_dq_claim_finish( day.key, a.account_id, 20, true );
		e_need_dq_result again = need_dq_core_claim( day, a, 20, ip, 0 );
		e_need_dq_result r40 = need_dq_core_claim( day, a, 40, ip, 0 );
		t.check( "CL-2", r == NEED_DQ_OK && st_reserved == 0 && fin && status_of( a.account_id, 20 ) == 1 && again == NEED_DQ_ALREADY && r40 == NEED_DQ_CLAIM_NOT_ENOUGH,
			"30 points: claim 20 -> OK (reserved, then granted); again -> %s; 40 -> %s", dq_test_rname( again ), dq_test_rname( r40 ) );

		s_need_dq_actor a2 = dq_test_actor( 3999601, 3999612, 120 );
		r = need_dq_core_claim( day, a2, 20, "10.9.9.10", 0 );
		t.check( "CL-3", r == NEED_DQ_ALREADY, "another character of the same account, other IP, tier 20 -> %s", dq_test_rname( r ) );

		dq_test_hunt( day, a, 2000, gained );   // hunt 50 -> total 80
		need_dq_read_account( day.key, a.account_id, b );
		bool all = true;
		for( uint16 tier : { 40, 60, 80 } ) {
			all = all && need_dq_core_claim( day, a, tier, ip, 0 ) == NEED_DQ_OK && need_dq_claim_finish( day.key, a.account_id, tier, true );
		}
		r = need_dq_core_claim( day, a, 100, ip, 0 );
		t.check( "CL-4", b.total_pts == 80 && all && r == NEED_DQ_CLAIM_BAD_TIER, "80 points: 40/60/80 each once (same IP, same account), tier 100 -> %s", dq_test_rname( r ) );

		s_need_dq_actor o = dq_test_actor( 3999602, 3999613, 110 );
		need_dq_core_accept( day, o );
		dq_test_collect( day, o, 45 );
		need_dq_core_submit( day, o, nullptr, gained );
		r = need_dq_core_claim( day, o, 20, ip, 0 );
		e_need_dq_result other_ip = need_dq_core_claim( day, o, 20, "10.9.9.11", 0 );
		need_dq_claim_finish( day.key, o.account_id, 20, true );
		t.check( "CL-5", r == NEED_DQ_CLAIM_IP_LIMIT && other_ip == NEED_DQ_OK,
			"second account on the same IP -> %s (honey songpyun rule), from another IP -> %s", dq_test_rname( r ), dq_test_rname( other_ip ) );

		// family exception: daily-quest family list (event 202699)
		s_need_dq_actor f = dq_test_actor( 3999603, 3999614, 110 );
		need_dq_core_accept( day, f );
		dq_test_collect( day, f, 45 );
		need_dq_core_submit( day, f, nullptr, gained );
		dq_query_str( "INSERT INTO `need_summer_attendance_family_group` (`event_id`,`group_name`,`active`,`approved_by`,`reason`) VALUES (202699,'dq_selftest',1,'selftest','selftest')" );
		uint32 gid = dq_test_count( "SELECT `family_group_id` FROM `need_summer_attendance_family_group` WHERE `group_name`='dq_selftest' ORDER BY `family_group_id` DESC LIMIT 1" );
		safesnprintf( q, sizeof( q ), "INSERT INTO `need_summer_attendance_family_member` (`event_id`,`account_id`,`family_group_id`,`active`,`approved_by`,`reason`) VALUES (202699,%u,%u,1,'selftest','selftest')", f.account_id, gid );
		dq_query_str( q );
		uint32 fam = need_dq_family_group( f.account_id );
		r = need_dq_core_claim( day, f, 20, ip, fam );
		need_dq_claim_finish( day.key, f.account_id, 20, true );
		t.check( "CL-6", gid > 0 && fam == gid && r == NEED_DQ_OK, "approved daily-quest family member on the taken IP -> %s (group %u)", dq_test_rname( r ), fam );

		// other events' approvals are not inherited
		s_need_dq_actor c = dq_test_actor( 3999604, 3999615, 110 );
		safesnprintf( q, sizeof( q ), "INSERT INTO `need_summer_attendance_family_member` (`event_id`,`account_id`,`family_group_id`,`active`,`approved_by`,`reason`) VALUES (202609,%u,%u,1,'selftest','selftest')", c.account_id, gid );
		dq_query_str( q );
		t.check( "CL-7", need_dq_family_group( c.account_id ) == 0, "a chuseok (202609) family member is not a daily-quest family member" );

		// reserved and not confirmed -> operator check, never paid again
		s_need_dq_actor p = dq_test_actor( 3999605, 3999616, 110 );
		need_dq_core_accept( day, p );
		dq_test_collect( day, p, 45 );
		need_dq_core_submit( day, p, nullptr, gained );
		r = need_dq_core_claim( day, p, 20, "10.9.9.12", 0 );
		e_need_dq_result pend = need_dq_core_claim( day, p, 20, "10.9.9.12", 0 );
		t.check( "CL-8", r == NEED_DQ_OK && pend == NEED_DQ_CLAIM_PENDING && status_of( p.account_id, 20 ) == 0,
			"reserved without a result -> next claim %s, row stays 0 (operator check)", dq_test_rname( pend ) );

		// certainly not granted -> may claim again on the same row
		bool not_granted = need_dq_claim_finish( day.key, p.account_id, 20, false );
		int32 st_failed = status_of( p.account_id, 20 );
		e_need_dq_result retry = need_dq_core_claim( day, p, 20, "10.9.9.12", 0 );
		char rows_q[256];
		safesnprintf( rows_q, sizeof( rows_q ), "SELECT COUNT(*) FROM `need_dq_claim` WHERE `day_key`='%s' AND `account_id`='%u'", day.key, p.account_id );
		t.check( "CL-9", not_granted && st_failed == 2 && retry == NEED_DQ_OK && status_of( p.account_id, 20 ) == 0 && dq_test_count( rows_q ) == 1,
			"not granted (2) -> claim again %s on the same row", dq_test_rname( retry ) );
		need_dq_claim_finish( day.key, p.account_id, 20, true );

		// another logical day: points and claims are per day
		const s_need_dq_day next = dq_test_day( 2099, 1, 17 );   // the day after dq_test_weekday( 5, 2 ) = 2099-01-16
		r = need_dq_core_claim( next, a, 20, ip, 0 );
		t.check( "CL-10", r == NEED_DQ_CLAIM_NOT_ENOUGH && status_of( a.account_id, 20 ) == 1,
			"next day without points -> %s; the earlier day keeps its claim", dq_test_rname( r ) );

		dq_query_str( "DELETE FROM `need_summer_attendance_family_member` WHERE `account_id` BETWEEN 3999601 AND 3999609" );
		dq_query_str( "DELETE FROM `need_summer_attendance_family_group` WHERE `group_name`='dq_selftest'" );
	}

	// ---- KST-1: day boundary at 04:00 KST (operator decision 2026-10-22), independent of the host time zone
	{
		const int64 saved = need_dq_test_offset;
		time_t real = time( nullptr );
		// next instant whose KST wall clock is 04:00:00
		time_t base = real + 9 * 3600 - 4 * 3600;
		time_t boundary = ( base / 86400 + 1 ) * 86400 - 9 * 3600 + 4 * 3600;
		s_need_dq_day before, after;
		need_dq_test_offset = static_cast<int64>( boundary - 30 - real );
		bool ok1 = need_dq_logical_day( before );
		need_dq_test_offset = static_cast<int64>( boundary + 30 - real );
		bool ok2 = need_dq_logical_day( after );
		need_dq_test_offset = saved;
		time_t day_after = boundary;   // UTC 19:00 of the previous calendar day = KST 04:00
		struct tm* k = gmtime( &day_after );
		char want[11] = {};
		time_t kst_after = day_after + 9 * 3600;
		struct tm* kk = gmtime( &kst_after );
		strftime( want, sizeof( want ), "%Y-%m-%d", kk );
		t.check( "KST-1", ok1 && ok2 && k != nullptr && strcmp( after.key, want ) == 0 && strcmp( before.key, after.key ) != 0 &&
			( before.wday + 1 ) % 7 == after.wday,
			"KST 03:59:30 -> %s (wday %d), KST 04:00:30 -> %s (wday %d)", before.key, before.wday, after.key, after.wday );
	}

	// ---- Global invariants (test plan 14 PASS rules)
	{
		uint32 bad = dq_test_count(
			"SELECT COUNT(*) FROM `need_dq_score_stage` WHERE `day_key`>='2099-01-01' AND NOT ("
			"(`track`=100 AND `base_level` BETWEEN 100 AND 129) OR (`track`=130 AND `base_level` BETWEEN 130 AND 149) OR "
			"(`track`=150 AND `base_level` BETWEEN 150 AND 174) OR (`track`=175 AND `base_level`=175))" );
		t.check( "INV-1", bad == 0, "stage rows whose track is not the real range of base_level: %u", bad );
		uint32 mismatch = dq_test_count(
			"SELECT COUNT(*) FROM `need_dq_score_stage` s JOIN `need_dq_account_day` b ON b.`day_key`=s.`day_key` AND b.`account_id`=s.`account_id` "
			"WHERE s.`day_key`>='2099-01-01' AND (b.`locked_track` IS NULL OR b.`locked_track`<>s.`track`)" );
		t.check( "INV-2", mismatch == 0, "stage rows whose track differs from the account lock: %u", mismatch );
		uint32 recompute = dq_test_count(
			"SELECT COUNT(*) FROM `need_dq_account_day` b WHERE b.`day_key`>='2099-01-01' AND b.`total_pts`<>LEAST(80,b.`hunt_pts`+b.`delivery_pts`+b.`bg_pts`+b.`inst_pts`)" );
		t.check( "INV-3", recompute == 0, "accounts whose total is not min(80, sum): %u", recompute );
	}

	// ---- QW: quest window entries (display only)
	{
		int32 checked = 0, ok = 0;
		for( const auto& entry : need_dq_db ) {
			for( const auto& dmap : entry.second->delivery_maps ) {
				checked++;
				std::shared_ptr<s_quest_db> qi = quest_search( dmap->quest_id );
				char label[NAME_LENGTH];
				if( qi != nullptr && qi->objectives.size() == 1 && qi->objectives[0]->mob_id == 0 &&
					qi->objectives[0]->count == need_dq_goal( *entry.second ) && qi->time > 0 &&
					need_dq_managed_quest( dmap->quest_id ) && need_daily_quest_objective_label( nullptr, dmap->quest_id, 0, label ) && label[0] != '\0' )
					ok++;
			}
		}
		for( const auto& entry : need_dq_db ) {
			checked++;
			std::shared_ptr<s_quest_db> qi = quest_search( entry.second->hunt_quest_id );
			char label[NAME_LENGTH];
			if( qi != nullptr && qi->objectives.size() == 1 && qi->objectives[0]->mob_id == 0 &&
				qi->objectives[0]->count == need_dq_hunt_goal( *entry.second ) && qi->time > 0 &&
				need_daily_quest_objective_label( nullptr, entry.second->hunt_quest_id, 0, label ) && label[0] != '\0' )
				ok++;
		}
		for( const auto& entry : need_dq_db ) {
			if( entry.second->bg_quest_id == 0 )
				continue;
			checked++;
			std::shared_ptr<s_quest_db> qi = quest_search( entry.second->bg_quest_id );
			char label[NAME_LENGTH];
			if( qi != nullptr && qi->objectives.size() == 1 && qi->objectives[0]->mob_id == 0 &&
				qi->objectives[0]->count == need_dq_bg_goal( *entry.second ) && qi->time > 0 &&
				need_daily_quest_objective_label( nullptr, entry.second->bg_quest_id, 0, label ) && label[0] != '\0' )
				ok++;
		}
		for( const auto& entry : need_dq_db ) {
			if( entry.second->inst_quest_id == 0 )
				continue;
			checked++;
			std::shared_ptr<s_quest_db> qi = quest_search( entry.second->inst_quest_id );
			char label[NAME_LENGTH];
			if( qi != nullptr && qi->objectives.size() == 1 && qi->objectives[0]->mob_id == 0 &&
				qi->objectives[0]->count == entry.second->inst_cap && qi->time > 0 &&
				need_daily_quest_objective_label( nullptr, entry.second->inst_quest_id, 0, label ) && label[0] != '\0' )
				ok++;
		}
		t.check( "QW-1", checked == 19 && ok == 19, "delivery + hunt + battleground + instance quests in quest_db with no Mob, goal count, 04:00 limit, label: %d / %d", ok, checked );
		char label[NAME_LENGTH];
		t.check( "QW-2", !need_daily_quest_objective_label( nullptr, 99001, 0, label ) && !need_daily_quest_objective_label( nullptr, 99101, 1, label ) &&
			!need_daily_quest_objective_label( nullptr, 12394, 0, label ), "no label for other quests or objectives" );
		t.check( "QW-3", need_dq_managed_quest( 99000 ) && need_dq_managed_quest( 99999 ) && !need_dq_managed_quest( 98999 ) && !need_dq_managed_quest( 100000 ),
			"auto-count exclusion range 99000-99999" );
	}

	// ---- T7-1: broken configurations are refused
	{
		static const char* broken[] = { "dq_broken_ver_eju.yml", "dq_broken_overlap.yml", "dq_broken_mob.yml", "dq_broken_questid.yml",
			"dq_broken_huntmap.yml", "dq_broken_huntquest.yml", "dq_broken_bgquest.yml",
			"dq_broken_instkind.yml", "dq_broken_instquest.yml" };
		for( const char* name : broken ) {
			NeedDailyQuestDatabase probe;
			probe.location_override = std::string( db_path ) + "/need/test/" + name;
			probe.load();
			char id[48];
			safesnprintf( id, sizeof( id ), "T7-1.%s", name );
			t.check( id, !probe.valid, "%s -> valid %d (expected 0)", name, probe.valid ? 1 : 0 );
		}
	}

	if( t.out != nullptr ) {
		fprintf( t.out, "RESULT %s  pass %d fail %d\n", t.fail == 0 ? "PASS" : "FAIL", t.pass, t.fail );
		fclose( t.out );
	}
	ShowStatus( "need_dq_selftest: pass %d fail %d\n", t.pass, t.fail );
	return t.fail == 0;
}

