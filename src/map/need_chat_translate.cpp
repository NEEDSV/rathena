// NEED Chat Translate v1 - server-side KR <-> EN chat translation (map-server half)
// Licensed under GNU GPL - see LICENCE in the main folder
//
// See need_chat_translate.hpp for the design. Rules this file keeps:
// - nothing here runs before the original chat was sent and logged (callers hook at the end),
// - no thread, no blocking call, no outbound connection: the relay connects IN to a 127.0.0.1
//   listener that only exists while battle_config.need_chat_translate is on and the port is free,
// - a translated line is only ever a SELF 0x2C1 packet with id 0; it never re-enters the chat
//   pipeline (no chatlog, no NPC listener, no command parser, no second translation),
// - recipients are re-checked when the answer arrives, from the CURRENT membership.

#include "need_chat_translate.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef WIN32
	#include <common/winapi.hpp>
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

#include <common/cbasetypes.hpp>
#include <common/db.hpp>
#include <common/nullpo.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "battle.hpp"
#include "battleground.hpp"
#include "channel.hpp"
#include "chat.hpp"
#include "clan.hpp"
#include "clif.hpp"
#include "guild.hpp"
#include "intif.hpp"
#include "map.hpp"
#include "party.hpp"
#include "pc.hpp"
#include "pc_groups.hpp"

#include "need_chat_translate_strings.inc"

// ---------------------------------------------------------------- registry keys (char_reg_num)
// NeedTrRecv : 0 = never set (server default), 1 = on, 2 = off
// NeedTrLang : 0 = auto (client language), 1 = Korean, 2 = English
// NeedTrBlock: 1 = the character's own chat is never translated / sent out
static const char* NCT_VAR_RECV = "NeedTrRecv";
static const char* NCT_VAR_LANG = "NeedTrLang";
static const char* NCT_VAR_BLOCK = "NeedTrBlock";

static constexpr t_tick NCT_TIMER_INTERVAL = 1000;
static constexpr t_tick NCT_STATS_INTERVAL = 60000;
static constexpr t_tick NCT_HELLO_TIMEOUT = 5000;
static constexpr t_tick NCT_RELAY_SILENCE = 30000;	// the relay sends STATUS every 5 s
static constexpr size_t NCT_MAX_TEXT = CHAT_SIZE_MAX - 1;

struct s_nct_pending {
	uint8 chan;
	uint8 tgt;
	uint32 speaker_cid;
	char name[NAME_LENGTH];
	int16 m, x, y;
	int32 group_id;			// party / guild / bg / clan / chat room id, whisper: receiver char_id
	uint8 channel_type;
	char channel_name[CHAN_NAME_LENGTH];
	char channel_alias[CHAN_NAME_LENGTH];
	t_tick sent;
	t_tick expire;
};

struct s_nct_quota {
	t_tick minute_start;
	t_tick hour_start;
	uint16 minute_count;
	uint16 hour_count;
};

static int32 nct_listen_fd = -1;
static uint16 nct_listen_port = 0;
static int32 nct_relay_fd = -1;
static bool nct_relay_ready = false;		// HELLO accepted
static t_tick nct_relay_accept_tick = 0;
static t_tick nct_relay_last_recv = 0;
static bool nct_relay_enabled = false;		// last STATUS said "translating"
static uint8 nct_relay_reason = 0;
static uint32 nct_relay_remaining_s = 0;
static uint32 nct_relay_day_micro = 0;
static uint32 nct_relay_total_micro = 0;
static std::string nct_relay_msg;
static std::string nct_token;
static uint32 nct_next_req_id = 1;
static std::unordered_map<uint32, s_nct_pending> nct_pending;
static std::unordered_map<uint32, s_nct_quota> nct_quota;
static t_tick nct_global_minute_start = 0;
static uint32 nct_global_minute_count = 0;
static uint32 nct_stats[NCT_STAT_MAX] = {};
static std::unordered_set<uint32> nct_recipients[NEED_LANG_MAX];	// unique receivers of the current KST day
static int64 nct_recipients_day = -1;
static t_tick nct_last_stats = 0;
static t_tick nct_last_bind_try = 0;
static bool nct_warned_no_token = false;

// ---------------------------------------------------------------- small helpers

static inline void nct_stat( e_need_ct_stat s, uint32 v = 1 ){
	nct_stats[s] += v;
}

/// per-chat-type message counter (whisper was added after the protocol order was fixed)
static inline e_need_ct_stat nct_msg_stat( e_need_ct_chan chan ){
	if( chan == NCT_WHISPER )
		return NCT_STAT_MSG_WHISPER;
	return static_cast<e_need_ct_stat>( NCT_STAT_MSG_SAY + chan );
}

/// does `tsd` refuse whispers from `name` right now (ignore all / ignore list, see clif_parse_WisMessage)?
static bool nct_whisper_ignored( const map_session_data* tsd, const char* name ){
	if( tsd->state.ignoreAll )
		return true;
	for( int32 i = 0; i < MAX_IGNORE_LIST && tsd->ignore[i].name[0] != '\0'; i++ ){
		if( strcmp( tsd->ignore[i].name, name ) == 0 )
			return true;
	}
	return false;
}

static int32 nct_lang_of( const map_session_data* sd ){
	// the display language of a recipient: explicit choice, else the client language
	if( sd->vars_ok ){
		int64 v = pc_readglobalreg( sd, add_str( NCT_VAR_LANG ) );

		if( v == 1 )
			return NEED_LANG_KR;
		if( v == 2 )
			return NEED_LANG_EN;
	}
	return need_lang_sanitize( (uint8)sd->need_lang );
}

/// 1 on, 0 off, -1 not set
static int32 nct_recv_setting( const map_session_data* sd ){
	if( !sd->vars_ok )
		return -1;

	int64 v = pc_readglobalreg( sd, add_str( NCT_VAR_RECV ) );

	if( v == 1 )
		return 1;
	if( v == 2 )
		return 0;
	return -1;
}

