# -*- coding: utf-8 -*-
"""Generate the unified enchant menu NPC script from the master + ClientId registry.

    python tools/need_enchant/menu_script.py [--out npc/NEED/need_enchant_v2.txt]

The script (CP949, CRLF like every npc/NEED file) holds:
  - NEED_EnchantV2_Data   data NPC: family list, per group label/condition/cost/quest, item -> groups
  - F_NeedEnchantV2       menu function: "my gear" list (inventory scan) and the full catalogue;
                          picking a group closes the dialog and opens the 2025 Enchant UI (item_enchant).
                          callfunc "F_NeedEnchantV2", "<FAMILY>" shows one family only (legacy NPC hook).
It is generated, never edited by hand; regenerate after every master / registry change.
Gates (exit 1, nothing written): every registry group of the master is in the script, every target item maps
to its groups, the text encodes to CP949, no label breaks a select() menu (':').
"""
import argparse
import io
import os
import sys
from collections import OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import generate   # noqa: E402

REPO = generate.REPO
OUT = os.path.join(REPO, 'npc', 'NEED', 'need_enchant_v2.txt')


def q(s):
    """script string literal"""
    if ':' in s:
        raise generate.GenError('label %r contains ":" (breaks select menus)' % s)
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def slot_costs(v):
    out = []
    for sl in v['order']:
        s = v['slots'][sl]
        if s['options']:
            out.append((s['cost']['zeny'], tuple(s['cost']['mats'].items())))
    return out


