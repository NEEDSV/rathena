'use strict';
// All analytical, READ-ONLY queries for NEED Summer Monitor.
// Design rules (see fishing19 sec.20/21):
//  - reward_log is the fast, indexed entry point -> always narrow here first (stage 1).
//  - Stage 2 (picklog/zenylog) is scoped by explicit char_id lists + time bounds + nameid/unique_id.
//  - Every list query is time-bounded and LIMITed. No unbounded full-table JOINs.
const { q, T } = require('./db');

const GOLD = 399990;
const DEFAULT_LIMIT = 100;
const MAX_LIMIT = 500;
const MAX_CHARS = 200;         // safety cap on how many chars we expand for one account scope
const P_WINDOW_SEC = 120;      // drop->pickup correlation window for gold type P

const SOURCE_NAMES = {
  399927: '황금 수박',
  399929: '1억 제니 주머니',
  399930: '캐시 10만 포인트 교환권',
  399931: '코코넛 쉐도우 상자',
};
const REWARD_TYPE = { 1: 'ZENY', 2: 'CASH', 3: 'ITEM' };
// zeny movement types that represent account-to-account transfers (fishing19 sec.12)
const ZENY_MOVE_TYPES = ['T', 'V', 'E'];
// gold movement types and their confidence (fishing19 sec.14/15)
const GOLD_TYPE_CONF = {
  N: { label: '스크립트/환전', conf: 'origin' },
  T: { label: '직접 거래', conf: 'high' },
  P: { label: '드롭/습득', conf: 'candidate' },
  G: { label: '길드창고', conf: 'medium' },
  R: { label: '개인창고', conf: 'internal' },
  E: { label: '우편', conf: 'high' },
  V: { label: '판매노점', conf: 'candidate' },
  B: { label: '구매노점', conf: 'candidate' },
};

function clampLimit(n, def) {
  n = Number(n);
  if (!Number.isFinite(n) || n <= 0) return def || DEFAULT_LIMIT;
  return Math.min(Math.floor(n), MAX_LIMIT);
}
function sourceName(id) { return SOURCE_NAMES[id] || String(id); }

// ---- scope helpers -------------------------------------------------------
async function charsOfAccount(accountId) {
  const rows = await q(
    'SELECT `char_id`, `name` FROM ' + T.char + ' WHERE `account_id` = ? LIMIT ' + MAX_CHARS,
    [accountId]
  );
  return rows;
}
function charIdList(rows) { return rows.map(r => r.char_id); }

// ---- 1. Origin list (fishing19 sec.9) -----------------------------------
async function originList(opt) {
  const { from, to } = opt;
  const where = ['r.`created_at` BETWEEN ? AND ?'];
  const params = [from, to];
  if (opt.accountId) { where.push('r.`account_id` = ?'); params.push(opt.accountId); }
  if (opt.charId) { where.push('r.`char_id` = ?'); params.push(opt.charId); }
  if (opt.ip) { where.push('r.`ip` = ?'); params.push(opt.ip); }
  if (opt.charName) { where.push('c.`name` = ?'); params.push(opt.charName); }
  const limit = clampLimit(opt.limit);
  const offset = Math.max(0, Number(opt.offset) || 0);
  const sql =
    'SELECT r.`id`, r.`created_at`, r.`account_id`, r.`char_id`, c.`name` AS char_name, r.`ip`,' +
    ' r.`source_item_id`, r.`reward_type`, r.`reward_item_id`, r.`reward_amount`, r.`unique_id`' +
    ' FROM ' + T.reward + ' r LEFT JOIN ' + T.char + ' c ON c.`char_id` = r.`char_id`' +
    ' WHERE ' + where.join(' AND ') +
    ' ORDER BY r.`id` DESC LIMIT ' + limit + ' OFFSET ' + offset;
  const rows = await q(sql, params);
  return rows.map(decorateOrigin);
}
function decorateOrigin(r) {
  return {
    id: r.id, time: r.created_at,
    account_id: r.account_id, char_id: r.char_id, char_name: r.char_name, ip: r.ip,
    source_item_id: r.source_item_id, source_name: sourceName(r.source_item_id),
    reward_type: r.reward_type, reward_type_name: REWARD_TYPE[r.reward_type] || String(r.reward_type),
    reward_item_id: r.reward_item_id, reward_amount: r.reward_amount, unique_id: r.unique_id,
  };
}

