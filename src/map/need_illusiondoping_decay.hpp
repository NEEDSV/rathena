// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#pragma once

#include <common/cbasetypes.hpp>
#include <common/timer.hpp> // t_tick

// NEED 2026-11-13 (BG balance request): battleground-only repeat decay for GN_ILLUSIONDOPING.
//
// The first successful application of SC_ILLUSIONDOPING keeps its original chance P0 (skill level,
// resistances, immunities - every existing check in status_change_start). Each further successful
// application on the SAME target inside the chain window multiplies the final chance by 1/2:
// P0, P0/2, P0/4, P0/8, ...
//
// The state lives on the TARGET (map_session_data), so every caster shares it - alternating Genetics
// cannot reset the chain. It is runtime-only and never saved: it is cleared on death, on every map
// change (leaving the battleground included) and starts empty for a new session.
//
// Only a successful application moves the chain: a cast, a hit, a failed roll or a resisted/immune
// attempt changes neither the stage nor the success time.

/// Time without a new successful application after which the next attempt starts again from P0.
/// 30 s is the request's proposed default; it is 3x the SC_ILLUSIONDOPING duration (10 s, Duration1)
/// and at least 6x every Illusion Doping cooldown (5..1 s), so the chain cannot be dropped by simply
/// letting one application expire.
constexpr t_tick NEED_ILLUSIONDOPING_DECAY_RESET = 30000;

/// Largest stage whose 1/2^stage gate is representable in a uint32 draw. A stage above it means a
/// chance below 1/2^31 and is rounded down to 0 (no minimum rate is ever added).
constexpr uint8 NEED_ILLUSIONDOPING_DECAY_MAX_SHIFT = 31;

struct s_need_illusiondoping_decay {
	uint8 stage = 0;          ///< successful applications in the current chain (saturates at 255)
	t_tick last_success = 0;  ///< tick of the last successful application (valid when stage > 0)
};

/// Stage to use for an attempt at `now`; drops the chain when the window has passed.
inline uint8 need_illusiondoping_decay_stage( s_need_illusiondoping_decay& state, t_tick now ){
	if( state.stage > 0 && now - state.last_success >= NEED_ILLUSIONDOPING_DECAY_RESET ){
		state = {};
	}
	return state.stage;
}

/// Record a successful application at `now`.
inline void need_illusiondoping_decay_success( s_need_illusiondoping_decay& state, t_tick now ){
	if( state.stage < UINT8_MAX ){
		state.stage++;
	}
	state.last_success = now;
}

/// Gate of the extra 1/2^stage factor. `draw` must be uniform in [1, 2^stage] when 0 < stage <= MAX_SHIFT
/// (it is not used otherwise). Returns true when the original roll may run.
inline bool need_illusiondoping_decay_gate( uint8 stage, uint32 draw ){
	if( stage == 0 ){
		return true;
	}
	if( stage > NEED_ILLUSIONDOPING_DECAY_MAX_SHIFT ){
		return false;
	}
	return draw == 1;
}
