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
}

void SkillReverberation::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	// 2017 behavior deals damage through WM_REVERBERATION_MELEE and WM_REVERBERATION_MAGIC.
	return;
}

void SkillReverberation::castendDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	flag |= 1; // Ammo is consumed by the skill unit group delete path.
	if (target != nullptr)
		skill_unitsetting(src, getSkillId(), skill_lv, target->x, target->y, 0);
	return;
}

void SkillReverberation::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
	flag |= 1; // Ammo is consumed by the skill unit group delete path.
	skill_unitsetting(src, getSkillId(), skill_lv, x, y, 0);
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
	const map_session_data* sd = BL_CAST(BL_PC, src);

	skillratio += 200 + 100 * (sd ? pc_checkskill(sd, WM_REVERBERATION) : 1);
	RE_LVL_DMOD(100);
}

// WM_REVERBERATION_MAGIC
SkillReverberationMagic::SkillReverberationMagic() : SkillImpl(WM_REVERBERATION_MAGIC) {
}

void SkillReverberationMagic::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	skillratio += 100 * skill_lv;
	RE_LVL_DMOD(100);
}