// ---- 2. Account summary (fishing19 sec.10) ------------------------------
async function accountSummary(accountId, opt) {
  const chars = await charsOfAccount(accountId);
  const originIps = await q(
    'SELECT DISTINCT `ip` FROM ' + T.reward + ' WHERE `account_id` = ? AND `ip` <> \'\' LIMIT 50',
    [accountId]
  );
  const originCount = await q(
    'SELECT COUNT(*) AS c FROM ' + T.reward + ' WHERE `account_id` = ?',
    [accountId]
  );
  return {
    account_id: Number(accountId),
    chars,
    origin_ips: originIps.map(r => r.ip),
    origin_count: originCount[0] ? originCount[0].c : 0,
  };
}

// ---- 3. Account timeline (fishing19 sec.10) -----------------------------
// Merges origins + shadow moves + zeny moves + gold moves for one account, time-ordered.
async function accountTimeline(accountId, opt) {
  const { from, to } = opt;
  const chars = await charsOfAccount(accountId);
  const cids = charIdList(chars);
  const events = [];

  const origins = await originList({ from, to, accountId, limit: MAX_LIMIT });
  const firstOriginTime = origins.length ? origins[origins.length - 1].time : from;
  origins.forEach(o => {
    events.push({
      time: o.time, kind: 'origin', conf: 'origin',
      char_id: o.char_id, char_name: o.char_name,
      text: '[Origin] ' + o.source_name + ' → ' + o.reward_type_name +
        (o.reward_type === 3 ? ' item ' + o.reward_item_id + ' (uid ' + o.unique_id + ')' : ' ' + fmtNum(o.reward_amount)),
      ref: o,
    });
  });

  if (cids.length) {
    // shadow moves: unique_ids from this account's ITEM origins
    const uids = origins.filter(o => o.reward_type === 3 && String(o.unique_id) !== '0').map(o => o.unique_id);
    if (uids.length) {
      const ph = uids.map(() => '?').join(',');
      const rows = await q(
        'SELECT p.`time`, p.`type`, p.`char_id`, p.`nameid`, p.`amount`, p.`unique_id`, p.`map`, c.`account_id`' +
        ' FROM ' + T.pick + ' p LEFT JOIN ' + T.char + ' c ON c.`char_id` = p.`char_id`' +
        ' WHERE p.`unique_id` IN (' + ph + ') AND p.`time` >= ? ORDER BY p.`time`, p.`id` LIMIT ' + MAX_LIMIT,
        [...uids, firstOriginTime]
      );
      rows.forEach(p => events.push({
        time: p.time, kind: 'shadow', conf: 'exact',
        char_id: p.char_id, account_id: p.account_id,
        text: '[Shadow/' + p.type + '] item ' + p.nameid + ' uid ' + p.unique_id + ' amount ' + signed(p.amount) +
          (p.account_id && Number(p.account_id) !== Number(accountId) ? ' → account ' + p.account_id : ''),
        ref: p,
      }));
    }

    // zeny moves for account chars in range
    const ph = cids.map(() => '?').join(',');
    const zrows = await q(
      'SELECT z.`time`, z.`type`, z.`char_id`, z.`src_id`, z.`amount`, z.`map`, c2.`account_id` AS peer_account' +
      ' FROM ' + T.zeny + ' z LEFT JOIN ' + T.char + ' c2 ON c2.`char_id` = z.`src_id`' +
      ' WHERE z.`char_id` IN (' + ph + ') AND z.`time` BETWEEN ? AND ? ORDER BY z.`time`, z.`id` LIMIT ' + MAX_LIMIT,
      [...cids, from, to]
    );
    zrows.forEach(z => events.push({
      time: z.time, kind: 'zeny', conf: 'high',
      char_id: z.char_id, peer_account: z.peer_account,
      text: '[Zeny/' + z.type + '] ' + signed(z.amount) +
        (z.peer_account ? ' (상대 account ' + z.peer_account + ')' : (z.src_id ? ' (상대 char ' + z.src_id + ')' : '')),
      ref: z,
    }));

    // gold moves for account chars in range
    const grows = await q(
      'SELECT p.`time`, p.`type`, p.`char_id`, p.`amount`, p.`map`' +
      ' FROM ' + T.pick + ' p WHERE p.`nameid` = ' + GOLD + ' AND p.`char_id` IN (' + ph + ')' +
      ' AND p.`time` BETWEEN ? AND ? ORDER BY p.`time`, p.`id` LIMIT ' + MAX_LIMIT,
      [...cids, from, to]
    );
    grows.forEach(g => {
      const meta = GOLD_TYPE_CONF[g.type] || { label: g.type, conf: 'candidate' };
      events.push({
        time: g.time, kind: 'gold', conf: meta.conf,
        char_id: g.char_id,
        text: '[Gold/' + g.type + ' ' + meta.label + '] ' + signed(g.amount) + ' @' + g.map,
        ref: g,
      });
    });
  }

  events.sort((a, b) => (a.time < b.time ? -1 : a.time > b.time ? 1 : 0));
  return { account_id: Number(accountId), chars, events };
}

