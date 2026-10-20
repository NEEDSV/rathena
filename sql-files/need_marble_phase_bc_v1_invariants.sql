-- NEED marble board Phase B/C - ledger invariant checks (READ ONLY, for the G2 isolated test)
--
-- Every query returns the rows that VIOLATE an invariant. Empty result = PASS.
-- SELECT only: no table is written. Run against the isolated test database only.
-- Invariant ids follow NEED_MARBLE_LEDGER_VALIDATION_PLAN_v0_3 (I1..I16).
-- Caps below are the v1.0 initial policy; change them to the policy in force for the test day.

SET @cap_total = 200, @cap_hunt = 100, @cap_inst = 100, @cap_bg = 100, @hold = 30, @lap_steps = 33;

-- I1 wallet = sum of its ledger rows
SELECT 'I1' AS inv, w.account_id, w.season_id, w.score, w.dice, COALESCE(t.s, 0) AS tx_score, COALESCE(t.d, 0) AS tx_dice
FROM need_marble_wallet w
LEFT JOIN (SELECT account_id, season_id, SUM(d_score) s, SUM(d_dice) d FROM need_marble_wallet_tx GROUP BY account_id, season_id) t
  ON t.account_id = w.account_id AND t.season_id = w.season_id
WHERE w.score <> COALESCE(t.s, 0) OR w.dice <> COALESCE(t.d, 0);

-- I2 period.earned = EARN rows of that day (total row 0 and per activity)
SELECT 'I2' AS inv, p.account_id, p.period_day, p.activity, p.earned, COALESCE(e.s, 0) AS earn_rows
FROM need_marble_period p
LEFT JOIN (
  SELECT account_id, season_id, period_day, 0 AS activity, SUM(d_score) s FROM need_marble_wallet_tx WHERE kind = 1 GROUP BY account_id, season_id, period_day
  UNION ALL
  SELECT account_id, season_id, period_day, ref_a, SUM(d_score) FROM need_marble_wallet_tx WHERE kind = 1 GROUP BY account_id, season_id, period_day, ref_a
) e ON e.account_id = p.account_id AND e.season_id = p.season_id AND e.period_day = p.period_day AND e.activity = p.activity
WHERE p.earned <> COALESCE(e.s, 0);

-- I3 caps
SELECT 'I3' AS inv, account_id, period_day, activity, earned FROM need_marble_period
WHERE (activity = 0 AND earned > @cap_total) OR (activity = 1 AND earned > @cap_hunt)
   OR (activity = 2 AND earned > @cap_inst) OR (activity = 3 AND earned > @cap_bg);

-- I4 period.exchanged = EXCHANGE dice of that day
SELECT 'I4' AS inv, p.account_id, p.period_day, p.exchanged, COALESCE(x.d, 0) AS exchange_rows
FROM need_marble_period p
LEFT JOIN (SELECT account_id, season_id, period_day, SUM(d_dice) d FROM need_marble_wallet_tx WHERE kind = 2 GROUP BY account_id, season_id, period_day) x
  ON x.account_id = p.account_id AND x.season_id = p.season_id AND x.period_day = p.period_day
WHERE p.activity = 0 AND p.exchanged <> COALESCE(x.d, 0);

-- I5 / I6 one SPEND row per roll and back
SELECT 'I5' AS inv, r.account_id, r.roll_seq FROM need_marble_roll r
LEFT JOIN need_marble_wallet_tx t ON t.account_id = r.account_id AND t.kind = 3 AND t.ref_type = 3 AND t.ref_a = r.roll_seq
WHERE t.account_id IS NULL OR t.d_dice <> -1;
SELECT 'I6' AS inv, t.account_id, t.ref_a FROM need_marble_wallet_tx t
LEFT JOIN need_marble_roll r ON r.account_id = t.account_id AND r.roll_seq = t.ref_a
WHERE t.kind = 3 AND r.account_id IS NULL;

-- I7a roll_seq, I7b step_seq (warps never add steps), I7c position, I7d jump_seq
SELECT 'I7a' AS inv, s.account_id, s.roll_seq, COALESCE(MAX(r.roll_seq), 0) AS max_roll
FROM need_marble_state s LEFT JOIN need_marble_roll r ON r.account_id = s.account_id
GROUP BY s.account_id, s.roll_seq HAVING s.roll_seq <> COALESCE(MAX(r.roll_seq), 0);

SELECT 'I7b' AS inv, s.account_id, s.step_seq, COALESCE(SUM(r.die), 0) - s.left_steps AS expected
FROM need_marble_state s LEFT JOIN need_marble_roll r ON r.account_id = s.account_id
GROUP BY s.account_id, s.step_seq, s.left_steps HAVING s.step_seq <> COALESCE(SUM(r.die), 0) - s.left_steps;

SELECT 'I7c' AS inv, s.account_id, s.pos, MOD(s.step_seq + COALESCE(j.shift, 0) + 33 * 1000000, 33) AS expected
FROM need_marble_state s
LEFT JOIN (SELECT account_id, SUM(CAST(to_pos AS SIGNED) - CAST(from_pos AS SIGNED)) shift FROM need_marble_jump GROUP BY account_id) j
  ON j.account_id = s.account_id
