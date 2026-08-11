// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// NEED summer event fishing system - implementation

#include "need_fishing.hpp"

#include <cstdio>
#include <cstring>
#include <unordered_map>

#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/strlib.hpp> // safestrncpy, safesnprintf
#include <common/timer.hpp>  // gettick, add_timer, delete_timer, DIFF_TICK

#include "battle.hpp" // battle_config
#include "clif.hpp"   // clif_displaymessage, clif_specialeffect
#include "map.hpp"    // map_id2sd, msg_txt
#include "pc.hpp"     // map_session_data, pc_addeventtimer, pc_setreg2

// Fishing msg_txt ids (conf/msg_conf/map_msg_need_summer.conf)
enum e_need_fishing_msg : int32 {
	MSG_FISH_NOT_FISHING   = 1956,
	MSG_FISH_ALREADY       = 1957,
	MSG_FISH_CAST          = 1958,
	MSG_FISH_WATCH         = 1959,
	MSG_FISH_BITE          = 1960,
	MSG_FISH_TOO_EARLY     = 1961,
	MSG_FISH_ESCAPED       = 1962,
	MSG_FISH_SUCCESS       = 1963, // first hook (stage 1 wording, kept)
	MSG_FISH_REACTION      = 1964, // "reaction time: %dms (%s)"
	MSG_FISH_GRADE_PERFECT = 1965,
	MSG_FISH_GRADE_GREAT   = 1966,
	MSG_FISH_GRADE_GOOD    = 1967,
	MSG_FISH_GRADE_NORMAL  = 1968,
	MSG_FISH_DISABLED      = 1969,
	MSG_FISH_CANCEL        = 1970,
	MSG_FISH_GRADE_FAIL    = 1971,
	MSG_FISH_INTERRUPTED   = 1972,
	// stage 2
	MSG_FISH_HOOKED        = 1973, // "the fish is hooked!"
	MSG_FISH_REEL_BITE     = 1974, // "it moves again! reel in now!"
	MSG_FISH_REEL_PROGRESS = 1975, // "reel %d / %d"
	MSG_FISH_REEL_EARLY    = 1976, // "pulled the line too early"
	MSG_FISH_REEL_TIMEOUT  = 1977, // "missed the reel timing"
	MSG_FISH_FINAL_SUCCESS = 1978, // "you landed the fish!"
	MSG_FISH_RESULT_FAIL   = 1979, // "could not process the fish result"
	MSG_FISH_INVALID_INFO  = 1980, // "invalid fish info"
};

// Visual cue effect (existing client effect, no new resource).
#ifndef NEED_FISHING_BITE_EFFECT
#define NEED_FISHING_BITE_EFFECT EF_WATERBALL
#endif

// Validation bounds for need_fishing_begin_reel / need_fishing_complete_catch.
static const int32 NEED_FISHING_MAX_LENGTH_MM = 100000000; // 100 km (sanity ceiling)
static const int32 NEED_FISHING_MAX_WEIGHT_G  = 1000000000; // 1000 t (sanity ceiling)
static const int32 NEED_FISHING_MAX_REEL      = 5;

static std::unordered_map<uint32, s_need_fishing_session> need_fishing_sessions;
static std::unordered_map<uint32, s_need_fishing_result> need_fishing_results;

static uint32 need_fishing_next_session_id = 1;

static TIMER_FUNC(need_fishing_bite_timer);
static TIMER_FUNC(need_fishing_deadline_timer);
static TIMER_FUNC(need_fishing_hooked_timer);
static TIMER_FUNC(need_fishing_reel_wait_timer);
static TIMER_FUNC(need_fishing_reel_deadline_timer);

bool need_fishing_is_enabled() {
	return battle_config.need_summer_fishing_enable != 0;
}

static s_need_fishing_session* need_fishing_get_session( uint32 char_id ) {
	auto it = need_fishing_sessions.find( char_id );
	return ( it != need_fishing_sessions.end() ) ? &it->second : nullptr;
}

