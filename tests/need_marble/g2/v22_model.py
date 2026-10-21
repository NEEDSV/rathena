"""V22 expectation model and comparator. Pure Python: no DB, no server, no network.

Why: the first V22 runner expected absolute values (kills 400, 20 batches). The account it used still
held one kill and one batch from R1, so the run showed 401 / 21 and the runner said FAIL although the
ledger did what the design says. Here the expected rows are derived from a snapshot taken right
before the feed, the policy values, and the hunt code path (need_marble_hunt_on_kill -> hunt_flush ->
hunt_checkpoint):

  - a login looks the day up once; a day whose next_tier > max_tiers counts nothing (F2 layer 1)
  - every flush_kills kills of a login become one batch row; the checkpoint adds the batch to the
    progress row, credits each tier it passes, and stops counting once next_tier > max_tiers
  - a checkpoint on a capped day writes no batch row and no kill count (F2 layer 2)
  - kills left below flush_kills stay in memory and are flushed at logout
  - each tier asks for tier_points, clamped by the daily hunt cap and the daily total cap

Snapshot (one account, one season) as built by bc_runner.v22_snapshot():
  {'day': yyyymmdd,
   'progress':   {day: (kills, next_tier)},
   'batches':    [(collect_session, batch_no, day, kills)],
   'milestones': [(day, tier, requested, credited, outcome)],    # need_marble_activity, activity 1
   'earn':       [(day, tier, d_score)],                         # wallet_tx kind EARN, ref_a 1 (hunt)
   'tx_count':   number of wallet_tx rows of the account,
   'period':     {(day, activity): earned},                      # activity 0 = all, 1 = hunt
   'score': int, 'dice': int}
"""

DEFAULT_POLICY = {'tier_kills': 100, 'tier_points': 25, 'max_tiers': 4, 'flush_kills': 20,
                  'hunt_cap': 100, 'total_cap': 200}
# need_marble_logic.hpp policy keys of the values above
POLICY_KEYS = {'tier_kills': 20, 'tier_points': 21, 'max_tiers': 22, 'flush_kills': 26, 'hunt_cap': 12, 'total_cap': 11}


def credit_amount(requested, earned_activity, cap_activity, earned_total, cap_total):
    """Mirror of need_marble::credit_amount."""
    c = min(requested, cap_activity - earned_activity, cap_total - earned_total)
    return c if c > 0 else 0


def hunt_next_tier(kills_after, next_tier, tier_kills, max_tiers):
    """Mirror of need_marble::hunt_next_tier."""
    while next_tier <= max_tiers and kills_after >= next_tier * tier_kills:
        next_tier += 1
    return next_tier


class Login:
    """One login of one account, fed kills one by one like need_marble_hunt_on_kill does."""

    def __init__(self, snap, pol):
        self.pol = pol
        self.day = snap['day']
        kills, nt = snap['progress'].get(self.day, (0, 1))
        self.kills, self.next_tier = kills, nt
        self.earned_hunt = snap['period'].get((self.day, 1), 0)
        self.earned_total = snap['period'].get((self.day, 0), 0)
        self.view0 = kills
        self.pending = 0
        self.done = None             # decided by the first kill of the login (checked_day lookup)
        self.batches = []            # kills of each new batch row
        self.milestones = []         # (tier, requested, credited, outcome)

    def maxed(self):
        return self.next_tier > self.pol['max_tiers']

    def checkpoint(self):
        if self.maxed():             # F2 layer 2: no batch row, no kill count
            self.pending = 0
            self.done = True
            return
        p = self.pol
        self.kills += self.pending
        self.batches.append(self.pending)
        self.pending = 0
        new_next = hunt_next_tier(self.kills, self.next_tier, p['tier_kills'], p['max_tiers'])
        for tier in range(self.next_tier, new_next):
            c = credit_amount(p['tier_points'], self.earned_hunt, p['hunt_cap'], self.earned_total, p['total_cap'])
            self.earned_hunt += c
            self.earned_total += c
            self.milestones.append((tier, p['tier_points'], c, 1 if c > 0 else 2))
        self.next_tier = new_next
        if self.maxed():
            self.done = True

    def feed(self, n):
        before = self.view()
        for _ in range(n):
            if self.done is None:
                self.done = self.maxed()          # F2 layer 1
            if self.done:
                continue
            self.pending += 1
            if self.pending >= self.pol['flush_kills']:
                self.checkpoint()
        return self.view() - before            # what the harness reports as counted=

    def view(self):
        return self.kills + self.pending       # @MB_HUNT_KILLS: DB row + in-memory batch

    def logout(self):
        if self.pending > 0:
            self.checkpoint()


def expected(before, login):
    """The snapshot the model predicts from `before` plus everything `login` wrote so far."""
    day = before['day']
    exp = {'day': day, 'progress': dict(before['progress']), 'tx_count': before['tx_count'],
           'period': dict(before['period']), 'score': before['score'], 'dice': before['dice'],
           'milestones': list(before['milestones']), 'earn': list(before['earn'])}
    if login.batches or login.milestones:
        exp['progress'][day] = (login.kills, login.next_tier)
    for tier, req, cred, outcome in login.milestones:
        exp['milestones'].append((day, tier, req, cred, outcome))
        if cred > 0:
            exp['earn'].append((day, tier, cred))
            exp['tx_count'] += 1
            exp['score'] += cred
            for act in (0, 1):
                exp['period'][(day, act)] = exp['period'].get((day, act), 0) + cred
    exp['new_batches'] = list(login.batches)
    return exp


