// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "earthstrain.hpp"

#include <config/core.hpp>

#include <common/random.hpp>

#include "map/battle.hpp"
#include "map/map.hpp"
#include "map/pc.hpp"
#include "map/status.hpp"

namespace {
constexpr int32 earthstrain_rate_scale = 10000;
constexpr int32 earthstrain_minimum_rate = 1; // 0.01%, the smallest meaningful unit in status/rnd rates.
constexpr t_tick earthstrain_strip_duration = 10000;

struct earthstrain_strip_candidate {
	sc_type status;
	uint32 position;
};

int32 earthstrain_equipped_count(const map_session_data& sd, uint32 position) {
	int16 indices[EQI_MAX];
	int32 count = 0;

	for (int32 i = 0; i < EQI_MAX; ++i) {
		int16 index = sd.equip_index[i];

		if (!(equip_bitmask[i] & position) || index < 0 || sd.inventory_data[index] == nullptr ||
			!(sd.inventory.u.items_inventory[index].equip & position))
			continue;
		if (std::find(indices, indices + count, index) == indices + count)
			indices[count++] = index;
	}
	return count;
}

std::vector<earthstrain_strip_candidate> earthstrain_strip_candidates(map_session_data& sd) {
	status_change& sc = sd.sc;
	std::vector<earthstrain_strip_candidate> candidates;
	static const earthstrain_strip_candidate normal[] = {
		{ SC_STRIPWEAPON, EQP_WEAPON }, { SC_STRIPHELM, EQP_HELM },
		{ SC_STRIPSHIELD, EQP_SHIELD }, { SC_STRIPARMOR, EQP_ARMOR },
	};
	static const sc_type protection[] = { SC_CP_WEAPON, SC_CP_HELM, SC_CP_SHIELD, SC_CP_ARMOR };

	for (int32 i = 0; i < static_cast<int32>(ARRAYLENGTH(normal)); ++i) {
		const earthstrain_strip_candidate& candidate = normal[i];

		if (sc.getSCE(candidate.status) || sc.getSCE(protection[i]) ||
			(sd.bonus.unstripable_equip & candidate.position) ||
			earthstrain_equipped_count(sd, candidate.position) == 0)
			continue;
		candidates.push_back(candidate);
	}

	// Existing accessory strip blocks both sides; the NEED state also prevents Earth Strain from taking
	// the other side while its first one-sided strip is active.
	if (!sc.getSCE(SC__STRIPACCESSORY) && !sc.getSCE(SC_NEED_EARTHSTRAIN_STRIPACC)) {
		bool has_accessory = false;

		if (!(sd.bonus.unstripable_equip & EQP_ACC_L) && pc_checkequip(&sd, EQP_ACC_L) >= 0)
			has_accessory = true;
		if (!(sd.bonus.unstripable_equip & EQP_ACC_R) && pc_checkequip(&sd, EQP_ACC_R) >= 0)
			has_accessory = true;
		if (has_accessory)
			candidates.push_back({ SC_NEED_EARTHSTRAIN_STRIPACC, EQP_ACC });
	}
	return candidates;
}

int32 earthstrain_strip_one(block_list* src, map_session_data& target, const earthstrain_strip_candidate& candidate, uint16 skill_lv) {
	if (candidate.position != EQP_ACC) {
		int32 before = earthstrain_equipped_count(target, candidate.position);
		uint8 flags = SCSTART_NOAVOID | SCSTART_NOTICKDEF | SCSTART_NORATEDEF;

		if (!status_change_start(src, &target, candidate.status, earthstrain_rate_scale, skill_lv, 0, 0, 0,
			earthstrain_strip_duration, flags))
			return 0;
		return max(0, before - earthstrain_equipped_count(target, candidate.position));
	}

	std::vector<uint32> positions;

	if (!(target.bonus.unstripable_equip & EQP_ACC_L) && pc_checkequip(&target, EQP_ACC_L) >= 0)
		positions.push_back(EQP_ACC_L);
	if (!(target.bonus.unstripable_equip & EQP_ACC_R) && pc_checkequip(&target, EQP_ACC_R) >= 0)
		positions.push_back(EQP_ACC_R);
	if (positions.empty())
		return 0;

	uint32 position = positions[rnd_value<size_t>(0, positions.size() - 1)];
	int16 index = pc_checkequip(&target, position);

	if (index < 0 || !pc_unequipitem(&target, index, 3))
		return 0;

	// No icon is registered in the DB and SCSTART_NOICON also suppresses the packet explicitly.
	status_change_start(src, &target, SC_NEED_EARTHSTRAIN_STRIPACC, earthstrain_rate_scale,
		static_cast<int32>(position), 0, 0, 0, earthstrain_strip_duration,
		SCSTART_NOAVOID | SCSTART_NOTICKDEF | SCSTART_NORATEDEF | SCSTART_NOICON);
	return 1;
}
}

SkillEarthStrain::SkillEarthStrain() : SkillImpl(WL_EARTHSTRAIN) {
}

