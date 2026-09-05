// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL

#include "need_jf_pattern.hpp"

#include <cstdio>
#include <cstring>

#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "battle.hpp"
#include "chrif.hpp"
#include "clif.hpp"
#include "map.hpp"
#include "pc.hpp"

namespace {

// Account scoped registry variables. A '#' prefix stores them per account, so a
// char select or relog cannot drop an active penalty.
constexpr const char* NEED_JF_VAR_AUTOLOOT_UNTIL = "#NeedJfAlUntil";
constexpr const char* NEED_JF_VAR_PENALTY_COUNT = "#NeedJfPenaltyCnt";
constexpr const char* NEED_JF_VAR_PENALTY_LAST = "#NeedJfPenaltyAt";

// Character scoped registry variables (no '#'). The pattern itself is tracked per
// character, so the suspicion score must not leak into the other characters of the
// account. These are written once, at logout.
constexpr const char* NEED_JF_VAR_SCORE = "NeedJfScore";
constexpr const char* NEED_JF_VAR_SCORE_STAGE = "NeedJfScoreStage";
constexpr const char* NEED_JF_VAR_SCORE_AT = "NeedJfScoreAt";

// The stage registry packs the diagnostic captcha flag into the high byte so the
// restore does not need a fourth variable.
constexpr int64 NEED_JF_STAGE_CAPTCHA_BIT = 0x100;

bool need_jf_pattern_enabled() {
	return battle_config.need_jf_pattern_enable != 0;
}

void need_jf_pattern_load( map_session_data& sd ) {
	s_need_jf_pattern& jf = sd.need_jf;

	if( jf.loaded )
		return;

	jf.autoloot_until = static_cast<time_t>( pc_readreg2( &sd, NEED_JF_VAR_AUTOLOOT_UNTIL ) );
	jf.penalty_count = static_cast<uint32>( pc_readreg2( &sd, NEED_JF_VAR_PENALTY_COUNT ) );
	jf.penalty_last = static_cast<time_t>( pc_readreg2( &sd, NEED_JF_VAR_PENALTY_LAST ) );
	jf.loaded = true;
}

uint32 need_jf_pattern_remaining( map_session_data& sd ) {
	need_jf_pattern_load( sd );

	if( sd.need_jf.autoloot_until <= last_tick )
		return 0;

	return static_cast<uint32>( sd.need_jf.autoloot_until - last_tick );
}

/// Clear the persisted score. Called when the score reaches zero so stale registry
/// rows do not pile up for characters that stopped being suspicious.
void need_jf_pattern_clear_stored_score( map_session_data& sd ) {
	if( !sd.need_jf.score_stored )
		return;

	pc_setreg2( &sd, NEED_JF_VAR_SCORE, 0 );
	pc_setreg2( &sd, NEED_JF_VAR_SCORE_STAGE, 0 );
	pc_setreg2( &sd, NEED_JF_VAR_SCORE_AT, 0 );
	sd.need_jf.score_stored = false;
}

/**
 * Records a state change to the main SQL database.
 * Mirrors the macro detector logging contract: only state changes are stored and a
 * failing query must never interrupt the calling flow.
 */
void need_jf_pattern_log( map_session_data& sd, const char* event, uint32 penalty_duration ) {
	if( mmysql_handle == nullptr )
		return;

	const s_need_jf_pattern& jf = sd.need_jf;
	const map_data* mapdata = sd.m >= 0 ? map_getmapdata( sd.m ) : nullptr;
	const char* map_name = mapdata != nullptr ? mapdata->name : "";
	char esc_char_name[NAME_LENGTH * 2 + 1];
	char esc_map[MAP_NAME_LENGTH_EXT * 2 + 1];
	char esc_event[32 * 2 + 1];

	Sql_EscapeString( mmysql_handle, esc_char_name, sd.status.name );
	Sql_EscapeString( mmysql_handle, esc_map, map_name );
	Sql_EscapeString( mmysql_handle, esc_event, event );

	// A penalty always restricts both features for the same period, so both flags
	// are stored and an operator can read the effect straight from the log row.
	const int32 blocked = penalty_duration > 0 ? 1 : 0;

	if( SQL_ERROR == Sql_Query( mmysql_handle,
		"INSERT INTO `need_jf_pattern_log` "
		"(`account_id`, `char_id`, `char_name`, `map`, `x`, `y`, `event`, `pattern_count`, `suspicion_score`, `stage`, `penalty_count`, `penalty_duration`, `autoloot_blocked`, `world_drop_blocked`, `created_at`) "
		"VALUES ('%d', '%d', '%s', '%s', '%d', '%d', '%s', '%u', '%u', '%hu', '%u', '%u', '%d', '%d', NOW())",
		sd.status.account_id, sd.status.char_id, esc_char_name, esc_map, sd.x, sd.y, esc_event,
		jf.pattern_count, jf.suspicion_score, jf.stage, jf.penalty_count, penalty_duration, blocked, blocked ) ) {
		Sql_ShowDebug( mmysql_handle );
	}
}

void need_jf_pattern_debug_log( const map_session_data& sd, const char* event, t_tick gap ) {
	if( battle_config.need_jf_pattern_debug == 0 )
		return;

	const s_need_jf_pattern& jf = sd.need_jf;
	const map_data* mapdata = sd.m >= 0 ? map_getmapdata( sd.m ) : nullptr;

	ShowInfo( "NeedJfPattern: aid=%d cid=%d map=%s x=%d y=%d event=%s gap_ms=%lld avg_ms=%lld pattern_count=%u score=%u stage=%hu penalty_count=%u\n",
		sd.status.account_id, sd.status.char_id, mapdata != nullptr ? mapdata->name : "", sd.x, sd.y, event,
		static_cast<long long>( gap ), static_cast<long long>( jf.avg_interval ),
		jf.pattern_count, jf.suspicion_score, jf.stage, jf.penalty_count );
}

/// Drops the stage back to whatever the current score justifies, so a decayed or
/// reduced score can trigger the lower stages again.
///
/// Falling below the warning threshold ends the current detection cycle: the captcha
/// flag is cleared so a later repeat is challenged again instead of jumping straight
/// to the penalty. Above that threshold the flag is kept, which is what stops a score
/// oscillating around the captcha threshold from raising a captcha over and over.
void need_jf_pattern_sync_stage( s_need_jf_pattern& jf ) {
	const uint32 cycle_reset = battle_config.need_jf_warn_score > 0 ?
		static_cast<uint32>( battle_config.need_jf_warn_score ) :
		static_cast<uint32>( battle_config.need_jf_captcha_score );

	if( cycle_reset > 0 && jf.suspicion_score < cycle_reset ) {
		jf.stage = 0;
		jf.captcha_issued = false;
		return;
	}

	if( jf.stage > 1 && battle_config.need_jf_captcha_score > 0 &&
		jf.suspicion_score < static_cast<uint32>( battle_config.need_jf_captcha_score ) )
		jf.stage = 1;
}

/// Lazy sliding decay. No per character timer exists; the score is aged whenever the
/// player produces an event or an operator inspects the state.
void need_jf_pattern_decay( map_session_data& sd, t_tick now ) {
	s_need_jf_pattern& jf = sd.need_jf;
	const int32 interval = battle_config.need_jf_decay_interval;

	if( interval <= 0 )
		return;

	if( jf.last_decay_tick == 0 || jf.suspicion_score == 0 ) {
		jf.last_decay_tick = now;
		return;
	}

	const t_tick elapsed = DIFF_TICK( now, jf.last_decay_tick );

	if( elapsed < interval )
		return;

	const uint32 steps = static_cast<uint32>( elapsed / interval );
	const uint32 amount = steps * static_cast<uint32>( battle_config.need_jf_decay_score );

	jf.suspicion_score = amount >= jf.suspicion_score ? 0 : jf.suspicion_score - amount;
	jf.last_decay_tick += static_cast<t_tick>( steps ) * interval;
	need_jf_pattern_sync_stage( jf );
}

void need_jf_pattern_disable_autoloot( map_session_data& sd ) {
	sd.state.autoloot = 0;
	sd.state.autoloottype = 0;
	memset( sd.state.autolootid, 0, sizeof( sd.state.autolootid ) );
	sd.state.autolooting = 0;
}

void need_jf_pattern_apply_penalty( map_session_data& sd ) {
	need_jf_pattern_load( sd );

	s_need_jf_pattern& jf = sd.need_jf;
	const time_t now = last_tick;
	uint32 count = jf.penalty_count;

	// The repeat counter ages out so a single bad session does not follow the account forever.
	if( battle_config.need_jf_penalty_count_reset > 0 && jf.penalty_last > 0 &&
		now - jf.penalty_last >= battle_config.need_jf_penalty_count_reset )
		count = 0;

	count++;

	const int32 duration = count <= 1 ? battle_config.need_jf_autoloot_penalty1 :
		count == 2 ? battle_config.need_jf_autoloot_penalty2 : battle_config.need_jf_autoloot_penalty3;

	if( duration <= 0 )
		return;

	const time_t until = now + duration;

	jf.penalty_count = count;
	jf.penalty_last = now;

	// Never shorten a penalty that is already running.
	if( until > jf.autoloot_until )
		jf.autoloot_until = until;

	jf.stage = 3;

	uint32 reset_score = static_cast<uint32>( battle_config.need_jf_penalty_reset_score );

	// A reset score at or above the penalty threshold would re-trigger immediately,
	// so a misconfiguration cannot turn one penalty into an endless chain.
	if( battle_config.need_jf_penalty_score > 0 && reset_score >= static_cast<uint32>( battle_config.need_jf_penalty_score ) )
		reset_score = static_cast<uint32>( battle_config.need_jf_penalty_score ) - 1;

	jf.suspicion_score = reset_score;

	// The only database writes of this feature happen here, when a penalty is real.
	pc_setreg2( &sd, NEED_JF_VAR_AUTOLOOT_UNTIL, static_cast<int64>( jf.autoloot_until ) );
	pc_setreg2( &sd, NEED_JF_VAR_PENALTY_COUNT, static_cast<int64>( count ) );
	pc_setreg2( &sd, NEED_JF_VAR_PENALTY_LAST, static_cast<int64>( now ) );

	need_jf_pattern_disable_autoloot( sd );

	// Flush the registry immediately so a crash between here and the next periodic
	// save cannot hand the penalty back.
	chrif_save( &sd, CSAVE_NORMAL );

	const uint32 remaining = need_jf_pattern_remaining( sd );
	char output[CHAT_SIZE_MAX];

	safesnprintf( output, sizeof( output ), msg_txt( &sd, NEED_JF_MSG_PENALTY_START ), remaining / 60, remaining % 60 );
	clif_displaymessage( sd.fd, output );

	need_jf_pattern_log( sd, "penalty", static_cast<uint32>( duration ) );
	need_jf_pattern_debug_log( sd, "penalty", 0 );
}

void need_jf_pattern_evaluate( map_session_data& sd ) {
	s_need_jf_pattern& jf = sd.need_jf;

	if( battle_config.need_jf_penalty_score > 0 && jf.suspicion_score >= static_cast<uint32>( battle_config.need_jf_penalty_score ) ) {
		need_jf_pattern_apply_penalty( sd );
		return;
	}

	// One captcha per detection cycle. The flag - not the stage - is the guard, so a
	// score drifting just under and over the threshold cannot re-issue it; only a drop
	// below the warning threshold starts a new cycle (see need_jf_pattern_sync_stage).
	if( !jf.captcha_issued && battle_config.need_jf_captcha_score > 0 && jf.suspicion_score >= static_cast<uint32>( battle_config.need_jf_captcha_score ) ) {
		jf.stage = 2;
		jf.captcha_issued = true;
		need_jf_pattern_log( sd, "captcha", 0 );
		need_jf_pattern_debug_log( sd, "captcha", 0 );
		// Reward-less captcha: the reason is what suppresses the bonus script on success.
		pc_macro_reporter_process( sd, -1, MACRO_CAPTCHA_REASON_JF_PATTERN );
		return;
	}

	if( jf.stage < 1 && battle_config.need_jf_warn_score > 0 && jf.suspicion_score >= static_cast<uint32>( battle_config.need_jf_warn_score ) ) {
		jf.stage = 1;
		clif_displaymessage( sd.fd, msg_txt( &sd, NEED_JF_MSG_WARNING ) );
		clif_displaymessage( sd.fd, msg_txt( &sd, NEED_JF_MSG_WARNING_HINT ) );
		need_jf_pattern_log( sd, "warning", 0 );
		need_jf_pattern_debug_log( sd, "warning", 0 );
	}
}

/**
 * A skill to teleport pair completed. Counting pairs alone would also hit legitimate
 * players, so the score only grows while the pairs are frequent, repeated and sustained.
 */
void need_jf_pattern_on_pattern( map_session_data& sd, t_tick now ) {
	s_need_jf_pattern& jf = sd.need_jf;

	// While a penalty is running the character is already restricted. Stop scoring so
	// the stages only escalate when the pattern comes back after the penalty ended.
	if( jf.autoloot_until > last_tick ) {
		need_jf_pattern_debug_log( sd, "pattern_penalty_active", 0 );
		return;
	}

	const int32 window = battle_config.need_jf_pattern_window;

	if( jf.window_start_tick == 0 || ( window > 0 && DIFF_TICK( now, jf.window_start_tick ) > window ) ) {
		jf.window_start_tick = now;
		jf.pattern_count = 0;
		jf.avg_interval = 0;
		jf.last_pattern_tick = 0;
	}

	const t_tick gap = jf.last_pattern_tick != 0 ? DIFF_TICK( now, jf.last_pattern_tick ) : 0;

	jf.last_pattern_tick = now;
	jf.pattern_count++;

	if( gap > 0 ) {
		// Smoothed average so a single pause does not reset the picture and a single
		// fast pair does not create one.
		jf.avg_interval = jf.avg_interval != 0 ? ( jf.avg_interval * 3 + gap ) / 4 : gap;
	}

	const bool repeated = battle_config.need_jf_pattern_min_count <= 0 ||
		jf.pattern_count >= static_cast<uint32>( battle_config.need_jf_pattern_min_count );
	const bool fast = jf.avg_interval > 0 && ( battle_config.need_jf_pattern_max_avg_interval <= 0 ||
		jf.avg_interval <= battle_config.need_jf_pattern_max_avg_interval );
	const bool sustained = battle_config.need_jf_pattern_min_duration <= 0 ||
		DIFF_TICK( now, jf.window_start_tick ) >= battle_config.need_jf_pattern_min_duration;

	if( !repeated || !fast || !sustained ) {
		need_jf_pattern_debug_log( sd, "pattern_ignored", gap );
		return;
	}

	jf.suspicion_score += static_cast<uint32>( battle_config.need_jf_pattern_score );
	need_jf_pattern_debug_log( sd, "pattern_scored", gap );
	need_jf_pattern_evaluate( sd );
}

} // namespace

