// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 summer attendance SQL provider.

#ifndef MAP_NEED_SUMMER_ATTENDANCE_HPP
#define MAP_NEED_SUMMER_ATTENDANCE_HPP

#include <common/cbasetypes.hpp>

class map_session_data;

bool need_summer_attendance_enabled();
int32 need_summer_attendance_ui_progress(map_session_data* sd);
bool need_summer_attendance_should_auto_open(map_session_data* sd);
void need_summer_attendance_claim(map_session_data* sd);

void need_summer_attendance_set_char_table(const char* table);
void need_summer_attendance_set_mail_table(const char* table);
void need_summer_attendance_set_mail_attachment_table(const char* table);

void need_summer_attendance_session_start(map_session_data* sd);
void need_summer_attendance_session_pause(map_session_data* sd);
void need_summer_attendance_session_end(map_session_data* sd);

void need_summer_attendance_init();
void need_summer_attendance_final();

#endif  // MAP_NEED_SUMMER_ATTENDANCE_HPP
