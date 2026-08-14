// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED 2026 summer exchange and token shop limits.

#ifndef MAP_NEED_SUMMER_SHOP_HPP
#define MAP_NEED_SUMMER_SHOP_HPP

#include <memory>
#include <vector>

class map_session_data;
struct s_barter_purchase;
struct s_npc_barter;

enum class need_summer_shop_begin_result {
	NOT_APPLICABLE,
	ALLOWED,
	REJECTED,
};

struct need_summer_shop_transaction {
	void* state = nullptr;
};

need_summer_shop_begin_result need_summer_shop_begin(map_session_data& sd,
	const std::shared_ptr<s_npc_barter>& barter, const std::vector<s_barter_purchase>& purchases,
	need_summer_shop_transaction& transaction);
bool need_summer_shop_finish(map_session_data& sd, need_summer_shop_transaction& transaction, bool delivered);

#endif  // MAP_NEED_SUMMER_SHOP_HPP