WHERE s.pos <> MOD(s.step_seq + COALESCE(j.shift, 0) + 33 * 1000000, 33);

SELECT 'I7d' AS inv, s.account_id, s.jump_seq, COALESCE(MAX(j.jump_seq), 0) AS max_jump
FROM need_marble_state s LEFT JOIN need_marble_jump j ON j.account_id = s.account_id
GROUP BY s.account_id, s.jump_seq HAVING s.jump_seq <> COALESCE(MAX(j.jump_seq), 0);

-- I8 at most one event per roll (W1), only for existing rolls
SELECT 'I8' AS inv, e.account_id, e.roll_seq, COUNT(*) n FROM need_marble_event e GROUP BY e.account_id, e.roll_seq HAVING COUNT(*) > 1;
SELECT 'I8b' AS inv, e.account_id, e.roll_seq FROM need_marble_event e
LEFT JOIN need_marble_roll r ON r.account_id = e.account_id AND r.roll_seq = e.roll_seq WHERE r.account_id IS NULL;

-- I9 credited = EARN
SELECT 'I9' AS inv, a.account_id, a.season_id, a.c, COALESCE(t.s, 0) AS earn
FROM (SELECT account_id, season_id, SUM(credited) c FROM need_marble_activity GROUP BY account_id, season_id) a
LEFT JOIN (SELECT account_id, season_id, SUM(d_score) s FROM need_marble_wallet_tx WHERE kind = 1 GROUP BY account_id, season_id) t
  ON t.account_id = a.account_id AND t.season_id = a.season_id
WHERE a.c <> COALESCE(t.s, 0);
SELECT 'I9b' AS inv, account_id, activity, key_a, key_b, key_c FROM need_marble_activity WHERE credited > requested OR outcome = 0;

-- I10 tiers, I11 kills = batch kills
SELECT 'I10' AS inv, h.account_id, h.period_day, h.next_tier, COUNT(a.account_id) AS milestones
FROM need_marble_hunt_progress h
LEFT JOIN need_marble_activity a ON a.account_id = h.account_id AND a.season_id = h.season_id AND a.activity = 1 AND a.key_a = h.period_day
GROUP BY h.account_id, h.period_day, h.next_tier HAVING h.next_tier - 1 <> COUNT(a.account_id);
SELECT 'I11' AS inv, h.account_id, h.period_day, h.kills, COALESCE(b.k, 0) AS batch_kills
FROM need_marble_hunt_progress h
LEFT JOIN (SELECT account_id, season_id, period_day, SUM(kills) k FROM need_marble_hunt_batch GROUP BY account_id, season_id, period_day) b
  ON b.account_id = h.account_id AND b.season_id = h.season_id AND b.period_day = h.period_day
WHERE h.kills <> LEAST(COALESCE(b.k, 0), 4294967295);

-- I12 one credited/consumed clear per account, day and dungeon (P_INSTANCE_CAPPED_CONSUMES = 1)
SELECT 'I12' AS inv, a.account_id, a.period_day, a.key_c AS dungeon, COUNT(*) n FROM need_marble_activity a
WHERE a.activity = 2 AND a.outcome IN (1, 2) GROUP BY a.account_id, a.period_day, a.key_c HAVING COUNT(*) > 1;

-- I13 committed requests = EXCHANGE rows, I14 pending_ack points to an unacked request
SELECT 'I13' AS inv, r.account_id, r.request_no FROM need_marble_exchange_req r
LEFT JOIN need_marble_wallet_tx t ON t.account_id = r.account_id AND t.kind = 2 AND t.ref_a = r.request_no
WHERE (r.status = 2) <> (t.account_id IS NOT NULL);
SELECT 'I14' AS inv, w.account_id, w.pending_ack FROM need_marble_wallet w
LEFT JOIN need_marble_exchange_req r ON r.account_id = w.account_id AND r.request_no = w.pending_ack
WHERE w.pending_ack <> 0 AND (r.account_id IS NULL OR r.acked = 1);

-- CP decisions: tile rows = events with CP, lap rows = laps, every row still DECIDED in this build
SELECT 'CP1' AS inv, e.account_id, e.roll_seq, e.cp_amount, p.amount FROM need_marble_event e
LEFT JOIN need_marble_cp_payout p ON p.account_id = e.account_id AND p.src_type = 1 AND p.src_a = e.roll_seq AND p.src_b = e.hop
WHERE (e.cp_amount > 0) <> (p.account_id IS NOT NULL) OR (p.account_id IS NOT NULL AND p.amount <> e.cp_amount);
SELECT 'CP2' AS inv, s.account_id, s.laps, COUNT(p.account_id) AS lap_rows, FLOOR(s.step_seq / @lap_steps) AS expected
FROM need_marble_state s LEFT JOIN need_marble_cp_payout p ON p.account_id = s.account_id AND p.src_type = 2
GROUP BY s.account_id, s.laps, s.step_seq HAVING s.laps <> COUNT(p.account_id) OR s.laps <> FLOOR(s.step_seq / @lap_steps);
SELECT 'CP3' AS inv, account_id, payout_no, status FROM need_marble_cp_payout WHERE status <> 1;

-- Hold limit
SELECT 'HOLD' AS inv, account_id, dice FROM need_marble_wallet WHERE dice > @hold;