// Delete only the timers that are actually pending in this session (id != INVALID_TIMER),
// then clear each field. A field is INVALID_TIMER whenever its timer has already fired
// (each callback clears its own field first), so no fired/reused id is ever delete_timer'd.
static void need_fishing_kill_timers( s_need_fishing_session& s ) {
	if( battle_config.need_summer_fishing_debug ) {
		ShowInfo( "need_fishing: cleanup session_id=%u bite=%d deadline=%d hooked=%d reel_wait=%d reel_deadline=%d\n",
			s.session_id, s.bite_timer, s.deadline_timer, s.hooked_timer, s.reel_wait_timer, s.reel_deadline_timer );
	}
	if( s.bite_timer != INVALID_TIMER ) { delete_timer( s.bite_timer, need_fishing_bite_timer ); s.bite_timer = INVALID_TIMER; }
	if( s.deadline_timer != INVALID_TIMER ) { delete_timer( s.deadline_timer, need_fishing_deadline_timer ); s.deadline_timer = INVALID_TIMER; }
	if( s.hooked_timer != INVALID_TIMER ) { delete_timer( s.hooked_timer, need_fishing_hooked_timer ); s.hooked_timer = INVALID_TIMER; }
	if( s.reel_wait_timer != INVALID_TIMER ) { delete_timer( s.reel_wait_timer, need_fishing_reel_wait_timer ); s.reel_wait_timer = INVALID_TIMER; }
	if( s.reel_deadline_timer != INVALID_TIMER ) { delete_timer( s.reel_deadline_timer, need_fishing_reel_deadline_timer ); s.reel_deadline_timer = INVALID_TIMER; }
}

static void need_fishing_msg( map_session_data* sd, int32 msg_number ) {
	if( sd != nullptr && msg_number > 0 )
		clif_displaymessage( sd->fd, msg_txt( sd, msg_number ) );
}

static int32 need_fishing_grade_msg( e_need_fishing_grade grade ) {
	switch( grade ) {
		case NEED_FISHING_GRADE_PERFECT: return MSG_FISH_GRADE_PERFECT;
		case NEED_FISHING_GRADE_GREAT:   return MSG_FISH_GRADE_GREAT;
		case NEED_FISHING_GRADE_GOOD:    return MSG_FISH_GRADE_GOOD;
		case NEED_FISHING_GRADE_NORMAL:  return MSG_FISH_GRADE_NORMAL;
		default:                         return MSG_FISH_GRADE_FAIL;
	}
}

static e_need_fishing_grade need_fishing_judge( int32 reaction_ms ) {
	if( reaction_ms <= battle_config.need_summer_fishing_perfect_ms ) return NEED_FISHING_GRADE_PERFECT;
	if( reaction_ms <= battle_config.need_summer_fishing_great_ms )   return NEED_FISHING_GRADE_GREAT;
	if( reaction_ms <= battle_config.need_summer_fishing_good_ms )    return NEED_FISHING_GRADE_GOOD;
	if( reaction_ms <= battle_config.need_summer_fishing_normal_ms )  return NEED_FISHING_GRADE_NORMAL;
	return NEED_FISHING_GRADE_FAIL;
}

/// Set all result script variables read by the result event.
static void need_fishing_set_vars( map_session_data* sd, int32 code, int32 reaction_ms, int32 grade,
	int32 spot_id, uint32 session_id, int32 fish_id, int32 rarity, int32 length_mm, int32 weight_g,
	int32 reel_total, int32 reel_success, int32 failed_round, t_tick final_tick )
{
	if( sd == nullptr )
		return;
	pc_setreg2( sd, "@need_fishing_result_code", (int64)code );
	pc_setreg2( sd, "@need_fishing_reaction_ms", (int64)reaction_ms );
	pc_setreg2( sd, "@need_fishing_reaction_grade", (int64)grade );
	pc_setreg2( sd, "@need_fishing_spot_id", (int64)spot_id );
	pc_setreg2( sd, "@need_fishing_session_id", (int64)session_id );
	pc_setreg2( sd, "@need_fishing_fish_id", (int64)fish_id );
	pc_setreg2( sd, "@need_fishing_rarity", (int64)rarity );
	pc_setreg2( sd, "@need_fishing_length_mm", (int64)length_mm );
	pc_setreg2( sd, "@need_fishing_weight_g", (int64)weight_g );
	pc_setreg2( sd, "@need_fishing_reel_total", (int64)reel_total );
	pc_setreg2( sd, "@need_fishing_reel_success", (int64)reel_success );
	pc_setreg2( sd, "@need_fishing_failed_round", (int64)failed_round );
	pc_setreg2( sd, "@need_fishing_final_tick", (int64)final_tick );
}

