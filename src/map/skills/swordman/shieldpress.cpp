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
	const status_change* sc = status_get_sc(src);

#ifdef NEED_2017_SKILL_FORMULA
	skillratio += -100 + 150 * skill_lv + status_get_str(src);
	if (sd) {
		short index = sd->equip_index[EQI_HAND_L];

		if (index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR)
			skillratio += sd->inventory_data[index]->weight / 10;
	}
	RE_LVL_DMOD(100);
#else
	skillratio += -100 + 200 * skill_lv;
	if (sd != nullptr) {
		// Shield Press only considers base STR without job bonus
		skillratio += sd->status.str;

		if (sc != nullptr && sc->getSCE(SC_SHIELD_POWER)) {
			skillratio += skill_lv * 15 * pc_checkskill(sd, IG_SHIELD_MASTERY);
		}

		int16 index = sd->equip_index[EQI_HAND_L];
		if (index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR) {
			skillratio += sd->inventory_data[index]->weight / 10;
		}
	}
	RE_LVL_DMOD(100);
#endif
}

#ifdef NEED_2017_SKILL_BEHAVIOR
void SkillShieldPress::applyAdditionalEffects(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	// 2017: Shield Press has a chance to stun the target on hit.
	int32 rate = 30 + 8 * skill_lv + (status_get_dex(src) / 10) + (status_get_lv(src) / 4);

	sc_start(src, target, SC_STUN, rate, skill_lv, skill_get_time(getSkillId(), skill_lv));
}
#endif