static bool nct_recv_on( const map_session_data* sd ){
	int32 v = nct_recv_setting( sd );

	if( v < 0 )
		return battle_config.need_chat_translate_default_on != 0;
	return v == 1;
}

static bool nct_blocked( const map_session_data* sd ){
	return sd->vars_ok && pc_readglobalreg( sd, add_str( NCT_VAR_BLOCK ) ) == 1;
}

static void nct_setreg( map_session_data* sd, const char* var, int64 val ){
	int64 reg = add_str( var );

	if( pc_readglobalreg( sd, reg ) != val )
		pc_setglobalreg( sd, reg, val );
	if( sd->vars_dirty )
		intif_saveregistry( sd );	// persist now (same rule as the autoloot settings)
}

static bool nct_service_on( void ){
	return battle_config.need_chat_translate != 0 && nct_relay_ready && nct_relay_enabled;
}

/// does this session want a translation into `tgt` of a line said by `speaker_cid`?
static bool nct_wants( const map_session_data* tsd, int32 tgt, uint32 speaker_cid ){
	if( tsd == nullptr || tsd->status.char_id == speaker_cid )
		return false;
	if( !session_isActive( tsd->fd ) || tsd->state.autotrade )
		return false;
	if( !tsd->vars_ok )
		return false;
	if( pc_get_group_level( tsd ) < battle_config.need_chat_translate_min_level )
		return false;
	if( !nct_recv_on( tsd ) )
		return false;
	return nct_lang_of( tsd ) == tgt;
}

/**
 * Source language of a CP949 line. Hangul syllables (KS X 1001 B0-C8/A1-FE and the UHC
 * extension) or compatibility jamo mean Korean; otherwise ASCII letters mean English;
 * otherwise (numbers, symbols) nothing to do.
 */
static int32 nct_detect_lang( const char* s, size_t len ){
	bool hangul = false, letters = false;

	for( size_t i = 0; i < len; i++ ){
		uint8 c = (uint8)s[i];

		if( c >= 0x81 && c <= 0xFE && i + 1 < len ){
			uint8 t = (uint8)s[i + 1];

			if( c >= 0xB0 && c <= 0xC8 && t >= 0xA1 && t <= 0xFE )
				hangul = true;	// KS X 1001 Hangul syllables
			else if( c <= 0xA0 && t >= 0x41 )
				hangul = true;	// UHC extension, lead 81-A0 (any trail)
			else if( c >= 0xA1 && c <= 0xC6 && t >= 0x41 && t <= 0xA0 )
				hangul = true;	// UHC extension, lead A1-C6 with a trail below A1
			else if( c == 0xA4 && t >= 0xA1 && t <= 0xFE )
				hangul = true;	// compatibility jamo (abbreviations such as "let's go" / "ok"); the relay
								// answers them from its phrase table or skips laughter without an API call
			// symbols and hanja are not "Korean text"
			i++;
			continue;
		}
		if( ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) )
			letters = true;
	}
	if( hangul )
		return NEED_LANG_KR;
	if( letters )
		return NEED_LANG_EN;
	return -1;
}

/// cut at `max` bytes without splitting a CP949 double-byte character
static size_t nct_cp949_cut( const char* s, size_t len, size_t max ){
	if( len <= max )
		return len;

	size_t i = 0;

	while( i < max ){
		uint8 c = (uint8)s[i];
		size_t w = ( c >= 0x81 && c <= 0xFE ) ? 2 : 1;

		if( i + w > max )
			break;
		i += w;
	}
	return i;
}

static bool nct_command_like( const char* body ){
	// a line that failed as a command falls through as chat for group 0 players
	// (atcommand.cpp is_atcommand). '#' alone is a valid text start ("#1 ..."), so only
	// '#'+letter is treated as a charcommand attempt.
	if( body[0] == '@' )
		return true;
	if( body[0] == '#' && ( ( body[1] >= 'a' && body[1] <= 'z' ) || ( body[1] >= 'A' && body[1] <= 'Z' ) ) )
		return true;
	return false;
}

// ---------------------------------------------------------------- recipients

static int32 nct_collect_sub( block_list* bl, va_list ap ){
	std::vector<map_session_data*>* out = va_arg( ap, std::vector<map_session_data*>* );
	map_session_data* sd = reinterpret_cast<map_session_data*>( bl );

	if( sd->chatID == 0 )	// AREA_CHAT_WOC drops chat room members
		out->push_back( sd );
	return 1;
}

static struct Channel* nct_find_channel( const s_nct_pending& p ){
	switch( p.channel_type ){
		case CHAN_TYPE_MAP: {
			struct map_data* md = map_getmapdata( p.m );
			return md != nullptr ? md->channel : nullptr;
		}
		case CHAN_TYPE_ALLY: {
			auto g = guild_search( p.group_id );
			return g != nullptr ? g->channel : nullptr;
		}
		default: {
			DBMap* db = channel_get_db();
			struct Channel* ch = db != nullptr ? (struct Channel*)strdb_get( db, p.channel_name ) : nullptr;
			return ch;
		}
	}
}

