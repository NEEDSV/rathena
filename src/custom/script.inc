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

BUILDIN_FUNC(need_register_equipment_build)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, NEED_BUILD_ERROR_NO_PLAYER);
		return SCRIPT_CMD_SUCCESS;
	}

	script_pushint(st, need_equipment_build_register(sd, script_getnum(st, 2), script_getstr(st, 3), script_getstr(st, 4)));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_get_equipment_build_count)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, NEED_BUILD_ERROR_NO_PLAYER);
		return SCRIPT_CMD_SUCCESS;
	}

	script_pushint(st, need_equipment_build_count(sd->status.account_id));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_show_last_equipment_build)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, NEED_BUILD_ERROR_NO_PLAYER);
		return SCRIPT_CMD_SUCCESS;
	}

	need_equipment_build_page page;
	const int64 count = need_equipment_build_owner_list(sd, 0, page);
	if (count <= 0) {
		script_pushint(st, count);
		return SCRIPT_CMD_SUCCESS;
	}
	const uint64 build_id = page.entries.front().build_id;

	script_pushint(st, need_equipment_build_owner_detail(sd, build_id) > 0 ? static_cast<int64>(build_id) : NEED_BUILD_ERROR_DATABASE);
	return SCRIPT_CMD_SUCCESS;
}

static void need_build_script_clear_page(map_session_data* sd)
{
	for (const char* name : { "@need_build_ids", "@need_build_job_ids", "@need_build_categories", "@need_build_statuses", "@need_build_like_counts",
		"@need_build_titles$", "@need_build_job_names$", "@need_build_owners$", "@need_build_has_more" })
		script_cleararray_pc(sd, name);
	script_setarray_pc(sd, "@need_build_has_more", 0, 0, nullptr);
}

static void need_build_script_set_page(map_session_data* sd, const need_equipment_build_page& page)
{
	for (size_t index = 0; index < page.entries.size(); ++index) {
		const need_equipment_build_list_entry& entry = page.entries[index];
		script_setarray_pc(sd, "@need_build_ids", static_cast<uint32>(index), entry.build_id, nullptr);
		script_setarray_pc(sd, "@need_build_job_ids", static_cast<uint32>(index), entry.job_id, nullptr);
		script_setarray_pc(sd, "@need_build_categories", static_cast<uint32>(index), entry.category, nullptr);
		script_setarray_pc(sd, "@need_build_statuses", static_cast<uint32>(index), entry.status, nullptr);
		script_setarray_pc(sd, "@need_build_like_counts", static_cast<uint32>(index), entry.like_count, nullptr);
		setd_sub_str(nullptr, sd, "@need_build_titles$", static_cast<int32>(index), entry.title.c_str(), nullptr);
		setd_sub_str(nullptr, sd, "@need_build_job_names$", static_cast<int32>(index), entry.job_name.c_str(), nullptr);
		setd_sub_str(nullptr, sd, "@need_build_owners$", static_cast<int32>(index), entry.owner_name.c_str(), nullptr);
	}
	script_setarray_pc(sd, "@need_build_has_more", 0, page.has_more ? 1 : 0, nullptr);
}

