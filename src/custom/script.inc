// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

/**
 * Custom Script Commands
 * Place the body of custom script commands in this file.
 * Format:
 *	BUILDIN_FUNC(command_name)
 *	{
 *		<code>;
 *	}
 **/

//BUILDIN_FUNC(example)
//{
//	ShowInfo("buildin_example: It works!\n");
//	script_pushint(st,1);
//	return 0;
//}

/**
 * Author: [royrdev]
 * skillselectmenu(<skill_id1>{,<skill_id2>,...})
 * @param skill_id1, skill_id2, ... Variable number of skill IDs
 * @return Selected skill ID, 0 if canceled, -1 on error
 */
BUILDIN_FUNC(skillselectmenu)
{
	map_session_data* sd;
	if( !script_rid2sd(sd) ) {
		ShowError("buildin_skillselectmenu: No player attached.\n");
		script_pushint(st, -1);
		return SCRIPT_CMD_FAILURE;
	}

	int32 fd = sd->fd;

	if( !session_isActive(fd) ) {
		ShowError("buildin_skillselectmenu: Invalid session for player '%s'.\n", sd->status.name);
		script_pushint(st, -1);
		return SCRIPT_CMD_FAILURE;
	}
	if( !script_hasdata(st, 2) ) {
		ShowError("buildin_skillselectmenu: No skill IDs provided.\n");
		script_pushint(st, -1);
		return SCRIPT_CMD_FAILURE;
	}
	if( sd->menuskill_id != 0 ) {
		ShowWarning("buildin_skillselectmenu: Player '%s' already has a skill menu open.\n", sd->status.name);
		script_pushint(st, 0);
		return SCRIPT_CMD_SUCCESS;
	}
#ifdef SECURE_NPCTIMEOUT
	sd->npc_idle_type = NPCT_MENU;
#endif
	if( !sd->state.menu_or_input ) {
		int32 skill_count = script_lastdata(st) - 1;
		WFIFOHEAD(fd, (8 + (skill_count * 2)));
		WFIFOW(fd, 0) = 0x0442;
		WFIFOW(fd, 2) = (8 + (skill_count * 2));
		WFIFOL(fd, 4) = skill_count;
		for( int32 i = 0; i < skill_count; ++i ) {
			int32 skill_id = script_getnum(st, (i + 2));

			if( skill_get_index(skill_id) == 0 ) {
				ShowError("buildin_skillselectmenu: Invalid skill ID '%d' at index %d.\n", skill_id, i);
				script_pushint(st, -1);
				return SCRIPT_CMD_FAILURE;
			}
			WFIFOW(fd, (8 + (i * 2))) = skill_id;
		}
		WFIFOSET(fd, WFIFOW(fd, 2));
		sd->menuskill_val2 = st->oid;
		st->state = RERUNLINE;
		sd->state.menu_or_input = 1;
	}
	else {
		sd->state.menu_or_input = 0;
		script_pushint(st, (sd->npc_amount > 0) ? sd->npc_amount : 0);
		sd->menuskill_val2 = 0;
		st->state = RUN;
	}
	return SCRIPT_CMD_SUCCESS;
}

/**
 * Author: [royrdev]
 * skillselectmenuarray(<array_name>)
 * @param array_name Array containing skill IDs
 * @return Selected skill ID, 0 if canceled, -1 on error
 */
BUILDIN_FUNC(skillselectmenuarray)
{
	map_session_data* sd;
	if( !script_rid2sd(sd) ) {
		ShowError("buildin_skillselectmenuarray: No player attached.\n");
		script_pushint(st, -1);
		return SCRIPT_CMD_FAILURE;
	}
	int32 fd = sd->fd;
	if( !session_isActive(fd) ) {
		ShowError("buildin_skillselectmenuarray: Invalid session for player '%s'.\n", sd->status.name);
		script_pushint(st, -1);
		return SCRIPT_CMD_FAILURE;
	}
	if( sd->menuskill_id != 0 ) {
		ShowWarning("buildin_skillselectmenuarray: Player '%s' already has a skill menu open.\n", sd->status.name);
		script_pushint(st, 0);
		return SCRIPT_CMD_SUCCESS;
	}
#ifdef SECURE_NPCTIMEOUT
	sd->npc_idle_type = NPCT_MENU;
#endif
	if( !sd->state.menu_or_input ) {
		struct script_data* data = script_getdata(st, 2);
		const char* name = reference_getname(data);
		int32 id = reference_getid(data);
		uint32 start = reference_getindex(data);
		struct reg_db* ref = reference_getref(data);
		int64 uid = reference_uid(id, 0);
		uint32 size = script_array_highest_key(st, sd, name, ref);
		if( size == 0 ) {
			ShowError("buildin_skillselectmenuarray: Empty array '%s'.\n", name);
			script_pushint(st, -1);
			return SCRIPT_CMD_FAILURE;
		}
		int32 skill_count = size - start;
		if( skill_count <= 0 ) {
			ShowError("buildin_skillselectmenuarray: No elements in array '%s' from index %u.\n", name, start);
			script_pushint(st, -1);
			return SCRIPT_CMD_FAILURE;
		}
		WFIFOHEAD(fd, (8 + (skill_count * 2)));
		WFIFOW(fd, 0) = 0x0442;
		WFIFOW(fd, 2) = (8 + (skill_count * 2));
		WFIFOL(fd, 4) = skill_count;
		for( int32 i = 0; i < skill_count; ++i ) {
			int64 elem_uid = reference_uid(id, start + i);
			int32 skill_id = static_cast<int32>(get_val2_num(st, elem_uid, ref));
			if( skill_get_index(skill_id) == 0 ) {
				ShowError("buildin_skillselectmenuarray: Invalid skill ID '%d' at array index %d.\n", skill_id, (start + i));
				script_pushint(st, -1);
				return SCRIPT_CMD_FAILURE;
			}
			WFIFOW(fd, (8 + (i * 2))) = skill_id;
		}
		WFIFOSET(fd, WFIFOW(fd, 2));
		sd->menuskill_val2 = st->oid;
		st->state = RERUNLINE;
		sd->state.menu_or_input = 1;
	}
	else {
		sd->state.menu_or_input = 0;
		script_pushint(st, (sd->npc_amount > 0) ? sd->npc_amount : 0);
		sd->menuskill_val2 = 0;
		st->state = RUN;
	}
	return SCRIPT_CMD_SUCCESS;
}

