"""NEED daily quest: quest window entries generated from db/need/daily_quest.yml.

usage: python -I -X utf8 gen_quests.py <rathena root> <out dir>

writes
  <root>/db/need/daily_quest_quests.yml   quest_db entries (imported by db/import/quest_db.yml)
  <out>/quests_kr.json, quests_en.json    client lub entries (OngoingQuestInfoList_True.lub) for build_quest_lub.py

The quest window only DISPLAYS the daily quest ledger. Objectives have no Mob, so the server
never counts kills for them (quest.cpp skips 99000-99999); need_daily_quest.cpp writes the count.
The objective name is sent by the server in the session language (msg 1586-1589), the yml
MapName is only a fallback.
"""
import json
import os
import sys

import yaml

root, out = sys.argv[1], sys.argv[2]

# track -> (KR range, EN range, KR material, EN material). Materials must match msg 1586-1589.
TRACKS = {
    100: ('Lv100~129', 'Lv100-129', '고장 난 동력핵', 'Broken Power Core'),
    130: ('Lv130~149', 'Lv130-149', '스카라바 껍질 조각', 'Scaraba Shell Shard'),
    150: ('Lv150~174', 'Lv150-174', '오염된 보급품', 'Tainted Supplies'),
    175: ('Lv175', 'Lv175', '고대 기계 부품', 'Ancient Machine Part'),
}
MAPS = {
    'juperos_02': ('유페로스 폐허 내부', 'Juperos 2'),
    'ice_dun03': ('얼음 동굴 3층', 'Ice Dungeon 3'),
    'ra_fild05': ('라헬 필드 5', 'Rachel Field 5'),
    'dic_dun02': ('스카라바 홀', 'Scaraba Hole 2'),
    'dic_dun01': ('카디널 터널', 'Scaraba Hole 1'),
    'bif_fild01': ('비프로스트 필드', 'Bifrost Field'),
    'prt_prison': ('지하 감옥', 'Prontera Prison'),
    'gl_cas02_': ('고성 2층 회랑', 'Glast Heim Castle 2 (N)'),
    'ver_eju': ('유페로스 동쪽폐허', 'Eastern Ruins of Juperos'),
    'verus02': ('베루스 연구동-WISH', 'Verus R&D-WISH'),
    'ra_fild10': ('라헬 필드 10', 'Rachel Field 10'),
}
# monster display names for the navi links (AegisName -> KR, EN)
MOBS = {
    'APOCALIPS': ('아포칼립스', 'Apocalips'), 'VENATU_1': ('베나투', 'Venatu'), 'VENATU_2': ('베나투', 'Venatu'),
    'VENATU_3': ('베나투', 'Venatu'), 'VENATU_4': ('베나투', 'Venatu'),
    'ICE_TITAN': ('아이스 타이탄', 'Ice Titan'), 'GAZETI': ('가제티', 'Gazeti'), 'SNOWIER': ('스노우어', 'Snowier'),
    'KOBOLD_1': ('코볼트', 'Kobold'), 'KOBOLD_2': ('코볼트', 'Kobold'), 'KOBOLD_3': ('코볼트', 'Kobold'),
    'ANTLER_SCARABA': ('앤틀러 스카라바', 'Antler Scaraba'), 'RAKE_SCARABA': ('레이크 스카라바', 'Rake Scaraba'),
    'HORN_SCARABA': ('혼 스카라바', 'Horn Scaraba'), 'HORN_SCARABA2': ('혼 스카라바', 'Horn Scaraba'),
    'MIMING': ('미밍', 'Miming'), 'LITTLE_FATUM': ('리틀 파툼', 'Little Fatum'),
    'ANGRA_MANTIS': ('앙그라 맨티스', 'Angra Mantis'), 'POM_SPIDER': ('폼 스파이더', 'Pom Spider'),
    'FROZENWOLF': ('프로즌 울프', 'Frozen Wolf'), 'TAFFY': ('태피', 'Taffy'), 'WATCHER': ('와처', 'Watcher'),
    'NG_WANDER_MAN': ('방랑자', 'Wanderer'), 'NG_RIDEWORD': ('라이드 워드', 'Rideword'),
    'RECON_ROBOT': ('정찰 로봇', 'Recon Robot'), 'EXCAVATOR_ROBOT': ('굴착 로봇', 'Excavator Robot'),
    'REPAIR_ROBOT_T': ('수리로봇 터보', 'Repair Robot Turbo'),
    'EXPLORATION_ROVER_T': ('탐사로봇 터보', 'Exploration Rover Turbo'),
    'EP18_GREY_GOAT': ('잿빛 고우트', 'Ashen Goat'), 'EP18_GREY_WOLF_BABY': ('새끼 회색 늑대', 'Baby Gray Wolf'),
}
MOBS.update({
    'EXPLOSION': ('익스플로젼', 'Explosion'), 'LAVA_GOLEM': ('라바 골렘', 'Lava Golem'), 'BLAZZER': ('블레이저', 'Blazer'),
    'KAHO': ('카호', 'Kaho'), 'NIGHTMARE_TERROR': ('나이트메어 테러', 'Nightmare Terror'), 'DELETER_': ('딜리터', 'Deleter'),
    'DELETER': ('딜리터', 'Deleter'), 'DIABOLIC': ('디아볼릭', 'Diabolic'), 'GIG': ('기그', 'Gig'),
    'DR_BASILISK2': ('돌격 바실리스크', 'Assault Basilisk'), 'DR_BASILISK3': ('돌격 바실리스크', 'Assault Basilisk'),
    'DR_MANDRAGORA': ('정글 만드라고라', 'Jungle Mandragora'), 'DR_POM_SPIDER': ('프루츠 폼 스파이더', 'Fruit Pom Spider'),
    'BIG_BELL': ('빅 벨', 'Big Bell'), 'TIME_KEEPER': ('타임 키퍼', 'Time Keeper'), 'OWL_VISCOUNT': ('아울 바이카운트', 'Owl Viscount'),
    'OWL_MARQUEES': ('아울 마퀴스', 'Owl Marquis'), 'NG_RIDEWORD': ('저주받은 책', 'Rideword'), 'NG_MIMIC': ('저주받은 상자', 'Mimic'),
})
# hunting grounds (phase 2): track -> (KR, EN)
HUNT_AREAS = {
    100: ('노그로드 1·2층', 'Magma Dungeon 1-2'),
    130: ('용의 둥지 2·3층', "Dragon's Nest 2-3"),
    150: ('뒤틀린 시간의 시계탑 3층', 'Twisted Clock Tower 3F'),
    175: ('뒤틀린 시간의 시계탑 3층', 'Twisted Clock Tower 3F'),
}
NAVI = '<NAVI>[{name}]<INFO>{aegis},0,0,3,-222,1</INFO></NAVI>'
# instance kinds (phase 4): config Name -> (KR, EN). Must match msg 1565-1567.
INST_NAMES = {
    'OGH_HARD': ('옛 글래스트헤임(상급)', 'Old Glast Heim (Hard)'),
    'OGH': ('옛 글래스트헤임', 'Old Glast Heim'),
    'SKY_FORTRESS': ('공중요새 침공', 'Sky Fortress Invasion'),
}