// ---- 4. Shadow trace by unique_id (fishing19 sec.11) --------------------
async function shadowTrace(uniqueId, opt) {
  // find origin row for context + created_at bound
  const org = await q(
    'SELECT `account_id`, `char_id`, `reward_item_id`, `source_item_id`, `created_at` FROM ' + T.reward +
    ' WHERE `unique_id` = ? ORDER BY `id` ASC LIMIT 1', [uniqueId]
  );
  const since = (opt && opt.since) || (org[0] ? org[0].created_at : '1970-01-01');
  const rows = await q(
    'SELECT p.`time`, p.`type`, p.`char_id`, c.`name` AS char_name, c.`account_id`, c.`guild_id`,' +
    ' p.`nameid`, p.`amount`, p.`unique_id`, p.`map`' +
    ' FROM ' + T.pick + ' p LEFT JOIN ' + T.char + ' c ON c.`char_id` = p.`char_id`' +
    ' WHERE p.`unique_id` = ? AND p.`time` >= ? ORDER BY p.`time`, p.`id` LIMIT ' + MAX_LIMIT,
    [uniqueId, since]
  );
  const originAccount = org[0] ? org[0].account_id : null;
  const steps = rows.map(p => ({
    time: p.time, type: p.type, char_id: p.char_id, char_name: p.char_name,
    account_id: p.account_id, guild_id: p.guild_id, amount: p.amount, map: p.map,
    internal: p.type === 'R',
    external_account: p.account_id && originAccount && Number(p.account_id) !== Number(originAccount),
  }));
  return {
    unique_id: String(uniqueId),
    origin: org[0] || null,
    origin_source_name: org[0] ? sourceName(org[0].source_item_id) : null,
    steps,
  };
}

// ---- 5. Zeny movement (fishing19 sec.12) --------------------------------
async function zenyMovement(accountId, opt) {
  const { from, to } = opt;
  const chars = await charsOfAccount(accountId);
  const cids = charIdList(chars);
  if (!cids.length) return { account_id: Number(accountId), rows: [] };
  const ph = cids.map(() => '?').join(',');
  const typeph = ZENY_MOVE_TYPES.map(() => '?').join(',');
  const rows = await q(
    'SELECT z.`time`, z.`type`, z.`char_id`, c1.`name` AS char_name, z.`src_id`,' +
    ' c2.`name` AS peer_name, c2.`account_id` AS peer_account, z.`amount`, z.`map`' +
    ' FROM ' + T.zeny + ' z' +
    ' LEFT JOIN ' + T.char + ' c1 ON c1.`char_id` = z.`char_id`' +
    ' LEFT JOIN ' + T.char + ' c2 ON c2.`char_id` = z.`src_id`' +
    ' WHERE z.`char_id` IN (' + ph + ') AND z.`type` IN (' + typeph + ')' +
    ' AND z.`time` BETWEEN ? AND ? ORDER BY z.`time`, z.`id` LIMIT ' + MAX_LIMIT,
    [...cids, ...ZENY_MOVE_TYPES, from, to]
  );
  return {
    account_id: Number(accountId),
    note: '이벤트 Zeny 획득 이후 발생한 계정 간 Zeny 이동 (특정 인스턴스 추적 아님)',
    rows: rows.map(z => ({
      time: z.time, type: z.type, direction: z.amount < 0 ? 'out' : 'in',
      char_id: z.char_id, char_name: z.char_name,
      peer_char: z.src_id, peer_name: z.peer_name, peer_account: z.peer_account,
      amount: z.amount, map: z.map, conf: 'high',
    })),
  };
}