/// everyone who may still see the original line, from the CURRENT state
static void nct_candidates( const s_nct_pending& p, std::vector<map_session_data*>& out ){
	switch( p.chan ){
		case NCT_SAY: {
			int16 r = static_cast<int16>( AREA_SIZE - 5 );
			map_foreachinallarea( nct_collect_sub, p.m, p.x - r, p.y - r, p.x + r, p.y + r, BL_PC, &out );
			// the area is the one of the moment the line was said: whoever left it or the map
			// does not get the translation
			break;
		}
		case NCT_CHATROOM: {
			block_list* bl = map_id2bl( p.group_id );

			if( bl != nullptr && bl->type == BL_CHAT ){
				chat_data* cd = reinterpret_cast<chat_data*>( bl );

				for( int32 i = 0; i < cd->users; i++ ){
					if( cd->usersd[i] != nullptr && cd->usersd[i]->chatID == p.group_id )
						out.push_back( cd->usersd[i] );
				}
			}
			break;
		}
		case NCT_CHANNEL: {
			struct Channel* ch = nct_find_channel( p );

			if( ch != nullptr && ch->users != nullptr ){
				DBIterator* iter = db_iterator( ch->users );

				for( map_session_data* u = (map_session_data*)dbi_first( iter ); dbi_exists( iter ); u = (map_session_data*)dbi_next( iter ) )
					out.push_back( u );
				dbi_destroy( iter );
			}
			break;
		}
		case NCT_PARTY: {
			struct party_data* pd = party_search( p.group_id );

			if( pd != nullptr ){
				for( int32 i = 0; i < MAX_PARTY; i++ ){
					map_session_data* u = pd->data[i].sd;

					if( u != nullptr && u->status.party_id == p.group_id )
						out.push_back( u );
				}
			}
			break;
		}
		case NCT_GUILD: {
			auto g = guild_search( p.group_id );

			if( g != nullptr ){
				for( int32 i = 0; i < g->guild.max_member && i < MAX_GUILD; i++ ){
					map_session_data* u = g->guild.member[i].sd;

					// GUILD_NOBG: members inside a battleground do not see guild chat
					if( u != nullptr && u->status.guild_id == p.group_id && u->bg_id == 0 )
						out.push_back( u );
				}
			}
			break;
		}
		case NCT_BG: {
			auto it = bg_team_db.find( p.group_id );

			if( it != bg_team_db.end() && it->second != nullptr ){
				for( auto& member : it->second->members ){
					if( member.sd != nullptr && member.sd->bg_id == p.group_id )
						out.push_back( member.sd );
				}
			}
			break;
		}
		case NCT_CLAN: {
			struct clan* c = clan_search( p.group_id );

			if( c != nullptr ){
				for( int32 i = 0; i < MAX_CLAN; i++ ){
					map_session_data* u = c->members[i];

					if( u != nullptr && u->status.clan_id == p.group_id )
						out.push_back( u );
				}
			}
			break;
		}
		case NCT_WHISPER: {
			// only the receiver, only while still online here and not ignoring the speaker since
			map_session_data* u = map_charid2sd( p.group_id );

			if( u != nullptr && !nct_whisper_ignored( u, p.name ) )
				out.push_back( u );
			break;
		}
	}
}

static bool nct_any_wants( const s_nct_pending& p ){
	std::vector<map_session_data*> c;

	nct_candidates( p, c );
	for( map_session_data* u : c ){
		if( nct_wants( u, p.tgt, p.speaker_cid ) )
			return true;
	}
	return false;
}

// ---------------------------------------------------------------- relay I/O

static void nct_send_line( map_session_data* tsd, const char* text ){
	uint8 buf[CHAT_SIZE_MAX + 16];
	size_t len = strnlen( text, CHAT_SIZE_MAX );
	size_t cut = nct_cp949_cut( text, len, CHAT_SIZE_MAX - 13 );
	uint32 rgb = static_cast<uint32>( battle_config.need_chat_translate_color );
	uint32 bgr = ( rgb & 0x0000FF ) << 16 | ( rgb & 0x00FF00 ) | ( rgb & 0xFF0000 ) >> 16;

	WBUFW( buf, 0 ) = 0x2C1;					// ZC_NPC_CHAT, the packet channel chat already uses
	WBUFW( buf, 2 ) = static_cast<uint16>( 12 + cut + 1 );
	WBUFL( buf, 4 ) = 0;						// id 0: chat window only, never a bubble over anyone
	WBUFL( buf, 8 ) = bgr;
	memcpy( WBUFP( buf, 12 ), text, cut );
	WBUFB( buf, 12 + cut ) = 0;
	clif_send( buf, WBUFW( buf, 2 ), tsd, SELF );
}

static void nct_relay_close( const char* why ){
	if( nct_relay_fd >= 0 ){
		ShowWarning( "NEED Chat Translate: relay session closed (%s).\n", why );
		set_eof( nct_relay_fd );	// nct_parse sees the eof and closes it (safe from inside nct_parse too)
	}
	nct_relay_fd = -1;
	nct_relay_ready = false;
	nct_relay_enabled = false;
	nct_relay_reason = 0;
	// pending answers can no longer arrive; the timer expires them
}

static void nct_send_stats( void ){
	if( nct_relay_fd < 0 || !nct_relay_ready || !session_isActive( nct_relay_fd ) )
		return;

	// snapshots
	uint32 online = 0, sub[NEED_LANG_MAX] = {};
	struct s_mapiterator* iter = mapit_getallusers();

	for( map_session_data* sd = (map_session_data*)mapit_first( iter ); mapit_exists( iter ); sd = (map_session_data*)mapit_next( iter ) ){
		online++;
		if( sd->vars_ok && nct_recv_on( sd ) )
			sub[nct_lang_of( sd )]++;
	}
	mapit_free( iter );
	nct_stats[NCT_STAT_ONLINE] = online;
	nct_stats[NCT_STAT_SUBSCRIBERS_KR] = sub[NEED_LANG_KR];
	nct_stats[NCT_STAT_SUBSCRIBERS_EN] = sub[NEED_LANG_EN];
	nct_stats[NCT_STAT_RECIPIENTS_KR] = static_cast<uint32>( nct_recipients[NEED_LANG_KR].size() );
	nct_stats[NCT_STAT_RECIPIENTS_EN] = static_cast<uint32>( nct_recipients[NEED_LANG_EN].size() );

	int32 fd = nct_relay_fd;
	uint16 len = static_cast<uint16>( 8 + 4 * NCT_STAT_MAX );

	WFIFOHEAD( fd, len );
	WFIFOW( fd, 0 ) = NCT_CMD_STATS;
	WFIFOW( fd, 2 ) = len;
	WFIFOW( fd, 4 ) = 1;
	WFIFOW( fd, 6 ) = NCT_STAT_MAX;
	for( int32 i = 0; i < NCT_STAT_MAX; i++ )
		WFIFOL( fd, 8 + 4 * i ) = nct_stats[i];
	WFIFOSET( fd, len );

	memset( nct_stats, 0, sizeof( nct_stats ) );
}