/// Print the finalize messages for a given result code (message-before-event ordering).
static void need_fishing_print_result( map_session_data* sd, e_need_fishing_result_code code, int32 reaction_ms, e_need_fishing_grade grade ) {
	if( sd == nullptr )
		return;
	bool show_reaction = false;
	switch( code ) {
		// Final success output (fish/length/weight/reward) is owned by the resolve script
		// (OnFishFinal); the reaction time is shown once at the first hook. So the C++ final
		// path stays silent for success (complete_catch finalizes with show_msg=false).
		case NEED_FISHING_RESULT_FINAL_SUCCESS: need_fishing_msg( sd, MSG_FISH_FINAL_SUCCESS ); break;
		case NEED_FISHING_RESULT_TIMEOUT:       need_fishing_msg( sd, MSG_FISH_ESCAPED ); show_reaction = true; break;
		case NEED_FISHING_RESULT_TOO_EARLY:     need_fishing_msg( sd, MSG_FISH_TOO_EARLY ); break;
		case NEED_FISHING_RESULT_USER_CANCEL:   need_fishing_msg( sd, MSG_FISH_CANCEL ); break;
		case NEED_FISHING_RESULT_ACTION_CANCEL: need_fishing_msg( sd, MSG_FISH_INTERRUPTED ); break;
		case NEED_FISHING_RESULT_REEL_TOO_EARLY: need_fishing_msg( sd, MSG_FISH_REEL_EARLY ); need_fishing_msg( sd, MSG_FISH_ESCAPED ); break;
		case NEED_FISHING_RESULT_REEL_TIMEOUT:  need_fishing_msg( sd, MSG_FISH_REEL_TIMEOUT ); need_fishing_msg( sd, MSG_FISH_ESCAPED ); break;
		case NEED_FISHING_RESULT_CATCH_REGISTER_FAIL:
		case NEED_FISHING_RESULT_EVENT_FAIL:
		case NEED_FISHING_RESULT_ERROR:         need_fishing_msg( sd, MSG_FISH_RESULT_FAIL ); break;
		default: break;
	}
	if( show_reaction && reaction_ms >= 0 ) {
		char buf[128];
		safesnprintf( buf, sizeof( buf ), msg_txt( sd, MSG_FISH_REACTION ), reaction_ms, msg_txt( sd, need_fishing_grade_msg( grade ) ) );
		clif_displaymessage( sd->fd, buf );
	}
}

/**
 * Single finalization path. Order (spec section 9/16):
 *   lookup -> not-locked check -> lock + FINALIZING -> kill timers
 *   -> store last result -> remove session -> set vars -> message -> event.
 * Uses final_result_event if registered, otherwise the start result_event.
 */
static void need_fishing_finalize( map_session_data* sd, uint32 char_id,
	e_need_fishing_result_code code, bool fire_event, bool show_msg )
{
	s_need_fishing_session* session = need_fishing_get_session( char_id );
	if( session == nullptr || session->result_locked )
		return;

	session->result_locked = true;
	session->state = NEED_FISHING_FINALIZING;
	need_fishing_kill_timers( *session );

	// snapshot
	int32 spot_id = session->spot_id;
	uint32 session_id = session->session_id;
	int32 reaction_ms = session->first_reaction_ms;
	e_need_fishing_grade grade = session->first_grade;
	int32 fish_id = session->fish_id;
	int32 rarity = session->rarity;
	int32 length_mm = session->length_mm;
	int32 weight_g = session->weight_g;
	int32 reel_total = session->reel_total;
	int32 reel_success = session->reel_success;
	int32 failed_round = session->reel_current;
	char event[EVENT_NAME_LENGTH];
	safestrncpy( event, ( session->final_result_event[0] != '\0' ) ? session->final_result_event : session->result_event, sizeof( event ) );
	t_tick now = gettick();

	// store last result (kept separately from the session)
	s_need_fishing_result& r = need_fishing_results[char_id];
	r.code = code;
	r.reaction_ms = reaction_ms;
	r.grade = grade;
	r.spot_id = spot_id;
	r.session_id = session_id;
	r.finalize_tick = now;
	r.fish_id = fish_id;
	r.rarity = rarity;
	r.length_mm = length_mm;
	r.weight_g = weight_g;
	r.reel_total = reel_total;
	r.reel_success = reel_success;

	need_fishing_sessions.erase( char_id );
	session = nullptr;

	need_fishing_set_vars( sd, code, reaction_ms, (int32)grade, spot_id, session_id,
		fish_id, rarity, length_mm, weight_g, reel_total, reel_success, failed_round, now );

	if( show_msg )
		need_fishing_print_result( sd, code, reaction_ms, grade );

	if( sd != nullptr && fire_event && event[0] != '\0' )
		pc_addeventtimer( sd, 100, event );

	if( battle_config.need_summer_fishing_debug )
		ShowInfo( "need_fishing: finalize char_id=%u session_id=%u code=%d fish=%d rarity=%d len=%d wt=%d reel=%d/%d\n",
			char_id, session_id, code, fish_id, rarity, length_mm, weight_g, reel_success, reel_total );
}

