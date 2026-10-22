"""SQL-level tests of the shared-stock statements used by need_lucky_ceiling.txt (isolated DB only).

Runs the exact UPDATE / ROW_COUNT / conditional-upsert statements of S_TakeStock and S_Normalize
against a throwaway MariaDB instance, including N concurrent clients. Never point this at a live DB:
the script refuses any port other than the one given and creates/drops its own schema.

Usage: python -I sql_stock_test.py <mariadb.exe> <port> <root_password>
"""
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

DB = "refine_policy_sqltest"
RESULTS = []


def run(cli, port, pw, sql, db=DB):
    args = [cli, "-h127.0.0.1", f"-P{port}", "-uroot", f"-p{pw}", "-N", "-B"]
    if db:
        args.append(db)
    p = subprocess.run(args + ["-e", sql], capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(p.stderr.strip())
    return [l.split("\t") for l in p.stdout.strip().splitlines() if l]


def take(cli, port, pw, pid, key):
    # S_TakeStock: UPDATE then SELECT ROW_COUNT() on the same connection
    out = run(cli, port, pw,
              f"UPDATE need_lucky_ceiling_stock SET stock=stock-1,updated_at=NOW() "
              f"WHERE product_id={pid} AND period_key={key} AND stock>=1; SELECT ROW_COUNT();")
    return int(out[-1][0])


def upsert(cli, port, pw, pid, ptype, key, base):
    # S_Normalize conditional reset
    run(cli, port, pw,
        "INSERT INTO need_lucky_ceiling_stock (product_id,period_type,period_key,stock,base_stock,updated_at) "
        f"VALUES ({pid},{ptype},{key},{base},{base},NOW()) ON DUPLICATE KEY UPDATE "
        "stock=IF(period_key=VALUES(period_key),stock,VALUES(stock)),"
        "updated_at=IF(period_key=VALUES(period_key),updated_at,NOW()),"
        "period_type=VALUES(period_type),base_stock=VALUES(base_stock),"
        "period_key=VALUES(period_key)")
    return run(cli, port, pw, f"SELECT stock,period_key FROM need_lucky_ceiling_stock WHERE product_id={pid}")[0]


def check(name, ok, detail=""):
    RESULTS.append((name, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  [{detail}]" if detail else ""))


def main():
    cli, port, pw = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    if port in (3306,):
        raise SystemExit("refusing the default/live port")
    run(cli, port, pw, f"DROP DATABASE IF EXISTS {DB}; CREATE DATABASE {DB};", db=None)
    raw = open("sql-files/need_lucky_ceiling.sql", "rb").read()
    try:
        schema = raw.decode("utf-8")
    except UnicodeDecodeError:
        schema = raw.decode("cp949")
    run(cli, port, pw, schema)

    # 1) first creation: no row -> insert base
    r = upsert(cli, port, pw, 102, 2, 202610, 1)
    check("upsert creates missing row with base stock", r == ["1", "202610"], str(r))
    # 2) sequential take: point route then blessing route on the SAME key (102)
    a = take(cli, port, pw, 102, 202610)
    b = take(cli, port, pw, 102, 202610)
    st = run(cli, port, pw, "SELECT stock FROM need_lucky_ceiling_stock WHERE product_id=102")[0][0]
    check("route A (point, PID 102) takes the monthly unit", a == 1)
    check("route B (blessing, PID 104 -> key 102) then sold out", b == 0 and st == "0", f"rc={b} stock={st}")
    check("no row was created for PID 104",
          run(cli, port, pw, "SELECT COUNT(*) FROM need_lucky_ceiling_stock WHERE product_id=104")[0][0] == "0")
    # 3) same-period upsert by a stale second map-server must NOT resurrect stock
    r = upsert(cli, port, pw, 102, 2, 202610, 1)
    check("same-period upsert keeps sold-out stock (no resurrection)", r == ["0", "202610"], str(r))
    # 4) new period resets to base
    r = upsert(cli, port, pw, 102, 2, 202611, 1)
    check("new period resets to base stock", r == ["1", "202611"], str(r))
    # 5) take with a stale period key is rejected
    rc = take(cli, port, pw, 102, 202610)
    check("take with stale period key rejected (rc=0)", rc == 0)
    # 6) concurrency: 20 clients race for the last unit
    with ThreadPoolExecutor(max_workers=20) as ex:
        rcs = list(ex.map(lambda _: take(cli, port, pw, 102, 202611), range(20)))
    st = run(cli, port, pw, "SELECT stock FROM need_lucky_ceiling_stock WHERE product_id=102")[0][0]
    check("20 concurrent takes on stock 1 -> exactly one success", sum(rcs) == 1 and st == "0",
          f"successes={sum(rcs)} stock={st}")
    # 7) concurrency on weekly stock 100 (PID 100): 150 clients -> exactly 100 successes, never negative
    upsert(cli, port, pw, 100, 1, 20261017, 100)
    with ThreadPoolExecutor(max_workers=30) as ex:
        rcs = list(ex.map(lambda _: take(cli, port, pw, 100, 20261017), range(150)))
    st = run(cli, port, pw, "SELECT stock FROM need_lucky_ceiling_stock WHERE product_id=100")[0][0]
    check("150 concurrent takes on weekly stock 100 -> exactly 100, stock 0", sum(rcs) == 100 and st == "0",
          f"successes={sum(rcs)} stock={st}")
    run(cli, port, pw, f"DROP DATABASE {DB};", db=None)
    fails = sum(1 for _, ok, _ in RESULTS if not ok)
    print(f"\n{len(RESULTS) - fails}/{len(RESULTS)} PASS")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