/**
 * Restore the persisted state when the character enters the map server:
 * the account scoped penalty and the character scoped suspicion score.
 * @param sd: Player data
 */
void need_jf_pattern_on_login( map_session_data& sd ) {
	need_jf_pattern_load( sd );

	s_need_jf_pattern& jf = sd.need_jf;

	if( jf.autoloot_until > last_tick )
		need_jf_pattern_disable_autoloot( sd );

	const int64 stored_score = pc_readreg2( &sd, NEED_JF_VAR_SCORE );

	if( stored_score <= 0 )
		return;

	jf.score_stored = true;

	const int64 stored_stage = pc_readreg2( &sd, NEED_JF_VAR_SCORE_STAGE );
	const time_t stored_at = static_cast<time_t>( pc_readreg2( &sd, NEED_JF_VAR_SCORE_AT ) );
	uint32 score = static_cast<uint32>( stored_score );

	// Offline decay uses the same configuration as the online decay; there is no
	// separate offline rule. Elapsed seconds are converted to decay steps.
	if( stored_at > 0 && stored_at < last_tick && battle_config.need_jf_decay_interval > 0 &&
		battle_config.need_jf_decay_score > 0 ) {
		const int64 elapsed_ms = static_cast<int64>( last_tick - stored_at ) * 1000;
		const int64 steps = elapsed_ms / battle_config.need_jf_decay_interval;
		const int64 amount = steps * battle_config.need_jf_decay_score;

		score = amount >= static_cast<int64>( score ) ? 0 : score - static_cast<uint32>( amount );
	}

	if( score == 0 ) {
		need_jf_pattern_clear_stored_score( sd );
		return;
	}

	jf.suspicion_score = score;
	jf.stage = static_cast<uint16>( stored_stage & 0xFF );
	jf.captcha_issued = ( stored_stage & NEED_JF_STAGE_CAPTCHA_BIT ) != 0;
	jf.last_decay_tick = gettick();

	// The thresholds may have been changed by an operator while the character was
	// offline, so re-align the stage with the restored score.
	need_jf_pattern_sync_stage( jf );

	need_jf_pattern_debug_log( sd, "score_restored", 0 );
}