/// First hook succeeded: go to HOOKED and let the script register the fish.
static void need_fishing_first_hook( map_session_data* sd, s_need_fishing_session& session, int32 reaction_ms, e_need_fishing_grade grade ) {
	// The input arrived before the first-bite deadline: cancel that (still pending) timer
	// so it cannot fire later and leave a stale id in the session.
	if( session.deadline_timer != INVALID_TIMER ) {
		delete_timer( session.deadline_timer, need_fishing_deadline_timer );
		session.deadline_timer = INVALID_TIMER;
	}

	session.state = NEED_FISHING_HOOKED;
	session.first_reaction_ms = reaction_ms;
	session.first_grade = grade;

	int32 timeout = battle_config.need_summer_fishing_result_script_timeout_ms;
	session.hooked_timer = add_timer( gettick() + timeout, need_fishing_hooked_timer, sd->id, (intptr_t)session.session_id );

	// expose intermediate result to the (start) result event
	need_fishing_set_vars( sd, NEED_FISHING_RESULT_FIRST_HOOK, reaction_ms, (int32)grade, session.spot_id, session.session_id,
		0, 0, 0, 0, 0, 0, 0, gettick() );

	need_fishing_msg( sd, MSG_FISH_HOOKED );

	char buf[128];
	safesnprintf( buf, sizeof( buf ), msg_txt( sd, MSG_FISH_REACTION ), reaction_ms, msg_txt( sd, need_fishing_grade_msg( grade ) ) );
	clif_displaymessage( sd->fd, buf );

	if( session.result_event[0] != '\0' )
		pc_addeventtimer( sd, 100, session.result_event );

	if( battle_config.need_summer_fishing_debug )
		ShowInfo( "need_fishing: first hook char_id=%u session_id=%u reaction=%dms grade=%d message_emit=first_hook\n", session.char_id, session.session_id, reaction_ms, grade );
}

/// Start the next reel-in round (REEL_WAIT).
static void need_fishing_start_reel_round( map_session_data* sd, s_need_fishing_session& session ) {
	// Defensive: the previous round's timers must already be cleared before a new round.
	// (reel_wait fires and clears itself; reel_deadline is deleted on input.) Clear any
	// lingering pending timer so we never overwrite/lose a still-pending timer id.
	if( session.reel_wait_timer != INVALID_TIMER ) {
		delete_timer( session.reel_wait_timer, need_fishing_reel_wait_timer );
		session.reel_wait_timer = INVALID_TIMER;
	}
	if( session.reel_deadline_timer != INVALID_TIMER ) {
		delete_timer( session.reel_deadline_timer, need_fishing_reel_deadline_timer );
		session.reel_deadline_timer = INVALID_TIMER;
	}

	session.reel_round_id++;
	session.reel_current = session.reel_success + 1;
	session.state = NEED_FISHING_REEL_WAIT;

	int32 min_ms = battle_config.need_summer_fishing_reel_wait_min_ms;
	int32 max_ms = battle_config.need_summer_fishing_reel_wait_max_ms;
	if( max_ms < min_ms )
		max_ms = min_ms;
	int32 wait_ms = rnd_value<int32>( min_ms, max_ms );

	session.reel_wait_timer = add_timer( gettick() + wait_ms, need_fishing_reel_wait_timer, sd->id, (intptr_t)session.session_id );

	if( battle_config.need_summer_fishing_debug )
		ShowInfo( "need_fishing: reel round create session_id=%u reel_round=%d/%d round_id=%u reel_wait_timer=%d wait=%dms\n",
			session.session_id, session.reel_current, session.reel_total, session.reel_round_id, session.reel_wait_timer, wait_ms );
}