static void nct_alert_gms( uint8 level, const std::string& msg ){
	std::string line = "[TL-ADMIN] " + msg;
	struct s_mapiterator* iter = mapit_getallusers();

	ShowWarning( "NEED Chat Translate alert(%u): %s\n", level, msg.c_str() );
	for( map_session_data* sd = (map_session_data*)mapit_first( iter ); mapit_exists( iter ); sd = (map_session_data*)mapit_next( iter ) ){
		if( pc_get_group_level( sd ) >= battle_config.need_chat_translate_alert_level )
			clif_displaymessage( sd->fd, line.c_str() );
	}
	mapit_free( iter );
}

static void nct_deliver( const s_nct_pending& p, const char* text, size_t len ){
	std::vector<map_session_data*> c;
	char line[CHAT_SIZE_MAX * 2];
	char body[CHAT_SIZE_MAX];
	int32 n = 0;
	const char* const* chtag;

	switch( p.chan ){
		case NCT_CHATROOM: chtag = NCT_S_CH_CHATROOM; break;
		case NCT_PARTY: chtag = NCT_S_CH_PARTY; break;
		case NCT_GUILD: chtag = NCT_S_CH_GUILD; break;
		case NCT_BG: chtag = NCT_S_CH_BG; break;
		case NCT_CLAN: chtag = NCT_S_CH_CLAN; break;
		case NCT_WHISPER: chtag = NCT_S_CH_WHISPER; break;
		default: chtag = NCT_S_CH_SAY; break;
	}

	// the relay already cut and validated the text; strip anything a client could interpret
	size_t bl = 0;

	for( size_t i = 0; i < len && bl < sizeof( body ) - 1; i++ ){
		uint8 ch = (uint8)text[i];

		if( ch < 0x20 || ch == 0x7F )
			continue;
		body[bl++] = (char)ch;
	}
	body[bl] = '\0';
	if( bl == 0 ){
		nct_stat( NCT_STAT_DELIVERY_EMPTY );
		return;
	}

	nct_candidates( p, c );
	for( map_session_data* u : c ){
		if( !nct_wants( u, p.tgt, p.speaker_cid ) )
			continue;

		int32 l = u->need_lang == NEED_LANG_EN ? 1 : 0;	// prefix in the client's own UI language

		if( p.chan == NCT_CHANNEL )
			safesnprintf( line, sizeof( line ), "%s %s %s : %s", NCT_S_PREFIX[l], p.channel_alias, p.name, body );
		else
			safesnprintf( line, sizeof( line ), "%s %s%s : %s", NCT_S_PREFIX[l], chtag[l], p.name, body );
		nct_send_line( u, line );
		nct_recipients[p.tgt].insert( u->status.char_id );
		n++;
	}
	if( n == 0 )
		nct_stat( NCT_STAT_DELIVERY_EMPTY );
	else
		nct_stat( NCT_STAT_LINES, n );
}

static int32 nct_parse( int32 fd ){
	if( session[fd]->flag.eof ){
		if( fd == nct_relay_fd ){
			nct_relay_fd = -1;
			nct_relay_ready = false;
			nct_relay_enabled = false;
			ShowWarning( "NEED Chat Translate: relay disconnected.\n" );
		}
		do_close( fd );
		return 0;
	}
	if( fd != nct_relay_fd ){	// replaced by a newer relay connection
		set_eof( fd );
		return 0;
	}

	while( RFIFOREST( fd ) >= 4 ){
		uint16 cmd = RFIFOW( fd, 0 );
		uint16 len = RFIFOW( fd, 2 );

		if( len < 4 || len > 4096 ){
			nct_relay_close( "bad frame length" );
			return 0;
		}
		if( RFIFOREST( fd ) < len )
			return 0;

		nct_relay_last_recv = gettick();

		if( !nct_relay_ready && cmd != NCT_CMD_HELLO ){
			nct_relay_close( "frame before HELLO" );
			return 0;
		}

		switch( cmd ){
			case NCT_CMD_HELLO: {
				uint16 result = 0;

				if( len < 8 + NCT_TOKEN_LEN || RFIFOW( fd, 4 ) != NCT_PROTO_VERSION )
					result = 2;
				else if( !nct_token.empty() ){
					char tok[NCT_TOKEN_LEN] = {};

					memcpy( tok, nct_token.c_str(), std::min<size_t>( nct_token.size(), NCT_TOKEN_LEN ) );
					if( memcmp( tok, RFIFOP( fd, 8 ), NCT_TOKEN_LEN ) != 0 )
						result = 1;
				}
				WFIFOHEAD( fd, 12 );
				WFIFOW( fd, 0 ) = NCT_CMD_HELLO_ACK;
				WFIFOW( fd, 2 ) = 12;
				WFIFOW( fd, 4 ) = NCT_PROTO_VERSION;
				WFIFOW( fd, 6 ) = result;
				WFIFOL( fd, 8 ) = 0;
				WFIFOSET( fd, 12 );
				RFIFOSKIP( fd, len );
				if( result != 0 ){
					ShowWarning( "NEED Chat Translate: relay HELLO rejected (%s).\n", result == 1 ? "token" : "protocol" );
					set_eof( fd );
					nct_relay_fd = -1;
					nct_relay_ready = false;
					return 0;
				}
				nct_relay_ready = true;
				ShowStatus( "NEED Chat Translate: relay connected.\n" );
				continue;
			}
			case NCT_CMD_RES: {
				if( len < NCT_RES_HEADER ){
					nct_relay_close( "short RES" );
					return 0;
				}

				uint32 req_id = RFIFOL( fd, 4 );
				uint8 status = RFIFOB( fd, 8 );
				auto it = nct_pending.find( req_id );

				if( it != nct_pending.end() ){
					s_nct_pending p = it->second;
					t_tick now = gettick();

					nct_pending.erase( it );
					nct_stat( NCT_STAT_RTT_MS_TOTAL, static_cast<uint32>( DIFF_TICK( now, p.sent ) ) );
					nct_stat( NCT_STAT_RTT_COUNT );
					if( DIFF_TICK( now, p.expire ) > 0 ){
						nct_stat( NCT_STAT_EXPIRED );	// too late: an old line after newer chat is worse than none
					}else if( status == NCT_ST_OK || status == NCT_ST_CACHED ){
						nct_stat( NCT_STAT_RES_OK );
						if( nct_service_on() )
							nct_deliver( p, RFIFOCP( fd, NCT_RES_HEADER ), len - NCT_RES_HEADER );
					}else{
						nct_stat( NCT_STAT_RES_OTHER );
					}
				}
				RFIFOSKIP( fd, len );
				continue;
			}
			case NCT_CMD_STATUS: {
				if( len < NCT_STATUS_HEADER ){
					nct_relay_close( "short STATUS" );
					return 0;
				}

				bool enabled = RFIFOB( fd, 4 ) != 0;

				if( enabled != nct_relay_enabled )
					ShowStatus( "NEED Chat Translate: relay %s.\n", enabled ? "translating" : "paused" );
				nct_relay_enabled = enabled;
				nct_relay_reason = RFIFOB( fd, 5 );
				nct_relay_remaining_s = RFIFOL( fd, 8 );
				nct_relay_day_micro = RFIFOL( fd, 12 );
				nct_relay_total_micro = RFIFOL( fd, 16 );
				nct_relay_msg.assign( RFIFOCP( fd, NCT_STATUS_HEADER ), strnlen( RFIFOCP( fd, NCT_STATUS_HEADER ), len - NCT_STATUS_HEADER ) );
				RFIFOSKIP( fd, len );
				continue;
			}
			case NCT_CMD_ALERT: {
				if( len >= NCT_ALERT_HEADER ){
					std::string msg( RFIFOCP( fd, NCT_ALERT_HEADER ), strnlen( RFIFOCP( fd, NCT_ALERT_HEADER ), len - NCT_ALERT_HEADER ) );

					for( char& ch : msg ){
						if( (uint8)ch < 0x20 || (uint8)ch >= 0x7F )
							ch = '?';
					}
					nct_alert_gms( RFIFOB( fd, 4 ), msg );
				}
				RFIFOSKIP( fd, len );
				continue;
			}
			default:
				nct_relay_close( "unknown frame" );
				return 0;
		}
	}
	return 0;
}