BUILDIN_FUNC(need_equipment_build_category_name)
{
	map_session_data* sd = nullptr;
	script_rid2sd(sd);
	script_pushstrcopy(st, need_equipment_build_category_name(script_getnum(st, 2), sd));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_equipment_build_status_name)
{
	map_session_data* sd = nullptr;
	script_rid2sd(sd);
	script_pushstrcopy(st, need_equipment_build_status_name(script_getnum(st, 2), sd));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_equipment_build_is_admin)
{
	map_session_data* sd = nullptr;
	script_pushint(st, script_rid2sd(sd) && need_equipment_build_is_admin(sd) ? 1 : 0);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_get_admin_equipment_build_list)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, NEED_BUILD_ERROR_NO_PLAYER);
		return SCRIPT_CMD_SUCCESS;
	}
	need_equipment_build_page page;
	need_build_script_clear_page(sd);
	const int64 result = need_equipment_build_admin_list(sd, script_getnum(st, 2), script_getnum(st, 3), page);
	if (result >= 0) need_build_script_set_page(sd, page);
	script_pushint(st, result);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_search_admin_equipment_builds)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, NEED_BUILD_ERROR_NO_PLAYER);
		return SCRIPT_CMD_SUCCESS;
	}
	need_equipment_build_page page;
	need_build_script_clear_page(sd);
	const int64 result = need_equipment_build_admin_search(sd, script_getnum(st, 2), script_getstr(st, 3), script_getnum(st, 4), page);
	if (result >= 0) need_build_script_set_page(sd, page);
	script_pushint(st, result);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_get_public_equipment_build_jobs)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, NEED_BUILD_ERROR_NO_PLAYER);
		return SCRIPT_CMD_SUCCESS;
	}
	std::vector<need_equipment_build_job_entry> jobs;
	need_build_script_clear_page(sd);
	const int64 result = need_equipment_build_public_jobs(script_getnum(st, 2), jobs);
	if (result >= 0) {
		for (size_t index = 0; index < jobs.size(); ++index) {
			script_setarray_pc(sd, "@need_build_job_ids", static_cast<uint32>(index), jobs[index].job_id, nullptr);
			setd_sub_str(nullptr, sd, "@need_build_job_names$", static_cast<int32>(index), jobs[index].name.c_str(), nullptr);
		}
	}
	script_pushint(st, result);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_get_public_equipment_build_list)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, NEED_BUILD_ERROR_NO_PLAYER);
		return SCRIPT_CMD_SUCCESS;
	}
	need_equipment_build_page page;
	need_build_script_clear_page(sd);
	const int64 result = need_equipment_build_public_list(script_getnum(st, 2), script_getnum(st, 3), script_getnum(st, 4), script_getnum(st, 5), page);
	if (result >= 0) need_build_script_set_page(sd, page);
	script_pushint(st, result);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_get_my_equipment_build_list)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, NEED_BUILD_ERROR_NO_PLAYER);
		return SCRIPT_CMD_SUCCESS;
	}
	need_equipment_build_page page;
	need_build_script_clear_page(sd);
	const int64 result = need_equipment_build_owner_list(sd, script_getnum(st, 2), page);
	if (result >= 0) need_build_script_set_page(sd, page);
	script_pushint(st, result);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_show_admin_equipment_build)
{
	map_session_data* sd = nullptr;
	script_pushint(st, script_rid2sd(sd) ? need_equipment_build_admin_detail(sd, script_getnum64(st, 2)) : NEED_BUILD_ERROR_NO_PLAYER);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_show_public_equipment_build)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, NEED_BUILD_ERROR_NO_PLAYER);
		return SCRIPT_CMD_SUCCESS;
	}
	uint32 like_count = 0;
	bool liked = false;
	bool is_owner = false;
	script_cleararray_pc(sd, "@need_build_detail_like_count");
	script_cleararray_pc(sd, "@need_build_detail_liked");
	script_cleararray_pc(sd, "@need_build_detail_is_owner");
	script_setarray_pc(sd, "@need_build_detail_like_count", 0, 0, nullptr);
	script_setarray_pc(sd, "@need_build_detail_liked", 0, 0, nullptr);
	script_setarray_pc(sd, "@need_build_detail_is_owner", 0, 0, nullptr);
	const int64 result = need_equipment_build_public_detail(sd, script_getnum64(st, 2), &like_count, &liked, &is_owner);
	script_setarray_pc(sd, "@need_build_detail_like_count", 0, like_count, nullptr);
	script_setarray_pc(sd, "@need_build_detail_liked", 0, liked ? 1 : 0, nullptr);
	script_setarray_pc(sd, "@need_build_detail_is_owner", 0, is_owner ? 1 : 0, nullptr);
	script_pushint(st, result);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_show_my_equipment_build)
{
	map_session_data* sd = nullptr;
	script_pushint(st, script_rid2sd(sd) ? need_equipment_build_owner_detail(sd, script_getnum64(st, 2)) : NEED_BUILD_ERROR_NO_PLAYER);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_open_equipment_build_window)
{
	map_session_data* sd = nullptr;
	script_pushint(st, script_rid2sd(sd) ? need_equipment_build_open_window(sd, script_getnum64(st, 2), script_getnum(st, 3)) : NEED_BUILD_VIEW_UNAVAILABLE);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_review_equipment_build)
{
	map_session_data* sd = nullptr;
	script_pushint(st, script_rid2sd(sd) ? need_equipment_build_review(sd, script_getnum64(st, 2), script_getnum(st, 3), script_getstr(st, 4)) : NEED_BUILD_ERROR_NO_PLAYER);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_approve_equipment_build)
{
	map_session_data* sd = nullptr;
	script_pushint(st, script_rid2sd(sd) ? need_equipment_build_approve(sd, script_getnum64(st, 2), script_getnum(st, 3), script_getnum(st, 4)) : NEED_BUILD_ERROR_NO_PLAYER);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_cancel_equipment_build)
{
	map_session_data* sd = nullptr;
	script_pushint(st, script_rid2sd(sd) ? need_equipment_build_cancel(sd, script_getnum64(st, 2)) : NEED_BUILD_ERROR_NO_PLAYER);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_toggle_equipment_build_like)
{
	map_session_data* sd = nullptr;
	script_pushint(st, script_rid2sd(sd) ? need_equipment_build_toggle_like(sd, script_getnum64(st, 2)) : NEED_BUILD_LIKE_FAILED);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_get_my_equipment_build_reward_list)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, NEED_BUILD_REWARD_NO_PLAYER);
		return SCRIPT_CMD_SUCCESS;
	}
	need_equipment_build_reward_page page;
	for (const char* name : { "@need_reward_build_ids", "@need_reward_item_ids", "@need_reward_amounts", "@need_reward_titles$", "@need_reward_has_more" })
		script_cleararray_pc(sd, name);
	script_setarray_pc(sd, "@need_reward_has_more", 0, 0, nullptr);
	const int64 result = need_equipment_build_reward_list(sd, script_getnum(st, 2), page);
	if (result >= 0) {
		for (size_t index = 0; index < page.entries.size(); ++index) {
			const need_equipment_build_reward_entry& entry = page.entries[index];
			script_setarray_pc(sd, "@need_reward_build_ids", static_cast<uint32>(index), entry.build_id, nullptr);
			script_setarray_pc(sd, "@need_reward_item_ids", static_cast<uint32>(index), entry.reward_item_id, nullptr);
			script_setarray_pc(sd, "@need_reward_amounts", static_cast<uint32>(index), entry.reward_amount, nullptr);
			setd_sub_str(nullptr, sd, "@need_reward_titles$", static_cast<int32>(index), entry.title.c_str(), nullptr);
		}
		script_setarray_pc(sd, "@need_reward_has_more", 0, page.has_more ? 1 : 0, nullptr);
	}
	script_pushint(st, result);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_show_my_equipment_build_reward)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, NEED_BUILD_REWARD_NO_PLAYER);
		return SCRIPT_CMD_SUCCESS;
	}
	need_equipment_build_reward_entry entry;
	script_cleararray_pc(sd, "@need_reward_detail_item_id");
	script_cleararray_pc(sd, "@need_reward_detail_amount");
	script_cleararray_pc(sd, "@need_reward_detail_title$");
	script_setarray_pc(sd, "@need_reward_detail_item_id", 0, 0, nullptr);
	script_setarray_pc(sd, "@need_reward_detail_amount", 0, 0, nullptr);
	setd_sub_str(nullptr, sd, "@need_reward_detail_title$", 0, "", nullptr);
	const int64 result = need_equipment_build_reward_detail(sd, script_getnum64(st, 2), entry);
	if (result > 0) {
		script_setarray_pc(sd, "@need_reward_detail_item_id", 0, entry.reward_item_id, nullptr);
		script_setarray_pc(sd, "@need_reward_detail_amount", 0, entry.reward_amount, nullptr);
		setd_sub_str(nullptr, sd, "@need_reward_detail_title$", 0, entry.title.c_str(), nullptr);
	}
	script_pushint(st, result);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_claim_equipment_build_reward)
{
	map_session_data* sd = nullptr;
	script_pushint(st, script_rid2sd(sd) ? need_equipment_build_claim_reward(sd, script_getnum64(st, 2)) : NEED_BUILD_REWARD_NO_PLAYER);
	return SCRIPT_CMD_SUCCESS;
}

