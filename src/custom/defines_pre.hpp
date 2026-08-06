// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef CONFIG_CUSTOM_DEFINES_PRE_HPP
#define CONFIG_CUSTOM_DEFINES_PRE_HPP

/**
 * rAthena configuration file (http://rathena.org)
 * For detailed guidance on these check http://rathena.org/wiki/SRC/config/
 **/
#define PACKETVER 20250604

// Temporary: compile the attendance UI compatibility PoC without reward delivery.
// Remove this define before implementing or deploying the real attendance service.
#define NEED_ATTENDANCE_UI_POC

#ifndef NEED_NOLOOT_MAX
#define NEED_NOLOOT_MAX 30
#endif

#endif /* CONFIG_CUSTOM_DEFINES_PRE_HPP */
