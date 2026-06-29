// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "kunaiexplosion.hpp"

#include "map/pc.hpp"
#include "map/status.hpp"

SkillKunaiExplosion::SkillKunaiExplosion() : SkillImpl(KO_BAKURETSU) {
}

void SkillKunaiExplosion::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	const status_data* sstatus = status_get_status_data(*src);
	const map_session_data* sd = BL_CAST(BL_PC, src);

	skillratio += -100 + (sd ? pc_checkskill(sd,NJ_TOBIDOUGU) : 1) * (50 + sstatus->dex / 4) * skill_lv * 4 / 10;
	RE_LVL_DMOD(120);
	skillratio += 10 * (sd ? sd->status.job_level : 1);
}

void SkillKunaiExplosion::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
	flag |= 1; // Set flag to 1 to prevent deleting ammo (it will be deleted on group-delete).
	skill_unitsetting(src, getSkillId(), skill_lv, x, y, 0);
}
