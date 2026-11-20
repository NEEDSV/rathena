"""Small offline smoke: compile the actual changed method bodies in a fake world.

No server/SQL connection. Engine, inventory, party and packet calls are doubles;
this verifies dispatch/consumption, not live gameplay or packet compatibility.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def method(path, name):
    text = (ROOT / path).read_text(encoding="utf-8")
    start = text.index("void " + name + "(")
    body = text.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
        end += 1
    return text[start:end]


prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>
using int32=int32_t; using int16=int16_t; using uint16=uint16_t;
using int64=int64_t; using t_tick=int64_t;
enum e_skill { SU_LUNATICCARROTBEAT=5036, SU_LUNATICCARROTBEAT2=5043,
 SU_CN_METEOR=5028, SU_CN_METEOR2=5042, SU_BUNCHOFSHRIMP=5040,
 SU_SPIRITOFSEA=5056, SU_SPIRITOFLAND=5054 };
constexpr int BL_PC=1, BCT_PARTY=4, SC_DORAM_SVSP=1, LOG_TYPE_CONSUME=1, SC_BLEEDING=2;
enum damage_lv { ATK_DEF };
struct block_list { int type=BL_PC, m=0, id=0; };
struct map_session_data : block_list {
 struct { int party_id=0; } status;
 std::map<int,int> inventory;
};
#define BL_CAST(kind, src) ((src) && (src)->type == (kind) ? static_cast<map_session_data*>(src) : nullptr)
struct SkillData { struct { int itemid[1]={0}, amount[1]={0}; } require; };
struct Database {
 std::map<int,std::shared_ptr<SkillData>> data;
 std::shared_ptr<SkillData> find(int id) {return data.count(id)?data[id]:nullptr;}
} skill_db;
int removed=0, units=0, heal_calls=0, buffs=0, root_calls=0, hit_calls=0;
int fail_delete=0, variant=0;
std::vector<map_session_data*> party;
uint16 pc_checkskill(map_session_data*,int) {return 0;}
int16 pc_search_inventory(map_session_data* sd,int id) {
 return id && sd->inventory[id]>0 ? static_cast<int16>(id) : -1;
}
int pc_delitem(map_session_data* sd,int index,int amount,int,int,int) {
 if(fail_delete || amount<1 || sd->inventory[index]<amount) return 1;
 sd->inventory[index]-=amount;removed+=amount;return 0;
}
int skill_get_time(int,int) {return 3500;}
int skill_get_time2(int,int) {return 120000;}
int skill_get_splash(int,int) {return 3;}
int skill_get_unit_interval(int) {return 500;}
int skill_get_sc(int) {return 1;}
int sc_start(block_list*,block_list*,int,int,int,int) {buffs++;return 1;}
int sc_start2(block_list*,block_list*,int,int,int,int,int duration) {assert(duration==120000);buffs++;return 1;}
int clif_skill_nodamage(block_list*,block_list&,int,int,int=1) {return 1;}
int skill_calc_heal(block_list*,block_list*,int id,uint16,bool) {assert(id==SU_BUNCHOFSHRIMP);return 123;}
int status_heal(block_list*,int hp,int,int) {assert(hp==123);heal_calls++;return hp;}
int rnd() {return 0;}
void skill_unitsetting(block_list*,e_skill id,uint16,int16,int16,int) {variant=id;units++;}
int skill_area_sub=0, skill_castend_nodamage_id=0;
class SkillBunchofShrimp { public:
 e_skill getSkillId()const{return SU_BUNCHOFSHRIMP;}
 void castendNoDamageId(block_list*,block_list*,uint16,t_tick,int32&)const;
};
void party_foreachsamemap(int,map_session_data* sd,int,block_list* src,int,uint16 lv,t_tick t,int flags,int) {
 assert(sd);for(auto p:party) SkillBunchofShrimp().castendNoDamageId(src,p,lv,t,flags);
}
class SkillCatnipMeteor {public:
 e_skill getSkillId()const{return SU_CN_METEOR;}
 void castendPos2(block_list*,int32,int32,uint16,t_tick,int32&)const;
};
class SkillScratch {public:
 e_skill getSkillId()const{return static_cast<e_skill>(5021);}
 void applyAdditionalEffects(block_list*,block_list*,uint16,t_tick,int32,enum damage_lv)const;
};
class SkillImplRecursiveDamageSplash {public:
 e_skill id; explicit SkillImplRecursiveDamageSplash(e_skill s):id(s){}
 void castendDamageId(block_list*,block_list*,uint16,t_tick,int32&)const;
};
class SkillLunaticCarrotBeat : public SkillImplRecursiveDamageSplash {public:
 SkillLunaticCarrotBeat():SkillImplRecursiveDamageSplash(SU_LUNATICCARROTBEAT){}
 void castendDamageId(block_list*,block_list*,uint16,t_tick,int32&)const;
};
class SkillLunaticCarrotBeat2 : public SkillImplRecursiveDamageSplash {public:
 SkillLunaticCarrotBeat2():SkillImplRecursiveDamageSplash(SU_LUNATICCARROTBEAT2){}
};
void SkillImplRecursiveDamageSplash::castendDamageId(block_list* src,block_list* dst,uint16 lv,t_tick t,int32& flags)const {
 variant=id;
 if(flags&1){hit_calls++;return;}
 root_calls++;int32 child=flags|1;
 for(int i=0;i<3;i++) {
  if(id==SU_LUNATICCARROTBEAT) SkillLunaticCarrotBeat().castendDamageId(src,dst,lv,t,child);
  else castendDamageId(src,dst,lv,t,child);
 }
}
void reset(){removed=units=heal_calls=buffs=root_calls=hit_calls=fail_delete=variant=0;party.clear();}
'''
parts = [prefix]
for path, name in [
    ("src/map/skills/summoner/catnipmeteor.cpp", "SkillCatnipMeteor::castendPos2"),
    ("src/map/skills/summoner/lunaticcarrotbeat.cpp", "SkillLunaticCarrotBeat::castendDamageId"),
    ("src/map/skills/summoner/bunchofshrimp.cpp", "SkillBunchofShrimp::castendNoDamageId"),
    ("src/map/skills/summoner/scratch.cpp", "SkillScratch::applyAdditionalEffects"),
]:
    parts.append(method(path, name))
status = (ROOT / "src/map/status.cpp").read_text(encoding="utf-8")
timer = status[status.index("TIMER_FUNC(status_change_timer)"):]
fresh = timer[timer.index("case SC_FRESHSHRIMP:"):]
expr = fresh.split("status_heal(bl, ", 1)[1].split(", 0, 2)", 1)[0]
parts.append("int64 fresh_heal(uint32_t max_hp, int spirit=0){struct {int val3;} state{spirit}; auto sce=&state; return " + expr.replace("status->max_hp", "max_hp") + ";}")
parts.append(r'''
int main(){
 auto fruit=std::make_shared<SkillData>();fruit->require.itemid[0]=11602;fruit->require.amount[0]=1;
 auto carrot=std::make_shared<SkillData>();carrot->require.itemid[0]=515;carrot->require.amount[0]=1;
 skill_db.data[SU_CN_METEOR2]=fruit;skill_db.data[SU_LUNATICCARROTBEAT2]=carrot;
 map_session_data src,a,b;int32 flags=0;
 // Magic: optional fruit is consumed once, never 6417, including multi-unit casts.
 reset();SkillCatnipMeteor().castendPos2(&src,20,20,5,0,flags);
 assert(units==7 && removed==0 && variant==SU_CN_METEOR);
 reset();src.inventory[11602]=2;src.inventory[6417]=3;
 SkillCatnipMeteor().castendPos2(&src,20,20,5,0,flags);
 assert(units==7 && removed==1 && src.inventory[11602]==1 && src.inventory[6417]==3 && variant==SU_CN_METEOR2);
 reset();fail_delete=1;SkillCatnipMeteor().castendPos2(&src,20,20,5,0,flags);
 assert(removed==0 && variant==SU_CN_METEOR);
 reset();src.inventory[11602]=0;SkillCatnipMeteor().castendPos2(&src,20,20,5,0,flags);
 assert(removed==0 && src.inventory[6417]==3 && variant==SU_CN_METEOR);
 reset();block_list npc;npc.type=2;SkillCatnipMeteor().castendPos2(&npc,20,20,5,0,flags);
 assert(units==7 && removed==0);
 // Physical: both paths hit every splash victim; no extra carrot per victim.
 reset();SkillLunaticCarrotBeat().castendDamageId(&src,&a,5,0,flags);
 assert(root_calls==1 && hit_calls==3 && removed==0 && variant==SU_LUNATICCARROTBEAT);
 reset();src.inventory[515]=2;SkillLunaticCarrotBeat().castendDamageId(&src,&a,5,0,flags);
 assert(root_calls==1 && hit_calls==3 && removed==1 && src.inventory[515]==1 && variant==SU_LUNATICCARROTBEAT2);
 reset();fail_delete=1;SkillLunaticCarrotBeat().castendDamageId(&src,&a,5,0,flags);
 assert(root_calls==1 && hit_calls==3 && removed==0 && variant==SU_LUNATICCARROTBEAT);
 reset();int32 child=1;SkillLunaticCarrotBeat().castendDamageId(&src,&a,5,0,child);
 assert(root_calls==0 && hit_calls==1 && removed==0);
 // Support: solo heal once; party root does not heal twice, nor include outsiders.
 reset();SkillBunchofShrimp().castendNoDamageId(&src,&src,5,0,flags);
 assert(heal_calls==1 && buffs==1);
 reset();src.status.party_id=1;party={&src,&a,&b};
 SkillBunchofShrimp().castendNoDamageId(&src,&src,5,0,flags);
 assert(heal_calls==3 && buffs==3);
 reset();SkillBunchofShrimp().castendNoDamageId(&npc,&a,5,0,flags);
 assert(heal_calls==1 && buffs==1);
 // Historical periodic heal uses current target max HP, with overflow-safe arithmetic.
 assert(fresh_heal(10000)==400 && fresh_heal(25000)==1000 && fresh_heal(10001)==400);
 assert(fresh_heal(1000000000)==40000000);
 assert(fresh_heal(10000,1)==800);
 reset();SkillScratch().applyAdditionalEffects(&src,&a,3,0,0,ATK_DEF);assert(buffs==1);
 puts("PASS: 14 source-body scenarios (magic 5, physical 5, support 3, periodic heal 1)");
}
''')
(ROOT / "docs/reports/need-doram-ep162-20261118/native_smoke.cpp").write_text("\n\n".join(parts), encoding="utf-8")
print("Generated native_smoke.cpp from current source method bodies")
