// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "overbrand.hpp"

#include <config/core.hpp>

#include <common/random.hpp>

#include "map/battle.hpp"
#include "map/clif.hpp"
#include "map/map.hpp"
#include "map/pc.hpp"
#include "map/skill.hpp"
#include "map/status.hpp"

// LG_OVERBRAND
SkillOverBrand::SkillOverBrand() : SkillImpl(LG_OVERBRAND) {
}

void SkillOverBrand::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
	// 2017: directional, ground-targeted attack using the no-unit cell layout, then triggers Overbrand Brandish.
	int32 dir = map_calc_dir(src, x, y);
	int32 sx = src->x, sy = src->y;
	struct s_skill_nounit_layout* layout = skill_get_nounit_layout(getSkillId(), skill_lv, src, sx, sy, dir);

	for( int32 i = 0; i < layout->count; i++ )
		map_foreachincell(skill_area_sub, src->m, sx + layout->dx[i], sy + layout->dy[i], BL_CHAR, src, getSkillId(), skill_lv, tick, flag|BCT_ENEMY|SD_ANIMATION|1, skill_castend_damage_id);

	skill_addtimerskill(src, tick + status_get_amotion(src), 0, x, y, LG_OVERBRAND_BRANDISH, skill_lv, dir, flag);
}

void SkillOverBrand::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& skillratio, int32 mflag) const {
	const map_session_data* sd = BL_CAST(BL_PC, src);

	skillratio += -100 + 400 * skill_lv + ((sd) ? pc_checkskill(sd, CR_SPEARQUICKEN) * 50 : 0);
	RE_LVL_DMOD(100);
}

// LG_OVERBRAND_BRANDISH
SkillOverBrandBrandish::SkillOverBrandBrandish() : SkillImpl(LG_OVERBRAND_BRANDISH) {
}

void SkillOverBrandBrandish::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& skillratio, int32 mflag) const {
	skillratio += -100 + 300 * skill_lv + status_get_str(src) + status_get_dex(src);
	RE_LVL_DMOD(100);
}

// LG_OVERBRAND_PLUSATK
SkillOverBrandPlusAtk::SkillOverBrandPlusAtk() : SkillImpl(LG_OVERBRAND_PLUSATK) {
}

void SkillOverBrandPlusAtk::calculateSkillRatio(const Damage* wd, const block_list* src, const block_list* target, uint16 skill_lv, int32& skillratio, int32 mflag) const {
	skillratio += -100 + 200 * skill_lv + rnd()%90 + 10;
}
