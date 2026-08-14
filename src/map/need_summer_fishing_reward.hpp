// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 summer fishing weekly-ranking reward delivery (durable outbox -> system mail).

#ifndef NEED_SUMMER_FISHING_REWARD_HPP
#define NEED_SUMMER_FISHING_REWARD_HPP

#include <common/cbasetypes.hpp>

// Override the char / mail / mail attachment table names from inter conf (mirrors attendance).
void need_summer_fishing_reward_set_char_table(const char* table);
void need_summer_fishing_reward_set_mail_table(const char* table);
void need_summer_fishing_reward_set_mail_attachment_table(const char* table);

// Start / stop the outbox consumer timer.
void need_summer_fishing_reward_init();
void need_summer_fishing_reward_final();

#endif /* NEED_SUMMER_FISHING_REWARD_HPP */