cfg = yaml.safe_load(open(os.path.join(root, 'db/need/daily_quest.yml'), encoding='utf-8'))
entries = []
kr, en = {}, {}
for t in cfg['Body']:
    track = t['Track']
    kr_range, en_range, kr_mat, en_mat = TRACKS[track]
    stages = t['Delivery']['Stages']
    goal = stages[-1]['Amount']
    kr_goal = ' / '.join('%d개 %d점' % (s['Amount'], s['Points']) for s in stages)
    en_goal = ' / '.join('%d = %d pts' % (s['Amount'], s['Points']) for s in stages)
    for m in t['Delivery']['Maps']:
        qid, name, rate = m['QuestId'], m['Map'], m['Rate']
        kr_map, en_map = MAPS[name]
        seen, kr_mobs, en_mobs = set(), [], []
        for aegis in m['Monsters']:
            k, e = MOBS[aegis]
            if k in seen:
                continue
            seen.add(k)
            kr_mobs.append(NAVI.format(name=k, aegis=aegis))
            en_mobs.append(NAVI.format(name=e, aegis=aegis))
        pct = rate / 100
        pct_s = ('%d' % pct) if pct == int(pct) else ('%.1f' % pct)
        entries.append({'Id': qid, 'Title': '[NEED Daily] %s %s' % (en_range, en_map), 'Goal': goal, 'Label': en_mat})
        kr[qid] = {
            'Title': '[일일 납품] %s' % kr_map,
            'IconName': 'ico_dq.bmp',
            'Summary': '%s %d개 모아 납품' % (kr_mat, goal),
            'Description': [
                '%s 일일 보급 납품입니다.' % kr_range,
                '%s에서 아래 몬스터를 처치하면' % kr_map,
                '%s%% 확률로 %s을(를) 얻습니다.' % (pct_s, kr_mat),
                '대상: ' + ', '.join(kr_mobs),
                '파티원도 같은 맵·근처·최근 60초 활동이면 각자 판정합니다.',
                '프론테라 일일 보급관에게 납품: %s' % kr_goal,
                '^777777이 창은 이 캐릭터의 진행입니다. 계정 점수와 보상은^000000',
                '^777777[!] 버튼이나 보급관에서 확인합니다. 04:00 초기화.^000000',
            ],
            'RewardEXP': 0, 'RewardJEXP': 0, 'RewardItemList': [],
        }
        en[qid] = {
            'Title': '[Daily Delivery] %s' % en_map,
            'IconName': 'ico_dq.bmp',
            'Summary': 'Collect and deliver %d %s' % (goal, en_mat),
            'Description': [
                'Daily supply delivery for %s.' % en_range,
                'Defeat the monsters below in %s' % en_map,
                'for a %s%% chance of 1 %s.' % (pct_s, en_mat),
                'Targets: ' + ', '.join(en_mobs),
                'Party members on the map, nearby and active in 60s roll on their own.',
                'Deliver to the Supply Officer in Prontera: %s' % en_goal,
                '^777777This window shows this character only. Account points^000000',
                '^777777and rewards: [!] button or the Supply Officer. Resets at 04:00.^000000',
            ],
            'RewardEXP': 0, 'RewardJEXP': 0, 'RewardItemList': [],
        }

    # hunt (phase 2): one quest per track, accepted together with the delivery
    h = t['Hunt']
    hstages = h['Stages']
    hgoal = hstages[-1]['Amount']
    kr_area, en_area = HUNT_AREAS[track]
    seen, kr_mobs, en_mobs = set(), [], []
    for hm in h['Maps']:
        for aegis in hm['Monsters']:
            k, e = MOBS[aegis]
            if k in seen:
                continue
            seen.add(k)
            kr_mobs.append(NAVI.format(name=k, aegis=aegis))
            en_mobs.append(NAVI.format(name=e, aegis=aegis))
    half = (len(hstages) + 1) // 2
    kr_st = ['%d마리 %d점' % (s['Amount'], s['Points']) for s in hstages]
    en_st = ['%d = %d pts' % (s['Amount'], s['Points']) for s in hstages]
    qid = h['QuestId']
    entries.append({'Id': qid, 'Title': '[NEED Daily] %s Hunt' % en_range, 'Goal': hgoal, 'Label': 'Monsters Defeated'})
    kr[qid] = {
        'Title': '[일일 사냥] %s' % kr_area,
        'IconName': 'ico_dq.bmp',
        'Summary': '%s 몬스터 %d마리 처치' % (kr_area, hgoal),
        'Description': [
            '%s 일일 사냥입니다.' % kr_range,
            '%s에서 아래 몬스터를 처치하면 1마리씩 셉니다.' % kr_area,
            '대상: ' + ', '.join(kr_mobs),
            '파티원도 같은 맵·근처·최근 60초 활동이면 각자 셉니다.',
            '단계: ' + ' / '.join(kr_st[:half]),
            '      ' + ' / '.join(kr_st[half:]),
            '^777777이 창은 이 캐릭터의 처치 수입니다. 계정 점수는 보급관/[!].^000000',
            '^777777보급관에서 수락해야 셉니다. 단계마다 점수가 바로 들어갑니다. 04:00 초기화.^000000',
        ],
        'RewardEXP': 0, 'RewardJEXP': 0, 'RewardItemList': [],
    }
    en[qid] = {
        'Title': '[Daily Hunt] %s' % en_area,
        'IconName': 'ico_dq.bmp',
        'Summary': 'Defeat %d monsters in %s' % (hgoal, en_area),
        'Description': [
            'Daily hunt for %s.' % en_range,
            'Every monster below defeated in %s counts 1.' % en_area,
            'Targets: ' + ', '.join(en_mobs),
            'Party members on the map, nearby and active in 60s count on their own.',
            'Stages: ' + ' / '.join(en_st[:half]),
            '        ' + ' / '.join(en_st[half:]),
            '^777777This window shows this character\'s kills. Account points: Supply Officer/[!].^000000',
            '^777777Accept at the Supply Officer first. Each stage scores at once. Resets at 04:00.^000000',
        ],
        'RewardEXP': 0, 'RewardJEXP': 0, 'RewardItemList': [],
    }

    # battleground (phase 3): optional, Lv150 and up only
    if 'Battleground' in t:
        bg = t['Battleground']
        bst = bg['Stages']
        bgoal = bst[-1]['Amount']
        qid = bg['QuestId']
        entries.append({'Id': qid, 'Title': '[NEED Daily] %s Battleground' % en_range, 'Goal': bgoal, 'Label': "This char's valid games"})
        kr[qid] = {
            'Title': '[일일 전장] %s (선택)' % kr_range,
            'IconName': 'ico_dq.bmp',
            'Summary': '전장 유효 경기 %d판' % bgoal,
            'Description': [
                '%s 일일 전장입니다. 선택 활동입니다.' % kr_range,
                '티에라·플라비우스·KVM 경기를 끝까지 마치면 1판입니다.',
                '승리·패배·무승부 모두 인정합니다.',
                '조건: 전투 시작부터 참가, 보상 때 전장에 있음, 경기 시간',
                '      (티에라·플라비우스 6분, KVM 3분 이상), 행동 1회 이상,',
                '      참가자 서로 다른 IP 75% 이상',
                '점수: 계정의 서로 다른 경기 합산 ' + ' / '.join('%d판 %d점' % (s['Amount'], s['Points']) for s in bst),
                '^777777이 창은 이 캐릭터의 유효 경기 수입니다. 계정 판수·점수는 보급관/[!].^000000',
                '^777777경기가 끝나는 순간 판정합니다(보상 NPC와 무관). 04:00 뒤에 끝난 경기는 새날 수락 필요.^000000',
            ],
            'RewardEXP': 0, 'RewardJEXP': 0, 'RewardItemList': [],
        }
        en[qid] = {
            'Title': '[Daily Battleground] %s (optional)' % en_range,
            'IconName': 'ico_dq.bmp',
            'Summary': '%d valid battleground games' % bgoal,
            'Description': [
                'Optional daily battleground for %s.' % en_range,
                'A Tierra, Flavius or KVM game played to the end counts 1.',
                'Win, loss and draw all count.',
                'Rules: in the battle from its start, on the map at the reward,',
                '       6 min (Tierra/Flavius) or 3 min (KVM), at least one action,',
                '       75% of the players on distinct IPs',
                'Points (distinct games of the account): ' + ' / '.join('%d game(s) = %d pts' % (s['Amount'], s['Points']) for s in bst),
                '^777777This window shows this character\'s valid games. Account games/points: Supply Officer/[!].^000000',
                '^777777Judged when the battle ends (not at the reward NPC). A battle ending after 04:00 needs a new accept.^000000',
            ],
            'RewardEXP': 0, 'RewardJEXP': 0, 'RewardItemList': [],
        }

    # instances (phase 4): optional, Lv175 only. Goal = Cap, count = points of the kinds this character cleared.
    if 'Instance' in t:
        ins = t['Instance']
        cap = ins['Cap']
        qid = ins['QuestId']
        kr_k = ' / '.join('%s %d점' % (INST_NAMES[k['Name']][0], k['Points']) for k in ins['Kinds'])
        en_k = ' / '.join('%s %d' % (INST_NAMES[k['Name']][1], k['Points']) for k in ins['Kinds'])
        entries.append({'Id': qid, 'Title': '[NEED Daily] %s Instance' % en_range, 'Goal': cap, 'Label': "This char's clear pts"})
        kr[qid] = {
            'Title': '[일일 인던] %s (선택)' % kr_range,
            'IconName': 'ico_dq.bmp',
            'Summary': '인던 클리어 점수 최대 %d점' % cap,
            'Description': [
                '%s 일일 인던입니다. 선택 활동입니다.' % kr_range,
                '최종 보스가 쓰러질 때 보스 맵에 있으면 클리어 1회입니다.',
                '인던마다 계정당 하루 한 번 점수가 됩니다.',
                '점수: ' + kr_k,
                '      합계 최대 %d점' % cap,
                '^777777이 창은 이 캐릭터의 클리어 점수입니다. 계정 점수는 보급관/[!].^000000',
                '^777777보급관에서 수락해야 셉니다. 04:00 뒤에 깬 인던은 새날 수락 필요.^000000',
            ],
            'RewardEXP': 0, 'RewardJEXP': 0, 'RewardItemList': [],
        }
        en[qid] = {
            'Title': '[Daily Instance] %s (optional)' % en_range,
            'IconName': 'ico_dq.bmp',
            'Summary': 'Instance clears, up to %d pts' % cap,
            'Description': [
                'Optional daily instances for %s.' % en_range,
                'Be on the boss map when the final boss falls: 1 clear.',
                'Each instance scores once per account a day.',
                'Points: ' + en_k,
                '        up to %d in total' % cap,
                '^777777This window shows this character\'s clear points. Account points: Supply Officer/[!].^000000',
                '^777777Accept at the Supply Officer first. A clear after 04:00 needs a new accept.^000000',
            ],
            'RewardEXP': 0, 'RewardJEXP': 0, 'RewardItemList': [],
        }