/**
 * Author: [royrdev]
 * getskillname(<skill_id>) / getskillname("<skill_name>")
 * Returns the display name of the specified skill.
 */
BUILDIN_FUNC(getskillname)
{
	uint16 skill_id;
	if( script_isstring(st, 2) ) {
		const char* name = script_getstr(st, 2);
		skill_id = skill_name2id(name);

		if( skill_id == 0 ) {
			ShowWarning("buildin_getskillname: Unknown skill constant '%s'.\n", name);
			script_pushconststr(st, "");
			return SCRIPT_CMD_SUCCESS;
		}
	}
	else {
		skill_id = static_cast<uint16>(script_getnum(st, 2));
	}
	if( (skill_id == 0) || (skill_get_index(skill_id) == 0) ) {
		script_pushconststr(st, "");
		return SCRIPT_CMD_SUCCESS;
	}
	script_pushstrcopy(st, skill_get_name(skill_id));
	return SCRIPT_CMD_SUCCESS;
}

/**
 * Author: [royrdev]
 * getskilldesc(<skill_id>) / getskilldesc("<skill_name>")
 * Returns the description of the specified skill.
 */
BUILDIN_FUNC(getskilldesc)
{
	uint16 skill_id;
	if( script_isstring(st, 2) ) {
		const char* name = script_getstr(st, 2);
		skill_id = skill_name2id(name);

		if( skill_id == 0 ) {
			ShowWarning("buildin_getskilldesc: Unknown skill constant '%s'.\n", name);
			script_pushconststr(st, "");
			return SCRIPT_CMD_SUCCESS;
		}
	}
	else {
		skill_id = static_cast<uint16>(script_getnum(st, 2));
	}
	if( (skill_id == 0) || (skill_get_index(skill_id) == 0) ) {
		script_pushconststr(st, "");
		return SCRIPT_CMD_SUCCESS;
	}

	script_pushstrcopy(st, skill_get_desc(skill_id));
	return SCRIPT_CMD_SUCCESS;
}

/**
 * Author: [royrdev]
 * getplagiarizedskill({<type>})
 * Returns the currently plagiarized skill ID.
 */
BUILDIN_FUNC(getplagiarizedskill)
{
	map_session_data* sd;
	if( !script_rid2sd(sd) ) {
		script_pushint(st, 0);
		return SCRIPT_CMD_FAILURE;
	}

	int32 type = script_hasdata(st, 2) ? script_getnum(st, 2) : 1;
	uint16 idx = 0;

	if( type == 1 ) {
		idx = sd->cloneskill_idx;
	} else if( type == 2 ) {
		idx = sd->reproduceskill_idx;
	}
	else {
		ShowWarning("buildin_getplagiarizedskill: Invalid type %d.\n", type);
		script_pushint(st, 0);
		return SCRIPT_CMD_SUCCESS;
	}
	if( (idx > 0) && (sd->status.skill[idx].flag == SKILL_FLAG_PLAGIARIZED) ) {
		script_pushint(st, sd->status.skill[idx].id);
	}
	else {
		script_pushint(st, 0);
	}

	return SCRIPT_CMD_SUCCESS;
}

/**
 * Author: [royrdev]
 * getplagiarizedskilllv({<type>})
 * Returns the level of the currently plagiarized skill.
 * @param type 1 = Plagiarism (default), 2 = Reproduce
 */
BUILDIN_FUNC(getplagiarizedskilllv)
{
	map_session_data* sd;
	if( !script_rid2sd(sd) ) {
		script_pushint(st, 0);
		return SCRIPT_CMD_FAILURE;
	}

	int32 type = script_hasdata(st, 2) ? script_getnum(st, 2) : 1;
	uint16 idx = 0;

	if( type == 1 ) {
		idx = sd->cloneskill_idx;
	} else if( type == 2 ) {
		idx = sd->reproduceskill_idx;
	}
	else {
		ShowWarning("buildin_getplagiarizedskilllv: Invalid type %d.\n", type);
		script_pushint(st, 0);
		return SCRIPT_CMD_SUCCESS;
	}
	if( (idx > 0) && (sd->status.skill[idx].flag == SKILL_FLAG_PLAGIARIZED) ) {
		script_pushint(st, sd->status.skill[idx].lv);
	}
	else {
		script_pushint(st, 0);
	}

	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(getskillid)
{
	const char* str = script_getstr(st,2);

	if (!str || !*str) {
		script_pushint(st,-1);
		return SCRIPT_CMD_SUCCESS;
	}

	if (atoi(str) > 0) {
		script_pushint(st,atoi(str));
		return SCRIPT_CMD_SUCCESS;
	}

	// skill constant
	int skill_id = skill_name2id(str);

	if (skill_id > 0) {
		script_pushint(st,skill_id);
		return SCRIPT_CMD_SUCCESS;
	}

	script_pushint(st,-1);
	return SCRIPT_CMD_SUCCESS;
}