static int32 buildin_clearmapskillunits_sub(block_list* bl, va_list ap)
{
	if (bl == nullptr)
		return 0;

	// 해당 캐릭터가 설치한 모든 스킬 유닛 제거
	skill_clear_unitgroup(bl);
	return 0;
}

BUILDIN_FUNC(clearmapskillunits)
{
	const char* mapname = script_getstr(st, 2);
	int16 m = map_mapname2mapid(mapname);

	if (m < 0) {
		ShowWarning(
			"buildin_clearmapskillunits: Unknown map '%s'.\n",
			mapname
		);
		return SCRIPT_CMD_FAILURE;
	}

	// 맵에 있는 플레이어들이 설치한 지형 스킬만 제거
	map_foreachinmap(
		buildin_clearmapskillunits_sub,
		m,
		BL_PC
	);

	return SCRIPT_CMD_SUCCESS;
}

// NEED: Memorial dungeon IP daily reward limit
BUILDIN_FUNC(instance_ip_complete)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}
	int32 type = script_getnum(st, 2);
	if (type != IP_REWARD_PERSONAL) {
		ShowWarning("buildin_instance_ip_complete: Only IP_REWARD_PERSONAL may be called by scripts. Monster rewards are checked before core drops.\n");
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}
	script_pushint(st, instance_ip_reward_complete(sd, static_cast<e_instance_ip_reward_type>(type)));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(instance_ip_reward_remaining)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}
	if (script_hasdata(st, 2)) {
		if (script_isstring(st, 2)) {
			ShowError("buildin_instance_ip_reward_remaining: Instance DB ID must be numeric.\n");
			script_pushint(st, -1);
			return SCRIPT_CMD_SUCCESS;
		}
		int64 instance_db_id = script_getnum64(st, 2);
		if (instance_db_id <= 0 || instance_db_id > INT32_MAX) {
			ShowError("buildin_instance_ip_reward_remaining: Invalid instance DB ID %" PRId64 ".\n", instance_db_id);
			script_pushint(st, -1);
			return SCRIPT_CMD_SUCCESS;
		}
		script_pushint(st, instance_ip_reward_remaining_by_db_id(sd, static_cast<int32>(instance_db_id)));
	} else {
		script_pushint(st, instance_ip_reward_remaining(sd));
	}
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(instance_ip_reward_limit)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}
	if (script_hasdata(st, 2)) {
		if (script_isstring(st, 2)) {
			ShowError("buildin_instance_ip_reward_limit: Instance DB ID must be numeric.\n");
			script_pushint(st, -1);
			return SCRIPT_CMD_SUCCESS;
		}
		int64 instance_db_id = script_getnum64(st, 2);
		if (instance_db_id <= 0 || instance_db_id > INT32_MAX) {
			ShowError("buildin_instance_ip_reward_limit: Invalid instance DB ID %" PRId64 ".\n", instance_db_id);
			script_pushint(st, -1);
			return SCRIPT_CMD_SUCCESS;
		}
		script_pushint(st, instance_ip_reward_limit_by_db_id(sd, static_cast<int32>(instance_db_id)));
	} else {
		script_pushint(st, instance_ip_reward_limit(sd));
	}
	return SCRIPT_CMD_SUCCESS;
}

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

