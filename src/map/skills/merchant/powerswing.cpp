// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "powerswing.hpp"

#include <config/core.hpp>

#include "map/pc.hpp"
#include "map/status.hpp"

SkillPowerSwing::SkillPowerSwing() : WeaponSkillImpl(NC_POWERSWING) {
}

void SkillPowerSwing::modifyDamageData(Damage& dmg, const block_list& src, const block_list& target, uint16 skill_lv) const {
	const status_change *sc = status_get_sc(&src);

	if (sc != nullptr && sc->hasSCE(SC_ABR_BATTLE_WARIOR))
		dmg.div_ = -2;
}

void SkillPowerSwing::applyAdditionalEffects(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	sc_start(src,target, SC_STUN, 10, skill_lv, skill_get_time(getSkillId(), skill_lv));
	// 2017: Power Swing has a 5*Lv% chance to trigger Axe Boomerang as a followup.
	// 2026 dropped it. Restore on the Power Swing path only; the direct
	// skill_castend_damage_id call bypasses Axe Boomerang's SP/cooldown/item cost.
	map_session_data* sd = BL_CAST(BL_PC, src);

	if (rnd() % 100 < 5 * skill_lv)
		skill_castend_damage_id(src, target, NC_AXEBOOMERANG, ((sd) ? pc_checkskill(sd, NC_AXEBOOMERANG) : skill_get_max(NC_AXEBOOMERANG)), tick, 1);
}

void SkillPowerSwing::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {

	skillratio += -100 + status_get_str(src) + status_get_dex(src);
	RE_LVL_DMOD(100);
	skillratio += 300 + 100 * skill_lv;
}
