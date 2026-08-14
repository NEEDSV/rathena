'use strict';
// NEED Summer Monitor v1 - read-only web server.
const path = require('path');
const express = require('express');
const auth = require('./auth');
const Q = require('./queries');
const db = require('./db');

const app = express();
app.disable('x-powered-by');
app.use(express.json({ limit: '64kb' }));
app.use(express.urlencoded({ extended: false, limit: '64kb' }));

// default date range = last 7 days (fishing19 sec.8). Caller may override.
function range(req) {
  const to = req.query.to || fmtNow();
  const from = req.query.from || fmtDaysAgo(Number(req.query.days || 7));
  return { from, to };
}
function pad(n) { return String(n).padStart(2, '0'); }
function fmt(d) {
  return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + ' ' +
    pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds());
}
function fmtNow() { return fmt(new Date()); }
function fmtDaysAgo(days) { const d = new Date(); d.setDate(d.getDate() - (days || 7)); return fmt(d); }

function wrap(fn) {
  return async (req, res) => {
    try { await fn(req, res); }
    catch (e) {
      console.error('[api-error]', req.path, e && e.message);
      res.status(500).json({ error: 'query_failed', detail: String(e && e.message || e) });
    }
  };
}

// ---- public auth endpoints ----
app.post('/api/login', express.json(), auth.login);
app.post('/api/logout', auth.logout);
app.get('/api/session', (req, res) => res.json({ authed: auth.isAuthed(req), configured: auth.configured() }));

// ---- guarded API ----
const api = express.Router();
api.use(auth.requireAuth);

api.get('/health', wrap(async (req, res) => {
  const ok = await db.ping();
  res.json({ ok, main: db.MAIN, log: db.LOG });
}));

api.get('/origins', wrap(async (req, res) => {
  const { from, to } = range(req);
  const rows = await Q.originList({
    from, to,
    accountId: req.query.account_id, charId: req.query.char_id,
    charName: req.query.char_name, ip: req.query.ip,
    limit: req.query.limit, offset: req.query.offset,
  });
  res.json({ from, to, count: rows.length, rows });
}));

api.get('/search', wrap(async (req, res) => {
  const resolved = await Q.resolveSearch({
    accountId: req.query.account_id, charId: req.query.char_id,
    charName: req.query.char_name, ip: req.query.ip,
  });
  res.json(resolved);
}));

api.get('/account/:id/summary', wrap(async (req, res) => {
  res.json(await Q.accountSummary(req.params.id, {}));
}));

api.get('/account/:id/timeline', wrap(async (req, res) => {
  const { from, to } = range(req);
  res.json(await Q.accountTimeline(req.params.id, { from, to }));
}));

api.get('/account/:id/zeny', wrap(async (req, res) => {
  const { from, to } = range(req);
  res.json(await Q.zenyMovement(req.params.id, { from, to }));
}));

api.get('/account/:id/gold', wrap(async (req, res) => {
  const { from, to } = range(req);
  res.json(await Q.goldMovement(req.params.id, { from, to }));
}));

api.get('/account/:id/concentration', wrap(async (req, res) => {
  const { from, to } = range(req);
  res.json(await Q.receiverConcentration(req.params.id, { from, to }));
}));

api.get('/shadow/:uniqueId', wrap(async (req, res) => {
  res.json(await Q.shadowTrace(req.params.uniqueId, { since: req.query.since }));
}));

api.get('/receivers', wrap(async (req, res) => {
  const { from, to } = range(req);
  res.json(await Q.receiverAutoRank({ from, to, limit: req.query.limit }));
}));

app.use('/api', api);

// ---- static frontend ----
app.use(express.static(path.join(__dirname, '..', 'public')));

const PORT = Number(process.env.PORT || 8787);
const HOST = process.env.HOST || '0.0.0.0';
app.listen(PORT, HOST, () => {
  console.log('NEED Summer Monitor v1 listening on http://' + HOST + ':' + PORT);
  console.log('DB main=' + db.MAIN + ' log=' + db.LOG);
  if (!auth.configured()) {
    console.warn('[WARN] MONITOR_USER / MONITOR_PASS / SESSION_SECRET(>=16) not fully set -> login disabled until configured.');
  }
});