// ---- 6. Gold movement (fishing19 sec.13/14) -----------------------------
async function goldMovement(accountId, opt) {
  const { from, to } = opt;
  const chars = await charsOfAccount(accountId);
  const cids = charIdList(chars);
  if (!cids.length) return { account_id: Number(accountId), rows: [] };
  const ph = cids.map(() => '?').join(',');
  const raw = await q(
    'SELECT p.`id`, p.`time`, p.`type`, p.`char_id`, p.`amount`, p.`map`' +
    ' FROM ' + T.pick + ' p WHERE p.`nameid` = ' + GOLD + ' AND p.`char_id` IN (' + ph + ')' +
    ' AND p.`time` BETWEEN ? AND ? ORDER BY p.`time`, p.`id` LIMIT ' + MAX_LIMIT,
    [...cids, from, to]
  );
  const out = [];
  for (const p of raw) {
    const meta = GOLD_TYPE_CONF[p.type] || { label: p.type, conf: 'candidate' };
    const item = {
      time: p.time, type: p.type, type_label: meta.label, conf: meta.conf,
      char_id: p.char_id, amount: p.amount, map: p.map, peer: null,
    };
    // resolve counterparty for direct trade (T): exact time+map+opposite amount, different char
    if (p.type === 'T' && p.amount < 0) {
      const peer = await q(
        'SELECT r.`char_id`, c.`account_id`, c.`name`' +
        ' FROM ' + T.pick + ' r LEFT JOIN ' + T.char + ' c ON c.`char_id` = r.`char_id`' +
        ' WHERE r.`nameid` = ' + GOLD + ' AND r.`type` = \'T\' AND r.`time` = ? AND r.`map` = ?' +
        ' AND r.`amount` = ? AND r.`char_id` <> ? LIMIT 5',
        [p.time, p.map, -p.amount, p.char_id]
      );
      if (peer.length) item.peer = { char_id: peer[0].char_id, account_id: peer[0].account_id, name: peer[0].name };
    }
    out.push(item);
  }
  return {
    account_id: Number(accountId),
    note: 'Cash가 정확히 이 금화로 변환됐다고 단정하지 않음. type별 신뢰도 상이.',
    rows: out,
  };
}

