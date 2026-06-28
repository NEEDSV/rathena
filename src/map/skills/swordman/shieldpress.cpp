// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "shieldpress.hpp"

#include <config/core.hpp>

#include "map/pc.hpp"
#include "map/status.hpp"

SkillShieldPress::SkillShieldPress() : WeaponSkillImpl(LG_SHIELDPRESS) {
}

void SkillShieldPress::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& skillratio, int32 mflag) const {
	const map_session_data* sd = BL_CAST(BL_PC, src);

	skillratio += -100 + 150 * skill_lv + status_get_str(src);
	if (sd) {
		short index = sd->equip_index[EQI_HAND_L];

		if (index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR)
			skillratio += sd->inventory_data[index]->weight / 10;
	}
	RE_LVL_DMOD(100);
}

void SkillShieldPress::applyAdditionalEffects(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	// 2017: Shield Press has a chance to stun the target on hit.
	int32 rate = 30 + 8 * skill_lv + (status_get_dex(src) / 10) + (status_get_lv(src) / 4);

	sc_start(src, target, SC_STUN, rate, skill_lv, skill_get_time(getSkillId(), skill_lv));
}
