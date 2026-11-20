"""Generate a small native packet smoke test from the current production source.

Run from the repository root with Python; compile the emitted .cpp with the map
server's include/define options. No server, DB connection or client is started.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / '.vs' / 'need_equipment_inspection'
OUT.mkdir(parents=True, exist_ok=True)


def extract(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


clif = (ROOT / 'src/map/clif.cpp').read_text(encoding='utf-8')
pc = (ROOT / 'src/map/pc.hpp').read_text(encoding='utf-8')
build = (ROOT / 'src/map/need_equipment_build.cpp').read_text(encoding='utf-8')
stub = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <memory>
#include <map>
#include <vector>
#include <string>
#include "map/packets.hpp"
#define nullpo_retv(p) do { if (!(p)) return; } while (0)
#define EQP_HELM (EQP_HEAD_LOW|EQP_HEAD_MID|EQP_HEAD_TOP)
#define EQP_COSTUME (EQP_COSTUME_HEAD_TOP|EQP_COSTUME_HEAD_MID|EQP_COSTUME_HEAD_LOW|EQP_COSTUME_GARMENT)
#define EQP_VISIBLE (EQP_HELM|EQP_GARMENT|EQP_COSTUME)
constexpr t_itemid CARD0_PET=0x100, CARD0_FORGE=0xff, CARD0_CREATE=0xfe;
constexpr int SELF=0;
int32 rnd() {return 0;}
char* safestrncpy(char* out,const char* in,size_t size) {
    if(size) {std::strncpy(out,in,size); out[size-1]=0;} return out;
}
struct item_data {
    t_itemid nameid=0, view_id=0; int32 type=IT_ARMOR; uint32 equip=0, look=0;
    struct {bool bindOnEquip=false;} flag;
};
struct FakeDB {
    std::map<t_itemid,std::shared_ptr<item_data>> entries;
    std::shared_ptr<item_data> find(t_itemid id) {return entries.count(id)?entries[id]:nullptr;}
} item_db;
t_itemid itemdb_viewid(t_itemid id) {auto p=item_db.find(id); return p?p->view_id:0;}
item_data* itemdb_search(t_itemid id) {return item_db.find(id).get();}
int itemdb_slots(t_itemid) {return 4;}
bool itemdb_isequip2(const item_data* id) {return id->type==IT_ARMOR||id->type==IT_WEAPON||id->type==IT_SHADOWGEAR;}
class map_session_data {
public:
    int fd=1, m=1;
    struct {bool show_equip=false; uint32 account_id=1, char_id=1; char name[NAME_LENGTH]="A";} status;
    struct {int16 look[16]{}; uint8 sex=0;} vd;
    struct {struct {item items_inventory[MAX_INVENTORY]{};} u;} inventory;
    item_data* inventory_data[MAX_INVENTORY]{};
    int32 equip_index[32];
    map_session_data() {for(auto& i:equip_index) i=-1;}
};
int32 pc_equippoint(const map_session_data* sd,int k) {return sd->inventory_data[k]->equip;}
char packet_buffer[65536];
std::vector<char> sent;
int sends=0, messages=0;
int clif_send(const void* p,int len,const map_session_data*,int) {sent.assign((const char*)p,(const char*)p+len); ++sends; return 0;}
void clif_displaymessage(int,const char*) {++messages;}
const char* map_msg_txt(const map_session_data*,int32) {return "base preview";}
struct s_clif_viewequip_snapshot_item {item data{}; int32 equip_position=0;};
struct PacketDB {int pos[1]{2};} packet_db[2];
map_session_data* target=nullptr;
uint16 RFIFOW(int,int) {return 1;}
uint32 RFIFOL(int,int) {return 2;}
map_session_data* map_id2sd(int) {return target;}
int checks=0;
void check(bool ok,const char* what) {++checks; if(!ok) {std::printf("FAIL: %s\n",what); std::exit(1);}}
'''
parts = [stub, extract(pc, 'enum equip_index') + ';']
for signature in [
    'static inline int32 itemtype(', 'static inline uint16 client_index(',
    'static inline uint32 client_nameid(', 'static void clif_addcards(',
    'static void clif_add_random_options(', 'static void clif_item_equip(',
    'static PACKET_ZC_EQUIPWIN_MICROSCOPE* clif_viewequip_start(',
    'static bool clif_viewequip_add_item(', 'void clif_viewequip_ack(',
    'bool clif_viewequip_snapshot(', 'void clif_parse_ViewPlayerEquip(',
]:
    parts.append(extract(clif, signature))

policy = re.search(r'const bool show_details = \(owner_account_id.*?;', build, re.S).group()
parts.append('bool snapshot_details(const map_session_data* sd, uint32 owner_account_id, uint32 owner_char_id, '
             'const map_session_data* owner, bool saved_show_details, bool owner_online) {\n' +
             policy.replace('const bool show_details =', 'return', 1) + '\n}')
parts.append((Path(__file__).parent / 'smoke_main.inc').read_text(encoding='utf-8'))
(OUT / 'smoke.cpp').write_text('\n\n'.join(parts), encoding='utf-8')
print('Generated', OUT / 'smoke.cpp')
