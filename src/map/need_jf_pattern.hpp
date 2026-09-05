// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL

#ifndef NEED_JF_PATTERN_HPP
#define NEED_JF_PATTERN_HPP

#include <ctime>

#include <common/cbasetypes.hpp>
#include <common/timer.hpp>

class map_session_data;

// Message ids 1904~1987 are reserved by conf/msg_conf/map_msg_need_summer.conf and
// 1988~1995 by the pet autofeed command. 1996~1999 are the last free slots
// (MAP_MAX_MSG is 2000).
enum e_need_jf_pattern_message : int32 {
	NEED_JF_MSG_WARNING = 1996,
	NEED_JF_MSG_WARNING_HINT,
	NEED_JF_MSG_PENALTY_START,
	NEED_JF_MSG_PENALTY_BLOCKED,
};

/// Session state of the Jack Frost + Teleport hunting pattern guard.
/// The running pattern state lives in the session. Only two things are persisted:
/// the penalty deadline and repeat counter as account registry values (so relog or
/// char select cannot clear them), and the suspicion score as character registry
/// values written once at logout.
struct s_need_jf_pattern {
	t_tick last_skill_tick;		///< Last watched AoE skill cast
	t_tick window_start_tick;	///< Start of the running aggregation window
	t_tick last_pattern_tick;	///< Last completed skill->teleport pattern
	t_tick last_decay_tick;		///< Reference tick of the lazy suspicion decay
	t_tick avg_interval;		///< Smoothed interval between patterns in ms
	uint32 pattern_count;		///< Patterns inside the running window
	uint32 suspicion_score;
	uint32 penalty_count;		///< Cached account penalty counter
	time_t penalty_last;		///< Cached unix time of the last penalty
	time_t autoloot_until;		///< Cached unix time the penalty ends
	uint16 stage;				///< 0 none, 1 warned, 2 captcha issued, 3 penalty applied
	bool captcha_issued;		///< A captcha was raised in the current detection cycle; guards re-issuing
	bool score_stored;			///< A persisted score existed for this character at login
	bool loaded;				///< Registry values already read in this session
};

void need_jf_pattern_on_login( map_session_data& sd );
void need_jf_pattern_on_logout( map_session_data& sd );
void need_jf_pattern_record_skill( map_session_data& sd, uint16 skill_id );
void need_jf_pattern_record_teleport( map_session_data& sd );
void need_jf_pattern_on_captcha_success( map_session_data& sd );

/// Shared penalty check for every restricted feature (autoloot, world drop).
/// Cheap enough for the kill/drop path: one integer comparison on a cached deadline.
bool need_jf_pattern_penalty_active( map_session_data& sd );
/// Command guard: reports the remaining time and returns true when blocked.
bool need_jf_pattern_autoloot_guard( map_session_data& sd, int32 fd );
uint32 need_jf_pattern_penalty_remaining( map_session_data& sd );
void need_jf_pattern_status_report( map_session_data& sd, map_session_data& target );

#endif /* NEED_JF_PATTERN_HPP */
