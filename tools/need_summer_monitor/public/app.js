'use strict';
// NEED Summer Monitor v1 frontend (vanilla). Read-only.
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const el = (t, cls, html) => { const e = document.createElement(t); if (cls) e.className = cls; if (html != null) e.innerHTML = html; return e; };
const esc = s => String(s == null ? '' : s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const num = n => Number(n).toLocaleString('en-US');
const signed = n => (Number(n) > 0 ? '+' : '') + num(n);

let DAYS = 7;
function qs() { return 'days=' + DAYS; }
async function api(path) {
  const r = await fetch('/api' + path, { credentials: 'same-origin' });
  if (r.status === 401) { showLogin(); throw new Error('unauthorized'); }
  const j = await r.json();
  if (!r.ok) throw new Error(j.detail || j.error || 'error');
  return j;
}

/* ---------- auth ---------- */
function showLogin() { $('#login').classList.remove('hidden'); $('#app').classList.add('hidden'); }
function showApp() { $('#login').classList.add('hidden'); $('#app').classList.remove('hidden'); }
async function checkSession() {
  const r = await fetch('/api/session', { credentials: 'same-origin' }).then(x => x.json());
  if (r.authed) { showApp(); loadOrigins(); } else showLogin();
}
$('#loginForm').addEventListener('submit', async e => {
  e.preventDefault();
  $('#loginErr').textContent = '';
  const r = await fetch('/api/login', {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, credentials: 'same-origin',
    body: JSON.stringify({ user: $('#lu').value, pass: $('#lp').value }),
  });
  const j = await r.json();
  if (j.ok) { showApp(); loadOrigins(); }
  else $('#loginErr').textContent = j.error === 'server_auth_not_configured'
    ? '서버 인증 미설정 (MONITOR_USER/PASS/SESSION_SECRET 필요)' : '로그인 실패';
});
$('#btnLogout').addEventListener('click', async () => {
  await fetch('/api/logout', { method: 'POST', credentials: 'same-origin' }); showLogin();
});

/* ---------- tabs ---------- */
$$('.tab').forEach(t => t.addEventListener('click', () => {
  $$('.tab').forEach(x => x.classList.remove('active')); t.classList.add('active');
  $$('.panel').forEach(p => p.classList.add('hidden'));
  $('#tab-' + t.dataset.tab).classList.remove('hidden');
}));
function gotoTab(name) { const t = $('.tab[data-tab="' + name + '"]'); if (t) t.click(); }
$$('.subtab').forEach(t => t.addEventListener('click', () => {
  $$('.subtab').forEach(x => x.classList.remove('active')); t.classList.add('active');
  $$('.subpanel').forEach(p => p.classList.add('hidden'));
  $('#sub-' + t.dataset.sub).classList.remove('hidden');
}));

/* ---------- search ---------- */
$('#days').addEventListener('change', e => { DAYS = Number(e.target.value); loadOrigins(); });
$('#searchForm').addEventListener('submit', async e => {
  e.preventDefault();
  const acc = $('#q_account').value.trim(), cn = $('#q_char').value.trim(),
    cid = $('#q_charid').value.trim(), ip = $('#q_ip').value.trim();
  if (acc) return openAccount(acc);
  if (cid || cn || ip) {
    const p = new URLSearchParams();
    if (cid) p.set('char_id', cid); if (cn) p.set('char_name', cn); if (ip) p.set('ip', ip);
    const r = await api('/search?' + p.toString());
    if (r.account_id) return openAccount(r.account_id);
    if (r.accounts && r.accounts.length) return openAccount(r.accounts[0]);
    alert('검색 결과 없음');
  } else loadOrigins();
});

/* ---------- origins ---------- */
async function loadOrigins() {
  gotoTab('origins');
  const box = $('#originsTable'); box.innerHTML = '<div class="empty">불러오는 중…</div>';
  try {
    const r = await api('/origins?' + qs() + '&limit=200');
    $('#originMeta').textContent = r.from + ' ~ ' + r.to + ' · ' + r.count + '건';
    if (!r.rows.length) { box.innerHTML = '<div class="empty">해당 기간 Origin 없음</div>'; return; }
    const rows = r.rows.map(o => {
      const reward = o.reward_type === 3
        ? ('item ' + o.reward_item_id + ' <span class="muted">uid ' + o.unique_id + '</span> ' +
           '<a class="link" onclick="traceShadow(\'' + o.unique_id + '\')">추적</a>')
        : signed(o.reward_amount);
      return '<tr>' +
        '<td>' + esc(o.time) + '</td>' +
        '<td><a class="link" onclick="openAccount(' + o.account_id + ')">' + o.account_id + '</a></td>' +
        '<td>' + esc(o.char_name || o.char_id) + '</td>' +
        '<td>' + esc(o.ip) + '</td>' +
        '<td>' + esc(o.source_name) + ' <span class="muted">' + o.source_item_id + '</span></td>' +
        '<td>' + esc(o.reward_type_name) + '</td>' +
        '<td class="num">' + reward + '</td>' +
        '</tr>';
    }).join('');
    box.innerHTML = '<table><thead><tr><th>시간</th><th>Account</th><th>Character</th><th>IP</th>' +
      '<th>출처</th><th>종류</th><th>보상</th></tr></thead><tbody>' + rows + '</tbody></table>';
  } catch (e) { box.innerHTML = '<div class="empty">오류: ' + esc(e.message) + '</div>'; }
}

/* ---------- account detail ---------- */
async function openAccount(id) {
  gotoTab('account');
  $('#accountHead').innerHTML = '<div class="empty">불러오는 중…</div>';
  $$('.subtab')[0].click();
  try {
    const s = await api('/account/' + id + '/summary');
    $('#accountHead').innerHTML =
      '<h2>Account ' + id + '</h2>' +
      '<div class="kv"><span>캐릭터: <b>' + s.chars.map(c => esc(c.name)).join(', ') + '</b></span>' +
      '<span>Origin IP: <b>' + (s.origin_ips.join(', ') || '-') + '</b></span>' +
      '<span>Origin 수: <b>' + s.origin_count + '</b></span></div>';
    loadTimeline(id); loadZeny(id); loadGold(id); loadConc(id);
  } catch (e) { $('#accountHead').innerHTML = '<div class="empty">오류: ' + esc(e.message) + '</div>'; }
}
window.openAccount = openAccount;

async function loadTimeline(id) {
  const box = $('#sub-timeline'); box.innerHTML = '불러오는 중…';
  const r = await api('/account/' + id + '/timeline?' + qs());
  if (!r.events.length) { box.innerHTML = '<div class="empty">기간 내 이벤트 없음</div>'; return; }
  box.innerHTML = '<div class="tl">' + r.events.map(ev =>
    '<div class="ev ' + ev.kind + '"><span class="time">' + esc(ev.time) + '</span>' +
    badge(ev.conf) + ' ' + esc(ev.text) + '</div>').join('') + '</div>';
}
async function loadZeny(id) {
  const box = $('#sub-zeny'); box.innerHTML = '불러오는 중…';
  const r = await api('/account/' + id + '/zeny?' + qs());
  const head = '<p class="muted">' + esc(r.note || '') + '</p>';
  if (!r.rows.length) { box.innerHTML = head + '<div class="empty">이동 없음</div>'; return; }
  box.innerHTML = head + '<div class="tablewrap"><table><thead><tr><th>시간</th><th>type</th><th>방향</th>' +
    '<th>Character</th><th>상대 account</th><th>금액</th><th>신뢰도</th></tr></thead><tbody>' +
    r.rows.map(z => '<tr><td>' + esc(z.time) + '</td><td>' + z.type + '</td>' +
      '<td>' + (z.direction === 'out' ? '지급' : '수령') + '</td>' +
      '<td>' + esc(z.char_name || z.char_id) + '</td>' +
      '<td>' + (z.peer_account ? '<a class="link" onclick="openAccount(' + z.peer_account + ')">' + z.peer_account + '</a>' : (z.peer_char || '-')) + '</td>' +
      '<td class="num ' + (z.amount < 0 ? 'neg' : 'pos') + '">' + signed(z.amount) + '</td>' +
      '<td>' + badge(z.conf) + '</td></tr>').join('') + '</tbody></table></div>';
}
async function loadGold(id) {
  const box = $('#sub-gold'); box.innerHTML = '불러오는 중…';
  const r = await api('/account/' + id + '/gold?' + qs());
  const head = '<p class="muted">' + esc(r.note || '') + '</p>';
  if (!r.rows.length) { box.innerHTML = head + '<div class="empty">금화(399990) 이동 없음</div>'; return; }
  box.innerHTML = head + '<div class="tablewrap"><table><thead><tr><th>시간</th><th>type</th><th>분류</th>' +
    '<th>Character</th><th>수량</th><th>상대</th><th>신뢰도</th></tr></thead><tbody>' +
    r.rows.map(g => '<tr><td>' + esc(g.time) + '</td><td>' + g.type + '</td><td>' + esc(g.type_label) + '</td>' +
      '<td>' + g.char_id + '</td>' +
      '<td class="num ' + (g.amount < 0 ? 'neg' : 'pos') + '">' + signed(g.amount) + '</td>' +
      '<td>' + (g.peer ? '<a class="link" onclick="openAccount(' + g.peer.account_id + ')">acc ' + g.peer.account_id + '</a>' : '-') + '</td>' +
      '<td>' + badge(g.conf) + '</td></tr>').join('') + '</tbody></table></div>';
}
async function loadConc(id) {
  const box = $('#sub-conc'); box.innerHTML = '불러오는 중…';
  const r = await api('/account/' + id + '/concentration?' + qs());
  box.innerHTML = renderConcentration(r);
}

/* ---------- shadow ---------- */
$('#shadowForm').addEventListener('submit', e => { e.preventDefault(); traceShadow($('#shadowUid').value.trim()); });
async function traceShadow(uid) {
  if (!uid) return;
  gotoTab('shadow'); $('#shadowUid').value = uid;
  const box = $('#shadowResult'); box.innerHTML = '<div class="empty">추적 중…</div>';
  try {
    const r = await api('/shadow/' + encodeURIComponent(uid));
    let head = '<div class="card"><div class="kv"><span>unique_id: <b>' + esc(r.unique_id) + '</b></span>';
    if (r.origin) head += '<span>Origin account: <b>' + r.origin.account_id + '</b></span>' +
      '<span>item: <b>' + r.origin.reward_item_id + '</b></span><span>생성: <b>' + esc(r.origin.created_at) + '</b></span>';
    head += '</div></div>';
    if (!r.steps.length) { box.innerHTML = head + '<div class="empty">picklog 이동 기록 없음</div>'; return; }
    box.innerHTML = head + '<div class="tablewrap"><table><thead><tr><th>시간</th><th>type</th><th>Character</th>' +
      '<th>account</th><th>수량</th><th>map</th><th>비고</th></tr></thead><tbody>' +
      r.steps.map(s => '<tr><td>' + esc(s.time) + '</td><td>' + s.type + '</td>' +
        '<td>' + esc(s.char_name || s.char_id) + '</td>' +
        '<td>' + (s.account_id ? '<a class="link" onclick="openAccount(' + s.account_id + ')">' + s.account_id + '</a>' : '-') + '</td>' +
        '<td class="num ' + (s.amount < 0 ? 'neg' : 'pos') + '">' + signed(s.amount) + '</td>' +
        '<td>' + esc(s.map) + '</td>' +
        '<td>' + (s.internal ? badge('internal') + ' 동일계정 내부' : (s.external_account ? badge('exact') + ' 타계정 이동' : '')) + '</td>' +
        '</tr>').join('') + '</tbody></table></div>';
  } catch (e) { box.innerHTML = '<div class="empty">오류: ' + esc(e.message) + '</div>'; }
}
window.traceShadow = traceShadow;

/* ---------- receivers ---------- */
$('#btnAutoRank').addEventListener('click', async () => {
  const box = $('#receiversResult'); box.innerHTML = '<div class="empty">탐지 중…</div>';
  try {
    const r = await api('/receivers?' + qs() + '&limit=30');
    if (!r.receivers.length) { box.innerHTML = '<div class="empty">기간 내 집중 패턴 없음</div>'; return; }
    box.innerHTML = '<div class="tablewrap"><table><thead><tr><th>수령 Account</th><th>유입 Origin 계정 수</th>' +
      '<th>Zeny 합</th><th>Gold(T) 합</th><th>상세</th></tr></thead><tbody>' +
      r.receivers.map(x => '<tr><td>' + x.account_id + '</td>' +
        '<td class="num">' + x.origin_account_count + '</td>' +
        '<td class="num">' + num(x.zeny) + '</td>' +
        '<td class="num">' + num(x.gold) + '</td>' +
        '<td><a class="link" onclick="openAccountConc(' + x.account_id + ')">집중 상세</a></td></tr>').join('') +
      '</tbody></table></div>';
  } catch (e) { box.innerHTML = '<div class="empty">오류: ' + esc(e.message) + '</div>'; }
});
function openAccountConc(id) { openAccount(id); setTimeout(() => $('.subtab[data-sub="conc"]').click(), 300); }
window.openAccountConc = openAccountConc;

function renderConcentration(r) {
  let head = '<div class="kv"><span>수령 account: <b>' + r.receiver_account + '</b></span>' +
    '<span>유입 Origin 계정: <b>' + r.origin_accounts + '</b></span>' +
    '<span>서로 다른 Origin IP: <b>' + r.origin_ips + '</b></span></div>';
  if (!r.inflows.length) return head + '<div class="empty">이 계정으로 집중된 이벤트 가치 유입 없음</div>';
  return head + '<div class="tablewrap"><table><thead><tr><th>Origin Account</th><th>Origin IP</th>' +
    '<th>Shadow(개)</th><th>Zeny</th><th>Gold(T)</th><th>근거</th></tr></thead><tbody>' +
    r.inflows.map(i => '<tr>' +
      '<td><a class="link" onclick="openAccount(' + i.account_id + ')">' + i.account_id + '</a></td>' +
      '<td>' + esc(i.ip) + '</td>' +
      '<td class="num">' + num(i.shadow) + '</td>' +
      '<td class="num">' + num(i.zeny) + '</td>' +
      '<td class="num">' + num(i.gold) + '</td>' +
      '<td>' + i.details.map(d => badge(d.conf) + ' ' + d.kind).slice(0, 6).join(' ') + '</td>' +
      '</tr>').join('') + '</tbody></table></div>';
}

function badge(conf) {
  const map = { exact: '정확', high: '높음', medium: '중간', candidate: '후보', internal: '내부이동', origin: 'Origin' };
  return '<span class="badge ' + conf + '">' + (map[conf] || conf) + '</span>';
}

checkSession();
