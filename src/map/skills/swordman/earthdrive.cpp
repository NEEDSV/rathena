// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "earthdrive.hpp"

#include <config/core.hpp>

#include "map/battle.hpp"
#include "map/clif.hpp"
#include "map/map.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

SkillEarthDrive::SkillEarthDrive() : SkillImplRecursiveDamageSplash(LG_EARTHDRIVE) {
}

void SkillEarthDrive::castendNoDamageId(block_list* src, block_list* bl, uint16 skill_lv, t_tick tick, int32& flag) const {
	int32 dummy = 1;

	clif_skill_damage( *src, *bl,tick, status_get_amotion(src), 0, DMGVAL_IGNORE, 1, getSkillId(), skill_lv, DMG_SINGLE );
	int32 i = skill_get_splash(getSkillId(),skill_lv);
	map_foreachinallarea(skill_cell_overlap, src->m, src->x-i, src->y-i, src->x+i, src->y+i, BL_SKILL, getSkillId(), &dummy, src);
	map_foreachinrange(skill_area_sub, bl,i,BL_CHAR,src,getSkillId(),skill_lv,tick,flag|BCT_ENEMY|1,skill_castend_damage_id);
	clif_skill_nodamage(src, *src, getSkillId(), skill_lv);
}

void SkillEarthDrive::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& skillratio, int32 mflag) const {
	const map_session_data* sd = BL_CAST(BL_PC, src);
	const status_change* sc = status_get_sc(src);
	const status_data* sstatus = status_get_status_data(*src);

#ifdef NEED_2017_SKILL_FORMULA
	if (sd) {
		short index = sd->equip_index[EQI_HAND_L];

		if (index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR)
			skillratio += -100 + (skill_lv + 1) * sd->inventory_data[index]->weight / 10;
	}
#else
	skillratio += -100 + 380 * skill_lv + sstatus->str + sstatus->vit; // !TODO: What's the STR/VIT bonus?

	if (sc != nullptr && sc->getSCE(SC_SHIELD_POWER)) {
		skillratio += skill_lv * 37 * pc_checkskill(sd, IG_SHIELD_MASTERY);
	}
#endif

	RE_LVL_DMOD(100);
}

#ifdef NEED_2017_SKILL_BEHAVIOR
void SkillEarthDrive::applyAdditionalEffects(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
	// 2017: caster's own shield is damaged and the target gets Earth Drive (DEF/MDEF down).
	skill_break_equip(src, src, EQP_SHIELD, 100 * skill_lv, BCT_SELF);
	sc_start(src, target, SC_EARTHDRIVE, 100, skill_lv, skill_get_time(getSkillId(), skill_lv));
}

void SkillEarthDrive::modifyElement(const Damage& dmg, const block_list& src, const block_list& target, uint16 skill_lv, int32& element, int32 flag) const {
	// 2017: Earth Drive is fixed Earth element (2026 uses the weapon element).
	element = ELE_EARTH;
}
#endif
