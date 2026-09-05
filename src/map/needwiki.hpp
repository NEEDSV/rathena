// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef NEEDWIKI_HPP
#define NEEDWIKI_HPP

#include <common/cbasetypes.hpp>

class map_session_data;

void do_init_needwiki(void);
void do_final_needwiki(void);
bool needwiki_reload_item_groups(void);
void needwiki_session_start(map_session_data* sd);
void needwiki_session_end(map_session_data* sd);
int32 needwiki_bind_code(map_session_data* sd, const char* code);

#endif /* NEEDWIKI_HPP */
