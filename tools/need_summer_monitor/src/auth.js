'use strict';
// Minimal operator auth: single admin account from env, HMAC-signed session cookie.
// No password is ever hardcoded; credentials and secret come from the environment.
const crypto = require('crypto');

const USER = process.env.MONITOR_USER || '';
const PASS = process.env.MONITOR_PASS || '';
const SECRET = process.env.SESSION_SECRET || '';
const TTL_MS = Number(process.env.SESSION_TTL_HOURS || 12) * 3600 * 1000;
const COOKIE = 'nsm_session';

function configured() {
  return USER.length > 0 && PASS.length > 0 && SECRET.length >= 16;
}

function sign(value) {
  return crypto.createHmac('sha256', SECRET).update(value).digest('base64url');
}

// Constant-time string compare that tolerates length differences.
function safeEqual(a, b) {
  const ab = Buffer.from(String(a));
  const bb = Buffer.from(String(b));
  const len = Math.max(ab.length, bb.length);
  const pa = Buffer.alloc(len);
  const pb = Buffer.alloc(len);
  ab.copy(pa); bb.copy(pb);
  return crypto.timingSafeEqual(pa, pb) && ab.length === bb.length;
}

function issueToken() {
  const exp = Date.now() + TTL_MS;
  const payload = USER + '|' + exp;
  return Buffer.from(payload).toString('base64url') + '.' + sign(payload);
}

function verifyToken(token) {
  if (!token || token.indexOf('.') < 0) return false;
  const [b64, mac] = token.split('.');
  let payload;
  try { payload = Buffer.from(b64, 'base64url').toString('utf8'); } catch { return false; }
  if (!safeEqual(mac, sign(payload))) return false;
  const [user, expStr] = payload.split('|');
  if (user !== USER) return false;
  const exp = Number(expStr);
  if (!Number.isFinite(exp) || Date.now() > exp) return false;
  return true;
}

function parseCookies(req) {
  const raw = req.headers.cookie || '';
  const out = {};
  raw.split(';').forEach(p => {
    const i = p.indexOf('=');
    if (i > 0) out[p.slice(0, i).trim()] = decodeURIComponent(p.slice(i + 1).trim());
  });
  return out;
}

function login(req, res) {
  const { user, pass } = req.body || {};
  if (!configured()) {
    return res.status(500).json({ error: 'server_auth_not_configured' });
  }
  if (safeEqual(user, USER) && safeEqual(pass, PASS)) {
    const token = issueToken();
    const secure = process.env.COOKIE_SECURE === '1' ? '; Secure' : '';
    res.setHeader('Set-Cookie',
      COOKIE + '=' + encodeURIComponent(token) +
      '; HttpOnly; SameSite=Lax; Path=/; Max-Age=' + Math.floor(TTL_MS / 1000) + secure);
    return res.json({ ok: true });
  }
  return res.status(401).json({ error: 'invalid_credentials' });
}

function logout(req, res) {
  res.setHeader('Set-Cookie', COOKIE + '=; HttpOnly; SameSite=Lax; Path=/; Max-Age=0');
  res.json({ ok: true });
}

// Express middleware guarding API routes.
function requireAuth(req, res, next) {
  const token = parseCookies(req)[COOKIE];
  if (verifyToken(token)) return next();
  res.status(401).json({ error: 'unauthorized' });
}

function isAuthed(req) {
  return verifyToken(parseCookies(req)[COOKIE]);
}

module.exports = { login, logout, requireAuth, isAuthed, configured };
