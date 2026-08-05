// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "magnusexorcismus.hpp"

#include "map/status.hpp"

SkillMagnusExorcismus::SkillMagnusExorcismus() : SkillImpl(PR_MAGNUS) {
}

void SkillMagnusExorcismus::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
	//Set flag to 1 to prevent deleting ammo (it will be deleted on group-delete).
	flag |= 1;

	skill_unitsetting(src, getSkillId(), skill_lv, x, y, 0);
}

void SkillMagnusExorcismus::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& base_skillratio, int32 mflag) const {
	const status_data* tstatus = status_get_status_data(*target);

	// NEED bishop rework (PvE access). In renewal Magnus already hits every target - the demon/undead gate in
	// skill_unit_onplace_timer (UNT_MAGNUS) is pre-renewal only - so no target-restriction change is needed.
	// Special targets (demon race, undead race, or undead element) keep the current NEED damage (+30). Plain
	// monsters take 70% of that special damage. Monsters only: players (PvP/BG/GvG) fall through to the
	// existing behaviour and are never newly damaged. A single +30 is used for special targets, so
	// overlapping demon/undead conditions never stack an extra bonus.
	if (battle_check_undead(tstatus->race, tstatus->def_ele) || tstatus->race == RC_DEMON)
		base_skillratio += 30;
	else if (target->type == BL_MOB)
		base_skillratio = (base_skillratio + 30) * 70 / 100;
}