/// All reels are done: enter RESOLVING and let the script draw the fish (anti cherry-pick:
/// the fish is only decided now, never before/during the reels). The script must call
/// need_fishing_complete_catch within the script-processing timeout.
static void need_fishing_enter_resolving( map_session_data* sd, s_need_fishing_session& session ) {
	session.state = NEED_FISHING_RESOLVING;

	// arm the script-processing timeout (reuse hooked_timer; HOOKED already left)
	int32 timeout = battle_config.need_summer_fishing_result_script_timeout_ms;
	session.hooked_timer = add_timer( gettick() + timeout, need_fishing_hooked_timer, sd->id, (intptr_t)session.session_id );

	// expose the pre-fish context to the resolve event (fish data is NOT known yet)
	need_fishing_set_vars( sd, NEED_FISHING_RESULT_FIRST_HOOK, session.first_reaction_ms, (int32)session.first_grade,
		session.spot_id, session.session_id, 0, 0, 0, 0, session.reel_total, session.reel_success, 0, gettick() );

	if( session.resolve_event[0] != '\0' )
		pc_addeventtimer( sd, 100, session.resolve_event );

	if( battle_config.need_summer_fishing_debug )
		ShowInfo( "need_fishing: resolving session_id=%u reel=%d/%d\n", session.session_id, session.reel_success, session.reel_total );
}

e_need_fishing_state need_fishing_get_state( map_session_data* sd ) {
	if( sd == nullptr )
		return NEED_FISHING_IDLE;
	s_need_fishing_session* session = need_fishing_get_session( sd->status.char_id );
	return session != nullptr ? session->state : NEED_FISHING_IDLE;
}

bool need_fishing_start( map_session_data* sd, int32 spot_id, const char* result_event ) {
	if( sd == nullptr )
		return false;

	if( !need_fishing_is_enabled() ) {
		need_fishing_msg( sd, MSG_FISH_DISABLED );
		return false;
	}

	s_need_fishing_session* existing = need_fishing_get_session( sd->status.char_id );
	if( existing != nullptr && existing->state != NEED_FISHING_IDLE ) {
		need_fishing_msg( sd, MSG_FISH_ALREADY );
		return false;
	}

	s_need_fishing_session& session = need_fishing_sessions[sd->status.char_id];
	need_fishing_kill_timers( session );
	session = s_need_fishing_session();
	session.char_id = sd->status.char_id;
	session.session_id = need_fishing_next_session_id++;
	if( need_fishing_next_session_id == 0 )
		need_fishing_next_session_id = 1;
	session.spot_id = spot_id;
	session.state = NEED_FISHING_WAITING;
	session.session_start_tick = gettick();
	if( result_event != nullptr )
		safestrncpy( session.result_event, result_event, sizeof( session.result_event ) );

	int32 min_ms = battle_config.need_summer_fishing_bite_min_ms;
	int32 max_ms = battle_config.need_summer_fishing_bite_max_ms;
	if( max_ms < min_ms )
		max_ms = min_ms;
	int32 wait_ms = rnd_value<int32>( min_ms, max_ms );

	session.bite_timer = add_timer( gettick() + wait_ms, need_fishing_bite_timer, sd->id, (intptr_t)session.session_id );

	need_fishing_msg( sd, MSG_FISH_CAST );
	need_fishing_msg( sd, MSG_FISH_WATCH );

	if( battle_config.need_summer_fishing_debug )
		ShowInfo( "need_fishing: start char_id=%u session_id=%u spot=%d wait=%dms\n", session.char_id, session.session_id, spot_id, wait_ms );

	return true;
}

