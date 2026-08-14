// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// NEED summer event fishing system
// - Stage 1: technical validation of timing input via the existing item-use
//   packet (no new client packet / no client modification).
// - Follow-up: session/last-result separation, finalize locking, result event
//   interface, action-based cancel policy, visual cue.
// - Stage 2 content: first hook -> script draws the fish -> optional reel-in
//   rounds -> final result. Fish data / probabilities live in NPC scripts.

#ifndef NEED_FISHING_HPP
#define NEED_FISHING_HPP

#include <common/cbasetypes.hpp>
#include <common/mmo.hpp> // EVENT_NAME_LENGTH
#include <common/timer.hpp> // t_tick, INVALID_TIMER

class map_session_data;

/// Current fishing session state (see need_fishing_state builtin)
enum e_need_fishing_state : uint8 {
	NEED_FISHING_IDLE = 0,       ///< no active session
	NEED_FISHING_WAITING = 1,    ///< rod cast, waiting for a bite
	NEED_FISHING_BITE = 2,       ///< bite happened, waiting for the first hook input
	NEED_FISHING_HOOKED = 3,     ///< first hook succeeded, waiting for the script to register the fish
	NEED_FISHING_REEL_WAIT = 4,  ///< reel round: waiting for the fish to move
	NEED_FISHING_REEL_BITE = 5,  ///< reel round: waiting for the reel input
	NEED_FISHING_FINALIZING = 6, ///< result being finalized (transient lock)
	NEED_FISHING_RESOLVING = 7,  ///< all reels done, waiting for the script to draw the fish
};

/// Reaction time grade (also used as script value)
enum e_need_fishing_grade : uint8 {
	NEED_FISHING_GRADE_NONE = 0,
	NEED_FISHING_GRADE_PERFECT = 1,
	NEED_FISHING_GRADE_GREAT = 2,
	NEED_FISHING_GRADE_GOOD = 3,
	NEED_FISHING_GRADE_NORMAL = 4,
	NEED_FISHING_GRADE_FAIL = 5,
};

/// Fish rarity (validated in need_fishing_complete_catch)
enum e_need_fishing_rarity : uint8 {
	NEED_FISHING_RARITY_COMMON = 1,   ///< common
	NEED_FISHING_RARITY_UNCOMMON = 2, ///< uncommon
	NEED_FISHING_RARITY_RARE = 3,     ///< rare
	NEED_FISHING_RARITY_LEGEND = 4,   ///< legend
};

/// Result code exposed to the result event (@need_fishing_result_code)
enum e_need_fishing_result_code : int32 {
	NEED_FISHING_RESULT_NONE = 0,               ///< no result yet
	NEED_FISHING_RESULT_FIRST_HOOK = 1,         ///< first hook succeeded (intermediate)
	NEED_FISHING_RESULT_TOO_EARLY = 2,          ///< input before the first bite
	NEED_FISHING_RESULT_TIMEOUT = 3,            ///< first bite: no/late input, fish escaped
	NEED_FISHING_RESULT_USER_CANCEL = 4,        ///< cancelled via builtin/NPC
	NEED_FISHING_RESULT_ACTION_CANCEL = 5,      ///< cancelled by an action (move/attack/warp/...)
	NEED_FISHING_RESULT_DISABLED = 6,           ///< feature disabled
	NEED_FISHING_RESULT_ERROR = 7,              ///< internal error
	NEED_FISHING_RESULT_FINAL_SUCCESS = 8,      ///< the fish was landed
	NEED_FISHING_RESULT_REEL_TOO_EARLY = 9,     ///< reel input before the reel bite
	NEED_FISHING_RESULT_REEL_TIMEOUT = 10,      ///< reel round timed out
	NEED_FISHING_RESULT_CATCH_REGISTER_FAIL = 11, ///< script did not register the fish in time / invalid data
	NEED_FISHING_RESULT_EVENT_FAIL = 12,        ///< result event could not be dispatched
};

/// need_fishing_input return codes (script). Non-negative values are grades.
enum e_need_fishing_input_result : int32 {
	NEED_FISHING_INPUT_DISABLED   = -3,
	NEED_FISHING_INPUT_NO_SESSION = -2,
	NEED_FISHING_INPUT_TOO_EARLY  = -1,
	// 0..5 : e_need_fishing_grade (first hook), or 100 for an accepted reel input
};

/// Special need_fishing_input return for an accepted reel-round input.
#define NEED_FISHING_INPUT_REEL_OK 100

