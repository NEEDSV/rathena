// NEED Phase 0.2 - KR / EN client language detection : shared diagnostics
// Licensed under GNU GPL - see LICENCE in the main folder
//
// The language TYPES (e_need_lang, NEED_CLIENTTYPE_EN_FLAG, need_lang_* helpers) live in
// common/mmo.hpp because every server already includes it and they are pure protocol types.
// This header only carries the PoC's temporary diagnostic plumbing, so that turning the
// diagnostics off is a one-line change in exactly one file.

#ifndef NEED_LANG_HPP
#define NEED_LANG_HPP

#include <cstdio>	// fflush

#include <common/showmsg.hpp>

/**
 * Set to 0 for release builds (all NEED_LANG_LOG calls compile away to nothing).
 *
 * Why ShowNotice and not ShowInfo/ShowStatus: conf/login_athena.conf and conf/char_athena.conf
 * both ship with `console_silent: 3` (1=Information + 2=Status), which silently swallows those
 * two levels on the login- and char-server. Notice (4) is not masked, so the diagnostics are
 * visible without editing the operator's configuration.
 *
 * Why the explicit fflush: when a server's stdout is redirected to a file the CRT switches to
 * block buffering, so a diagnostic line would otherwise sit in a 4 KB buffer instead of
 * appearing when the event happens.
 *
 * No password, token, MD5 or IP material is ever printed by these logs - only account id,
 * clienttype and the resolved language.
 */
/**
 * Phase 0.2.2 release cleanup: this is now 0, so every NEED_LANG_LOG call and the
 * need_lang_force_en() test switch are compiled out of the binaries entirely.
 *
 * To re-enable the diagnostics for a verification pass, set this to 1 and rebuild
 * login-server / char-server / map-server. The KR-fallback behaviour (need_lang_sanitize) is
 * NOT affected by this switch - it is always active.
 */
#ifndef NEED_LANG_DEBUG
	#define NEED_LANG_DEBUG 0
#endif

#if NEED_LANG_DEBUG
	#define NEED_LANG_LOG(fmt, ...) \
		do { \
			ShowNotice( fmt, __VA_ARGS__ ); \
			fflush( stdout ); \
		} while( 0 )
#else
	#define NEED_LANG_LOG(fmt, ...) do {} while( 0 )
#endif

#if NEED_LANG_DEBUG
	#include <cstdlib>	// getenv

/**
 * TEST ONLY - marker injection for the PoC.
 *
 * Phase 0.2 delivers the SERVER half of the language chain; the client-side hook that ORs
 * NEED_CLIENTTYPE_EN_FLAG into its login packet is still outstanding (see the Phase 0.2 report).
 * Until it exists there is no way to make the real client announce itself as EN, which would
 * leave the whole char / auth_node / map / map-change chain untestable.
 *
 * With the environment variable NEED_LANG_FORCE_EN=1 set on the LOGIN-SERVER process only, the
 * login-server pretends every client set the marker. Everything downstream (char-server
 * conversion, auth_node, 0x2afd, map_session_data, character-select round trip, map-server
 * change) then runs exactly the production code path - only the single byte the client will
 * eventually set itself is injected here.
 *
 * It is evaluated once, it is compiled out entirely when NEED_LANG_DEBUG is 0, and it is never
 * read from a config file so it cannot be switched on by accident in production.
 */
static inline bool need_lang_force_en( void ){
	static int8 cached = -1;

	if( cached < 0 ){
		const char* v = getenv( "NEED_LANG_FORCE_EN" );

		cached = ( v != nullptr && v[0] == '1' ) ? 1 : 0;

		if( cached ){
			ShowWarning( "[NEED LANG] TEST MODE: NEED_LANG_FORCE_EN=1 - every login is treated as an EN client.\n" );
			fflush( stdout );
		}
	}

	return cached != 0;
}
#endif

#endif /* NEED_LANG_HPP */
