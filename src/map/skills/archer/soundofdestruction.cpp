// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "soundofdestruction.hpp"

#include <config/core.hpp>

#include <common/random.hpp>

#include "map/battle.hpp"
#include "map/clif.hpp"
#include "map/map.hpp"
#include "map/pc.hpp"
#include "map/skill.hpp"
#include "map/status.hpp"

SkillSoundOfDestruction::SkillSoundOfDestruction() : SkillImpl(WM_SOUND_OF_DESTRUCTION) {
}

// 2017: Sound of Destruction is a ground-targeted skill (TargetType Ground from Phase 1 DB).
void SkillSoundOfDestruction::castendPos2(block_list *src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
	int32 i = skill_get_splash(getSkillId(), skill_lv);
	map_foreachinarea(skill_area_sub, src->m, x - i, y - i, x + i, y + i, BL_CHAR, src, getSkillId(), skill_lv, tick, flag | BCT_ENEMY | 1, skill_castend_damage_id);
}

void SkillSoundOfDestruction::castendDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	// 2017: chance to stun the target and strip all performance (song/dance/chorus) states.
	status_change *tsc = status_get_sc(target);
	map_session_data* sd = BL_CAST(BL_PC, src);

	if( tsc && ( tsc->getSCE(SC_SWINGDANCE) || tsc->getSCE(SC_SYMPHONYOFLOVER) || tsc->getSCE(SC_MOONLITSERENADE) ||
		tsc->getSCE(SC_RUSHWINDMILL) || tsc->getSCE(SC_ECHOSONG) || tsc->getSCE(SC_HARMONIZE) ||
		tsc->getSCE(SC_VOICEOFSIREN) || tsc->getSCE(SC_DEEPSLEEP) || tsc->getSCE(SC_SIRCLEOFNATURE) ||
		tsc->getSCE(SC_GLOOMYDAY) || tsc->getSCE(SC_GLOOMYDAY_SK) || tsc->getSCE(SC_SONGOFMANA) ||
		tsc->getSCE(SC_DANCEWITHWUG) || tsc->getSCE(SC_SATURDAYNIGHTFEVER) || tsc->getSCE(SC_LERADSDEW) ||
		tsc->getSCE(SC_MELODYOFSINK) || tsc->getSCE(SC_BEYONDOFWARCRY) || tsc->getSCE(SC_UNLIMITEDHUMMINGVOICE) ) &&
		rnd()%100 < 4 * skill_lv + 2 * ((sd) ? pc_checkskill(sd, WM_LESSON) : skill_get_max(WM_LESSON)) + 10 * battle_calc_chorusbonus(sd) ) {
		status_change_start(src, target, SC_STUN, 10000, skill_lv, 0, 0, 0, skill_get_time(getSkillId(), skill_lv), SCSTART_NOTICKDEF);
		status_change_end(target, SC_DANCING);
		status_change_end(target, SC_RICHMANKIM);
		status_change_end(target, SC_ETERNALCHAOS);
		status_change_end(target, SC_DRUMBATTLE);
		status_change_end(target, SC_NIBELUNGEN);
		status_change_end(target, SC_INTOABYSS);
		status_change_end(target, SC_SIEGFRIED);
		status_change_end(target, SC_WHISTLE);
		status_change_end(target, SC_ASSNCROS);
		status_change_end(target, SC_POEMBRAGI);
		status_change_end(target, SC_APPLEIDUN);
		status_change_end(target, SC_HUMMING);
		status_change_end(target, SC_FORTUNE);
		status_change_end(target, SC_SERVICE4U);
		status_change_end(target, SC_LONGING);
		status_change_end(target, SC_SWINGDANCE);
		status_change_end(target, SC_SYMPHONYOFLOVER);
		status_change_end(target, SC_MOONLITSERENADE);
		status_change_end(target, SC_RUSHWINDMILL);
		status_change_end(target, SC_ECHOSONG);
		status_change_end(target, SC_HARMONIZE);
		status_change_end(target, SC_WINKCHARM);
		status_change_end(target, SC_SONGOFMANA);
		status_change_end(target, SC_DANCEWITHWUG);
		status_change_end(target, SC_LERADSDEW);
		status_change_end(target, SC_MELODYOFSINK);
		status_change_end(target, SC_BEYONDOFWARCRY);
		status_change_end(target, SC_UNLIMITEDHUMMINGVOICE);
	}
}