static int32 nct_accept( int32 listen_fd ){
	int32 fd = connect_client( listen_fd );

	if( fd < 0 )
		return fd;
	if( session[fd]->client_addr != MAKEIP( 127, 0, 0, 1 ) ){
		ShowWarning( "NEED Chat Translate: rejected non-local connection from %d.%d.%d.%d.\n", CONVIP( session[fd]->client_addr ) );
		do_close( fd );
		return -1;
	}
	if( nct_relay_fd >= 0 && nct_relay_fd != fd )
		nct_relay_close( "replaced by a new relay connection" );

	session[fd]->func_parse = nct_parse;
	session[fd]->rdata_tick = 0;	// the relay heartbeats every 5 s; stall_time and nct_timer catch silence
	session[fd]->wdata_tick = 0;
	nct_relay_fd = fd;
	nct_relay_ready = false;
	nct_relay_enabled = false;
	nct_relay_accept_tick = gettick();
	nct_relay_last_recv = nct_relay_accept_tick;
	return fd;
}

/**
 * make_listen_bind() calls exit() when the bind fails (common/socket.cpp), which would take the
 * whole map-server down for a translation feature. So the port is first probed with a plain OS
 * socket; only a port that is free right now is handed to make_listen_bind().
 */
static bool nct_port_free( uint16 port ){
#ifdef WIN32
	SOCKET s = ::socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

	if( s == INVALID_SOCKET )
		return false;

	BOOL excl = TRUE;
	setsockopt( s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof( excl ) );
#else
	int s = ::socket( AF_INET, SOCK_STREAM, 0 );

	if( s < 0 )
		return false;
#endif
	struct sockaddr_in a = {};

	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl( MAKEIP( 127, 0, 0, 1 ) );
	a.sin_port = htons( port );

	bool ok = ::bind( s, (struct sockaddr*)&a, sizeof( a ) ) == 0;
#ifdef WIN32
	closesocket( s );
#else
	close( s );
#endif
	return ok;
}

static void nct_ensure_listener( t_tick now ){
	if( battle_config.need_chat_translate == 0 || nct_listen_fd >= 0 )
		return;
	if( nct_last_bind_try != 0 && DIFF_TICK( now, nct_last_bind_try ) < 60000 )
		return;	// retry once a minute at most
	nct_last_bind_try = now;

	const char* env = getenv( "NEED_CHAT_TRANSLATE_PORT" );	// test copies run beside each other
	int32 port = env != nullptr ? atoi( env ) : battle_config.need_chat_translate_port;

	if( port <= 0 || port >= 65536 ){
		ShowError( "NEED Chat Translate: invalid port %d - translation stays off.\n", port );
		return;
	}
	if( !nct_port_free( static_cast<uint16>( port ) ) ){
		ShowError( "NEED Chat Translate: 127.0.0.1:%d is in use - translation stays off (retrying every minute).\n", port );
		return;
	}

	nct_listen_fd = make_listen_bind( MAKEIP( 127, 0, 0, 1 ), static_cast<uint16>( port ) );
	if( nct_listen_fd < 0 ){
		ShowError( "NEED Chat Translate: listener on 127.0.0.1:%d failed.\n", port );
		return;
	}
	nct_listen_port = static_cast<uint16>( port );
	session[nct_listen_fd]->func_recv = nct_accept;
	ShowStatus( "NEED Chat Translate: relay listener ready on 127.0.0.1:%d.\n", port );

	const char* tok = getenv( "NEED_CHAT_TRANSLATE_TOKEN" );

	nct_token = tok != nullptr ? tok : "";
	if( nct_token.empty() && !nct_warned_no_token ){
		ShowWarning( "NEED Chat Translate: NEED_CHAT_TRANSLATE_TOKEN is not set - any local process can act as the relay.\n" );
		nct_warned_no_token = true;
	}
}

