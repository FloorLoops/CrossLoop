"""Exercise production feed import against the firmware's actual Expat sources."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CXX') or shutil.which('cl.exe') or shutil.which('c++')
if not compiler:
    raise SystemExit('Run in a C++20 developer environment.')
expat = ROOT/'lib/expat'
with tempfile.TemporaryDirectory(prefix='crossloop-feeds-') as directory:
    directory = Path(directory)
    exe = directory/'feeds.exe'
    includes = [ROOT/'test/web_text/stubs', ROOT/'src', ROOT/'lib/Memory', expat]
    sources = [ROOT/'test/personal/FeedTest.cpp', ROOT/'src/util/PersonalCache.cpp', ROOT/'src/util/FeedParser.cpp']
    objects = []
    msvc = Path(compiler).name.lower() == 'cl.exe'
    for source in ['xmlparse.c', 'xmlrole.c', 'xmltok.c']:
        output = directory/(source + '.obj')
        if msvc:
            args = [compiler, '/nologo', '/TC', '/DXML_STATIC', '/DXML_GE=0', '/DXML_CONTEXT_BYTES=0', '/DHAVE_EXPAT_CONFIG_H', '/I'+str(expat), '/c', str(expat/source), '/Fo'+str(output)]
        else:
            args = [shutil.which('cc'), '-DXML_STATIC', '-DXML_GE=0', '-DXML_CONTEXT_BYTES=0', '-DXML_POOR_ENTROPY', '-DHAVE_EXPAT_CONFIG_H', '-I'+str(expat), '-c', str(expat/source), '-o', str(output)]
        subprocess.run(args, cwd=directory, check=True)
        objects.append(output)
    if msvc:
        args = [compiler, '/nologo', '/std:c++20', '/EHsc', '/W4', '/DXML_STATIC'] + ['/I'+str(x) for x in includes] + list(map(str, sources + objects)) + ['/Fe:'+str(exe)]
    else:
        args = [compiler, '-std=c++20', '-Wall', '-Wextra', '-DXML_STATIC'] + ['-I'+str(x) for x in includes] + list(map(str, sources + objects)) + ['-o', str(exe)]
    subprocess.run(args, cwd=directory, check=True)
    subprocess.run([str(exe)], check=True)