/**
 * Persist the suspicion score at logout. This is the only write of this feature
 * outside of an actual penalty, and it happens once per session.
 * @param sd: Player data
 */
void need_jf_pattern_on_logout( map_session_data& sd ) {
	s_need_jf_pattern& jf = sd.need_jf;

	need_jf_pattern_decay( sd, gettick() );

	if( jf.suspicion_score == 0 ) {
		need_jf_pattern_clear_stored_score( sd );
		return;
	}

	pc_setreg2( &sd, NEED_JF_VAR_SCORE, static_cast<int64>( jf.suspicion_score ) );
	pc_setreg2( &sd, NEED_JF_VAR_SCORE_STAGE,
		static_cast<int64>( jf.stage ) | ( jf.captcha_issued ? NEED_JF_STAGE_CAPTCHA_BIT : 0 ) );
	pc_setreg2( &sd, NEED_JF_VAR_SCORE_AT, static_cast<int64>( last_tick ) );
	jf.score_stored = true;

	need_jf_pattern_debug_log( sd, "score_stored", 0 );
}

/**
 * Remember the cast of a watched area of effect skill.
 * @param sd: Player data
 * @param skill_id: Skill that finished casting
 */
void need_jf_pattern_record_skill( map_session_data& sd, uint16 skill_id ) {
	if( !need_jf_pattern_enabled() || battle_config.need_jf_pattern_skill_id <= 0 )
		return;

	if( skill_id != static_cast<uint16>( battle_config.need_jf_pattern_skill_id ) )
		return;

	const t_tick now = gettick();

	need_jf_pattern_decay( sd, now );
	sd.need_jf.last_skill_tick = now;
}

