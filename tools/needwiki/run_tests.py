"""Runs the NEED Wiki server tests that need no running server:
  1. test_needwiki_server.py (data / msg / call-site policy)
  2. needwiki_item_group_test.cpp, compiled with MSVC against the solution's ryml.lib
     (build web-server or map-server first so .vs/build/ryml.lib exists).

    python tools/needwiki/run_tests.py
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
YML = os.path.join(ROOT, 'db', 'import', 'wiki_item_group.yml')


def vcvars():
    vswhere = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'),
                           'Microsoft Visual Studio', 'Installer', 'vswhere.exe')
    path = subprocess.check_output([vswhere, '-latest', '-products', '*', '-property', 'installationPath'],
                                   text=True).strip()
    return os.path.join(path, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat')


def expected_tsv(path):
    lines = []
    cur = None
    for line in open(YML, 'rb').read().split(b'\n'):
        if line.startswith(b'  - Id: '):
            cur = line[8:].strip().decode('ascii')
        elif line.startswith(b'    Name: ') and cur:
            name = line[10:]
            lines.append(f"{cur}\t{name.hex()}\t{name.decode('cp949').encode('utf-8').hex()}")
    open(path, 'w', encoding='ascii').write('\n'.join(lines) + '\n')


def main():
    rc = subprocess.call([sys.executable, os.path.join(HERE, 'test_needwiki_server.py')])

    lib = os.path.join(ROOT, '.vs', 'build', 'ryml.lib')
    if not os.path.isfile(lib):
        print('SKIP  C++ test: build the solution first (.vs/build/ryml.lib is missing)')
        return rc or 1

    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, 'needwiki_item_group_test.exe')
        tsv = os.path.join(tmp, 'expected.tsv')
        expected_tsv(tsv)
        includes = ['src', r'3rdparty\rapidyaml\src', r'3rdparty\rapidyaml\ext\c4core\src', r'3rdparty\json\include']
        cl = ['cl', '/nologo', '/EHsc', '/std:c++17', '/O2', '/MT', '/W3', '/DWIN32', '/DNDEBUG']
        cl += [f'/I{os.path.join(ROOT, i)}' for i in includes]
        cl += [os.path.join(HERE, 'needwiki_item_group_test.cpp'), f'/Fe{exe}', f'/Fo{tmp}\\', '/link', lib]
        build = f'call "{vcvars()}" >nul && ' + subprocess.list2cmdline(cl)
        if subprocess.call(build, shell=True, cwd=tmp) != 0:
            print('FAIL  C++ test did not build')
            return 1
        rc = subprocess.call([exe, YML, tsv]) or rc
    return rc


if __name__ == '__main__':
    sys.exit(main())
