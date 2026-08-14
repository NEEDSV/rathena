'use strict';
// Read-only DB access for NEED Summer Monitor.
// All table references are schema-qualified so the app works whether the main
// game DB and the log DB share one schema (dev) or are split (production).
const mysql = require('mysql2/promise');

const MAIN = process.env.DB_NAME || 'ragnarok';       // char, need_summer_reward_log
const LOG = process.env.LOG_DB_NAME || MAIN;          // picklog, zenylog, loginlog

// Qualified table identifiers (backticked, safe: names come from server config, not user input).
const T = {
  reward: '`' + MAIN + '`.`need_summer_reward_log`',
  char: '`' + MAIN + '`.`char`',
  pick: '`' + LOG + '`.`picklog`',
  zeny: '`' + LOG + '`.`zenylog`',
  login: '`' + LOG + '`.`' + (process.env.LOG_LOGIN_TABLE || 'loginlog') + '`',
};

const pool = mysql.createPool({
  host: process.env.DB_HOST || '127.0.0.1',
  port: Number(process.env.DB_PORT || 3306),
  user: process.env.DB_USER || 'root',
  password: process.env.DB_PASS || '',
  database: MAIN,
  waitForConnections: true,
  connectionLimit: Number(process.env.DB_POOL || 5),
  queueLimit: 0,
  // Never allow the web to mutate. mysql2 has no per-connection read-only flag,
  // so the intended deployment uses a SELECT-only DB grant (see README).
  multipleStatements: false,
  dateStrings: true,
});

async function q(sql, params) {
  const [rows] = await pool.query(sql, params || []);
  return rows;
}

async function ping() {
  const r = await q('SELECT 1 AS ok');
  return r[0] && r[0].ok === 1;
}

module.exports = { pool, q, ping, T, MAIN, LOG };