/// Current fishing session (exists only while actively fishing)
struct s_need_fishing_session {
	e_need_fishing_state state = NEED_FISHING_IDLE;
	uint32 char_id = 0;
	uint32 session_id = 0;
	int32 spot_id = 0;

	t_tick session_start_tick = 0;
	t_tick bite_start_tick = 0;      ///< tick when the current bite (first or reel) started
	t_tick input_deadline_tick = 0;

	// first-stage timers
	int32 bite_timer = INVALID_TIMER;      ///< WAITING -> BITE
	int32 deadline_timer = INVALID_TIMER;  ///< BITE -> first timeout
	int32 hooked_timer = INVALID_TIMER;    ///< HOOKED -> script registration timeout

	// events
	char result_event[EVENT_NAME_LENGTH] = { 0 };       ///< fired on first hook / early terminal fail
	char resolve_event[EVENT_NAME_LENGTH] = { 0 };      ///< fired after all reels (script draws the fish here)
	char final_result_event[EVENT_NAME_LENGTH] = { 0 }; ///< fired on final success / reel fail (set via complete_catch)

	// first hook reaction (reported as the reaction of the final result)
	int32 first_reaction_ms = -1;
	e_need_fishing_grade first_grade = NEED_FISHING_GRADE_NONE;

	// fish info registered by the script (need_fishing_complete_catch, only at RESOLVING)
	int32 fish_id = 0;
	int32 rarity = 0;
	int32 length_mm = 0;
	int32 weight_g = 0;

	// reel-in progress
	int32 reel_total = 0;               ///< required extra reel rounds
	int32 reel_current = 0;             ///< rounds started
	int32 reel_success = 0;             ///< rounds succeeded
	uint32 reel_round_id = 0;           ///< increments each round (timer re-validation)
	int32 reel_wait_timer = INVALID_TIMER;
	int32 reel_deadline_timer = INVALID_TIMER;

	bool result_locked = false; ///< set once the result is being finalized
};

/// Last finalized result (kept after the session ends, until logout / next finalize)
struct s_need_fishing_result {
	e_need_fishing_result_code code = NEED_FISHING_RESULT_NONE;
	int32 reaction_ms = -1;
	e_need_fishing_grade grade = NEED_FISHING_GRADE_NONE;
	int32 spot_id = 0;
	uint32 session_id = 0;
	t_tick finalize_tick = 0;

	// stage-2 fish info
	int32 fish_id = 0;
	int32 rarity = 0;
	int32 length_mm = 0;
	int32 weight_g = 0;
	int32 reel_total = 0;
	int32 reel_success = 0;
};

// Feature toggle (battle_config)
bool need_fishing_is_enabled();

// Current session state (IDLE if none)
e_need_fishing_state need_fishing_get_state( map_session_data* sd );

// Start a session. Returns true on success. result_event may be nullptr/"".
bool need_fishing_start( map_session_data* sd, int32 spot_id, const char* result_event );

// Handle a timing input (from the item script). Returns e_need_fishing_input_result / grade / reel-ok.
int32 need_fishing_input( map_session_data* sd );

// Begin the reel-in phase while HOOKED. reel_rounds is decided independently by the script
// (not from the fish). 0 goes straight to RESOLVING. resolve_event fires once all reels succeed.
bool need_fishing_begin_reel( map_session_data* sd, int32 reel_rounds, const char* resolve_event );

// Complete the catch while RESOLVING: the script has drawn the fish. Finalizes as success and
// fires final_event (OnFishFinal). Fish data is only known/valid at this point.
bool need_fishing_complete_catch( map_session_data* sd, int32 fish_id, int32 rarity, int32 length_mm, int32 weight_g, const char* final_event );

// User/NPC cancel. Returns true if an active session was cancelled.
bool need_fishing_cancel( map_session_data* sd, bool notify );

// Action-based cancel (walk / attack / skill ...). Records ACTION_CANCEL, keeps last result.
void need_fishing_notify_action( map_session_data* sd );

// Silent session cleanup for map change / warp / death (keeps last result).
void need_fishing_clear( map_session_data* sd );

// Full cleanup on logout / disconnect (removes session AND last result).
void need_fishing_free( map_session_data* sd );

// Last finalized result (nullptr if none).
const s_need_fishing_result* need_fishing_last_result( map_session_data* sd );

// Server shutdown / reload cleanup.
void need_fishing_final();

#endif /* NEED_FISHING_HPP */
