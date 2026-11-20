"""Build a native harness from current production bodies and effective produce DB.

World, inventory, packets, RNG and item DB are test doubles. The complete
skill_can_produce_mix/skill_produce_mix bodies and Twilight I/II/III handlers
are copied from source at generation time, without rewriting their logic.
The outer cast lifecycle (SP/Medicine Bowl) is inspected separately.
"""
from pathlib import Path
import re
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'generated/twilight3/smoke'
OUT.mkdir(parents=True, exist_ok=True)


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    pos = brace + 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]


s = (ROOT / 'src/map/skill.cpp').read_text('utf-8')
check = function(s, 'int16 skill_can_produce_mix(')
produce = function(s, 'bool skill_produce_mix(')
handlers = '\n'.join(function(
    (ROOT / f'src/map/skills/merchant/twilightalchemy{i}.cpp').read_text('utf-8'),
    f'void SkillTwilightAlchemy{i}::castendNoDamageId(') for i in (1, 2, 3))
baseline_source = subprocess.run(
    # Pin the original blob so committing the implementation does not change
    # the baseline used by the before/after consumption checks.
    ['git', '-C', str(ROOT), 'show', '00e0250d365e3a3b59fb9c373637f337de9513c1'],
    capture_output=True, check=True).stdout.decode('utf-8')
baseline = function(baseline_source, 'void SkillTwilightAlchemy3::castendNoDamageId(').replace(
    'SkillTwilightAlchemy3::', 'BaselineTwilight3::')
item_header = (ROOT / 'src/map/itemdb.hpp').read_text('utf-8')
fixed = {'AM_PHARMACY': 228, 'AM_TWILIGHT1': 496, 'AM_TWILIGHT2': 497,
         'AM_TWILIGHT3': 498, 'MAX_SKILL_PRODUCE_DB': 300, 'MAX_PRODUCE_RESOURCE': 12,
         'MAX_SKILL_CHANGEMATERIAL_DB': 10, 'MAX_SKILL_CHANGEMATERIAL_SET': 5,
         'JOBL_BABY': 1 << 20}
names = set(re.findall(r'\b[A-Z][A-Z_0-9]+\b', check + produce + handlers + baseline))
names -= {'TODO', 'SP', 'STR', 'LUK', 'DEX', 'INT', 'MSI', 'HP', 'CR', 'C', 'D',
          'PC', 'BL_CAST', 'DB'}
constants = []
for n in sorted(names):
    if n in fixed:
        v = fixed[n]
    elif (m := re.search(r'\b' + n + r'\s*=\s*(\d+)\b', item_header)):
        v = int(m[1])
    else:
        v = 100000 + len(constants)
    constants.append(f'constexpr int {n} = {v};')