int32 need_fishing_input( map_session_data* sd ) {
	if( sd == nullptr )
		return NEED_FISHING_INPUT_NO_SESSION;

	if( !need_fishing_is_enabled() ) {
		need_fishing_msg( sd, MSG_FISH_DISABLED );
		return NEED_FISHING_INPUT_DISABLED;
	}

	s_need_fishing_session* session = need_fishing_get_session( sd->status.char_id );
	if( session == nullptr ) {
		need_fishing_msg( sd, MSG_FISH_NOT_FISHING );
		return NEED_FISHING_INPUT_NO_SESSION;
	}
	if( session->result_locked )
		return 0; // finalizing, ignore

	uint32 char_id = sd->status.char_id;

	switch( session->state ) {
		case NEED_FISHING_WAITING:
			// pulled before the first bite
			need_fishing_finalize( sd, char_id, NEED_FISHING_RESULT_TOO_EARLY, true, true );
			return NEED_FISHING_INPUT_TOO_EARLY;

		case NEED_FISHING_BITE: {
			int32 reaction_ms = (int32)DIFF_TICK( gettick(), session->bite_start_tick );
			if( reaction_ms < 0 )
				reaction_ms = 0;
			e_need_fishing_grade grade = need_fishing_judge( reaction_ms );
			session->first_reaction_ms = reaction_ms;
			session->first_grade = grade;
			if( grade == NEED_FISHING_GRADE_FAIL ) {
				need_fishing_finalize( sd, char_id, NEED_FISHING_RESULT_TIMEOUT, true, true );
			} else {
				need_fishing_first_hook( sd, *session, reaction_ms, grade );
			}
			return (int32)grade;
		}

		case NEED_FISHING_HOOKED:
			// waiting for the script to register the fish; ignore extra input
			return 0;

		case NEED_FISHING_REEL_WAIT:
			// pulled before the reel bite
			need_fishing_finalize( sd, char_id, NEED_FISHING_RESULT_REEL_TOO_EARLY, true, true );
			return NEED_FISHING_INPUT_TOO_EARLY;

		case NEED_FISHING_REEL_BITE: {
			if( session->reel_deadline_timer != INVALID_TIMER ) {
				delete_timer( session->reel_deadline_timer, need_fishing_reel_deadline_timer );
				session->reel_deadline_timer = INVALID_TIMER;
			}
			session->reel_success++;
			// No progress message here: the "reel N / TOTAL" line is owned by the reel_wait
			// timer (shown once when each round's bite starts). Re-printing it on a successful
			// input caused the duplicated "reel N / TOTAL" seen in the client.

			if( session->reel_success >= session->reel_total )
				need_fishing_enter_resolving( sd, *session ); // all reels done: script draws the fish now
			else
				need_fishing_start_reel_round( sd, *session );
			return NEED_FISHING_INPUT_REEL_OK;
		}

		default:
			return 0;
	}
}

bool need_fishing_begin_reel( map_session_data* sd, int32 reel_rounds, const char* resolve_event ) {
	if( sd == nullptr )
		return false;

	s_need_fishing_session* session = need_fishing_get_session( sd->status.char_id );
	if( session == nullptr || session->result_locked || session->state != NEED_FISHING_HOOKED )
		return false; // only valid right after the first hook

	uint32 char_id = sd->status.char_id;

	// reel count is decided by the script, independent of any fish (anti cherry-pick)
	if( reel_rounds < 0 || reel_rounds > NEED_FISHING_MAX_REEL || resolve_event == nullptr || resolve_event[0] == '\0' ) {
		need_fishing_msg( sd, MSG_FISH_INVALID_INFO );
		need_fishing_finalize( sd, char_id, NEED_FISHING_RESULT_CATCH_REGISTER_FAIL, true, false );
		return false;
	}

	// stop the HOOKED script-processing timeout (a new one is armed on RESOLVING)
	if( session->hooked_timer != INVALID_TIMER ) {
		delete_timer( session->hooked_timer, need_fishing_hooked_timer );
		session->hooked_timer = INVALID_TIMER;
	}

	// fish fields stay 0 until complete_catch; they must not be known during the reels
	session->fish_id = 0;
	session->rarity = 0;
	session->length_mm = 0;
	session->weight_g = 0;
	session->reel_total = reel_rounds;
	session->reel_current = 0;
	session->reel_success = 0;
	safestrncpy( session->resolve_event, resolve_event, sizeof( session->resolve_event ) );

	if( reel_rounds == 0 )
		need_fishing_enter_resolving( sd, *session );
	else
		need_fishing_start_reel_round( sd, *session );

	if( battle_config.need_summer_fishing_debug )
		ShowInfo( "need_fishing: begin_reel char_id=%u reel=%d\n", char_id, reel_rounds );

	return true;
}