/**
 * A random teleport happened. Only a teleport that closely follows the watched skill
 * completes the pattern.
 * @param sd: Player data
 */
void need_jf_pattern_record_teleport( map_session_data& sd ) {
	if( !need_jf_pattern_enabled() )
		return;

	s_need_jf_pattern& jf = sd.need_jf;

	if( jf.last_skill_tick == 0 )
		return;

	const t_tick now = gettick();
	const t_tick since_skill = DIFF_TICK( now, jf.last_skill_tick );

	// One cast can only complete one pattern.
	jf.last_skill_tick = 0;

	need_jf_pattern_decay( sd, now );

	if( battle_config.need_jf_teleport_window > 0 && since_skill > battle_config.need_jf_teleport_window )
		return;

	need_jf_pattern_on_pattern( sd, now );
}

/**
 * Reduce, but never clear, the suspicion after a solved captcha.
 * A human solving one captcha does not prove that the following hours are manual.
 * @param sd: Player data
 */
void need_jf_pattern_on_captcha_success( map_session_data& sd ) {
	s_need_jf_pattern& jf = sd.need_jf;

	if( jf.suspicion_score == 0 )
		return;

	const uint32 target = static_cast<uint32>( battle_config.need_jf_captcha_success_score );

	if( jf.suspicion_score > target )
		jf.suspicion_score = target;

	jf.last_decay_tick = gettick();
	need_jf_pattern_sync_stage( jf );
	need_jf_pattern_log( sd, "captcha_success", 0 );
	need_jf_pattern_debug_log( sd, "captcha_success", 0 );
}

