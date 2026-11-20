// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "twilightalchemy3.hpp"

#include <unordered_map>

#include "map/clif.hpp"
#include "map/itemdb.hpp"
#include "map/pc.hpp"

SkillTwilightAlchemy3::SkillTwilightAlchemy3() : SkillImpl(AM_TWILIGHT3) {
}

void SkillTwilightAlchemy3::castendNoDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	map_session_data* sd = BL_CAST(BL_PC, src);

	if (sd) {
		int16 acid_idx = -1, fire_idx = -1;
		// Both recipes must be available before consuming any manufacturing materials.
		// Fire Bottles require pre-made Alcohol from the inventory, via produce_db.
		if (!(acid_idx = skill_can_produce_mix(sd,ITEMID_ACID_BOTTLE,-1, 50)) //50 Acid Bottle
			|| !(fire_idx = skill_can_produce_mix(sd,ITEMID_FIRE_BOTTLE,-1, 50)) //50 Flame Bottle
		) {
			clif_skill_fail( *sd, getSkillId() );
			return;
		}
		// Individual recipe checks cannot account for shared materials (e.g. Empty Bottles).
		// Sum the actual DB requirements; skill_produce_mix remains the only consumer.
		std::unordered_map<t_itemid, uint32> materials;
		for (int16 idx : { acid_idx, fire_idx }) {
			const auto& recipe = skill_produce_db[idx - 1];

			for (int32 i = 0; i < MAX_PRODUCE_RESOURCE; i++) {
				if (recipe.mat_id[i] && recipe.mat_amount[i])
					materials[recipe.mat_id[i]] += 50 * recipe.mat_amount[i];
			}
		}
		for (const auto& material : materials) {
			if (pc_inventory_count(sd, material.first) < material.second) {
				clif_skill_fail( *sd, getSkillId() );
				return;
			}
		}
		clif_skill_nodamage(src,*target,getSkillId(),skill_lv);
		skill_produce_mix(sd, getSkillId(), ITEMID_ACID_BOTTLE, 0, 0, 0, 50, acid_idx-1);
		skill_produce_mix(sd, getSkillId(), ITEMID_FIRE_BOTTLE, 0, 0, 0, 50, fire_idx-1);
	}
}
