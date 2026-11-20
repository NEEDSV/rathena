// Stubs isolate I/O and item DB lookup; serializers/response/parser/policy are
// extracted unchanged by smoke.py. Packet definitions come from map/packets.hpp.
int main() {
    static_assert(PACKETVER == 20250604, "Recheck packet layout when changing the client version");
    static_assert(HEADER_ZC_EQUIPWIN_MICROSCOPE == 0x0b37, "Unexpected response header");
    static_assert(sizeof(PACKET_ZC_EQUIPWIN_MICROSCOPE)==47, "Header packing changed");
    static_assert(sizeof(EQUIPITEM_INFO)==68, "Item packing changed");
    static_assert(offsetof(EQUIPITEM_INFO, RefiningLevel)==65, "Refine offset changed");
    static_assert(offsetof(EQUIPITEM_INFO, grade)==66, "Grade offset changed");
    map_session_data a,b; b.status.account_id=2; b.status.char_id=2;
    const uint32 positions[EQI_MAX]={EQP_ACC_L,EQP_ACC_R,EQP_SHOES,EQP_GARMENT,
        EQP_HEAD_LOW,EQP_HEAD_MID,EQP_HEAD_TOP,EQP_ARMOR,EQP_HAND_L,EQP_HAND_R,
        EQP_COSTUME_HEAD_TOP,EQP_COSTUME_HEAD_MID,EQP_COSTUME_HEAD_LOW,EQP_COSTUME_GARMENT,
        EQP_AMMO,EQP_SHADOW_ARMOR,EQP_SHADOW_WEAPON,EQP_SHADOW_SHIELD,EQP_SHADOW_SHOES,
        EQP_SHADOW_ACC_R,EQP_SHADOW_ACC_L};
    for(int i=0;i<EQI_MAX;++i) {
        const int k=i+40;
        auto id=std::make_shared<item_data>(); id->nameid=10000+i; id->equip=positions[i];
        id->type=i>=EQI_SHADOW_ARMOR?IT_SHADOWGEAR:IT_ARMOR; id->look=100+i;
        item_db.entries[id->nameid]=id; a.inventory_data[k]=id.get(); a.equip_index[i]=k;
        item& it=a.inventory.u.items_inventory[k];
        it.nameid=id->nameid; it.equip=positions[i]; it.identify=1; it.refine=12;
        it.enchantgrade=3; it.card[0]=4001; it.card[1]=4002; it.card[2]=4700; it.card[3]=4800;
        it.option[0]={123,456,7}; it.option[4]={321,654,9};
        it.expire_time=123456789; it.bound=2; it.attribute=1; it.favorite=1;
        it.unique_id=0xfedcba9876543210ULL;
    }
    // Current serializer intentionally excludes ammo from the microscope.
    item_db.entries[10000+EQI_AMMO]->type=IT_AMMO;
    clif_viewequip_ack(b,a);
    auto off=sent;
    auto p=reinterpret_cast<const PACKET_ZC_EQUIPWIN_MICROSCOPE*>(off.data());
    check(p->PacketLength==47+20*68 && sent.size()==p->PacketLength,"all 20 normal/costume/shadow positions, empty ammo");
    bool base=true, full=true;
    int n=0;
    for(int i=0;i<EQI_MAX;++i) {
        if(i==EQI_AMMO) continue;
        const auto& it=a.inventory.u.items_inventory[i+40];
        const auto& id=*a.inventory_data[i+40];
        item clean{}; clean.nameid=it.nameid; clean.equip=it.equip; clean.identify=1;
        EQUIPITEM_INFO expected{};
        clif_item_equip(client_index(i),&expected,&clean,&id,positions[i]);
        base &= std::memcmp(&expected,&p->list[n++],sizeof(expected))==0;
    }
    check(base,"OFF bytes contain only base identity across normal/costume/shadow slots");
    check(messages==1,"OFF viewer receives missing-details explanation");
    auto original=a.inventory.u.items_inventory[40];
    for(int i=0;i<EQI_MAX;++i) {
        auto& it=a.inventory.u.items_inventory[i+40];
        it.refine=3; it.enchantgrade=4; it.card[0]=CARD0_FORGE;
        it.card[1]=65535; it.card[2]=5555; it.card[3]=6666;
        it.identify=0; it.expire_time=42; it.bound=3; it.favorite=0; it.attribute=0;
        for(auto& op:it.option) op={999,888,77};
    }
    clif_viewequip_ack(b,a);
    check(sent==off,"changing every secret including forge/maker data leaves OFF packet identical");
    a.status.show_equip=true;
    clif_viewequip_ack(b,a);
    auto on=sent; p=reinterpret_cast<const PACKET_ZC_EQUIPWIN_MICROSCOPE*>(on.data()); n=0;
    for(int i=0;i<EQI_MAX;++i) {
        if(i==EQI_AMMO) continue;
        EQUIPITEM_INFO expected{};
        clif_item_equip(client_index(i+40),&expected,&a.inventory.u.items_inventory[i+40],a.inventory_data[i+40],positions[i]);
        full &= std::memcmp(&expected,&p->list[n++],sizeof(expected))==0;
    }
    check(full,"ON keeps byte-for-byte original per-item serializer output");
    a.status.show_equip=false;
    clif_viewequip_ack(a,a);
    check(sent==on,"self inspection retains all original details with OFF");
    clif_viewequip_ack(b,a);
    check(sent==off,"ON to OFF returns sanitized packet on next request");
    check(a.inventory.u.items_inventory[40].refine==3 && a.inventory.u.items_inventory[40].card[0]==CARD0_FORGE,
        "response does not modify real inventory");
    a.inventory.u.items_inventory[40]=original;
    a.equip_index[EQI_ACC_L]=-1;
    clif_viewequip_ack(b,a);
    check(sent.size()==47+19*68,"unequipping removes the slot in the next response");
    a.equip_index[EQI_ACC_L]=40;
    target=&a; int before=sends;
    clif_parse_ViewPlayerEquip(1,&b);
    check(sends==before+1,"normal OFF request is accepted");
    b.m=2; before=sends; clif_parse_ViewPlayerEquip(1,&b);
    check(sends==before,"cross-map request remains rejected");
    b.m=1; target=nullptr; clif_parse_ViewPlayerEquip(1,&b);
    check(sends==before,"missing target remains rejected");
    std::vector<s_clif_viewequip_snapshot_item> items{{original,static_cast<int32>(original.equip)}};
    check(clif_viewequip_snapshot(b,"Build",1,items,false),"snapshot OFF accepted");
    p=reinterpret_cast<const PACKET_ZC_EQUIPWIN_MICROSCOPE*>(sent.data());
    check(p->list[0].RefiningLevel==0 && p->list[0].slot.card[0]==0 && p->list[0].option_data[0].index==0,
        "published snapshot cannot bypass sanitization");
    check(clif_viewequip_snapshot(b,"Build",1,items,true),"snapshot ON accepted");
    p=reinterpret_cast<const PACKET_ZC_EQUIPWIN_MICROSCOPE*>(sent.data());
    check(p->list[0].RefiningLevel==12 && p->list[0].slot.card[2]==4700 && p->list[0].option_data[0].index==123,
        "snapshot ON preserves refine/enchant/random option");
    check(!snapshot_details(&b,1,1,&a,true,true),"live OFF overrides stale public SQL");
    a.status.show_equip=true;
    check(snapshot_details(&b,1,1,&a,false,true),"live ON overrides stale private SQL");
    check(!snapshot_details(&b,1,1,nullptr,true,true),"remote online owner fails closed");
    check(snapshot_details(&b,1,1,nullptr,true,false),"offline ON uses saved setting");
    check(!snapshot_details(&b,1,1,nullptr,false,false),"offline OFF is private");
    check(snapshot_details(&a,1,1,nullptr,false,true),"snapshot owner always sees details");
    auto overflow=reinterpret_cast<PACKET_ZC_EQUIPWIN_MICROSCOPE*>(packet_buffer);
    overflow->PacketLength=INT16_MAX-1;
    check(!clif_viewequip_add_item(overflow,2,original,*a.inventory_data[40],original.equip,false),"packet size overflow rejected");
    std::printf("PASS: %d checks; PACKETVER=%d header=%zu item=%zu max-live=%zu\n",checks,PACKETVER,
        sizeof(PACKET_ZC_EQUIPWIN_MICROSCOPE),sizeof(EQUIPITEM_INFO),sizeof(PACKET_ZC_EQUIPWIN_MICROSCOPE)+EQI_MAX*sizeof(EQUIPITEM_INFO));
}
