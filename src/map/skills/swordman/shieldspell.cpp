// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "shieldspell.hpp"

#include <common/random.hpp>

#include "map/battle.hpp"
#include "map/clif.hpp"
#include "map/itemdb.hpp"
#include "map/map.hpp"
#include "map/pc.hpp"
#include "map/skill.hpp"
#include "map/status.hpp"

SkillShieldSpell::SkillShieldSpell() : SkillImpl(LG_SHIELDSPELL) {
}

void SkillShieldSpell::castendNoDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	map_session_data* sd = BL_CAST(BL_PC, src);

	if (sd == nullptr)
		return;

	int16 index = sd->equip_index[EQI_HAND_L];
	int16 shield_def = 0, shield_mdef = 0, shield_refine = 0;
	struct item_data* shield_data = nullptr;

	if (index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR)
		shield_data = sd->inventory_data[index];
	if (shield_data == nullptr || shield_data->type != IT_ARMOR) // Group with 'skill_unconditional' gets these as default
		shield_def = shield_mdef = shield_refine = 10;
	else {
		shield_def = shield_data->def;
		shield_mdef = sd->bonus.shieldmdef;
		shield_refine = sd->inventory.u.items_inventory[index].refine;
	}

	if (flag&1) {
		sc_start(src, target, SC_SILENCE, 100, skill_lv, shield_mdef * 30000);
		return;
	}

	int32 opt = rnd()%3 + 1; // Generates a number between 1 - 3. The number generated will determine which effect will be triggered.

	switch (skill_lv) {
		case 1: { // DEF Based
				status_change_end(target, SC_SHIELDSPELL_MDEF);
				status_change_end(target, SC_SHIELDSPELL_REF);
				status_change_end(target, SC_MAGNIFICAT);
				int32 splashrange = 0;

				if (shield_def >= 0 && shield_def <= 40)
					splashrange = 1;
				else if (shield_def >= 41 && shield_def <= 80)
					splashrange = 2;
				else
					splashrange = 3;
				switch (opt) {
					case 1: // Splash AoE ATK
						sc_start(src, target, SC_SHIELDSPELL_DEF, 100, opt, INVALID_TIMER);
						clif_skill_damage(*src, *src, tick, status_get_amotion(src), 0, DMGVAL_IGNORE, 1, getSkillId(), skill_lv, DMG_SINGLE);
						map_foreachinrange(skill_area_sub, src, splashrange, BL_CHAR, src, getSkillId(), skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_damage_id);
						status_change_end(target, SC_SHIELDSPELL_DEF);
						break;
					case 2: // % Damage Reflecting Increase
						sc_start2(src, target, SC_SHIELDSPELL_DEF, 100, opt, shield_def / 10, shield_def * 1000);
						break;
					case 3: // Equipment Attack Increase
						sc_start2(src, target, SC_SHIELDSPELL_DEF, 100, opt, shield_def, shield_def * 3000);
						break;
				}
			}
			break;

		case 2: { // MDEF Based
				status_change_end(target, SC_SHIELDSPELL_DEF);
				status_change_end(target, SC_SHIELDSPELL_REF);
				status_change_end(target, SC_MAGNIFICAT);
				int32 splashrange = 0;

				if (shield_mdef >= 1 && shield_mdef <= 3)
					splashrange = 1;
				else if (shield_mdef >= 4 && shield_mdef <= 5)
					splashrange = 2;
				else
					splashrange = 3;
				switch (opt) {
					case 1: // Splash AoE MATK
						sc_start(src, target, SC_SHIELDSPELL_MDEF, 100, opt, INVALID_TIMER);
						clif_skill_damage(*src, *src, tick, status_get_amotion(src), 0, DMGVAL_IGNORE, 1, getSkillId(), skill_lv, DMG_SINGLE);
						map_foreachinrange(skill_area_sub, src, splashrange, BL_CHAR, src, getSkillId(), skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_damage_id);
						status_change_end(target, SC_SHIELDSPELL_MDEF);
						break;
					case 2: // Splash AoE Lex Divina
						sc_start(src, target, SC_SHIELDSPELL_MDEF, 100, opt, shield_mdef * 2000);
						clif_skill_damage(*src, *src, tick, status_get_amotion(src), 0, DMGVAL_IGNORE, 1, getSkillId(), skill_lv, DMG_SINGLE);
						map_foreachinallrange(skill_area_sub, src, splashrange, BL_CHAR, src, getSkillId(), skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_nodamage_id);
						break;
					case 3: // Casts Magnificat.
						if (sc_start(src, target, SC_SHIELDSPELL_MDEF, 100, opt, shield_mdef * 30000))
							clif_skill_nodamage(src, *target, PR_MAGNIFICAT, skill_lv,
								sc_start(src, target, SC_MAGNIFICAT, 100, 1, shield_mdef * 30000));
						break;
				}
			}
			break;

		case 3: // Refine Based
				status_change_end(target, SC_SHIELDSPELL_DEF);
				status_change_end(target, SC_SHIELDSPELL_MDEF);
				status_change_end(target, SC_MAGNIFICAT);
			switch (opt) {
				case 1: // Allows you to break armor at a 100% rate when you do damage.
					sc_start(src, target, SC_SHIELDSPELL_REF, 100, opt, shield_refine * 30000);
					break;
				case 2: // Increases DEF and Status Effect resistance depending on Shield refine rate.
					sc_start4(src, target, SC_SHIELDSPELL_REF, 100, opt, shield_refine * 10 * status_get_lv(src) / 100, (shield_refine * 2) + (status_get_luk(src) / 10), 0, shield_refine * 20000);
					break;
				case 3: // Recovers HP depending on Shield refine rate.
					sc_start(src, target, SC_SHIELDSPELL_REF, 100, opt, INVALID_TIMER); // HP Recovery.
					status_heal(target, status_get_max_hp(src) * ((status_get_lv(src) / 10) + (shield_refine + 1)) / 100, 0, 0, 2);
					status_change_end(target, SC_SHIELDSPELL_REF);
					break;
			}
			break;
	}
	clif_skill_nodamage(src, *target, getSkillId(), skill_lv, 1);
}

void SkillShieldSpell::castendDamageId(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32& flag) const {
	// 2017: Level 1 deals weapon damage (DEF based), Level 2 deals magic damage (MDEF based).
	if (skill_lv == 1)
		skill_attack(BF_WEAPON, src, src, target, getSkillId(), skill_lv, tick, flag);
	else if (skill_lv == 2)
		skill_attack(BF_MAGIC, src, src, target, getSkillId(), skill_lv, tick, flag);
}

void SkillShieldSpell::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& skillratio, int32 mflag) const {
	const map_session_data* sd = BL_CAST(BL_PC, src);

	if (wd->flag & BF_MAGIC) {
		// [(Casters Base Level x 4) + (Shield MDEF x 100) + (Casters INT x 2)] %
		if (sd && skill_lv == 2)
			skillratio += -100 + status_get_lv(src) * 4 + sd->bonus.shieldmdef * 100 + status_get_int(src) * 2;
		else
			skillratio = 0;
	} else {
		// [(Casters Base Level x 4) + (Shield DEF x 10) + (Casters VIT x 2)] %
		if (sd && skill_lv == 1) {
			int16 index = sd->equip_index[EQI_HAND_L];

			skillratio += -100 + status_get_lv(src) * 4 + status_get_vit(src) * 2;
			if (index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR)
				skillratio += sd->inventory_data[index]->def * 10;
		} else
			skillratio = 0; // Prevent damage since level 2 is MATK. [Aleos]
	}
}