// 반환값
//  1 : 성공
//  0 : 수량 부족, 무게 초과, 인벤토리 공간 부족 또는 잘못된 요청
// -1 : 창고 인출 권한 없음
// -2 : 다른 창고 창이 열려 있음
int32 storage_storageget_script(
	map_session_data* sd,
	struct s_storage* stor,
	int32 index,
	int32 amount,
	bool favorite
) {
	if (sd == nullptr || stor == nullptr)
		return 0;

	if (!stor->state.get)
		return -1;

	if (index < 0 || index >= ARRAYLENGTH(stor->u.items_storage))
		return 0;

	struct item& storage_item = stor->u.items_storage[index];

	if (storage_item.nameid <= 0)
		return 0;

	if (amount < 1 || amount > storage_item.amount)
		return 0;

	unsigned char flag = pc_additem(
		sd,
		&storage_item,
		amount,
		LOG_TYPE_STORAGE,
		favorite
	);

	if (flag != ADDITEM_SUCCESS) {
		clif_additem(sd, 0, 0, flag);
		return 0;
	}

	storage_delitem(sd, stor, index, amount);
	return 1;
}

BUILDIN_FUNC(storagegetitem)
{
	map_session_data* sd = nullptr;

	if (!script_rid2sd(sd)) {
		script_pushint(st, 0);
		return SCRIPT_CMD_SUCCESS;
	}

	t_itemid nameid = script_getnum(st, 2);
	int32 amount = script_getnum(st, 3);

	if (nameid <= 0 || amount <= 0) {
		script_pushint(st, 0);
		return SCRIPT_CMD_SUCCESS;
	}

	struct item_data* id = itemdb_search(nameid);

	if (id == nullptr) {
		script_pushint(st, 0);
		return SCRIPT_CMD_SUCCESS;
	}

	// 자동 보충에서 허용하는 아이템 종류와 동일하게 제한
	switch (id->type) {
	case IT_HEALING:
	case IT_USABLE:
	case IT_ETC:
	case IT_AMMO:
		break;

	default:
		script_pushint(st, 0);
		return SCRIPT_CMD_SUCCESS;
	}

	if (!itemdb_isstackable2(id)) {
		script_pushint(st, 0);
		return SCRIPT_CMD_SUCCESS;
	}

	s_storage* stor = &sd->storage;

	// 창고 인출 권한 확인
	if (!stor->state.get) {
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}

	// 다른 창고 UI가 열린 상태에서 기본 창고를 조작하지 않음
	if (sd->state.storage_flag != 0) {
		script_pushint(st, -2);
		return SCRIPT_CMD_SUCCESS;
	}

	int32 index = -1;

	for (int32 i = 0;
		i < ARRAYLENGTH(stor->u.items_storage);
		i++
	) {
		const struct item& storage_item = stor->u.items_storage[i];

		if (storage_item.nameid != nameid)
			continue;

		if (storage_item.amount < amount)
			continue;

		index = i;
		break;
	}

	if (index < 0) {
		script_pushint(st, 0);
		return SCRIPT_CMD_SUCCESS;
	}

	int32 result = storage_storageget_script(
		sd,
		stor,
		index,
		amount,
		false
	);

	script_pushint(st, result);
	return SCRIPT_CMD_SUCCESS;
}