bool need_fishing_complete_catch( map_session_data* sd, int32 fish_id, int32 rarity, int32 length_mm, int32 weight_g, const char* final_event ) {
	if( sd == nullptr )
		return false;

	s_need_fishing_session* session = need_fishing_get_session( sd->status.char_id );
	if( session == nullptr || session->result_locked || session->state != NEED_FISHING_RESOLVING )
		return false; // only valid once all reels are done and the script is resolving

	uint32 char_id = sd->status.char_id;

	// validate the drawn fish data
	bool valid = fish_id > 0
		&& rarity >= NEED_FISHING_RARITY_COMMON && rarity <= NEED_FISHING_RARITY_LEGEND
		&& length_mm > 0 && length_mm <= NEED_FISHING_MAX_LENGTH_MM
		&& weight_g > 0 && weight_g <= NEED_FISHING_MAX_WEIGHT_G
		&& final_event != nullptr && final_event[0] != '\0';

	if( !valid ) {
		need_fishing_msg( sd, MSG_FISH_INVALID_INFO );
		need_fishing_finalize( sd, char_id, NEED_FISHING_RESULT_CATCH_REGISTER_FAIL, true, false );
		return false;
	}

	// stop the RESOLVING script-processing timeout
	if( session->hooked_timer != INVALID_TIMER ) {
		delete_timer( session->hooked_timer, need_fishing_hooked_timer );
		session->hooked_timer = INVALID_TIMER;
	}

	session->fish_id = fish_id;
	session->rarity = rarity;
	session->length_mm = length_mm;
	session->weight_g = weight_g;
	safestrncpy( session->final_result_event, final_event, sizeof( session->final_result_event ) );

	// show_msg=false: the final-success user output (fish name/length/weight/reward) is owned by
	// the resolve script (OnFishFinal). Avoids duplicating the reaction/"landed" line already
	// shown at the first hook.
	need_fishing_finalize( sd, char_id, NEED_FISHING_RESULT_FINAL_SUCCESS, true, false );

	if( battle_config.need_summer_fishing_debug )
		ShowInfo( "need_fishing: complete_catch char_id=%u fish=%d rarity=%d len=%d wt=%d\n", char_id, fish_id, rarity, length_mm, weight_g );

	return true;
}

bool need_fishing_cancel( map_session_data* sd, bool notify ) {
	if( sd == nullptr )
		return false;
	s_need_fishing_session* session = need_fishing_get_session( sd->status.char_id );
	if( session == nullptr || session->result_locked )
		return false;
	need_fishing_finalize( sd, sd->status.char_id, NEED_FISHING_RESULT_USER_CANCEL, true, notify );
	return true;
}

void need_fishing_notify_action( map_session_data* sd ) {
	if( sd == nullptr )
		return;
	s_need_fishing_session* session = need_fishing_get_session( sd->status.char_id );
	if( session == nullptr || session->result_locked )
		return;
	need_fishing_finalize( sd, sd->status.char_id, NEED_FISHING_RESULT_ACTION_CANCEL, false, true );
}

void need_fishing_clear( map_session_data* sd ) {
	if( sd == nullptr )
		return;
	s_need_fishing_session* session = need_fishing_get_session( sd->status.char_id );
	if( session == nullptr )
		return;
	need_fishing_finalize( sd, sd->status.char_id, NEED_FISHING_RESULT_ACTION_CANCEL, false, false );
}

void need_fishing_free( map_session_data* sd ) {
	if( sd == nullptr )
		return;
	uint32 char_id = sd->status.char_id;
	auto it = need_fishing_sessions.find( char_id );
	if( it != need_fishing_sessions.end() ) {
		need_fishing_kill_timers( it->second );
		need_fishing_sessions.erase( it );
	}
	need_fishing_results.erase( char_id );
}

const s_need_fishing_result* need_fishing_last_result( map_session_data* sd ) {
	if( sd == nullptr )
		return nullptr;
	auto it = need_fishing_results.find( sd->status.char_id );
	return ( it != need_fishing_results.end() ) ? &it->second : nullptr;
}

void need_fishing_final() {
	for( auto& pair : need_fishing_sessions )
		need_fishing_kill_timers( pair.second );
	need_fishing_sessions.clear();
	need_fishing_results.clear();
}

// All fishing timer callbacks follow the same order (see spec):
//   map_id2sd(id) -> session lookup -> session_id check -> stored id == tid check
//   -> clear the field to INVALID_TIMER FIRST -> lock/state check -> process.
// Clearing the field immediately after confirming this callback owns the fired timer
// guarantees a fired (auto-freed) timer id never lingers in the session, so it can never
// be delete_timer()'d again after the timer subsystem reuses that id (the cause of the
// "delete_timer error: function mismatch" reports).

/// WAITING -> BITE
static TIMER_FUNC(need_fishing_bite_timer) {
	map_session_data* sd = map_id2sd( id );
	if( sd == nullptr )
		return 0;
	s_need_fishing_session* session = need_fishing_get_session( sd->status.char_id );
	if( session == nullptr || session->session_id != (uint32)data || tid != session->bite_timer )
		return 0;

	session->bite_timer = INVALID_TIMER; // fired: clear before any state check

	if( session->result_locked || session->state != NEED_FISHING_WAITING )
		return 0;

	session->state = NEED_FISHING_BITE;
	session->bite_start_tick = tick;

	int32 window_ms = battle_config.need_summer_fishing_input_window_ms;
	session->input_deadline_tick = tick + window_ms;
	session->deadline_timer = add_timer( session->input_deadline_tick, need_fishing_deadline_timer, sd->id, (intptr_t)session->session_id );

	clif_specialeffect( sd, NEED_FISHING_BITE_EFFECT, AREA );
	need_fishing_msg( sd, MSG_FISH_BITE );
	return 0;
}