static TIMER_FUNC( nct_timer ){
	nct_ensure_listener( tick );

	// the unique-receiver sets cover one Korean calendar day (UTC+9, no DST); the relay keeps
	// the largest value it saw per day, which is the day's unique count
	int64 kst_day = ( static_cast<int64>( time( nullptr ) ) + 9 * 3600 ) / 86400;

	if( kst_day != nct_recipients_day ){
		nct_recipients_day = kst_day;
		for( auto& set : nct_recipients )
			set.clear();
	}

	// relay that never said HELLO, or went silent
	if( nct_relay_fd >= 0 ){
		if( !nct_relay_ready && DIFF_TICK( tick, nct_relay_accept_tick ) > NCT_HELLO_TIMEOUT )
			nct_relay_close( "no HELLO" );
		else if( nct_relay_ready && DIFF_TICK( tick, nct_relay_last_recv ) > NCT_RELAY_SILENCE )
			nct_relay_close( "relay silent" );
	}

	// expire answers that did not come in time
	for( auto it = nct_pending.begin(); it != nct_pending.end(); ){
		if( DIFF_TICK( tick, it->second.expire ) > 0 ){
			nct_stat( NCT_STAT_EXPIRED );
			it = nct_pending.erase( it );
		}else
			++it;
	}

	// drop quota windows older than an hour
	if( nct_quota.size() > 0 ){
		for( auto it = nct_quota.begin(); it != nct_quota.end(); ){
			if( DIFF_TICK( tick, it->second.hour_start ) > 3600000 )
				it = nct_quota.erase( it );
			else
				++it;
		}
	}

	if( nct_last_stats == 0 )
		nct_last_stats = tick;
	if( DIFF_TICK( tick, nct_last_stats ) >= NCT_STATS_INTERVAL ){
		nct_last_stats = tick;
		nct_send_stats();
	}
	return 0;
}

static bool nct_quota_take( uint32 cid, t_tick now ){
	s_nct_quota& q = nct_quota[cid];

	if( q.hour_start == 0 || DIFF_TICK( now, q.hour_start ) >= 3600000 ){
		q.hour_start = now;
		q.hour_count = 0;
	}
	if( q.minute_start == 0 || DIFF_TICK( now, q.minute_start ) >= 60000 ){
		q.minute_start = now;
		q.minute_count = 0;
	}
	if( q.minute_count >= battle_config.need_chat_translate_sender_per_min || q.hour_count >= battle_config.need_chat_translate_sender_per_hour )
		return false;
	q.minute_count++;
	q.hour_count++;
	return true;
}

static bool nct_global_take( t_tick now ){
	if( nct_global_minute_start == 0 || DIFF_TICK( now, nct_global_minute_start ) >= 60000 ){
		nct_global_minute_start = now;
		nct_global_minute_count = 0;
	}
	if( nct_global_minute_count >= static_cast<uint32>( battle_config.need_chat_translate_global_per_min ) )
		return false;
	nct_global_minute_count++;
	return true;
}

// ---------------------------------------------------------------- the hook

static void nct_on_line( map_session_data* sd, e_need_ct_chan chan, const char* name, const char* body, const Channel* channel, uint32 target_cid ){
	if( sd == nullptr || name == nullptr || body == nullptr || chan >= NCT_CHAN_MAX )
		return;
	if( battle_config.need_chat_translate == 0 )
		return;	// master switch off: not even counted

	auto t0 = std::chrono::steady_clock::now();

	nct_stat( nct_msg_stat( chan ) );

	do{
		if( ( battle_config.need_chat_translate_channels & ( 1 << chan ) ) == 0 )
			break;
		if( !nct_service_on() ){
			nct_stat( NCT_STAT_SKIP_SERVICE_OFF );
			break;
		}
		nct_stat( NCT_STAT_ELIGIBLE );
		if( !sd->vars_ok )
			break;
		if( nct_blocked( sd ) ){
			nct_stat( NCT_STAT_SKIP_SENDER_BLOCK );
			break;
		}
		if( nct_command_like( body ) ){
			nct_stat( NCT_STAT_SKIP_COMMAND );
			break;
		}

		size_t len = strnlen( body, NCT_MAX_TEXT );
		int32 src = nct_detect_lang( body, len );

		if( src < 0 ){
			nct_stat( NCT_STAT_SKIP_NO_LANG );
			break;
		}

		s_nct_pending p = {};
		t_tick now = gettick();

		p.chan = chan;
		p.tgt = static_cast<uint8>( src == NEED_LANG_KR ? NEED_LANG_EN : NEED_LANG_KR );
		p.speaker_cid = sd->status.char_id;
		safestrncpy( p.name, name, sizeof( p.name ) );
		p.m = sd->m;
		p.x = sd->x;
		p.y = sd->y;
		switch( chan ){
			case NCT_CHATROOM: p.group_id = sd->chatID; break;
			case NCT_PARTY: p.group_id = sd->status.party_id; break;
			case NCT_GUILD: p.group_id = sd->status.guild_id; break;
			case NCT_BG: p.group_id = sd->bg_id; break;
			case NCT_CLAN: p.group_id = sd->status.clan_id; break;
			case NCT_WHISPER:
				if( target_cid == 0 )
					return;
				p.group_id = static_cast<int32>( target_cid );
				break;
			case NCT_CHANNEL:
				if( channel == nullptr )
					return;
				p.channel_type = static_cast<uint8>( channel->type );
				safestrncpy( p.channel_name, channel->name, sizeof( p.channel_name ) );
				safestrncpy( p.channel_alias, channel->alias, sizeof( p.channel_alias ) );
				p.group_id = sd->status.guild_id;	// ALLY channels are looked up through the guild
				break;
			default: break;
		}

		if( !nct_any_wants( p ) ){
			nct_stat( NCT_STAT_SKIP_NO_RECIPIENT );	// nobody would see it: no request, no cost
			break;
		}
		if( !nct_quota_take( p.speaker_cid, now ) ){
			nct_stat( NCT_STAT_SKIP_SENDER_QUOTA );
			break;
		}
		if( nct_pending.size() >= static_cast<size_t>( battle_config.need_chat_translate_max_inflight ) || !nct_global_take( now ) ){
			nct_stat( NCT_STAT_SKIP_INFLIGHT );
			break;
		}

		int32 fd = nct_relay_fd;

		if( fd < 0 || !session_isActive( fd ) ){
			nct_stat( NCT_STAT_SKIP_SERVICE_OFF );
			break;
		}
		// do not let a stalled relay grow the write fifo towards WFIFO_MAX (1 MB, then EOF)
		if( session[fd]->wdata_size > 128 * 1024 ){
			nct_stat( NCT_STAT_SKIP_INFLIGHT );
			break;
		}

		uint32 req_id = nct_next_req_id++;

		if( nct_next_req_id == 0 )
			nct_next_req_id = 1;
		p.sent = now;
		p.expire = now + battle_config.need_chat_translate_timeout;

		uint16 flen = static_cast<uint16>( NCT_REQ_HEADER + len );

		WFIFOHEAD( fd, flen );
		WFIFOW( fd, 0 ) = NCT_CMD_REQ;
		WFIFOW( fd, 2 ) = flen;
		WFIFOL( fd, 4 ) = req_id;
		WFIFOB( fd, 8 ) = chan;
		WFIFOB( fd, 9 ) = static_cast<uint8>( src );
		WFIFOB( fd, 10 ) = p.tgt;
		WFIFOB( fd, 11 ) = 0;
		WFIFOL( fd, 12 ) = p.speaker_cid;	// for the relay's per-speaker limit; never sent to the API
		WFIFOL( fd, 16 ) = static_cast<uint32>( battle_config.need_chat_translate_timeout );
		memcpy( WFIFOP( fd, NCT_REQ_HEADER ), body, len );
		WFIFOSET( fd, flen );

		nct_pending[req_id] = p;
		nct_stat( NCT_STAT_REQUESTS );
	}while( false );

	uint32 us = static_cast<uint32>( std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now() - t0 ).count() );

	nct_stat( NCT_STAT_HOOK_US_TOTAL, us );
	nct_stat( NCT_STAT_HOOK_CALLS );
	if( us > nct_stats[NCT_STAT_HOOK_US_MAX] )
		nct_stats[NCT_STAT_HOOK_US_MAX] = us;
}