// ---- 7. Receiver concentration (fishing19 sec.16/17) --------------------
// For a given receiver account, find event-origin value flowing IN, grouped by origin account.
async function receiverConcentration(receiverAccountId, opt) {
  const { from, to } = opt;
  const rchars = await charsOfAccount(receiverAccountId);
  const rcids = charIdList(rchars);
  const result = { receiver_account: Number(receiverAccountId), inflows: [], origin_accounts: 0, origin_ips: 0 };
  if (!rcids.length) return result;
  const rph = rcids.map(() => '?').join(',');

  // origin-account pool in range (indexed on created_at)
  const origins = await q(
    'SELECT `account_id`, `char_id`, `ip`, `unique_id`, `reward_type`, `source_item_id`, `created_at`' +
    ' FROM ' + T.reward + ' WHERE `created_at` BETWEEN ? AND ? LIMIT ' + MAX_LIMIT,
    [from, to]
  );
  const originAccounts = new Set(origins.map(o => o.account_id));
  const originIpByAccount = {};
  origins.forEach(o => { if (!originIpByAccount[o.account_id]) originIpByAccount[o.account_id] = o.ip; });
  // map origin char -> account (for zeny src_id / gold sender resolution)
  const originChars = origins.map(o => o.char_id);
  const originShadowUids = origins.filter(o => o.reward_type === 3 && String(o.unique_id) !== '0').map(o => o.unique_id);

  const inflowByOrigin = {}; // account -> {shadow:0, zeny:0, gold:0, items:[]}
  const bump = (acc, kind, amount, detail) => {
    if (Number(acc) === Number(receiverAccountId)) return; // ignore self
    if (!originAccounts.has(Number(acc)) && !originAccounts.has(acc)) return; // only event accounts
    if (!inflowByOrigin[acc]) inflowByOrigin[acc] = { account_id: Number(acc), ip: originIpByAccount[acc] || '', shadow: 0, zeny: 0, gold: 0, details: [] };
    inflowByOrigin[acc][kind] += Number(amount) || 0;
    if (detail) inflowByOrigin[acc].details.push(detail);
  };

  // (a) Shadow into receiver: +rows for receiver chars on origin unique_ids
  if (originShadowUids.length) {
    const uph = originShadowUids.map(() => '?').join(',');
    const rows = await q(
      'SELECT p.`unique_id`, p.`amount`, p.`time`, p.`nameid` FROM ' + T.pick + ' p' +
      ' WHERE p.`unique_id` IN (' + uph + ') AND p.`char_id` IN (' + rph + ') AND p.`amount` > 0' +
      ' AND p.`time` BETWEEN ? AND ? LIMIT ' + MAX_LIMIT,
      [...originShadowUids, ...rcids, from, to]
    );
    const originByUid = {};
    origins.forEach(o => { originByUid[String(o.unique_id)] = o.account_id; });
    rows.forEach(p => {
      const oacc = originByUid[String(p.unique_id)];
      if (oacc) bump(oacc, 'shadow', 1, { kind: 'shadow', conf: 'exact', unique_id: String(p.unique_id), item: p.nameid, time: p.time });
    });
  }

  // (b) Zeny into receiver: +rows for receiver chars, src_id belongs to an origin char
  if (originChars.length) {
    const cph = originChars.map(() => '?').join(',');
    const rows = await q(
      'SELECT z.`amount`, z.`time`, z.`type`, z.`src_id`, c.`account_id` AS src_account' +
      ' FROM ' + T.zeny + ' z LEFT JOIN ' + T.char + ' c ON c.`char_id` = z.`src_id`' +
      ' WHERE z.`char_id` IN (' + rph + ') AND z.`amount` > 0 AND z.`src_id` IN (' + cph + ')' +
      ' AND z.`time` BETWEEN ? AND ? LIMIT ' + MAX_LIMIT,
      [...rcids, ...originChars, from, to]
    );
    rows.forEach(z => { if (z.src_account) bump(z.src_account, 'zeny', z.amount, { kind: 'zeny', conf: 'high', type: z.type, amount: z.amount, time: z.time }); });
  }

  // (c) Gold direct trade (T) into receiver: receiver +row pairs with origin sender -row
  {
    const rows = await q(
      'SELECT recv.`time`, recv.`map`, recv.`amount` AS recv_amount, recv.`char_id` AS recv_char,' +
      ' send.`char_id` AS send_char, cs.`account_id` AS send_account' +
      ' FROM ' + T.pick + ' recv' +
      ' JOIN ' + T.pick + ' send ON send.`nameid` = ' + GOLD + ' AND send.`type` = \'T\'' +
      '   AND send.`time` = recv.`time` AND send.`map` = recv.`map`' +
      '   AND send.`amount` = -recv.`amount` AND send.`char_id` <> recv.`char_id`' +
      ' LEFT JOIN ' + T.char + ' cs ON cs.`char_id` = send.`char_id`' +
      ' WHERE recv.`nameid` = ' + GOLD + ' AND recv.`type` = \'T\' AND recv.`amount` > 0' +
      '   AND recv.`char_id` IN (' + rph + ') AND recv.`time` BETWEEN ? AND ? LIMIT ' + MAX_LIMIT,
      [...rcids, from, to]
    );
    rows.forEach(g => { if (g.send_account) bump(g.send_account, 'gold', g.recv_amount, { kind: 'gold', conf: 'high', type: 'T', amount: g.recv_amount, time: g.time }); });
  }

  const inflows = Object.values(inflowByOrigin).sort((a, b) => (b.shadow + b.zeny + b.gold) - (a.shadow + a.zeny + a.gold));
  result.inflows = inflows;
  result.origin_accounts = inflows.length;
  result.origin_ips = new Set(inflows.map(i => i.ip).filter(Boolean)).size;
  return result;
}

