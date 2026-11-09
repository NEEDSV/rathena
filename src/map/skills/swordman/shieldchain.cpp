// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "shieldchain.hpp"

#include <config/const.hpp>
#include <config/core.hpp>

#include "map/pc.hpp"
#include "map/status.hpp"

SkillShieldChain::SkillShieldChain() : WeaponSkillImpl(PA_SHIELDCHAIN) {
}

void SkillShieldChain::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	const status_change *sc = status_get_sc(src);

#ifdef RENEWAL
	// NEED: the 2017 ratio (100 + 30 x SkillLv) is the pre-renewal value. Combined with the renewal base damage
	// (shield weight/10 + 4 x refine only, see battle_calc_skill_base_damage) it dropped to 1 damage per hit on
	// high DEF targets. Use the renewal ratio that this base damage is designed for (upstream 6192559c5).
	const map_session_data* sd = BL_CAST( BL_PC, src );

	// [300 + 200 x SkillLv + Shield Weight + 4 x Shield Refine] x BaseLv / 100 % (Lv1 500% .. Lv5 1300% before shield).
	// Upstream (382d6bf54a) wrote "skillratio = -100 + ..." which drops the default 100 and gives 100% less; NEED uses "+=".
	skillratio += -100 + 300 + 200 * skill_lv;

	if( sd != nullptr ){
		int16 index = sd->equip_index[EQI_HAND_L];

		// Damage affected by the shield's weight and refine.
		if( index >= 0 && sd->inventory_data[index] != nullptr && sd->inventory_data[index]->type == IT_ARMOR ){
			skillratio += sd->inventory_data[index]->weight / 10 + 4 * sd->inventory.u.items_inventory[index].refine;
		}

		// Damage affected by shield mastery
		if( sc != nullptr && sc->getSCE( SC_SHIELD_POWER ) ){
			skillratio += skill_lv * 14 * pc_checkskill( sd, IG_SHIELD_MASTERY );
		}
	}

	RE_LVL_DMOD(100);

	if (sc && sc->getSCE(SC_SHIELD_POWER))// Whats the official increase? [Rytech]
		skillratio += skillratio * 50 / 100;
#else
	skillratio += 30 * skill_lv;
#endif
}