ids = [e['Id'] for e in entries]
assert len(ids) == len(set(ids)) and all(99100 <= i <= 99499 for i in ids), ids

lines = [
    '# NEED daily quest - quest window entries (99000-99999). GENERATED by tools/need_daily_quest/gen_quests.py',
    '# from db/need/daily_quest.yml; do not edit by hand. Display only: no Mob, so kills never count by',
    '# themselves (quest.cpp skips this range) and need_daily_quest.cpp writes the count from the ledger.',
    '# TimeLimit 4h = expires at the next 04:00 (daily reset).',
    '',
    'Header:',
    '  Type: QUEST_DB',
    '  Version: 3',
    '',
    'Body:',
]
for e in sorted(entries, key=lambda e: e['Id']):
    lines += [
        '  - Id: %d' % e['Id'],
        '    Title: "%s"' % e['Title'],
        '    TimeLimit: 4h',
        '    Targets:',
        '      - Id: 1',
        '        Count: %d' % e['Goal'],
        '        MapName: "%s"' % e['Label'],
    ]
qpath = os.path.join(root, 'db/need/daily_quest_quests.yml')
open(qpath, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
os.makedirs(out, exist_ok=True)
json.dump({str(k): v for k, v in kr.items()}, open(os.path.join(out, 'quests_kr.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
json.dump({str(k): v for k, v in en.items()}, open(os.path.join(out, 'quests_en.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('quest_db entries:', len(entries), '->', qpath)
print('lub entries KR/EN:', len(kr), len(en), '->', out)