// ---- 8. Auto-rank receivers (bounded; fishing19 sec.16/21) --------------
// Bounded discovery: for the origin-account pool in range, tally where their value lands.
async function receiverAutoRank(opt) {
  const { from, to } = opt;
  const limit = clampLimit(opt.limit, 20);
  const origins = await q(
    'SELECT DISTINCT `account_id`, `char_id` FROM ' + T.reward +
    ' WHERE `created_at` BETWEEN ? AND ? LIMIT ' + MAX_LIMIT, [from, to]
  );
  if (!origins.length) return { receivers: [] };
  const originChars = origins.map(o => o.char_id);
  const originAccounts = new Set(origins.map(o => o.account_id));
  const accByChar = {};
  origins.forEach(o => { accByChar[o.char_id] = o.account_id; });
  const cph = originChars.map(() => '?').join(',');

  const tally = {}; // receiver account -> Set(origin accounts)
  const add = (recvAcc, origAcc) => {
    if (!recvAcc || Number(recvAcc) === Number(origAcc)) return;
    if (originAccounts.has(Number(recvAcc))) { /* still count: event acct receiving from another event acct */ }
    const key = Number(recvAcc);
    if (!tally[key]) tally[key] = { account_id: key, origins: new Set(), zeny: 0, gold: 0, shadow: 0 };
    tally[key].origins.add(Number(origAcc));
  };

  // outgoing zeny from origin chars -> receiver
  const z = await q(
    'SELECT z.`amount`, z.`src_id`, z.`char_id`, c.`account_id` AS recv_account' +
    ' FROM ' + T.zeny + ' z LEFT JOIN ' + T.char + ' c ON c.`char_id` = z.`char_id`' +
    ' WHERE z.`src_id` IN (' + cph + ') AND z.`amount` > 0 AND z.`time` BETWEEN ? AND ? LIMIT ' + MAX_LIMIT,
    [...originChars, from, to]
  );
  z.forEach(r => { const oacc = accByChar[r.src_id]; if (oacc && r.recv_account) { add(r.recv_account, oacc); tally[Number(r.recv_account)].zeny += Number(r.amount) || 0; } });

  // gold T from origin sender -> receiver
  const g = await q(
    'SELECT send.`char_id` AS send_char, recv.`char_id` AS recv_char, cr.`account_id` AS recv_account, recv.`amount`' +
    ' FROM ' + T.pick + ' send' +
    ' JOIN ' + T.pick + ' recv ON recv.`nameid` = ' + GOLD + ' AND recv.`type` = \'T\'' +
    '   AND recv.`time` = send.`time` AND recv.`map` = send.`map`' +
    '   AND recv.`amount` = -send.`amount` AND recv.`char_id` <> send.`char_id`' +
    ' LEFT JOIN ' + T.char + ' cr ON cr.`char_id` = recv.`char_id`' +
    ' WHERE send.`nameid` = ' + GOLD + ' AND send.`type` = \'T\' AND send.`amount` < 0' +
    '   AND send.`char_id` IN (' + cph + ') AND send.`time` BETWEEN ? AND ? LIMIT ' + MAX_LIMIT,
    [...originChars, from, to]
  );
  g.forEach(r => { const oacc = accByChar[r.send_char]; if (oacc && r.recv_account) { add(r.recv_account, oacc); tally[Number(r.recv_account)].gold += Number(r.amount) || 0; } });

  const receivers = Object.values(tally)
    .map(t => ({ account_id: t.account_id, origin_account_count: t.origins.size, origin_accounts: [...t.origins], zeny: t.zeny, gold: t.gold }))
    .filter(t => t.origin_account_count >= 1)
    .sort((a, b) => b.origin_account_count - a.origin_account_count || (b.zeny + b.gold) - (a.zeny + a.gold))
    .slice(0, limit);
  return { receivers };
}

// ---- search resolver -----------------------------------------------------
async function resolveSearch(opt) {
  // returns candidate accounts to inspect
  if (opt.accountId) return { account_id: Number(opt.accountId) };
  if (opt.charId) {
    const r = await q('SELECT `account_id` FROM ' + T.char + ' WHERE `char_id` = ? LIMIT 1', [opt.charId]);
    return r[0] ? { account_id: r[0].account_id, char_id: Number(opt.charId) } : {};
  }
  if (opt.charName) {
    const r = await q('SELECT `account_id`, `char_id` FROM ' + T.char + ' WHERE `name` = ? LIMIT 1', [opt.charName]);
    return r[0] ? { account_id: r[0].account_id, char_id: r[0].char_id } : {};
  }
  if (opt.ip) {
    const r = await q('SELECT DISTINCT `account_id` FROM ' + T.reward + ' WHERE `ip` = ? LIMIT 20', [opt.ip]);
    return { accounts: r.map(x => x.account_id) };
  }
  return {};
}

// ---- format helpers ------------------------------------------------------
function fmtNum(n) { return Number(n).toLocaleString('en-US'); }
function signed(n) { n = Number(n); return (n > 0 ? '+' : '') + fmtNum(n); }

module.exports = {
  originList, accountSummary, accountTimeline, shadowTrace,
  zenyMovement, goldMovement, receiverConcentration, receiverAutoRank,
  resolveSearch, SOURCE_NAMES, REWARD_TYPE, GOLD_TYPE_CONF,
};