void need_ct_on_chat( map_session_data* sd, e_need_ct_chan chan, const char* name, const char* body, const Channel* channel ){
	if( chan == NCT_WHISPER )
		return;	// whispers come through need_ct_on_whisper (they need the receiver)
	nct_on_line( sd, chan, name, body, channel, 0 );
}

void need_ct_on_whisper( map_session_data* sd, map_session_data* dstsd, const char* body ){
	if( sd == nullptr || dstsd == nullptr || sd == dstsd )
		return;
	nct_on_line( sd, NCT_WHISPER, sd->status.name, body, nullptr, static_cast<uint32>( dstsd->status.char_id ) );
}

// ---------------------------------------------------------------- commands

static void nct_show_channels( int32 fd, int32 l ){
	static const char* const* names[NCT_CHAN_MAX] = { NCT_S_CHN_SAY, NCT_S_CHN_CHATROOM, NCT_S_CHN_CHANNEL, NCT_S_CHN_PARTY, NCT_S_CHN_GUILD, NCT_S_CHN_BG, NCT_S_CHN_CLAN, NCT_S_CHN_WHISPER };
	std::string list;

	for( int32 i = 0; i < NCT_CHAN_MAX; i++ ){
		if( battle_config.need_chat_translate_channels & ( 1 << i ) ){
			if( !list.empty() )
				list += ", ";
			list += names[i][l];
		}
	}
	if( list.empty() )
		list = NCT_S_CHN_NONE[l];

	char out[CHAT_SIZE_MAX];

	safesnprintf( out, sizeof( out ), NCT_S_CHANNELS[l], list.c_str() );
	clif_displaymessage( fd, out );
}

static void nct_show_status( int32 fd, map_session_data* sd ){
	int32 l = sd->need_lang == NEED_LANG_EN ? 1 : 0;
	char out[CHAT_SIZE_MAX];

	if( nct_recv_on( sd ) ){
		int64 lv = pc_readglobalreg( sd, add_str( NCT_VAR_LANG ) );
		const char* lang = lv == 1 ? NCT_S_LANG_KO[l] : ( lv == 2 ? NCT_S_LANG_EN[l] : NCT_S_LANG_AUTO[l] );

		safesnprintf( out, sizeof( out ), NCT_S_RECV_ON[l], lang );
		clif_displaymessage( fd, out );
	}else
		clif_displaymessage( fd, NCT_S_RECV_OFF[l] );
	if( nct_recv_setting( sd ) < 0 )
		clif_displaymessage( fd, NCT_S_RECV_DEFAULT[l] );
	clif_displaymessage( fd, nct_blocked( sd ) ? NCT_S_BLOCK_ON[l] : NCT_S_BLOCK_OFF[l] );

	if( nct_service_on() )
		clif_displaymessage( fd, NCT_S_SERVICE_ON[l] );
	else{
		const char* why = battle_config.need_chat_translate == 0 ? NCT_S_REASON_MASTER[l] : ( !nct_relay_ready ? NCT_S_REASON_RELAY[l] : NCT_S_REASON_RELAY_OFF[l] );

		safesnprintf( out, sizeof( out ), NCT_S_SERVICE_OFF[l], why );
		clif_displaymessage( fd, out );
	}
	nct_show_channels( fd, l );
}

