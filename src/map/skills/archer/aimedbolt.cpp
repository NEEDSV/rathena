// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "aimedbolt.hpp"

#include <config/core.hpp>

#include <common/random.hpp>

#include "map/status.hpp"

SkillAimedBolt::SkillAimedBolt() : WeaponSkillImpl(RA_AIMEDBOLT) {
}

#ifdef NEED_2017_SKILL_BEHAVIOR
void SkillAimedBolt::modifyDamageData(Damage& dmg, const block_list& src, const block_list& target, uint16 skill_lv) const {
	const status_change* tsc = status_get_sc(&target);
	const status_data* tstatus = status_get_status_data(target);

	// 2017 behavior: 1 hit by default, but a dynamic 2-5 hits while the target
	// is bound by Wug Bite / Ankle Snare / Electric Shocker. The hit count grows
	// with the target size plus a size-scaled random extra hit. The 2026 default
	// uses a flat skill_db HitCount of 5, so restore the dynamic count here
	// instead of touching skill_db.yml.
	dmg.div_ = 1;
	if (tsc && (tsc->getSCE(SC_BITE) || tsc->getSCE(SC_ANKLE) || tsc->getSCE(SC_ELECTRICSHOCKER)))
		dmg.div_ = tstatus->size + 2 + ((rnd() % 100 < 50 - tstatus->size * 10) ? 1 : 0);
}
#endif

void SkillAimedBolt::calculateSkillRatio(const Damage *wd, const block_list *src, const block_list *target, uint16 skill_lv, int32 &skillratio, int32 mflag) const {
	const status_change *sc = status_get_sc(src);

#ifdef NEED_2017_SKILL_FORMULA
	skillratio += 400 + 50 * skill_lv;
	RE_LVL_DMOD(100);
#else
	if (sc && sc->getSCE(SC_FEARBREEZE))
		skillratio += -100 + 800 + 35 * skill_lv;
	else
		skillratio += -100 + 500 + 20 * skill_lv;
	RE_LVL_DMOD(100);
#endif
}