prefix = r'''
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
using int16=int16_t; using int32=int32_t; using uint8=uint8_t;
using uint16=uint16_t; using uint32=uint32_t; using t_itemid=uint32;
using t_tick=int64_t;
#define nullpo_ret(x) if(!(x)) return 0
#define BL_CAST(type, p) static_cast<map_session_data*>(p)
struct block_list {};
struct item {t_itemid nameid; int amount,identify,card[4];};
struct status_data {int int_=100,dex=100,luk=100;};
struct map_session_data: block_list {
 struct {struct {item items_inventory[128];}u;} inventory={};
 struct {int job_level=50,base_level=99,char_id=12345;} status;
 status_data stats;
 int class_=0,cook_mastery=0,itemid=0,menuskill_id=0,menuskill_val=0;
 int potion_success_counter=7,skill_id_old=0,skill_lv_old=1,m=0,x=0,y=0;
 void* hd=nullptr;
 std::map<int,int> skills;
};
'''
struct_db = (ROOT / 'src/map/skill.hpp').read_text('utf-8')
struct_db = function(struct_db, 'struct s_skill_produce_db {') + ';\n'
fixtures = r'''
s_skill_produce_db skill_produce_db[MAX_SKILL_PRODUCE_DB]={};
struct Change {int nameid,rate,qty_rate[MAX_SKILL_CHANGEMATERIAL_SET],qty[MAX_SKILL_CHANGEMATERIAL_SET];} skill_changematerial_db[MAX_SKILL_CHANGEMATERIAL_DB]={};
struct {
 int pp_rate=100,wp_rate=100,oridecon_research_fix=0,produce_item_name_input=2;
 int fame_forge=10,fame_pharmacy_3=1,fame_pharmacy_5=2,fame_pharmacy_7=3,fame_pharmacy_10=5;
 bool skill_drop_items_full=true;
} battle_config;
int failures=0,checks=0,skill_failures=0,material_errors=0,fame_total=0;
int rng_mode=0,rng_counter=0; bool add_fail=false;
std::map<int,int> removed,added,floor_items,attempts;std::vector<item> products;
struct ItemDB {bool exists(t_itemid id){return id!=0;}} item_db;
uint32 pc_inventory_count(map_session_data* sd,t_itemid id){uint32 n=0;for(auto&i:sd->inventory.u.items_inventory)if(i.nameid==id)n+=i.amount;return n;}
int16 pc_search_inventory(map_session_data* sd,t_itemid id){for(int16 n=0;n<128;n++){auto&i=sd->inventory.u.items_inventory[n];if(i.nameid==id&&i.amount>0)return n;}return -1;}
int pc_checkskill(map_session_data* sd,int id){return sd->skills[id];}
int pc_checkadditem(map_session_data*,int,int){return 0;}
int pc_delitem(map_session_data* sd,int idx,int n,int,int,int){auto&i=sd->inventory.u.items_inventory[idx];if(n<0||n>i.amount){++material_errors;return 1;}i.amount-=n;removed[i.nameid]+=n;return 0;}
void put(map_session_data& sd,int id,int n){for(auto&i:sd.inventory.u.items_inventory)if(i.amount==0){i={};i.nameid=id;i.amount=n;return;}throw "full fixture";}
int pc_additem(map_session_data*sd,item*p,int n,int){if(add_fail)return 1;added[p->nameid]+=n;products.push_back(*p);put(*sd,p->nameid,n);return 0;}
void map_addflooritem(item*p,int n,int,int,int,int,int,int,int,int){floor_items[p->nameid]+=n;}
status_data* status_get_status_data(map_session_data&sd){return &sd.stats;}
bool itemdb_isequip(int){return false;} int itemdb_type(int){return 0;}
int itemdb_wlv(int){return 0;} bool itemdb_isstackable(int){return true;}
bool hom_is_active(void*){return false;} int hom_checkskill(void*,int){return 0;}
uint32 rnd(){++rng_counter;return rng_mode==0?0:rng_mode==1?9999:(rng_counter%2?0:9999);}
int rnd_value(int,int){return 1;}
int cap_value(int v,int a,int b){return std::max(a,std::min(v,b));}
int GetWord(int v,int n){return (v>>(16*n))&65535;}
void ShowError(const char*){++material_errors;}
void pc_addfame(map_session_data&,int n){fame_total+=n;}
template<class... T> void clif_produceeffect(T...){}
template<class... T> void clif_misceffect(T...){}
template<class... T> void clif_msg_skill(T...){}
template<class... T> void clif_additem(T...){}
template<class... T> void clif_specialeffect(T...){}
template<class... T> void clif_skill_nodamage(T...){}
template<class... T> void status_percent_damage(T...){}
void pc_setparam(map_session_data*,int,int){}
void clif_skill_fail(map_session_data&,int){++skill_failures;}
'''
wrapper = r'''
bool skill_produce_mix(map_session_data* sd,uint16 skill,t_itemid id,int32 s1,int32 s2,int32 s3,int32 qty,int16 idx){attempts[id]+=qty;return production_mix(sd,skill,id,s1,s2,s3,qty,idx);}
'''
classes = '\n'.join(f'''struct SkillTwilightAlchemy{i} {{
 uint16 getSkillId() const {{return AM_TWILIGHT{i};}}
 void castendNoDamageId(block_list*,block_list*,uint16,t_tick,int32&) const;
}};''' for i in (1, 2, 3))
classes += '''struct BaselineTwilight3 {
 uint16 getSkillId() const {return AM_TWILIGHT3;}
 void castendNoDamageId(block_list*,block_list*,uint16,t_tick,int32&) const;
};'''

# Same overwrite order as skill_readdb(): base mode followed by db/import.
recipes = {}
for p in (ROOT / 'db/re/produce_db.txt', ROOT / 'db/import/produce_db.txt'):
    for line in p.read_text('utf-8-sig').splitlines():
        line = line.split('//', 1)[0].strip()
        if line:
            row = [int(x) for x in line.split(',')]
            recipes[row[0]] = row
