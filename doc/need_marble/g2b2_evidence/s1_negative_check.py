"""G2-B2: the new static checks must fail on the code before the fix (d0f6b3d7f) and pass now."""
import os
import subprocess
import sys
import tempfile

WT = r'E:\tools\Need\need-marble-phase-bc-v1'
NEW = ['hunt_checkpoint: resend check', 'a key taken during the batch is a CONFLICT', 'NEED_MARBLE_CONFLICT is a new code',
       'hunt_flush: CONFLICT stops', 'resend rule is the pure function']
show = lambda rev, p: subprocess.run(['git', '-C', WT, 'show', f'{rev}:{p}'], capture_output=True).stdout
with tempfile.TemporaryDirectory() as d:
    # an old tree: every file the static check reads, from d0f6b3d7f, but today's static_check.py
    files = subprocess.run(['git', '-C', WT, 'ls-tree', '-r', '--name-only', 'd0f6b3d7f'], capture_output=True, text=True).stdout.split()
    need = [f for f in files if f.startswith(('src/map/need_marble', 'src/map/mob.cpp', 'src/custom/script', 'sql-files/need_marble', 'npc/NEED/', 'npc/scripts_need.conf',
                                              'npc/re/scripts_main.conf', 'tests/need_marble/', 'conf/battle/client.conf', 'npc/NEED/marble/'))]
    need += [f for f in files if f.startswith('src/') and f.endswith(('.cpp', '.hpp', '.inc', '.vcxproj', '.filters', '.txt', '.in'))]
    for f in set(need):
        p = os.path.join(d, f)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(show('d0f6b3d7f', f))
    out = subprocess.run([sys.executable, '-I', '-B', os.path.join(WT, 'tests', 'need_marble', 'static_check.py'), d], capture_output=True, text=True,
                         encoding='utf-8', errors='replace')
    out = r.stdout + r.stderr if False else out.stdout + out.stderr
crashed = 'Traceback' in out or 'static_check:' not in out
old = {n: any(l.startswith('FAIL ') and n in l for l in out.splitlines()) for n in NEW}
print('old tree run crashed:', crashed)
print('on d0f6b3d7f (before the fix), new checks failing:', old)
now = subprocess.run([sys.executable, '-I', '-B', os.path.join(WT, 'tests', 'need_marble', 'static_check.py'), WT], capture_output=True, text=True,
                     encoding='utf-8', errors='replace').stdout
print('now:', {n: any(l.startswith('PASS ') and n in l for l in now.splitlines()) for n in NEW})