/**
 * Remaining penalty time in seconds.
 * @param sd: Player data
 * @return Seconds left, 0 when no penalty is active
 */
uint32 need_jf_pattern_penalty_remaining( map_session_data& sd ) {
	return need_jf_pattern_remaining( sd );
}

/**
 * Shared penalty check for autoloot and world drop.
 * @param sd: Player data
 * @return True while the restricted features are denied
 */
bool need_jf_pattern_penalty_active( map_session_data& sd ) {
	// The deadline is cached at login, so the hot path is a single comparison.
	return sd.need_jf.autoloot_until > last_tick;
}

/**
 * Guard for every command and script that can enable autoloot.
 * @param sd: Player data
 * @param fd: Session file descriptor for the notice
 * @return True when the caller has to abort
 */
bool need_jf_pattern_autoloot_guard( map_session_data& sd, int32 fd ) {
	const uint32 remaining = need_jf_pattern_remaining( sd );

	if( remaining == 0 )
		return false;

	if( fd > 0 ) {
		char output[CHAT_SIZE_MAX];

		safesnprintf( output, sizeof( output ), msg_txt( &sd, NEED_JF_MSG_PENALTY_BLOCKED ), remaining / 60, remaining % 60 );
		clif_displaymessage( fd, output );
	}

	return true;
}

