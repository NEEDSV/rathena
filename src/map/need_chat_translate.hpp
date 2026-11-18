// NEED Chat Translate v1 - server-side KR <-> EN chat translation (map-server half)
// Licensed under GNU GPL - see LICENCE in the main folder
//
// The map-server never talks to an external service. It only:
//   1. lets ONE local relay process (need-translate-relay) connect to a 127.0.0.1 listener,
//   2. after a chat line was already delivered and logged the normal way, decides whether any
//      online recipient wants a translation of it, and if so writes one small request frame,
//   3. when the relay answers, re-checks who may still see that chat (same map area / party /
//      guild / battleground / clan / channel / chat room) and sends each of them one extra
//      0x2C1 line with id 0, "[TL] Speaker : ..." (EN) or its Korean form (KR).
// Everything runs on the map-server main loop (no threads, no blocking sockets). Any failure
// on the relay side only means "no translation line"; the original chat is never touched.

#ifndef NEED_CHAT_TRANSLATE_HPP
#define NEED_CHAT_TRANSLATE_HPP

#include <common/cbasetypes.hpp>

class map_session_data;
struct Channel;

enum e_need_ct_chan : uint8 {
	NCT_SAY = 0,		// nearby public chat (AREA_CHAT_WOC)
	NCT_CHATROOM,		// public chat said inside a chat room (CHAT_WOS)
	NCT_CHANNEL,		// #channel chat
	NCT_PARTY,
	NCT_GUILD,
	NCT_BG,				// battleground team chat
	NCT_CLAN,			// NEED '~' clan chat
	NCT_CHAN_MAX
};

// relay protocol (little endian, rAthena framing: cmd.W len.W ...)
#define NCT_PROTO_VERSION     1
#define NCT_CMD_HELLO         0x7B00	// relay -> map : proto.W pad.W token.32B
#define NCT_CMD_HELLO_ACK     0x7B01	// map -> relay : proto.W result.W reserved.L
#define NCT_CMD_REQ           0x7B10	// map -> relay : req_id.L chan.B src.B tgt.B flags.B speaker_cid.L deadline_ms.L text.?B
#define NCT_CMD_RES           0x7B11	// relay -> map : req_id.L status.B flags.B reserved.W text.?B
#define NCT_CMD_STATUS        0x7B20	// relay -> map : enabled.B reason.B pad.W remaining_s.L day_micro_usd.L total_micro_usd.L msg.?B
#define NCT_CMD_ALERT         0x7B21	// relay -> map : level.B pad.3B msg.?B
#define NCT_CMD_STATS         0x7B30	// map -> relay : version.W count.W value.L * count

#define NCT_REQ_HEADER   20
#define NCT_RES_HEADER   12
#define NCT_STATUS_HEADER 20
#define NCT_ALERT_HEADER 8
#define NCT_TOKEN_LEN    32

// relay answer status (RES)
enum e_need_ct_status : uint8 {
	NCT_ST_OK = 0,			// translated now
	NCT_ST_CACHED,			// served from the relay cache (no API call)
	NCT_ST_SKIP,			// relay prefilter: nothing to translate
	NCT_ST_QUOTA,			// relay rate limit
	NCT_ST_BUDGET,			// budget cap reached
	NCT_ST_DISABLED,		// relay paused / pilot not running
	NCT_ST_ERROR,			// API error, validation failure ...
	NCT_ST_TIMEOUT,
	NCT_ST_REFUSED,
	NCT_ST_MAX
};

// counters reported to the relay every minute (deltas). Order is part of the protocol.
enum e_need_ct_stat : uint16 {
	NCT_STAT_MSG_SAY = 0,
	NCT_STAT_MSG_CHATROOM,
	NCT_STAT_MSG_CHANNEL,
	NCT_STAT_MSG_PARTY,
	NCT_STAT_MSG_GUILD,
	NCT_STAT_MSG_BG,
	NCT_STAT_MSG_CLAN,
	NCT_STAT_ELIGIBLE,
	NCT_STAT_SKIP_NO_RECIPIENT,
	NCT_STAT_SKIP_SENDER_BLOCK,
	NCT_STAT_SKIP_SENDER_QUOTA,
	NCT_STAT_SKIP_NO_LANG,
	NCT_STAT_SKIP_COMMAND,
	NCT_STAT_SKIP_INFLIGHT,
	NCT_STAT_SKIP_SERVICE_OFF,
	NCT_STAT_REQUESTS,
	NCT_STAT_RES_OK,
	NCT_STAT_RES_OTHER,
	NCT_STAT_EXPIRED,
	NCT_STAT_LINES,
	NCT_STAT_DELIVERY_EMPTY,
	NCT_STAT_TOGGLE_ON,
	NCT_STAT_TOGGLE_OFF,
	NCT_STAT_BLOCK_ON,
	NCT_STAT_BLOCK_OFF,
	NCT_STAT_HOOK_US_TOTAL,
	NCT_STAT_HOOK_US_MAX,
	NCT_STAT_HOOK_CALLS,
	NCT_STAT_RECIPIENTS_KR,		// unique chars that got >= 1 line this period
	NCT_STAT_RECIPIENTS_EN,
	NCT_STAT_SUBSCRIBERS_KR,	// snapshot: online chars with translation on, by display language
	NCT_STAT_SUBSCRIBERS_EN,
	NCT_STAT_ONLINE,			// snapshot: online chars
	NCT_STAT_RTT_MS_TOTAL,		// map-observed request -> answer time
	NCT_STAT_RTT_COUNT,
	NCT_STAT_MAX
};

void do_init_need_chat_translate( void );
void do_final_need_chat_translate( void );

/**
 * Called AFTER a chat line has been delivered and logged by the normal code path.
 * @param name: the speaker name exactly as the original line showed it (fakename aware)
 * @param body: the message text without the "name : " part (client code page, CP949)
 * @param channel: only for NCT_CHANNEL
 */
void need_ct_on_chat( map_session_data* sd, e_need_ct_chan chan, const char* name, const char* body, const Channel* channel = nullptr );

// command bodies (registered as ACMD wrappers in atcommand.cpp)
int32 need_ct_cmd_translate( int32 fd, map_session_data* sd, const char* message );
int32 need_ct_cmd_block( int32 fd, map_session_data* sd, const char* message );
int32 need_ct_cmd_admin( int32 fd, map_session_data* sd, const char* message );

#endif /* NEED_CHAT_TRANSLATE_HPP */
