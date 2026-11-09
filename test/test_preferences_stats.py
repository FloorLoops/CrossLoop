"""Validate preferences, persistence failures and actual reading-session accounting."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CXX') or shutil.which('cl.exe') or shutil.which('c++')
if not compiler:
    raise SystemExit('Run in a C++20 developer environment.')
with tempfile.TemporaryDirectory(prefix='crossloop-stats-') as directory:
    directory = Path(directory)
    exe = directory/'stats.exe'
    includes = [ROOT/'test/web_text/stubs', ROOT/'src', ROOT/'lib/Memory']
    sources = [ROOT/'test/personal/PreferencesStatsTest.cpp', ROOT/'src/util/CrossLoopPreferences.cpp',
               ROOT/'src/util/LocalReadingStats.cpp', ROOT/'src/activities/reader/ReaderSession.cpp']
    if Path(compiler).name.lower() == 'cl.exe':
        args = [compiler, '/nologo', '/std:c++20', '/EHsc', '/W4', '/FIArduino.h'] + ['/I'+str(x) for x in includes] + list(map(str, sources)) + ['/Fe:'+str(exe)]
    else:
        args = [compiler, '-std=c++20', '-Wall', '-Wextra'] + ['-I'+str(x) for x in includes] + list(map(str, sources)) + ['-o', str(exe)]
    subprocess.run(args, cwd=directory, check=True)
    subprocess.run([str(exe)], check=True)
