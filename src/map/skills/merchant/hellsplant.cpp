// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "hellsplant.hpp"

#include <config/core.hpp>

#include "map/pc.hpp"
#include "map/status.hpp"

// GN_HELLS_PLANT
SkillHellsPlant::SkillHellsPlant() : SkillImpl(GN_HELLS_PLANT) {
}

void SkillHellsPlant::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
	// NEED 2017 fix: GN_HELLS_PLANT is a ground-unit skill. It was wrongly bound to StatusSkillImpl, whose
	// base castendPos2 is a no-op, so skill_unitsetting was never called - the plant unit was never created
	// even though the plant bottle was still consumed at castend. Restore the 2017 behavior by creating the
	// ground unit; the UNT_HELLS_PLANT onplace timer then triggers GN_HELLS_PLANT_ATK on approaching enemies.
	skill_unitsetting(src, getSkillId(), skill_lv, x, y, 0);
}

// GN_HELLS_PLANT_ATK
SkillHellsPlantAttack::SkillHellsPlantAttack() : SkillImplRecursiveDamageSplash(GN_HELLS_PLANT_ATK) {
}

void SkillHellsPlantAttack::applyAdditionalEffects(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	// 2017: stun/bleeding duration uses Duration2 (skill_get_time2).
	sc_start(src,target, SC_STUN,  20 + 10 * skill_lv, skill_lv, skill_get_time2(getSkillId(), skill_lv));
	sc_start2(src,target, SC_BLEEDING, 5 + 5 * skill_lv, skill_lv, src->id,skill_get_time2(getSkillId(), skill_lv));
}

void SkillHellsPlantAttack::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	const status_data* sstatus = status_get_status_data(*src);
	const map_session_data* sd = BL_CAST(BL_PC, src);

	skillratio += -100 + 100 * skill_lv + sstatus->int_ * (sd ? pc_checkskill(sd, AM_CANNIBALIZE) : 5); // !TODO: Confirm INT and Cannibalize bonus
	RE_LVL_DMOD(100);
}