void SkillEarthStrain::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
#ifdef NEED_2017_SKILL_FORMULA
	skillratio += 1900 + 100 * skill_lv;
	RE_LVL_DMOD(100);
#else
	skillratio += -100 + 1000 + 600 * skill_lv;
	RE_LVL_DMOD(100);
#endif
}

void SkillEarthStrain::castendPos2(block_list* src, int32 x, int32 y, uint16 skill_lv, t_tick tick, int32& flag) const {
	int32 w, wave = skill_lv + 4, dir = map_calc_dir(src,x,y);
	int32 sx = x = src->x, sy = y = src->y; // Store first caster's location to avoid glitch on unit setting

	for( w = 1; w <= wave; w++ )
	{
		switch( dir ){
			case 0: case 1: case 7: sy = y + w; break;
			case 3: case 4: case 5: sy = y - w; break;
			case 2: sx = x - w; break;
			case 6: sx = x + w; break;
		}
		skill_addtimerskill(src,gettick() + (140 * w),0,sx,sy,getSkillId(),skill_lv,dir,flag&2);
	}
}

void SkillEarthStrain::applyAdditionalEffects(block_list* src, block_list* target, uint16 skill_lv, t_tick tick, int32 attack_type, enum damage_lv dmg_lv) const {
#ifdef NEED_2017_SKILL_BEHAVIOR
	if (dmg_lv != ATK_DEF)
		return;

	status_change* tsc = status_get_sc(target);

	if (tsc == nullptr || tsc->option&OPTION_MADOGEAR) // Mado Gear cannot be divested
		return;

	map_session_data* tsd = BL_CAST(BL_PC, target);

	// Preserve the non-player stat-debuff behavior. Accumulated resistance and actual equipment checks are
	// player-only because only map_session_data owns equipment and the runtime counter.
	if (tsd == nullptr) {
		static const sc_type sc_atk[5] = { SC_STRIPWEAPON, SC_STRIPHELM, SC_STRIPSHIELD, SC_STRIPARMOR, SC__STRIPACCESSORY };
		static const sc_type sc_def[5] = { SC_CP_WEAPON, SC_CP_HELM, SC_CP_SHIELD, SC_CP_ARMOR, SC_NONE };
		for (uint16 i = 0; i < skill_lv && i < 5; ++i) {
			if (sc_def[i] > SC_NONE && tsc->getSCE(sc_def[i]))
				continue;
			if (rnd_chance<int32>(5 * skill_lv, 100))
				sc_start(src, target, sc_atk[i], 100, skill_lv, earthstrain_strip_duration);
		}
		return;
	}

	std::vector<earthstrain_strip_candidate> candidates = earthstrain_strip_candidates(*tsd);

	if (candidates.empty())
		return;

	// Existing 5% per skill level is the neutral-stat baseline. DEX and LUK differences adjust it in the
	// same 1/10000 scale used by status rates, allowing resistance divisions without rounding to zero.
	int64 base_rate = static_cast<int64>(5 * skill_lv) * 100;
	base_rate += (static_cast<int64>(status_get_dex(src)) - status_get_dex(target)) * 20;
	base_rate += (static_cast<int64>(status_get_luk(src)) - status_get_luk(target)) * 10;
	base_rate = std::clamp<int64>(base_rate, earthstrain_minimum_rate, earthstrain_rate_scale);
	uint8 resist_level = std::min<uint8>(tsd->earthstrain_strip_resist, 4);
	int64 effective_rate = std::max<int64>(base_rate / (static_cast<int64>(1) << resist_level), earthstrain_minimum_rate);

	if (!rnd_chance<int64>(effective_rate, static_cast<int64>(earthstrain_rate_scale)))
		return;

	earthstrain_strip_candidate selected = candidates[rnd_value<size_t>(0, candidates.size() - 1)];
	int32 stripped_count = earthstrain_strip_one(src, *tsd, selected, skill_lv);

	// A second successful roll may strip one more candidate, but never more than two actual inventory items.
	if (stripped_count > 0 && stripped_count < 2) {
		candidates = earthstrain_strip_candidates(*tsd);
		int32 remaining = 2 - stripped_count;
		candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [tsd, remaining](const earthstrain_strip_candidate& candidate) {
			return candidate.position != EQP_ACC && earthstrain_equipped_count(*tsd, candidate.position) > remaining;
		}), candidates.end());
		if (!candidates.empty() && rnd_chance<int64>(effective_rate, static_cast<int64>(earthstrain_rate_scale))) {
			selected = candidates[rnd_value<size_t>(0, candidates.size() - 1)];
			stripped_count += earthstrain_strip_one(src, *tsd, selected, skill_lv);
		}
	}

	// One Earth Strain hit raises target-owned resistance once, regardless of whether one or two items left.
	if (stripped_count > 0)
		tsd->earthstrain_strip_resist = std::min<uint8>(tsd->earthstrain_strip_resist + 1, 4);
#endif
}
