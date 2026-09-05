// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// Temporary NEED Wiki bootstrap diagnostics. Keep every message secret-free.

#ifndef NEEDWIKI_DIAGNOSTICS_HPP
#define NEEDWIKI_DIAGNOSTICS_HPP

#include <common/showmsg.hpp>

// 0: disabled, 1: debug, 2: info (temporary smoke-test default)
#define NEEDWIKI_DIAGNOSTIC_LOG_LEVEL 2

#if NEEDWIKI_DIAGNOSTIC_LOG_LEVEL >= 2
	#define NEEDWIKI_DIAG(...) ShowInfo(__VA_ARGS__)
#elif NEEDWIKI_DIAGNOSTIC_LOG_LEVEL == 1
	#define NEEDWIKI_DIAG(...) ShowDebug(__VA_ARGS__)
#else
	#define NEEDWIKI_DIAG(...) ((void)0)
#endif

#endif // NEEDWIKI_DIAGNOSTICS_HPP
