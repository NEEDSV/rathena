// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "hundredspear.hpp"

#include <config/core.hpp>

#include "map/pc.hpp"
#include "map/skill.hpp"
#include "map/status.hpp"

SkillHundredSpear::SkillHundredSpear() : SkillImplRecursiveDamageSplash(RK_HUNDREDSPEAR) {
}

void SkillHundredSpear::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	const map_session_data* sd = BL_CAST(BL_PC, src);

	skillratio += 500 + (80 * skill_lv);
	if (sd) {
		short index = sd->equip_index[EQI_HAND_R];

		if (index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_WEAPON)
			skillratio += max(10000 - sd->inventory_data[index]->weight, 0) / 10;
		skillratio += 50 * pc_checkskill(sd, LK_SPIRALPIERCE);
	} // (1 + [(Casters Base Level - 100) / 200])
	skillratio = skillratio * (100 + (status_get_lv(src) - 100) / 2) / 100;
}

void SkillHundredSpear::castendDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	SkillImplRecursiveDamageSplash::castendDamageId(src, target, skill_lv, tick, flag);

	if (!(flag & 1) && rnd() % 100 < 10 + 3 * skill_lv) {
		const map_session_data* sd = BL_CAST(BL_PC, src);
		const uint16 skill_req = sd ? pc_checkskill(sd, KN_SPEARBOOMERANG) : skill_get_max(KN_SPEARBOOMERANG);

		if (skill_req > 0) {
			skill_blown(src, target, 6, -1, BLOWN_NONE);
			skill_castend_damage_id(src, target, KN_SPEARBOOMERANG, skill_req, tick, 0);
		}
	}
}
