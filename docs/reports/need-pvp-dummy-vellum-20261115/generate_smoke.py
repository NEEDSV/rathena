"""Limited native smoke: production calculation bodies + real headers; world access is stubbed.

This is not an equipped-character/client integration test. The two Claymore script
branches are read from the DB, and their bonus values seed the real bonus arrays.
"""
from pathlib import Path
import re
import subprocess

ROOT = next(p for p in Path(__file__).resolve().parents if (p/'src/map/battle.cpp').is_file())
OUT = ROOT/'tools/need_pvp_dummy'
OUT.mkdir(parents=True, exist_ok=True)
source = (ROOT / 'src/map/battle.cpp').read_text(encoding='utf-8')

def function(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

item = (ROOT / 'db/re/item_db_equip.yml').read_bytes()
claymore = item.split(b'  - Id: 21001\n')[1].split(b'  - Id: ')[0].decode('cp949')
adds = re.findall(r'bonus2 bAddRace,RC_Player_Human,(\d+);', claymore)
ignore = re.findall(r'bonus2 bIgnoreDefRaceRate,RC_Player_Human,(\d+);', claymore)
assert adds == ['80', '40'] and ignore == ['30'] and 'if (.@r>=6)' in claymore

prefix = r'''
#include <cstdio>
#include <memory>
#include <common/utils.hpp>
#include <map/battle.hpp>
#include <map/pc.hpp>
#include <map/mob.hpp>
#include <map/need_pvp_dummy.hpp>
using namespace rathena;
Battle_Config battle_config{};
static map_data world{};
static int mapflag = -1;
static bool left_hand = false;
map_data* map_getmapdata(int16 m) { return m == 0 ? &world : nullptr; }
int32 map_data::getMapFlag(int32 flag) const { return flag == mapflag; }
status_change::status_change() {}
status_change_entry::~status_change_entry() {}
status_change_entry* status_change::getSCE(sc_type) { return nullptr; }
status_change* status_get_sc(block_list*) { return nullptr; }
status_data* status_get_status_data(block_list& bl) {
 return bl.type == BL_PC ? &static_cast<map_session_data&>(bl).battle_status : &static_cast<mob_data&>(bl).status;
}
const status_data* status_get_status_data(const block_list& bl) { return status_get_status_data(const_cast<block_list&>(bl)); }
int32 status_get_class(const block_list* bl) { return bl->type == BL_PC ? 0 : static_cast<const mob_data*>(bl)->mob_id; }
std::vector<e_race2> status_get_race2(const block_list*) { return {}; }
uint8 pc_checkskill(const map_session_data*,uint16) { return 0; }
defType status_get_def(const block_list* bl) { return status_get_status_data(*bl)->def; }
int32 unit_counttargeted(const block_list*) { return 0; }
int32 battle_check_undead(int32,int32) { return 0; }
static bool is_attack_left_handed(const block_list*,int32) { return left_hand; }
static bool attack_ignores_def(Damage*,block_list*,const block_list*,uint16,uint16,int16) { return false; }
static int32 is_attack_piercing(Damage*,block_list*,const block_list*,uint16,uint16,int16) { return 0; }
int32 battle_calc_attack_skill_ratio(Damage*,block_list*,block_list*,uint16,uint16) { return 100; }
static std::bitset<NK_MAX> battle_skill_get_damage_properties(uint16,int32) { return {}; }
#define ATK_ADD2(damage, damage2, a, b) do { int64 rate_ = (a), rate2_ = (b); (damage) += rate_; if(is_attack_left_handed(src, skill_id)) (damage2) += rate2_; } while(0);
'''

tests = r'''
static int failed = 0, checks = 0;
static void check(const char* name, bool ok) { ++checks; if (!ok) ++failed; printf("%s %s\n",ok?"PASS":"FAIL",name); }
static int64 hit(map_session_data* src, block_list* dst, int flags = BF_WEAPON|BF_SHORT|BF_NORMAL, uint16 skill = 0) {
 Damage wd{}; wd.damage=10000; wd.flag=flags;
 wd.damage += battle_calc_cardfix(BF_WEAPON,src,dst,{},ELE_NEUTRAL,ELE_NEUTRAL,wd.damage,2,flags);
 battle_calc_defense_reduction(&wd,src,dst,skill,1);
 return wd.damage;
}
int main() {
 strcpy_s(world.name,"hero_tra");
 auto src = std::make_unique<map_session_data>(); src->type=BL_PC;
 auto old = std::make_unique<mob_data>(); old->type=BL_MOB;old->mob_id=21070;old->m=0;
 old->status.race=RC_DEMIHUMAN;old->status.size=SZ_MEDIUM;old->status.class_=CLASS_BOSS;
 old->status.def=100;old->status.def2=50;old->status.def_ele=ELE_NEUTRAL;
 auto dummy = std::make_unique<mob_data>(); dummy->type=BL_MOB;dummy->mob_id=NEED_PVP_DUMMY_ID;dummy->m=0;
 dummy->status=old->status;dummy->status.race=RC_FORMLESS;
 const int64 ordinary_old=hit(src.get(),old.get()), ordinary_dummy=hit(src.get(),dummy.get());
 check("ordinary weapon, identical defense", ordinary_old==8150 && ordinary_dummy==ordinary_old);
 src->right_weapon.addrace[RC_PLAYER_HUMAN]=80;
 const int64 add_only=hit(src.get(),dummy.get());
 check("bAddRace in native cardfix",add_only==14710);
 src->indexed_bonus.ignore_def_by_race[RC_PLAYER_HUMAN]=30;
 const int64 v0_old=hit(src.get(),old.get()),v0_dummy=hit(src.get(),dummy.get());
 printf("DAMAGE normalized ordinary old=%lld dummy=%lld; Vellum+0 old=%lld dummy=%lld\n",ordinary_old,ordinary_dummy,v0_old,v0_dummy);
 check("old dummy ignores player-only bonuses",v0_old==ordinary_old);
 check("Vellum+0 includes native DEF-ignore",v0_dummy>add_only);
 src->right_weapon.addrace[RC_PLAYER_HUMAN]+=40;
 const int64 v6_dummy=hit(src.get(),dummy.get());
 printf("DAMAGE normalized Vellum+6 old=%lld dummy=%lld\n",hit(src.get(),old.get()),v6_dummy);
 check("+6 script bonus changes damage",v6_dummy>v0_dummy && hit(src.get(),old.get())==ordinary_old);
 check("weapon skill flag uses same cardfix/DEF path",hit(src.get(),dummy.get(),BF_WEAPON|BF_SHORT|BF_SKILL,SM_BASH)==v6_dummy);
 src->right_weapon.addrace[RC_DEMIHUMAN]=200;src->indexed_bonus.ignore_def_by_race[RC_DEMIHUMAN]=70;
 check("human and player bonuses do not stack",hit(src.get(),dummy.get())==v6_dummy);
 src->right_weapon.addrace[RC_PLAYER_HUMAN]=0;src->indexed_bonus.ignore_def_by_race[RC_PLAYER_HUMAN]=0;
 check("human-only gear does not affect player test",hit(src.get(),dummy.get())==ordinary_dummy && hit(src.get(),old.get())>ordinary_old);
 src->right_weapon.addrace[RC_DEMIHUMAN]=0;src->indexed_bonus.ignore_def_by_race[RC_DEMIHUMAN]=0;
 src->right_weapon.addrace[RC_PLAYER_HUMAN]=80;src->indexed_bonus.ignore_def_by_race[RC_PLAYER_HUMAN]=30;
 dummy->m=1;check("same ID off map has no special bonuses",hit(src.get(),dummy.get())==ordinary_dummy);dummy->m=0;
 for(int flag: {MF_PVP,MF_BATTLEGROUND,MF_GVG,MF_GVG_CASTLE,MF_GVG_TE,MF_GVG_TE_CASTLE,MF_GVG_DUNGEON}) {
  mapflag=flag; check("versus map guard",!mob_is_need_pvp_dummy(dummy.get()) && hit(src.get(),dummy.get())==ordinary_dummy);
 }
 mapflag=-1;
 dummy->mob_id=1002;check("ordinary mob excluded",hit(src.get(),dummy.get())==ordinary_dummy);dummy->mob_id=NEED_PVP_DUMMY_ID;
 dummy->master_id=1;check("summoned/owned target excluded",!mob_is_need_pvp_dummy(dummy.get()));dummy->master_id=0;
 dummy->bg_id=1;check("BG objective excluded",!mob_is_need_pvp_dummy(dummy.get()));dummy->bg_id=0;
 auto player=std::make_unique<map_session_data>();player->type=BL_PC;player->battle_status=dummy->status;player->battle_status.race=RC_PLAYER_HUMAN;
 check("real player native lookup preserved",!mob_is_need_pvp_dummy(player.get()) && hit(src.get(),player.get())==v0_dummy);
 src->state.arrow_atk=1;check("ranged cardfix uses player race",hit(src.get(),dummy.get(),BF_WEAPON|BF_LONG|BF_NORMAL)==v0_dummy);src->state.arrow_atk=0;
 int64 lh = 10000 + battle_calc_cardfix(BF_WEAPON,src.get(),dummy.get(),{},ELE_NEUTRAL,ELE_NEUTRAL,10000,3,BF_WEAPON|BF_SHORT|BF_NORMAL);
 check("left-hand bonuses stay separate",lh==10000);
 src->left_weapon.addrace[RC_PLAYER_HUMAN]=20;
 lh = 10000 + battle_calc_cardfix(BF_WEAPON,src.get(),dummy.get(),{},ELE_NEUTRAL,ELE_NEUTRAL,10000,3,BF_WEAPON|BF_SHORT|BF_NORMAL);
 check("left-hand player bonus",lh==12000);
 battle_config.left_cardfix_to_right=1;
 check("combined-hand server setting",hit(src.get(),dummy.get())>v0_dummy);battle_config.left_cardfix_to_right=0;
 src->right_weapon.addrace[RC_PLAYER_HUMAN]=0;src->left_weapon.addrace[RC_PLAYER_HUMAN]=0;src->indexed_bonus.ignore_def_by_race[RC_PLAYER_HUMAN]=0;
 check("unequip restores baseline",hit(src.get(),dummy.get())==ordinary_dummy);
 src->indexed_bonus.ignore_def_by_race[RC_PLAYER_HUMAN]=30;
 Damage magic{};magic.damage=10000;magic.flag=BF_MAGIC|BF_SKILL;
 battle_calc_defense_reduction(&magic,src.get(),dummy.get(),CR_GRANDCROSS,1);
 check("hybrid magic DEF call excluded",magic.damage==ordinary_dummy);
 src->indexed_bonus.magic_addrace[RC_PLAYER_HUMAN]=80;
 check("player magic cardfix not opted in",battle_calc_cardfix(BF_MAGIC,src.get(),dummy.get(),{},ELE_NEUTRAL,0,10000,0,BF_MAGIC|BF_SKILL)==0);
 src->indexed_bonus.ignore_def_by_race[RC_PLAYER_HUMAN]=0;
 // Real old DB target has DEF 0; do not hide that difference behind normalization.
 old->status.def=0;
 printf("DAMAGE DB defense ordinary old=%lld dummy=%lld\n",hit(src.get(),old.get()),hit(src.get(),dummy.get()));
 // Compare unmodified pre-change bodies with current ones for unaffected targets.
 src->right_weapon.addrace[RC_PLAYER_HUMAN]=80;src->indexed_bonus.ignore_def_by_race[RC_PLAYER_HUMAN]=30;
 check("baseline equality: old dummy",hit(src.get(),old.get())==baseline_hit(src.get(),old.get()));
 dummy->m=1;check("baseline equality: dedicated ID outside training map",hit(src.get(),dummy.get())==baseline_hit(src.get(),dummy.get()));dummy->m=0;
 for(int flag: {MF_PVP,MF_BATTLEGROUND,MF_GVG}) {
  mapflag=flag;check("baseline equality: real player on versus map",hit(src.get(),player.get())==baseline_hit(src.get(),player.get()));
 }
 mapflag=-1;
 dummy->status.max_hp=2000000000;
 for(int n=0;n<3;++n) {
  dummy->status.hp=0;dummy->dmglog.push_back({});
  check("lethal hit cancels before reward sentinel",death_guard(dummy.get(),src.get(),0)==0 && dummy->status.hp==dummy->status.max_hp && dummy->dmglog.empty() && rewards_touched==0);
 }
 check("ordinary mob reaches downstream sentinel",death_guard(old.get(),src.get(),0)==1 && rewards_touched==1);
 printf("RESULT %d passed, %d failed\n",checks-failed,failed);
 return failed ? 1 : 0;
}
'''

# Use the implementation, including its rounding and caps, verbatim.
body = '\n\n'.join(function(s) for s in [
    'static int32 battle_calc_cardfix_debuff(', 'int32 battle_calc_cardfix(',
    'static void battle_calc_defense_reduction( Damage* wd, block_list* src, block_list* target, uint16 skill_id, uint16 skill_lv )\n{',
])
current = source
source = subprocess.check_output(['git','show','d7fa1a369:src/map/battle.cpp'],cwd=ROOT).decode('utf-8')
baseline = '\n\n'.join(function(s) for s in [
    'int32 battle_calc_cardfix(',
    'static void battle_calc_defense_reduction( Damage* wd, block_list* src, block_list* target, uint16 skill_id, uint16 skill_lv )\n{',
]).replace('battle_calc_cardfix(', 'baseline_cardfix(').replace('battle_calc_defense_reduction(', 'baseline_defense(')
source = current
start = tests.index('static int64 hit(')
end = tests.index('int main()')
baseline += '\n'+tests[start:end].replace(' hit(', ' baseline_hit(').replace('battle_calc_cardfix(', 'baseline_cardfix(').replace('battle_calc_defense_reduction(', 'baseline_defense(')
mob = (ROOT/'src/map/mob.cpp').read_text(encoding='utf-8')
guard = mob.split('int32 mob_dead(mob_data *md, block_list *src, int32 type)\n{')[1].split('\n\tstruct status_data *status;')[0]
sentinel = 'static int rewards_touched=0;\nstatic int death_guard(mob_data *md, block_list *src, int32 type) {\n'+guard+'\n++rewards_touched; return 1;\n}\n'
(OUT/'native_smoke.cpp').write_text(prefix+body+baseline+sentinel+tests,encoding='utf-8')
cmd = (OUT/'build_changed.cmd').read_text()
cmd = cmd.replace('/c ', '')
cmd = cmd.replace('"F:\\rathena\\src\\map\\battle.cpp" "F:\\rathena\\src\\map\\mob.cpp"', '/FeF:\\rathena\\tools\\need_pvp_dummy\\native_smoke.exe F:\\rathena\\tools\\need_pvp_dummy\\native_smoke.cpp')
(OUT/'build_smoke.cmd').write_text(cmd,encoding='ascii')
print('Generated native smoke from production bodies and actual Claymore DB branches.')