effective = [r for r in recipes.values() if r[1] in (970, 7135, 7136, 504, 547) and r[3] == 228]
setup = 'void setup(){\n'
for row in effective:
    idx, nameid, itemlv, req, lv, *mats = row
    setup += f'skill_produce_db[{idx}].nameid={nameid};skill_produce_db[{idx}].itemlv={itemlv};skill_produce_db[{idx}].req_skill={req};skill_produce_db[{idx}].req_skill_lv={lv};\n'
    for j in range(0, len(mats), 2):
        setup += f'skill_produce_db[{idx}].mat_id[{j//2}]={mats[j]};skill_produce_db[{idx}].mat_amount[{j//2}]={mats[j+1]};\n'
setup += '}\n'
(OUT / 'effective_recipes.json').write_text(json.dumps(effective, indent=2), 'utf-8')

tests = r'''
void expect(bool ok,const std::string& name){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";}
void reset(){removed.clear();added.clear();floor_items.clear();attempts.clear();products.clear();skill_failures=material_errors=fame_total=rng_counter=0;rng_mode=0;add_fail=false;}
map_session_data normal(int alcohol=50,int bottle=100,int acid=50,int fabric=50){
 reset();map_session_data sd;sd.skills[AM_PHARMACY]=10;
 put(sd,7128,1);put(sd,7129,1);put(sd,970,alcohol);put(sd,713,bottle);put(sd,929,acid);put(sd,1059,fabric);return sd;
}
void run(map_session_data&sd){int32 flag=0;SkillTwilightAlchemy3().castendNoDamageId(&sd,&sd,1,0,flag);}
int main(){setup();
 {auto sd=normal();run(sd);expect(!skill_failures&&attempts[7136]==50&&attempts[7135]==50&&attempts[970]==0,"T1/T4 no Alcohol raw materials or guide; exactly 50+50 attempts");
 expect(removed[970]==50&&removed[713]==100&&removed[929]==50&&removed[1059]==50&&added[970]==0&&removed[7128]==0&&removed[7129]==0&&!material_errors,"T5 consumption Alcohol=50 Bottle=100 ImmortalHeart=50 Fabric=50; guides retained, no double debit");
 expect(added[7135]==50&&added[7136]==50,"all successful RNG gives 50+50, no Alcohol");
 expect(!products.empty()&&products[0].card[0]==CARD0_CREATE&&products[0].card[2]==12345,"maker name preserved");}
 {auto sd=normal(49);run(sd);expect(skill_failures==1&&attempts.empty()&&removed.empty()&&added.empty(),"T2 49 Alcohol rejects both batches before recipe debit");}
 for(int which=0;which<3;which++){auto sd=normal(50,which==0?99:100,which==1?49:50,which==2?49:50);run(sd);expect(skill_failures==1&&attempts.empty()&&removed.empty()&&added.empty(),"T3 missing material "+std::to_string(which)+" rejects both batches without partial debit");}
 {auto sd=normal();int idx=pc_search_inventory(&sd,7128);sd.inventory.u.items_inventory[idx].amount=0;run(sd);expect(skill_failures==1&&removed.empty(),"missing Fire Bottle guide rejects before debit");}
 {auto sd=normal();int idx=pc_search_inventory(&sd,7129);sd.inventory.u.items_inventory[idx].amount=0;run(sd);expect(skill_failures==1&&removed.empty(),"missing Acid Bottle guide rejects before debit");}
 for(int mode:{1,2}){auto sd=normal();rng_mode=mode;run(sd);expect(removed[970]==50&&removed[713]==100&&removed[929]==50&&removed[1059]==50&&!material_errors&&attempts[7135]==50&&attempts[7136]==50,"T6 failure/mixed RNG consumes recipe once, mode="+std::to_string(mode));
 expect(mode==1?(added.empty()&&sd.potion_success_counter==0):(added[7135]==25&&added[7136]==25&&fame_total==0),"T6 zero/mixed output follows original probability and counters");}
 {auto sd=normal(25);put(sd,970,25);run(sd);expect(!skill_failures&&removed[970]==50&&!material_errors,"split Alcohol stacks consumed exactly once");}
 {auto sd=normal();sd.skills[AM_PHARMACY]=0;run(sd);expect(skill_failures==1&&removed.empty(),"Pharmacy prerequisite preserved");}
 {auto sd=normal();add_fail=true;run(sd);expect(floor_items[7135]==50&&floor_items[7136]==50&&removed[970]==50&&!material_errors,"existing inventory-full floor drop policy retained");}
 {auto sd=normal();int idx=skill_can_produce_mix(&sd,7135,-1,50)-1;skill_produce_db[idx].mat_amount[2]=2;run(sd);expect(skill_failures==1&&removed.empty(),"actual DB quantities drive preflight (Fabric override)");skill_produce_db[idx].mat_amount[2]=1;}
 {auto sd=normal(50,100,99);int idx=skill_can_produce_mix(&sd,7135,-1,50)-1;skill_produce_db[idx].mat_id[4]=929;skill_produce_db[idx].mat_amount[4]=1;run(sd);expect(skill_failures==1&&removed.empty(),"shared non-Bottle material from DB is summed before either batch");skill_produce_db[idx].mat_id[4]=skill_produce_db[idx].mat_amount[4]=0;}
 {auto sd=normal(50,200);put(sd,7127,1);put(sd,7033,500);put(sd,905,500);put(sd,1092,100);int32 flag=0;BaselineTwilight3().castendNoDamageId(&sd,&sd,1,0,flag);
 expect(!skill_failures&&removed[713]==200&&removed[7033]==500&&removed[905]==500&&removed[1092]==100&&removed[970]==50&&added[970]==100,"baseline old handler confirms actual before-material table");}
 {auto sd=normal(49,200);put(sd,7127,1);put(sd,7033,500);put(sd,905,500);put(sd,1092,100);int32 flag=0;BaselineTwilight3().castendNoDamageId(&sd,&sd,1,0,flag);expect(skill_failures==1&&removed.empty(),"baseline already required 50 pre-made Alcohol before manufacturing Alcohol");}
 {reset();map_session_data sd;sd.skills[AM_PHARMACY]=10;put(sd,7144,1);put(sd,509,200);put(sd,1093,200);int32 flag=0;SkillTwilightAlchemy1().castendNoDamageId(&sd,&sd,1,0,flag);expect(attempts[504]==200&&added[504]==200&&removed[509]==200&&removed[1093]==200,"T7 Twilight I original production path");}
 {reset();map_session_data sd;sd.skills[AM_PHARMACY]=10;put(sd,7133,1);put(sd,504,200);put(sd,1092,200);put(sd,1061,200);int32 flag=0;SkillTwilightAlchemy2().castendNoDamageId(&sd,&sd,1,0,flag);expect(attempts[547]==200&&added[547]==200&&removed[504]==200&&removed[1061]==200,"T7 Twilight II original production path");}
 {auto sd=normal(1,1,1,1);bool ok=skill_produce_mix(&sd,AM_PHARMACY,7135,0,0,0,1,-1);expect(ok&&added[7135]==1&&removed[970]==1&&removed[1059]==1&&removed[713]==1,"T7 ordinary Pharmacy Fire Bottle unchanged");}
 std::cout<<"RESULT "<<checks-failures<<" passed, "<<failures<<" failed\n";return failures?1:0;
}
'''
body = prefix + '\n'.join(constants) + '\n' + struct_db + fixtures + check + '\n' + produce.replace('bool skill_produce_mix(', 'bool production_mix(', 1) + wrapper + classes + handlers + baseline + setup + tests
(OUT / 'twilight_smoke.cpp').write_text(body, 'utf-8')
project = '''<Project DefaultTargets="Build" ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<ItemGroup Label="ProjectConfigurations"><ProjectConfiguration Include="Debug|Win32"><Configuration>Debug</Configuration><Platform>Win32</Platform></ProjectConfiguration></ItemGroup>
<PropertyGroup Label="Globals"><ProjectGuid>{7E0CF53D-8CDD-491B-8D28-FD37AB357F8A}</ProjectGuid></PropertyGroup>
<Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props"/>
<PropertyGroup Label="Configuration"><ConfigurationType>Application</ConfigurationType><UseDebugLibraries>true</UseDebugLibraries><PlatformToolset>$(DefaultPlatformToolset)</PlatformToolset></PropertyGroup>
<Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props"/>
<PropertyGroup><OutDir>$(ProjectDir)bin\\</OutDir><IntDir>$(ProjectDir)obj\\</IntDir></PropertyGroup>
<ItemDefinitionGroup><ClCompile><LanguageStandard>stdcpp17</LanguageStandard><DebugInformationFormat>OldStyle</DebugInformationFormat><AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions></ClCompile><Link><SubSystem>Console</SubSystem></Link></ItemDefinitionGroup>
<ItemGroup><ClCompile Include="twilight_smoke.cpp"/></ItemGroup><Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets"/>
</Project>'''
(OUT / 'twilight_smoke.vcxproj').write_text(project, 'utf-8')
print('Generated native smoke using complete production bodies and effective recipes:', OUT)