FUNCTION = r'''
//= F_NeedEnchantV2 ( { "<FAMILY>" } )
//=   no argument : my gear (inventory) or the full catalogue
//=   "<FAMILY>"  : only the groups of that family (for an existing NPC's menu)
function	script	F_NeedEnchantV2	{
	.@npc$ = "NEED_EnchantV2_Data";
	.@filter$ = getarg(0, "");
	.@title$ = needtr("[통합 인챈트]", "[Unified Enchant]");
	mes .@title$;
	mes needtr("인챈트할 장비를 고르면 바로 인챈트 창이 열립니다.", "Pick your gear and the enchant window opens.");
	mes needtr("^FF0000장비는 착용을 해제한 상태여야 합니다.^000000", "^FF0000The gear must be unequipped.^000000");
	next;
	switch (select(needtr("내 장비에서 찾기", "Find from my gear"), needtr("전체 인챈트 목록", "All enchants"), needtr("그만두기", "Cancel"))) {
	case 1:
		break;
	case 2:
		callsub S_Catalogue, .@filter$;
		end;
	default:
		close;
	}

	// ---- my gear: one line per item id that has at least one group (of the filter family)
	getinventorylist;
	.@n = 0;
	for (.@i = 0; .@i < @inventorylist_count; .@i++) {
		.@id = @inventorylist_id[.@i];
		.@gs$ = callsub(S_Groups, .@id, .@filter$);
		if (.@gs$ == "")
			continue;
		for (.@j = 0; .@j < .@n; .@j++)
			if (.@ids[.@j] == .@id)
				break;
		if (.@j == .@n) {
			.@ids[.@n] = .@id;
			.@grp$[.@n] = .@gs$;
			.@n++;
		}
		if (!@inventorylist_equip[.@i])
			.@free[.@j] = 1;
	}
	if (.@n == 0) {
		mes .@title$;
		mes needtr("인챈트할 수 있는 장비를 가지고 있지 않습니다.", "You have no gear that can be enchanted.");
		mes needtr("'전체 인챈트 목록'에서 대상 장비를 확인할 수 있습니다.", "See 'All enchants' for the target gear.");
		close;
	}
	.@menu$ = "";
	for (.@j = 0; .@j < .@n; .@j++) {
		explode(.@one$, .@grp$[.@j], ",");
		.@menu$ += getitemname(.@ids[.@j]) + " (" + getarraysize(.@one$) + needtr("종)", " kinds)") + (.@free[.@j] ? "" : needtr(" [착용 중]", " [equipped]")) + ":";
		deletearray .@one$[0], getarraysize(.@one$);
	}
	mes .@title$;
	mes needtr("인챈트할 장비를 고르세요.", "Choose the gear to enchant.");
	next;
	.@s = select(.@menu$ + needtr("그만두기", "Cancel")) - 1;
	if (.@s >= .@n)
		close;
	callsub S_Pick, .@grp$[.@s], getitemname(.@ids[.@s]);
	end;

// ---- groups of an item (comma list), limited to a family when given
S_Groups:
	.@all$ = getvariableofnpc(.it$[getarg(0)], "NEED_EnchantV2_Data");
	if (.@all$ == "" || getarg(1) == "")
		return .@all$;
	explode(.@g$, .@all$, ",");
	.@out$ = "";
	for (.@k = 0; .@k < getarraysize(.@g$); .@k++)
		if (getvariableofnpc(.gfk$[atoi(.@g$[.@k])], "NEED_EnchantV2_Data") == getarg(1))
			.@out$ += (.@out$ == "" ? "" : ",") + .@g$[.@k];
	return .@out$;

// ---- one line per group: label [condition] - cost; a single group opens at once
S_Pick:
	explode(.@pg$, getarg(0), ",");
	.@pn = getarraysize(.@pg$);
	if (.@pn == 1)
		callsub S_Open, atoi(.@pg$[0]);
	.@pm$ = "";
	for (.@k = 0; .@k < .@pn; .@k++)
		.@pm$ += callsub(S_Line, atoi(.@pg$[.@k])) + ":";
	mes needtr("[통합 인챈트]", "[Unified Enchant]");
	mes "^0000FF" + getarg(1) + "^000000";
	mes needtr("인챈트 종류를 고르세요. 조건과 1회 비용이 함께 표시됩니다.", "Choose the enchant. Each line shows its condition and the cost per try.");
	next;
	.@ps = select(.@pm$ + needtr("그만두기", "Cancel")) - 1;
	if (.@ps >= .@pn)
		close;
	callsub S_Open, atoi(.@pg$[.@ps]);
	end;

S_Line:
	.@lg = getarg(0);
	.@ls$ = needtr(getvariableofnpc(.gkr$[.@lg], "NEED_EnchantV2_Data"), getvariableofnpc(.gen$[.@lg], "NEED_EnchantV2_Data"));
	.@lmin = getvariableofnpc(.grmin[.@lg], "NEED_EnchantV2_Data");
	.@lmax = getvariableofnpc(.grmax[.@lg], "NEED_EnchantV2_Data");
	if (.@lmin && .@lmax)
		.@ls$ += " [+" + .@lmin + "~" + .@lmax + "]";
	else if (.@lmin)
		.@ls$ += needtr(" [+" + .@lmin + " 이상]", " [+" + .@lmin + " or higher]");
	else if (.@lmax)
		.@ls$ += needtr(" [+" + .@lmax + " 이하]", " [+" + .@lmax + " or lower]");
	return .@ls$ + " - " + callsub(S_Cost, .@lg);

S_Cost:
	.@cz = getvariableofnpc(.gz[getarg(0)], "NEED_EnchantV2_Data");
	.@cm$ = getvariableofnpc(.gm$[getarg(0)], "NEED_EnchantV2_Data");
	if (.@cm$ == "-")
		return needtr("칸마다 다름", "varies by slot");
	if (.@cm$ == "P")
		return needtr("확정 인챈트", "guaranteed pick");
	.@cs$ = "";
	if (.@cz)
		.@cs$ = callfunc("F_InsertComma", .@cz) + "z";
	if (.@cm$ != "") {
		explode(.@cp$, .@cm$, ";");
		for (.@c = 0; .@c < getarraysize(.@cp$); .@c++) {
			explode(.@ci$, .@cp$[.@c], "/");
			.@cs$ += (.@cs$ == "" ? "" : " + ") + getitemname(atoi(.@ci$[0])) + " " + .@ci$[1];
		}
	}
	return (.@cs$ == "" ? needtr("무료", "free") : .@cs$);

// ---- quest condition, then the 2025 Enchant UI
S_Open:
	.@og = getarg(0);
	.@oq = getvariableofnpc(.gq[.@og], "NEED_EnchantV2_Data");
	if (.@oq && isbegin_quest(.@oq) != 2) {
		mes needtr("[통합 인챈트]", "[Unified Enchant]");
		mes needtr("이 인챈트는 선행 퀘스트를 완료해야 이용할 수 있습니다.", "This enchant needs its prerequisite quest to be completed first.");
		close;
	}
	close2;
	item_enchant .@og;
	end;

// ---- full catalogue: family -> group
S_Catalogue:
	.@cf$ = getarg(0);
	.@fn = getvariableofnpc(.famn, "NEED_EnchantV2_Data");
	if (.@cf$ == "") {
		.@fm$ = "";
		for (.@f = 0; .@f < .@fn; .@f++)
			.@fm$ += needtr(getvariableofnpc(.famkr$[.@f], "NEED_EnchantV2_Data"), getvariableofnpc(.famen$[.@f], "NEED_EnchantV2_Data")) + ":";
		mes needtr("[통합 인챈트]", "[Unified Enchant]");
		mes needtr("인챈트 계열을 고르세요.", "Choose an enchant family.");
		next;
		.@f = select(.@fm$ + needtr("그만두기", "Cancel")) - 1;
		if (.@f >= .@fn)
			close;
	} else {
		for (.@f = 0; .@f < .@fn; .@f++)
			if (getvariableofnpc(.fam$[.@f], "NEED_EnchantV2_Data") == .@cf$)
				break;
		if (.@f >= .@fn)
			close;
	}
	callsub S_Pick, getvariableofnpc(.famg$[.@f], "NEED_EnchantV2_Data"), needtr(getvariableofnpc(.famkr$[.@f], "NEED_EnchantV2_Data"), getvariableofnpc(.famen$[.@f], "NEED_EnchantV2_Data"));
	end;
}
'''