def _old_batches(before, after):
    keys = {b[:3] for b in before['batches']}
    return [b for b in after['batches'] if b[:3] not in keys]


def compare(before, after, exp, pol, label):
    """Returns [(check id, ok, detail)]. `exp` comes from expected()."""
    day = before['day']
    out = []

    def chk(cid, ok, detail):
        out.append((f'{label} {cid}', bool(ok), detail))

    new = _old_batches(before, after)
    kept = [b for b in after['batches'] if b[:3] in {x[:3] for x in before['batches']}]
    chk('a1 progress', after['progress'].get(day) == exp['progress'].get(day),
        f"progress {before['progress'].get(day)} -> {after['progress'].get(day)}, expected {exp['progress'].get(day)}")
    chk('a2 new batch rows', sorted(b[3] for b in new) == sorted(exp['new_batches']) and all(b[2] == day for b in new),
        f"{len(new)} new rows kills {sorted(b[3] for b in new)[:6]}..., expected {len(exp['new_batches'])} rows")
    chk('a3 old batch rows unchanged', sorted(kept) == sorted(before['batches']), f"{len(before['batches'])} rows before")
    day_kills = sum(b[3] for b in after['batches'] if b[2] == day)
    prog = after['progress'].get(day, (0, 1))
    chk('a4 I11 kills = sum of batches', prog[0] == day_kills, f'progress kills {prog[0]}, batch sum {day_kills}')
    tiers = sorted(m[1] for m in after['milestones'] if m[0] == day)
    chk('a5 tier rows', tiers == list(range(1, prog[1])) and len(tiers) <= pol['max_tiers'] and prog[1] <= pol['max_tiers'] + 1,
        f'tiers {tiers}, next_tier {prog[1]}, max_tiers {pol["max_tiers"]}')
    chk('a6 tier rows match model', sorted(after['milestones']) == sorted(exp['milestones']),
        f"{len(after['milestones'])} rows, expected {len(exp['milestones'])}")
    hunt = after['period'].get((day, 1), 0)
    total = after['period'].get((day, 0), 0)
    chk('b1 daily hunt cap', hunt <= pol['hunt_cap'] and total <= pol['total_cap'] and hunt == exp['period'].get((day, 1), 0),
        f"hunt earned {before['period'].get((day, 1), 0)} -> {hunt} (cap {pol['hunt_cap']}, expected {exp['period'].get((day, 1), 0)}), "
        f"total {total} (cap {pol['total_cap']})")
    chk('b2 period rows match model', after['period'] == exp['period'], f"period {after['period']}")
    credited = sum(m[3] for m in after['milestones'] if m[0] == day)
    chk('b3 credited = hunt earned', credited == hunt, f'tier credited sum {credited}, period hunt {hunt}')
    chk('c1 EARN rows match model', sorted(after['earn']) == sorted(exp['earn']) and after['tx_count'] == exp['tx_count'],
        f"EARN {len(before['earn'])} -> {len(after['earn'])}, tx rows {before['tx_count']} -> {after['tx_count']} (expected {exp['tx_count']})")
    chk('d1 wallet', after['score'] == exp['score'] and after['dice'] == before['dice'],
        f"score {before['score']} -> {after['score']} (expected {exp['score']}), dice {before['dice']} -> {after['dice']}")
    return out


def unchanged(a, b, label):
    """After the cap nothing may move: progress, batches, tiers, EARN, tx rows, period, wallet."""
    keys = ('progress', 'batches', 'milestones', 'earn', 'tx_count', 'period', 'score', 'dice')
    diff = [k for k in keys if (sorted(a[k]) if isinstance(a[k], list) else a[k]) != (sorted(b[k]) if isinstance(b[k], list) else b[k])]
    return [(f'{label} c2 no rows after the cap', not diff, f'changed: {diff}' if diff else 'progress, batches, tiers, EARN, tx, period, wallet identical')]


def fixture_state(snap, pol):
    """Which fixture the account is in today: 'clean', 'prior1', 'capped' or None (contaminated)."""
    day = snap['day']
    prog = snap['progress'].get(day)
    day_batches = [b for b in snap['batches'] if b[2] == day]
    day_tiers = [m for m in snap['milestones'] if m[0] == day]
    if prog is None and not day_batches and not day_tiers and not snap['period'].get((day, 1)):
        return 'clean'
    if prog == (1, 1) and len(day_batches) == 1 and day_batches[0][3] == 1 and not day_tiers:
        return 'prior1'
    if prog is not None and prog[1] > pol['max_tiers']:
        return 'capped'
    return None


def parse_feed(text):
    """'[MBQA] feed n=30000 counted=400' -> (30000, 400); missing parts -> None."""
    if not text or 'feed n=' not in text:
        return None, None
    n = counted = None
    try:
        n = int(text.split('n=')[1].split()[0])
    except (IndexError, ValueError):
        pass
    if 'counted=' in text:
        try:
            counted = int(text.split('counted=')[1].split()[0])
        except (IndexError, ValueError):
            pass
    return n, counted
