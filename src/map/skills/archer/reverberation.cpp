// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "reverberation.hpp"

#include <config/core.hpp>

#include "map/battle.hpp"
#include "map/clif.hpp"
#include "map/map.hpp"
#include "map/pc.hpp"
#include "map/skill.hpp"
#include "map/status.hpp"

SkillReverberation::SkillReverberation() : SkillImpl(WM_REVERBERATION) {
}

void SkillReverberation::applyAdditionalEffects(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
#ifndef NEED_2017_SKILL_BEHAVIOR
	status_change_end(target, SC_SOUNDBLEND);
#endif
}

void SkillReverberation::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
#ifdef NEED_2017_SKILL_BEHAVIOR
	// 2017 behavior deals damage through WM_REVERBERATION_MELEE and WM_REVERBERATION_MAGIC.
	return;
#else
	const status_change *tsc = status_get_sc(target);

	// MATK [{(Skill Level x 300) + 400} x Casters Base Level / 100] %
	skillratio += -100 + 700 + 300 * skill_lv;
	RE_LVL_DMOD(100);
	if (tsc && tsc->getSCE(SC_SOUNDBLEND))
		skillratio += skillratio * 50 / 100;
#endif
}

void SkillReverberation::castendDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
#ifdef NEED_2017_SKILL_BEHAVIOR
	flag |= 1; // Ammo is consumed by the skill unit group delete path.
	if (target != nullptr)
		skill_unitsetting(src, getSkillId(), skill_lv, target->x, target->y, 0);
	return;
#else
	map_session_data* sd = BL_CAST(BL_PC, src);

	if (flag & 1)
		skill_attack(skill_get_type(getSkillId()), src, src, target, getSkillId(), skill_lv, tick, flag);
	else {
		clif_skill_nodamage(src, *target, getSkillId(), skill_lv);
		map_foreachinallrange(skill_area_sub, target, skill_get_splash(getSkillId(), skill_lv), BL_CHAR|BL_SKILL, src, getSkillId(), skill_lv, tick, flag|BCT_ENEMY|SD_SPLASH|1, skill_castend_damage_id);
		battle_consume_ammo(sd, getSkillId(), skill_lv); // Consume here since Magic/Misc attacks reset arrow_atk
	}
#endif
}

void SkillReverberation::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
#ifdef NEED_2017_SKILL_BEHAVIOR
	flag |= 1; // Ammo is consumed by the skill unit group delete path.
	skill_unitsetting(src, getSkillId(), skill_lv, x, y, 0);
#else
	SkillImpl::castendPos2(src, x, y, skill_lv, tick, flag);
#endif
}

void SkillReverberation::modifyElement(const Damage& dmg, const block_list& src, const block_list& target, uint16 skill_lv, int32& element, int32 flag) const {
	const map_session_data* sd = BL_CAST(BL_PC, &src);

	if (sd != nullptr)
		element = sd->bonus.arrow_ele;
}

// WM_REVERBERATION_MELEE
SkillReverberationMelee::SkillReverberationMelee() : WeaponSkillImpl(WM_REVERBERATION_MELEE) {
}

void SkillReverberationMelee::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
#ifdef NEED_2017_SKILL_BEHAVIOR
	const map_session_data* sd = BL_CAST(BL_PC, src);

	skillratio += 200 + 100 * (sd ? pc_checkskill(sd, WM_REVERBERATION) : 1);
	RE_LVL_DMOD(100);
#endif
}

// WM_REVERBERATION_MAGIC
SkillReverberationMagic::SkillReverberationMagic() : SkillImpl(WM_REVERBERATION_MAGIC) {
}

void SkillReverberationMagic::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
#ifdef NEED_2017_SKILL_BEHAVIOR
	skillratio += 100 * skill_lv;
	RE_LVL_DMOD(100);
#endif
}