def build(master, registry):
    items, items_by_id = generate.load_items(REPO)
    families, variants = generate.load_master(master, items)
    ids, _retired = generate.load_registry(registry)
    for v in variants:
        if v['key'] not in ids:
            raise generate.GenError('%s has no ClientId (run generate.py --commit-registry first)' % v['key'])
        v['id'] = ids[v['key']]
    fam_order = OrderedDict()
    for v in variants:
        fam_order.setdefault(v['family'], {'display': v['family_display'], 'groups': []})['groups'].append(v['id'])
    item_groups = OrderedDict()
    L = ['//===== NEED Unified Enchant V2: menu =========================================',
         '//= GENERATED by tools/need_enchant/menu_script.py from db/need/enchant_master.yml',
         '//= and db/need/enchant_clientid_registry.yml - do not edit by hand, regenerate.',
         '//= %d families, %d groups.' % (len(fam_order), len(variants)),
         '//============================================================================',
         '-\tscript\tNEED_EnchantV2_Data\t-1,{',
         'OnInit:',
         '\t.famn = %d;' % len(fam_order)]
    for n, (fkey, f) in enumerate(fam_order.items()):
        d = f['display'] or {}
        L += ['\t.fam$[%d] = %s;' % (n, q(fkey)),
              '\t.famkr$[%d] = %s;' % (n, q(d.get('KR', fkey))),
              '\t.famen$[%d] = %s;' % (n, q(d.get('EN', fkey))),
              '\t.famg$[%d] = "%s";' % (n, ','.join(str(g) for g in f['groups']))]
    for v in variants:
        g = v['id']
        d = v['display'] or {}
        costs = slot_costs(v)
        if not costs:
            mats = 'P'                       # perfect-only groups (Excelion, time boots stage 1)
            zeny = 0
        elif len(set(costs)) > 1:
            mats, zeny = '-', 0
        else:
            zeny = costs[0][0]
            mats = ';'.join('%d/%d' % (items[m].id, a) for m, a in costs[0][1])
        quest = int((v['source'] or {}).get('Quest', 0) or 0)
        L += ['\t.gfk$[%d] = %s;' % (g, q(v['family'])),
              '\t.gkr$[%d] = %s;' % (g, q(d.get('KR', v['key']))),
              '\t.gen$[%d] = %s;' % (g, q(d.get('EN', v['key']))),
              '\t.grmin[%d] = %d;' % (g, v['min_refine']),
              '\t.grmax[%d] = %d;' % (g, v['max_refine']),
              '\t.gz[%d] = %d;' % (g, zeny),
              '\t.gm$[%d] = "%s";' % (g, mats)]
        if quest:
            L.append('\t.gq[%d] = %d;' % (g, quest))
        for t in v['targets']:
            item_groups.setdefault(items[t].id, []).append(g)
    for iid, gs in item_groups.items():
        L.append('\t.it$[%d] = "%s";' % (iid, ','.join(str(g) for g in gs)))
    L += ['\tend;', '}']
    text = '\r\n'.join(L) + '\r\n' + FUNCTION.replace('\r\n', '\n').replace('\n', '\r\n')
    # gates
    for v in variants:
        if ('.gkr$[%d]' % v['id']) not in text:
            raise generate.GenError('group %d missing from the script' % v['id'])
        for t in v['targets']:
            if v['id'] not in item_groups.get(items[t].id, []):
                raise generate.GenError('%s -> %d missing' % (t, v['id']))
    data = text.encode('cp949')
    return data, len(fam_order), len(variants), len(item_groups)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--master', default=os.path.join(REPO, 'db', 'need', 'enchant_master.yml'))
    ap.add_argument('--registry', default=os.path.join(REPO, 'db', 'need', 'enchant_clientid_registry.yml'))
    ap.add_argument('--out', default=OUT)
    args = ap.parse_args(argv)
    try:
        data, nf, ng, ni = build(args.master, args.registry)
    except (generate.GenError, UnicodeEncodeError) as e:
        print('MENU SCRIPT ERROR: %s' % e)
        return 1
    io.open(args.out, 'wb').write(data)
    print('wrote %s: %d families, %d groups, %d target items, %d bytes' % (generate.display_path(args.out), nf, ng, ni, len(data)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