/**
 * Operator readout of the pattern state of a character.
 * @param sd: Requesting player
 * @param target: Inspected player
 */
void need_jf_pattern_status_report( map_session_data& sd, map_session_data& target ) {
	s_need_jf_pattern& jf = target.need_jf;
	char output[CHAT_SIZE_MAX];

	need_jf_pattern_load( target );
	need_jf_pattern_decay( target, gettick() );

	safesnprintf( output, sizeof( output ), "Character: %s", target.status.name );
	clif_displaymessage( sd.fd, output );
	safesnprintf( output, sizeof( output ), "Recent JF-Teleport: %u", jf.pattern_count );
	clif_displaymessage( sd.fd, output );
	safesnprintf( output, sizeof( output ), "Average Interval: %lld ms", static_cast<long long>( jf.avg_interval ) );
	clif_displaymessage( sd.fd, output );
	safesnprintf( output, sizeof( output ), "Suspicion Score: %u", jf.suspicion_score );
	clif_displaymessage( sd.fd, output );
	safesnprintf( output, sizeof( output ), "Warning Stage: %hu", jf.stage );
	clif_displaymessage( sd.fd, output );
	safesnprintf( output, sizeof( output ), "Captcha Triggered: %s", jf.captcha_issued ? "Yes" : "No" );
	clif_displaymessage( sd.fd, output );

	const uint32 remaining = need_jf_pattern_remaining( target );
	const bool blocked = remaining > 0;

	safesnprintf( output, sizeof( output ), "Penalty: %s", blocked ? "ACTIVE" : "NONE" );
	clif_displaymessage( sd.fd, output );
	safesnprintf( output, sizeof( output ), "Remaining: %u sec", remaining );
	clif_displaymessage( sd.fd, output );
	safesnprintf( output, sizeof( output ), "Autoloot: %s", blocked ? "BLOCKED" : "OK" );
	clif_displaymessage( sd.fd, output );
	safesnprintf( output, sizeof( output ), "World Drop: %s", blocked ? "BLOCKED" : "OK" );
	clif_displaymessage( sd.fd, output );
	safesnprintf( output, sizeof( output ), "Penalty Count: %u", jf.penalty_count );
	clif_displaymessage( sd.fd, output );
}