/// BITE timeout (no first input in time)
static TIMER_FUNC(need_fishing_deadline_timer) {
	map_session_data* sd = map_id2sd( id );
	if( sd == nullptr )
		return 0;
	s_need_fishing_session* session = need_fishing_get_session( sd->status.char_id );
	if( session == nullptr || session->session_id != (uint32)data || tid != session->deadline_timer )
		return 0;

	session->deadline_timer = INVALID_TIMER; // fired: clear before any state check

	if( session->result_locked || session->state != NEED_FISHING_BITE )
		return 0;

	// no input -> first_reaction stays -1
	need_fishing_finalize( sd, sd->status.char_id, NEED_FISHING_RESULT_TIMEOUT, true, true );
	return 0;
}

/// HOOKED -> script did not register the fish in time
static TIMER_FUNC(need_fishing_hooked_timer) {
	map_session_data* sd = map_id2sd( id );
	if( sd == nullptr )
		return 0;
	s_need_fishing_session* session = need_fishing_get_session( sd->status.char_id );
	if( session == nullptr || session->session_id != (uint32)data || tid != session->hooked_timer )
		return 0;

	session->hooked_timer = INVALID_TIMER; // fired: clear before any state check

	// used for both the HOOKED (waiting for begin_reel) and RESOLVING (waiting for
	// complete_catch) script-processing windows.
	if( session->result_locked || ( session->state != NEED_FISHING_HOOKED && session->state != NEED_FISHING_RESOLVING ) )
		return 0;

	need_fishing_finalize( sd, sd->status.char_id, NEED_FISHING_RESULT_CATCH_REGISTER_FAIL, true, true );
	return 0;
}

/// REEL_WAIT -> REEL_BITE
static TIMER_FUNC(need_fishing_reel_wait_timer) {
	map_session_data* sd = map_id2sd( id );
	if( sd == nullptr )
		return 0;
	s_need_fishing_session* session = need_fishing_get_session( sd->status.char_id );
	if( session == nullptr || session->session_id != (uint32)data || tid != session->reel_wait_timer )
		return 0;

	session->reel_wait_timer = INVALID_TIMER; // fired: clear before any state check

	if( battle_config.need_summer_fishing_debug )
		ShowInfo( "need_fishing: reel wait fired session_id=%u round_id=%u tid=%d message_emit=reel_wait\n", session->session_id, session->reel_round_id, tid );

	if( session->result_locked || session->state != NEED_FISHING_REEL_WAIT )
		return 0;

	session->state = NEED_FISHING_REEL_BITE;
	session->bite_start_tick = tick;

	int32 window_ms = battle_config.need_summer_fishing_reel_input_window_ms;
	session->input_deadline_tick = tick + window_ms;
	session->reel_deadline_timer = add_timer( session->input_deadline_tick, need_fishing_reel_deadline_timer, sd->id, (intptr_t)session->session_id );

	if( battle_config.need_summer_fishing_debug )
		ShowInfo( "need_fishing: reel deadline session_id=%u round_id=%u tid=%d\n", session->session_id, session->reel_round_id, session->reel_deadline_timer );

	clif_specialeffect( sd, NEED_FISHING_BITE_EFFECT, AREA );
	need_fishing_msg( sd, MSG_FISH_REEL_BITE );

	char buf[128];
	safesnprintf( buf, sizeof( buf ), msg_txt( sd, MSG_FISH_REEL_PROGRESS ), session->reel_current, session->reel_total );
	clif_displaymessage( sd->fd, buf );
	return 0;
}

/// REEL_BITE timeout (missed the reel input)
static TIMER_FUNC(need_fishing_reel_deadline_timer) {
	map_session_data* sd = map_id2sd( id );
	if( sd == nullptr )
		return 0;
	s_need_fishing_session* session = need_fishing_get_session( sd->status.char_id );
	if( session == nullptr || session->session_id != (uint32)data || tid != session->reel_deadline_timer )
		return 0;

	session->reel_deadline_timer = INVALID_TIMER; // fired: clear before any state check

	if( session->result_locked || session->state != NEED_FISHING_REEL_BITE )
		return 0;

	need_fishing_finalize( sd, sd->status.char_id, NEED_FISHING_RESULT_REEL_TIMEOUT, true, true );
	return 0;
}
