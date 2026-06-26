// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef NEEDWIKI_HPP
#define NEEDWIKI_HPP

class map_session_data;

void do_init_needwiki(void);
void do_final_needwiki(void);
bool needwiki_reload_item_groups(void);
void clif_needwiki_open(map_session_data* sd, const char* article_id);

#endif /* NEEDWIKI_HPP */
