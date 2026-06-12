// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "comet.hpp"

#include <config/core.hpp>

#include "map/status.hpp"

SkillComet::SkillComet() : SkillImpl(WL_COMET) {
}

void SkillComet::applyAdditionalEffects(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	sc_start(src, target, SC_MAGIC_POISON, 100, skill_lv, 20000);
}

void SkillComet::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {

#ifdef NEED_2017_SKILL_FORMULA
	const status_change* sc = status_get_sc(src);
	const map_session_data* sd = BL_CAST(BL_PC, src);

	int i = (sc ? distance_xy(target->x, target->y, sc->comet_x, sc->comet_y) : 8);
	if (i <= 3)
		skillratio += 2400 + 500 * skill_lv; // 7 x 7 cell
	else if (i <= 5)
		skillratio += 1900 + 500 * skill_lv; // 11 x 11 cell
	else if (i <= 7)
		skillratio += 1400 + 500 * skill_lv; // 15 x 15 cell
	else
		skillratio += 900 + 500 * skill_lv; // 19 x 19 cell

	if (sd && sd->status.party_id) {
		map_session_data* psd;
		int p_sd[MAX_PARTY], c;

		c = 0;
		memset(p_sd, 0, sizeof(p_sd));
		party_foreachsamemap(skill_check_condition_char_sub, sd, 3, src, &c, &p_sd, getSkillId());
		c = (c > 1 ? rnd() % c : 0);

		if ((psd = map_id2sd(p_sd[c])) && pc_checkskill(psd, WL_COMET) > 0) {
			skillratio = skill_lv * 400; //MATK [{( Skill Level x 400 ) x ( Caster's Base Level / 120 )} + 2500 ] %
			RE_LVL_DMOD(120);
			skillratio += 2500;
			status_zap(psd, 0, skill_get_sp(getSkillId(), skill_lv) / 2);
		}
	}
#else
	skillratio += -100 + 2500 + 700 * skill_lv;
	RE_LVL_DMOD(100);
#endif
}

void SkillComet::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
	flag|=1;//Set flag to 1 to prevent deleting ammo (it will be deleted on group-delete).
	skill_unitsetting(src,getSkillId(),skill_lv,x,y,0);
}
