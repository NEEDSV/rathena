// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "varetyrspear.hpp"

#include <config/core.hpp>

#include "map/pc.hpp"
#include "map/status.hpp"

SkillVaretyrSpear::SkillVaretyrSpear() : SkillImplRecursiveDamageSplash(SO_VARETYR_SPEAR) {
}

void SkillVaretyrSpear::modifyDamageData(Damage& dmg, const block_list& src, const block_list& target, uint16 skill_lv) const {
	// 2017: Varetyr Spear is a single-hit skill (renewal splits it into multiple hits).
	dmg.div_ = 1;
}

void SkillVaretyrSpear::applyAdditionalEffects(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	sc_start(src,target, SC_STUN, 5 * skill_lv, skill_lv, skill_get_time(getSkillId(), skill_lv));
}

void SkillVaretyrSpear::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	const status_change *sc = status_get_sc(src);
	const map_session_data* sd = BL_CAST(BL_PC, src);


	//ATK [{( Striking Level x 50 ) + ( Varetyr Spear Skill Level x 50 )} x Caster Base Level / 100 ] %
	skillratio += -100 + 50 * skill_lv + ((sd) ? pc_checkskill(sd, SO_STRIKING) * 50 : 0);
	RE_LVL_DMOD(100);
	if (sc && sc->getSCE(SC_BLAST_OPTION))
		skillratio += (sd ? sd->status.job_level * 5 : 0);
}