int32 need_ct_cmd_translate( int32 fd, map_session_data* sd, const char* message ){
	nullpo_retr( -1, sd );

	int32 l = sd->need_lang == NEED_LANG_EN ? 1 : 0;

	if( !sd->vars_ok ){
		clif_displaymessage( fd, NCT_S_NOT_READY[l] );
		return -1;
	}

	char arg[32] = {};

	if( message != nullptr )
		sscanf( message, "%31s", arg );

	if( arg[0] == '\0' || !strcmpi( arg, "status" ) ){
		nct_show_status( fd, sd );
		if( arg[0] == '\0' )
			clif_displaymessage( fd, NCT_S_USAGE[l] );
		return 0;
	}
	if( !strcmpi( arg, "on" ) ){
		nct_setreg( sd, NCT_VAR_RECV, 1 );
		nct_stat( NCT_STAT_TOGGLE_ON );
		nct_show_status( fd, sd );
		clif_displaymessage( fd, NCT_S_NOTICE[l] );
		clif_displaymessage( fd, NCT_S_NOTICE_BLOCK[l] );
		return 0;
	}
	if( !strcmpi( arg, "off" ) ){
		nct_setreg( sd, NCT_VAR_RECV, 2 );	// an explicit OFF survives any later default change
		nct_stat( NCT_STAT_TOGGLE_OFF );
		nct_show_status( fd, sd );
		return 0;
	}
	if( !strcmpi( arg, "ko" ) || !strcmpi( arg, "kr" ) || !strcmpi( arg, "en" ) || !strcmpi( arg, "auto" ) ){
		int64 v = !strcmpi( arg, "auto" ) ? 0 : ( !strcmpi( arg, "en" ) ? 2 : 1 );

		nct_setreg( sd, NCT_VAR_LANG, v );
		nct_show_status( fd, sd );
		return 0;
	}
	clif_displaymessage( fd, NCT_S_USAGE[l] );
	return -1;
}

int32 need_ct_cmd_block( int32 fd, map_session_data* sd, const char* message ){
	nullpo_retr( -1, sd );

	int32 l = sd->need_lang == NEED_LANG_EN ? 1 : 0;

	if( !sd->vars_ok ){
		clif_displaymessage( fd, NCT_S_NOT_READY[l] );
		return -1;
	}

	char arg[32] = {};

	if( message != nullptr )
		sscanf( message, "%31s", arg );

	if( !strcmpi( arg, "on" ) ){
		nct_setreg( sd, NCT_VAR_BLOCK, 1 );
		nct_stat( NCT_STAT_BLOCK_ON );
	}else if( !strcmpi( arg, "off" ) ){
		nct_setreg( sd, NCT_VAR_BLOCK, 0 );
		nct_stat( NCT_STAT_BLOCK_OFF );
	}else if( arg[0] != '\0' && strcmpi( arg, "status" ) ){
		clif_displaymessage( fd, NCT_S_USAGE_BLOCK[l] );
		return -1;
	}
	clif_displaymessage( fd, nct_blocked( sd ) ? NCT_S_BLOCK_ON[l] : NCT_S_BLOCK_OFF[l] );
	if( arg[0] == '\0' )
		clif_displaymessage( fd, NCT_S_USAGE_BLOCK[l] );
	return 0;
}

int32 need_ct_cmd_admin( int32 fd, map_session_data* sd, const char* message ){
	nullpo_retr( -1, sd );

	char out[CHAT_SIZE_MAX];

	safesnprintf( out, sizeof( out ), "[TL-ADMIN] master=%d channels=0x%02X default_on=%d min_level=%d timeout=%dms inflight=%d/%d",
		battle_config.need_chat_translate, battle_config.need_chat_translate_channels, battle_config.need_chat_translate_default_on,
		battle_config.need_chat_translate_min_level, battle_config.need_chat_translate_timeout,
		(int32)nct_pending.size(), battle_config.need_chat_translate_max_inflight );
	clif_displaymessage( fd, out );
	safesnprintf( out, sizeof( out ), "[TL-ADMIN] listener=%s port=%u relay=%s ready=%d enabled=%d reason=%u",
		nct_listen_fd >= 0 ? "up" : "down", nct_listen_port, nct_relay_fd >= 0 ? "connected" : "none",
		nct_relay_ready ? 1 : 0, nct_relay_enabled ? 1 : 0, nct_relay_reason );
	clif_displaymessage( fd, out );
	safesnprintf( out, sizeof( out ), "[TL-ADMIN] pilot_remaining=%uh%02um spent_today=$%.4f spent_total=$%.4f",
		nct_relay_remaining_s / 3600, ( nct_relay_remaining_s % 3600 ) / 60, nct_relay_day_micro / 1e6, nct_relay_total_micro / 1e6 );
	clif_displaymessage( fd, out );
	if( !nct_relay_msg.empty() ){
		std::string m = "[TL-ADMIN] relay: " + nct_relay_msg;

		for( char& ch : m ){
			if( (uint8)ch < 0x20 || (uint8)ch >= 0x7F )
				ch = '?';
		}
		clif_displaymessage( fd, m.substr( 0, CHAT_SIZE_MAX - 1 ).c_str() );
	}
	safesnprintf( out, sizeof( out ), "[TL-ADMIN] this minute: requests=%u lines=%u expired=%u no_recipient=%u quota=%u inflight_skip=%u",
		nct_stats[NCT_STAT_REQUESTS], nct_stats[NCT_STAT_LINES], nct_stats[NCT_STAT_EXPIRED], nct_stats[NCT_STAT_SKIP_NO_RECIPIENT],
		nct_stats[NCT_STAT_SKIP_SENDER_QUOTA], nct_stats[NCT_STAT_SKIP_INFLIGHT] );
	clif_displaymessage( fd, out );
	return 0;
}

// ---------------------------------------------------------------- init / final

void do_init_need_chat_translate( void ){
	add_timer_func_list( nct_timer, "need_ct_timer" );
	add_timer_interval( gettick() + NCT_TIMER_INTERVAL, nct_timer, 0, 0, NCT_TIMER_INTERVAL );
	nct_ensure_listener( gettick() );
}

void do_final_need_chat_translate( void ){
	if( nct_relay_fd >= 0 )
		do_close( nct_relay_fd );
	nct_relay_fd = -1;
	if( nct_listen_fd >= 0 )
		do_close( nct_listen_fd );
	nct_listen_fd = -1;
	nct_pending.clear();
	nct_quota.clear();
}
