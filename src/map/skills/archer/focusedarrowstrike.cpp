// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "focusedarrowstrike.hpp"

#include <config/core.hpp>

#include "map/battle.hpp"
#include "map/status.hpp"

SkillFocusedArrowStrike::SkillFocusedArrowStrike() : SkillImplRecursiveDamageSplash(SN_SHARPSHOOTING) {
}

// Sharp Shooting directional-path callback (mirrors rathena-20171231 semantics via skill_attack_area()).
// The designated target id and a 'processed' bool pointer are passed as the last two varargs (NOT via the
// shared skill_area_temp[] globals):
//  - designated target reached on the path (on-axis): attacked here with flag bit 2 set (2017 path-processed
//    marker; bit 2 also disables the double-cast re-trigger and is carried into battle_calc as miscflag) and
//    no SD_ANIMATION (full skill animation); *processed is set so castendDamageId() does not hit it again.
//  - secondary path targets: flag bit 2 set + SD_ANIMATION (no per-hit animation), as in 2017.
static int32 sharpshooting_path_sub(block_list* bl, va_list ap) {
	int32 atk_type = va_arg(ap, int32);
	block_list* src = va_arg(ap, block_list*);
	block_list* dsrc = va_arg(ap, block_list*);
	int32 skill_id = va_arg(ap, int32);
	int32 skill_lv = va_arg(ap, int32);
	t_tick tick = va_arg(ap, t_tick);
	int32 flag = va_arg(ap, int32);
	int32 type = va_arg(ap, int32);
	int32 designated_target_id = va_arg(ap, int32);
	bool* designated_processed = va_arg(ap, bool*);

	if (status_isdead(*bl))
		return 0;

	if (designated_target_id == bl->id) {
		// On-axis designated target: mark as processed (attempt recorded even on miss/0 damage to avoid a
		// second hit from the fallback) and attack once with the 2017 path-processed flag (bit 2, no anim).
		*designated_processed = true;
		return (int32)skill_attack(atk_type, src, dsrc, bl, skill_id, skill_lv, tick, flag | 2);
	}

	if (battle_check_target(dsrc, bl, type) <= 0 || !status_check_skilluse(nullptr, bl, skill_id, 2))
		return 0;

	return (int32)skill_attack(atk_type, src, dsrc, bl, skill_id, skill_lv, tick, flag | 2 | SD_ANIMATION);
}

void SkillFocusedArrowStrike::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	skillratio += 100 + 50 * skill_lv;
}

void SkillFocusedArrowStrike::castendDamageId(block_list *src, block_list *target, uint16 skill_lv, t_tick tick, int32& flag) const {
	// NEED 2017 range fix: SN_SHARPSHOOTING is a directional path attack from the caster toward the target
	// (width = SplashArea radius 1 -> 3 cells, forward length = MaxCount/ActiveInstance = 14 cells), not the
	// target-centered splash the compiled RENEWAL branch used. Restores the rathena-20171231 directional
	// structure. Damage/crit are unchanged (calculateSkillRatio = 100 + 50 * skill_lv).

	// Snapshot the designated target id and position BEFORE any attack, so the path direction and the
	// callback skip id are computed from stable data (the on-axis hit and secondaries can move/kill units
	// or trigger reactive effects). Do not re-read target->x/y/id after this point for path calculations.
	const int32 designated_target_id = target->id;
	const int16 designated_x = target->x;
	const int16 designated_y = target->y;
	bool designated_processed = false;

	// Path search (width = skill_get_splash 3 cells, length = plain skill_get_maxcount 14 from DB/2017; no
	// cannonspear-style +1). The designated target id and &designated_processed are the last two varargs.
	// eightpath (default) reaches forward 13 cells; non-eightpath reaches 14.
	if (battle_config.skill_eightpath_algorithm) {
		map_foreachindir(sharpshooting_path_sub, src->m, src->x, src->y, designated_x, designated_y,
			skill_get_splash(getSkillId(), skill_lv), skill_get_maxcount(getSkillId(), skill_lv), 0, splash_target(src),
			skill_get_type(getSkillId()), src, src, getSkillId(), skill_lv, tick, flag, BCT_ENEMY, designated_target_id, &designated_processed);
	} else {
		map_foreachinpath(sharpshooting_path_sub, src->m, src->x, src->y, designated_x, designated_y,
			skill_get_splash(getSkillId(), skill_lv), skill_get_maxcount(getSkillId(), skill_lv), splash_target(src),
			skill_get_type(getSkillId()), src, src, getSkillId(), skill_lv, tick, flag, BCT_ENEMY, designated_target_id, &designated_processed);
	}

	// Off-axis designated target: the 8-direction path did not reach it. Attack it once directly here, which
	// corresponds to 2017's direct fallback, so flag bit 2 is NOT set (2017 removed it for the fallback).
	// Unlike 2017 (whose fallback ran only when the whole path hit nothing), this uses the per-target
	// 'processed' flag, so the designated target is not missed when other enemies are on the path (NEED bugfix).
	// Wall / line-of-sight to the designated target is rechecked by skill_castend_id() through OFFICIAL_WALKPATH
	// before this handler is reached. Default range recheck behavior remains unchanged from the 2017 path.
	if (!designated_processed && !status_isdead(*target))
		skill_attack(skill_get_type(getSkillId()), src, src, target, getSkillId(), skill_lv, tick, flag);

	// 2017: Sharp Shooting ends the caster's Camouflage once after the hit (battle calc excludes it).
	status_change_end(src, SC_CAMOUFLAGE);
}
