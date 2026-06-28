// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "fatalmenace.hpp"

#include <config/core.hpp>

#include "map/clif.hpp"
#include "map/map.hpp"
#include "map/status.hpp"

SkillFatalMenace::SkillFatalMenace() : WeaponSkillImpl(SC_FATALMENACE) {
}

void SkillFatalMenace::modifyDamageData(Damage& dmg, const block_list& src, const block_list& target, uint16 skill_lv) const {
	const map_session_data* sd = BL_CAST(BL_PC, &src);

	if (sd != nullptr && sd->weapontype1 == W_DAGGER)
		dmg.div_++;
}

void SkillFatalMenace::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	skillratio += 100 * skill_lv;
	RE_LVL_DMOD(100);
}

void SkillFatalMenace::castendDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	if( flag&1 )
		WeaponSkillImpl::castendDamageId(src, target, skill_lv, tick, flag);
	else {
		int16 x, y;

		// 2017: pick a random destination cell for the caster; targets are warped near it (applyAdditionalEffects).
		map_search_freecell(src, 0, &x, &y, -1, -1, 0);
		skill_area_temp[4] = x;
		skill_area_temp[5] = y;
		map_foreachinrange(skill_area_sub, target, skill_get_splash(getSkillId(), skill_lv), splash_target(src), src, getSkillId(), skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_damage_id);
		skill_addtimerskill(src, tick + 800, src->id, x, y, getSkillId(), skill_lv, 0, flag); // 2017: teleport the caster itself
		clif_skill_damage( *src, *src, tick, status_get_amotion(src), 0, DMGVAL_IGNORE, 1, getSkillId(), skill_lv, DMG_SINGLE );
	}
}

void SkillFatalMenace::applyAdditionalEffects(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	// 2017: each enemy that takes damage (non status-immune) is warped near the caster's destination cell.
	// applyAdditionalEffects is only reached when dmg_lv >= ATK_DEF (damage > 0), matching the 2017 gate.
	const status_data* tstatus = status_get_status_data(*target);

	if (!status_has_mode(tstatus, MD_STATUSIMMUNE)) {
		int16 x = static_cast<int16>(skill_area_temp[4]), y = static_cast<int16>(skill_area_temp[5]);

		map_search_freecell(nullptr, target->m, &x, &y, 2, 2, 1);
		skill_addtimerskill(target, tick + 800, target->id, x, y, getSkillId(), skill_lv, 0, 0);
	}
}

void SkillFatalMenace::modifyHitRate(int16& hit_rate, const block_list* src, const block_list* target, uint16 skill_lv) const {
	if (skill_lv < 6)
		hit_rate -= 35 - 5 * skill_lv;
	else if (skill_lv > 6)
		hit_rate += 5 * skill_lv - 30;
}
