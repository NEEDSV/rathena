"""V11 analysis of a long roll run. Pure Python: no DB, no server.

The rate / CP tables, warp destinations and dungeon values are read from src/map/need_marble_logic.hpp
(policy_defaults), then overridden by the isolated DB policy rows the runner passes in. Nothing is
hard-coded here, so the check follows the policy the server actually ran with.

Input rows are bc_runner.one_roll() results:
  {'from', 'die', 'roll', 'to', 'state', 'steps', 'dice_used', 'label', 'choice'?, 'event': [[tile, kind,
   result, cp, choice, rng]], 'pay': [[amount, status]]}
"""
import os
import re

BOARD = [10, 1, 2, 6, 1, 3, 1, 11, 7, 1, 4, 1, 2, 1, 8, 1, 5, 12, 3, 1, 2, 1, 11, 4, 1, 9, 1, 2, 1, 3, 1, 4, 1]
KINDS = {'TILE_CITY': 1, 'TILE_EVENT': 2, 'TILE_TREASURE': 3, 'TILE_MONSTER': 4, 'TILE_BOSS': 5, 'TILE_PORING': 6,
         'TILE_BONUS': 7, 'TILE_TAX': 8, 'TILE_JAIL': 9, 'TILE_START': 10}
TABLE_KINDS = set(KINDS.values())
WARP, DUNGEON = 11, 12
CHI2_P001 = {5: 20.515, 9: 27.877}   # chi-square critical values, p = 0.001


