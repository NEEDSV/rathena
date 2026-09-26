// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef NEEDWIKI_CONTROLLER_HPP
#define NEEDWIKI_CONTROLLER_HPP

#include "http.hpp"

HANDLER_FUNC(needwiki_test);
HANDLER_FUNC(needwiki_bootstrap_start);
HANDLER_FUNC(needwiki_auth);
HANDLER_FUNC(needwiki_navi);
HANDLER_FUNC(needwiki_showitem);
HANDLER_FUNC(needwiki_showgroup);
HANDLER_FUNC(needwiki_itemgroups);
HANDLER_FUNC(needwiki_report);

/// Refuses an oversized POST /api/wiki/report before its body is read. Returns true when handled.
bool needwiki_report_pre_routing(const Request& req, Response& res);

#endif /* NEEDWIKI_CONTROLLER_HPP */
