// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

/**
 * Custom Atcommands
 * Place the body of custom atcommands in this file.
 * Format:
 *	ACMD_FUNC(command_name)
 *	{
 *		<code>;
 *	}
 **/

//ACMD_FUNC(newcommand)
//{
//	clif_displaymessage(fd, "It works!");
//	clif_specialeffect(&sd->bl, EF_HEARTCASTING, AREA);
//	return 0;
//}

static bool need_noloot_token_equals(const std::string& token, const char* value)
{
	return token == value;
}

static bool need_noloot_is_delete(const std::string& token)
{
	return need_noloot_token_equals(token, "del")
		|| need_noloot_token_equals(token, "delete")
		|| need_noloot_token_equals(token, "remove")
		|| need_noloot_token_equals(token, "\273\350\301\246")
		|| need_noloot_token_equals(token, "\354\202\255\354\240\234");
}

static bool need_noloot_is_list(const std::string& token)
{
	return need_noloot_token_equals(token, "list")
		|| need_noloot_token_equals(token, "\270\361\267\317")
		|| need_noloot_token_equals(token, "\353\252\251\353\241\235");
}

static bool need_noloot_is_clear(const std::string& token)
{
	return need_noloot_token_equals(token, "clear")
		|| need_noloot_token_equals(token, "reset")
		|| need_noloot_token_equals(token, "\303\312\261\342\310\255")
		|| need_noloot_token_equals(token, "\354\264\210\352\270\260\355\231\224");
}

static std::shared_ptr<item_data> need_noloot_search_item(const char* item_name)
{
	std::shared_ptr<item_data> item_data = item_db.find(strtoul(item_name, nullptr, 10));

	if (item_data == nullptr)
		item_data = item_db.searchname(item_name);

	return item_data;
}

ACMD_FUNC(noloot)
{
	nullpo_retr(-1, sd);

	std::string args = message ? message : "";
	size_t begin = args.find_first_not_of(" \t");

	if (begin == std::string::npos) {
		clif_displaymessage(fd, msg_txt(sd,1642));
		return -1;
	}

	args.erase(0, begin);

	std::string token;
	std::string item_name;
	size_t split = args.find_first_of(" \t");

	if (split == std::string::npos) {
		token = args;
		item_name = args;
	} else {
		token = args.substr(0, split);
		item_name = args.substr(split + 1);
		size_t item_begin = item_name.find_first_not_of(" \t");

		if (item_begin != std::string::npos)
			item_name.erase(0, item_begin);
		else
			item_name.clear();
	}

	if (need_noloot_is_list(token)) {
		if (sd->state.noloot_count == 0) {
			clif_displaymessage(fd, "\276\310\301\335\261\342\040\270\361\267\317\300\314\040\272\361\276\356\040\300\326\275\300\264\317\264\331\056");
			return 0;
		}

		clif_displaymessage(fd, "\276\310\301\335\261\342\040\276\306\300\314\305\333\040\270\361\267\317\072");

		for (uint16 i = 0; i < sd->state.noloot_count && i < NEED_NOLOOT_MAX; i++) {
			std::shared_ptr<item_data> item_data = item_db.find(sd->state.noloot_itemid[i]);

			if (item_data == nullptr) {
				snprintf(atcmd_output, sizeof(atcmd_output), "- unknown item {%u}", sd->state.noloot_itemid[i]);
			} else {
				snprintf(atcmd_output, sizeof(atcmd_output), "- '%s'/'%s' {%u}", item_data->name.c_str(), item_db.create_item_link(item_data).c_str(), item_data->nameid);
			}

			clif_displaymessage(fd, atcmd_output);
		}

		return 0;
	}

	if (need_noloot_is_clear(token)) {
		memset(sd->state.noloot_itemid, 0, sizeof(sd->state.noloot_itemid));
		sd->state.noloot_count = 0;
		clif_displaymessage(fd, "\276\310\301\335\261\342\040\270\361\267\317\300\273\040\303\312\261\342\310\255\307\337\275\300\264\317\264\331\056");
		return 0;
	}

	bool remove = false;

	if (need_noloot_is_delete(token)) {
		remove = true;

		if (item_name.empty()) {
			clif_displaymessage(fd, "Usage: @noloot del <item name or ID>");
			return -1;
		}
	} else {
		item_name = args;
	}

	std::shared_ptr<item_data> item_data = need_noloot_search_item(item_name.c_str());

	if (item_data == nullptr) {
		clif_displaymessage(fd, "\301\270\300\347\307\317\301\366\040\276\312\264\302\040\276\306\300\314\305\333\270\355\057\111\104\300\324\264\317\264\331\056");
		return -1;
	}

	uint16 i = 0;

	for (i = 0; i < sd->state.noloot_count && i < NEED_NOLOOT_MAX; i++) {
		if (sd->state.noloot_itemid[i] == item_data->nameid)
			break;
	}

	if (remove) {
		if (i == sd->state.noloot_count || i == NEED_NOLOOT_MAX) {
			clif_displaymessage(fd, "\276\310\301\335\261\342\040\270\361\267\317\277\241\040\265\356\267\317\265\307\276\356\040\300\326\301\366\040\276\312\275\300\264\317\264\331\056");
			return -1;
		}

		for (; i + 1 < sd->state.noloot_count && i + 1 < NEED_NOLOOT_MAX; i++)
			sd->state.noloot_itemid[i] = sd->state.noloot_itemid[i + 1];

		if (sd->state.noloot_count > 0) {
			sd->state.noloot_count--;
			sd->state.noloot_itemid[sd->state.noloot_count] = 0;
		}

		snprintf(atcmd_output, sizeof(atcmd_output), "\276\310\301\335\261\342\040\270\361\267\317\277\241\274\255\040\301\246\260\305\307\337\275\300\264\317\264\331\072\040\047\045\163\047\057\047\045\163\047\040\173\045\165\175", item_data->name.c_str(), item_db.create_item_link(item_data).c_str(), item_data->nameid);
		clif_displaymessage(fd, atcmd_output);
		return 0;
	}

	if (i != sd->state.noloot_count && i != NEED_NOLOOT_MAX) {
		clif_displaymessage(fd, "\300\314\271\314\040\276\310\301\335\261\342\040\270\361\267\317\277\241\040\265\356\267\317\265\307\276\356\040\300\326\275\300\264\317\264\331\056");
		return -1;
	}

	if (sd->state.noloot_count >= NEED_NOLOOT_MAX) {
		snprintf(atcmd_output, sizeof(atcmd_output), "\276\310\301\335\261\342\040\270\361\267\317\300\272\040\303\326\264\353\040\045\144\260\263\261\356\301\366\040\265\356\267\317\307\322\040\274\366\040\300\326\275\300\264\317\264\331\056", NEED_NOLOOT_MAX);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	sd->state.noloot_itemid[sd->state.noloot_count++] = item_data->nameid;
	snprintf(atcmd_output, sizeof(atcmd_output), "\276\310\301\335\261\342\040\270\361\267\317\277\241\040\265\356\267\317\307\337\275\300\264\317\264\331\072\040\047\045\163\047\057\047\045\163\047\040\173\045\165\175", item_data->name.c_str(), item_db.create_item_link(item_data).c_str(), item_data->nameid);
	clif_displaymessage(fd, atcmd_output);
	return 0;
}