def policy_from_source(logic_hpp, overrides=None):
    """{'rates': {kind: [4]}, 'cps': {kind: [4]}, 'warp': {7: d, 22: d}, 'safe', 'chal_rate', 'chal_cp', 'lap_steps', 'lap_cp'}"""
    src = open(logic_hpp, encoding='utf-8').read()
    rates, cps = {}, {}
    for name, r, c in re.findall(r'set\((TILE_\w+),\s*\{([^}]*)\},\s*\{([^}]*)\}\);', src):
        rates[KINDS[name]] = [int(x) for x in r.split(',')]
        cps[KINDS[name]] = [int(x) for x in c.split(',')]
    val = lambda key: int(re.search(r'v\[' + key + r'\]\s*=\s*(\d+);', src).group(1))
    pol = {'rates': rates, 'cps': cps, 'warp': {7: val('P_WARP_DEST_7'), 22: val('P_WARP_DEST_22')},
           'safe': val('P_DUNGEON_SAFE_CP'), 'chal_rate': val('P_DUNGEON_CHALLENGE_RATE'),
           'chal_cp': val('P_DUNGEON_CHALLENGE_CP'), 'lap_steps': val('P_LAP_STEPS'), 'lap_cp': val('P_LAP_CP')}
    for key, v in (overrides or {}).items():   # need_marble_logic.hpp key numbers
        if 100 <= key < 200:
            pol['rates'].setdefault((key - 100) // 10, [0] * 4)[(key - 100) % 10] = v
        elif 200 <= key < 300:
            pol['cps'].setdefault((key - 200) // 10, [0] * 4)[(key - 200) % 10] = v
        elif key in (50, 51):
            pol['warp'][7 if key == 50 else 22] = v
        elif key == 56:
            pol['safe'] = v
        elif key == 57:
            pol['chal_rate'] = v
        elif key == 58:
            pol['chal_cp'] = v
    return pol


def default_logic_path():
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'src', 'map', 'need_marble_logic.hpp')


def pick(pol, kind, rng):
    """Mirror of pick_outcome: (slot 0-based, cp) or (-1, 0)."""
    if kind not in pol['rates'] or not 1 <= rng <= 10000:
        return -1, 0
    acc = 0
    for s in range(4):
        acc += pol['rates'][kind][s]
        if rng <= acc:
            return s, pol['cps'][kind][s]
    return -1, 0


def chi2(counts, expected):
    return sum((o - e) ** 2 / e for o, e in zip(counts, expected) if e > 0)


def analyze(rows, pol, spass_delta=None, steps_total=None, lap_rows=None, payouts=None, cash_rows=None):
    """Returns [(check id, ok, detail)]."""
    out = []
    bad = {}

    def flag(k, roll):
        bad.setdefault(k, []).append(roll)

    die = [0] * 6
    rng_bins = [0] * 10
    rng_n = 0
    crossings = 0
    slots = {}
    for r in rows:
        roll = r['roll']
        if not 1 <= r['die'] <= 6:
            flag('die range', roll)
            continue
        die[r['die'] - 1] += 1
        if r['label'] == '주사위 굴리기' and r['dice_used'] != 1:
            flag('one die per roll', roll)
        if r['steps'] != r['die']:
            flag('steps = die', roll)
        if r['from'] + r['die'] >= 33:
            crossings += 1
        if not r['event'] or len(r['event']) != 1:
            flag('one event per roll', roll)
            continue
        tile, kind, res, cp, choice, rng = [int(x) for x in r['event'][0]]
        landing = (r['from'] + r['die']) % 33
        if tile != landing or kind != BOARD[tile]:
            flag('event tile = landing', roll)
        pay = [(int(p[0]), p[1]) for p in r['pay']]
        if kind == WARP:
            if r['to'] != pol['warp'][tile] or cp != 0 or pay:
                flag('warp W1', roll)
        else:
            if r['to'] != landing:
                flag('position = landing', roll)
            if kind in TABLE_KINDS:
                s, want_cp = pick(pol, kind, rng)
                if s < 0 or res != s + 1 or cp != want_cp:
                    flag('outcome = rate table', roll)
                slots.setdefault(kind, [0] * 4)[max(s, 0)] += 1
                rng_bins[min((rng - 1) // 1000, 9)] += 1
                rng_n += 1
            elif kind == DUNGEON:
                if choice == 1:
                    if res != 1 or cp != pol['safe']:
                        flag('dungeon safe', roll)
                elif choice == 2:
                    win = 1 <= rng <= pol['chal_rate']
                    if res != (2 if win else 3) or cp != (pol['chal_cp'] if win else 0):
                        flag('dungeon challenge', roll)
                    rng_bins[min(max(rng - 1, 0) // 1000, 9)] += 1
                    rng_n += 1
                else:
                    flag('dungeon resolved', roll)
            if (cp > 0) != bool(pay) or (pay and (pay[0][0] != cp or pay[0][1] != '1')):
                flag('CP row = event CP, DECIDED', roll)
    n = sum(die)
    out.append(('V11-1 every roll: one die, steps = die, one event on the landing tile', not any(
        k in bad for k in ('die range', 'one die per roll', 'steps = die', 'one event per roll', 'event tile = landing', 'position = landing')),
        f'{len(rows)} rolls; ' + '; '.join(f'{k}: {v[:5]}' for k, v in bad.items() if k not in ('warp W1', 'outcome = rate table'))))
    c = chi2(die, [n / 6] * 6) if n else None
    out.append(('V11-2 die faces uniform (chi2 df 5, p 0.001)', n > 0 and min(die) > 0 and c < CHI2_P001[5],
                f'faces {die}, chi2 {c:.2f} < {CHI2_P001[5]}' if n else 'no rolls'))
    c2 = chi2(rng_bins, [rng_n / 10] * 10) if rng_n else None
    out.append(('V11-3 drawn values uniform over 1..10000 (chi2 df 9, p 0.001)', rng_n > 0 and c2 < CHI2_P001[9],
                f'{rng_n} draws, deciles {rng_bins}, chi2 {c2:.2f} < {CHI2_P001[9]}' if rng_n else 'no draws'))
    out.append(('V11-4 outcome slot and CP = rate table for the drawn value', 'outcome = rate table' not in bad,
                f'slots by kind {slots}; mismatches {bad.get("outcome = rate table", [])[:5]}'))
    warps = [r for r in rows if r['event'] and int(r['event'][0][1]) == WARP]
    out.append(('V11-5 warp W1: destination, no destination event, no CP', 'warp W1' not in bad,
                f'{len(warps)} warp landings to {sorted({(int(r["event"][0][0]), r["to"]) for r in warps})}; bad {bad.get("warp W1", [])[:5]}'))
    dun = [r for r in rows if r['event'] and int(r['event'][0][1]) == DUNGEON]
    out.append(('V11-6 dungeon tile: safe / challenge results', not any(k in bad for k in ('dungeon safe', 'dungeon challenge', 'dungeon resolved')),
                f'{len(dun)} dungeon landings; bad {[(k, v[:5]) for k, v in bad.items() if k.startswith("dungeon")]}'))
    out.append(('V11-7 CP rows DECIDED and equal to the event CP', 'CP row = event CP, DECIDED' not in bad,
                f'bad {bad.get("CP row = event CP, DECIDED", [])[:5]}'))
    if spass_delta is not None:
        out.append(('V11-8 START passes = rolls crossing 32 -> 0', spass_delta == crossings, f'start_pass +{spass_delta}, crossings {crossings}'))
    if steps_total is not None and lap_rows is not None:
        ok = len(lap_rows) == steps_total // pol['lap_steps'] and all(int(x[1]) == pol['lap_cp'] and x[2] == '1' for x in lap_rows)
        out.append(('V11-9 33-step bonus rows = steps // 33', ok, f'steps {steps_total}, lap rows {len(lap_rows)}'))
    if payouts is not None and cash_rows is not None:
        ok = all(p[0] == '1' for p in payouts) and cash_rows == 0
        out.append(('V11-10 CP only DECIDED, #CASHPOINTS untouched', ok, f'payouts by status {payouts}, #CASHPOINTS rows {cash_rows}'))
    return out
